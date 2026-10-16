#include "meshtastic_companion.h"
#include "meshtastic_runtime.h"
#include "../../lib/Meshtastic/src/BluetoothCommon.h"
#include "../../lib/Meshtastic/src/mesh/PhoneAPI.h"
#include "../../lib/Meshtastic/src/mesh/NodeDB.h"
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include "../companion_notice.h"
#include "../backup_restore.h"
#include "../message_store.h"
#include "../hardware/board.h"
#include "../hardware/buttons.h"
#include "../hardware/gps.h"
#include "../hardware/performance.h"
#include "../hardware/power.h"
#include "../hardware/rtc.h"
#include "../hardware/radio.h"
#include "../hardware/storage.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <BLEAdvertising.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <memory>
#include <new>
#include <cstdio>
#include <cstring>

namespace {
// Preserve the same one-shot, reboot-to-UI contract as MeshCore companion.
constexpr char kBootNamespace[] = "t5-boot";
constexpr char kCompanionRequest[] = "companion_once";
constexpr uint32_t kBleReadTimeoutMs = 3000;
constexpr unsigned kToRadioQueueDepth = 5;

// The ESP32 Arduino 2.x build already uses Bluedroid for MeshCore. Reuse that
// controller/host in this *mutually exclusive* boot mode; never link a second
// NimBLE stack. The wire interface is the official Meshtastic PhoneAPI and
// the UUIDs above are from pinned upstream BluetoothCommon.h.
struct ToRadioFrame {
    uint16_t size = 0;
    uint8_t data[meshtastic_ToRadio_size]{};
};
struct FromRadioFrame {
    uint16_t size = 0;
    uint8_t data[meshtastic_FromRadio_size]{};
};
QueueHandle_t to_radio = nullptr;
QueueHandle_t from_radio = nullptr;
BLEServer* server = nullptr;
BLECharacteristic* from_num = nullptr;
std::atomic<bool> connected{false};
std::atomic<bool> disconnect_pending{false};
std::atomic<bool> read_requested{false};
std::atomic<bool> shutting_down{false};
bool active = false;
bool ready = false;
uint32_t next_gps_snapshot = 0;
char ble_name[24]{};

class CompanionPhoneAPI final : public PhoneAPI {
  public:
    CompanionPhoneAPI() { api_type = TYPE_BLE; }
  protected:
    bool checkIsConnected() override {
        return connected.load(std::memory_order_acquire) &&
               !shutting_down.load(std::memory_order_acquire);
    }
    void onNowHasData(uint32_t fromRadioNum) override {
        if (!checkIsConnected() || !from_num) return;
        uint8_t value[4] = {
            static_cast<uint8_t>(fromRadioNum),
            static_cast<uint8_t>(fromRadioNum >> 8),
            static_cast<uint8_t>(fromRadioNum >> 16),
            static_cast<uint8_t>(fromRadioNum >> 24)
        };
        from_num->setValue(value, sizeof(value));
        from_num->notify();
    }
};
std::unique_ptr<CompanionPhoneAPI> api;

class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        connected.store(true, std::memory_order_release);
        disconnect_pending.store(false, std::memory_order_release);
    }
    void onDisconnect(BLEServer*) override {
        connected.store(false, std::memory_order_release);
        disconnect_pending.store(true, std::memory_order_release);
        // Only the companion loop may touch PhoneAPI; this callback is a
        // Bluedroid task. Advertising is restarted by that loop as well.
    }
};
class ToRadioCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* ch) override {
        if (!to_radio || shutting_down.load(std::memory_order_acquire)) return;
        const std::string value = ch->getValue();
        if (value.empty() || value.size() > meshtastic_ToRadio_size) return;
        ToRadioFrame frame{};
        frame.size = static_cast<uint16_t>(value.size());
        memcpy(frame.data, value.data(), frame.size);
        if (xQueueSend(to_radio, &frame, 0) != pdTRUE)
            Serial.println("[MT-BLE] ToRadio queue full: frame rejected");
    }
};
class FromRadioCallbacks final : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* ch) override {
        if (!from_radio || shutting_down.load(std::memory_order_acquire) ||
            !connected.load(std::memory_order_acquire)) {
            ch->setValue(static_cast<uint8_t*>(nullptr), 0);
            return;
        }
        read_requested.store(true, std::memory_order_release);
        FromRadioFrame frame{};
        // The Bluetooth task never calls PhoneAPI. The headless protocol loop
        // handles ToRadio first, then supplies this read. This preserves the
        // official write-then-read configuration handshake ordering.
        if (xQueueReceive(from_radio, &frame, pdMS_TO_TICKS(kBleReadTimeoutMs)) == pdTRUE)
            ch->setValue(frame.data, frame.size);
        else {
            read_requested.store(false, std::memory_order_release);
            ch->setValue(static_cast<uint8_t*>(nullptr), 0);
            Serial.println("[MT-BLE] FromRadio read timeout");
        }
    }
};
ServerCallbacks server_callbacks;
ToRadioCallbacks to_radio_callbacks;
FromRadioCallbacks from_radio_callbacks;
BLE2902 from_num_cccd;

bool start_ble() {
    // The official config is the single source of truth. A static e-paper
    // notice cannot display a new randomly generated passkey for each pairing;
    // require the official fixed-PIN or no-PIN setting, never silently alter it.
    if (config.bluetooth.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN) {
        Serial.println("[MT-BLE] Random PIN requires a dynamic pairing display; set fixed PIN in Meshtastic settings first");
        return false;
    }
    uint8_t mac[6]{};
    getMacAddr(mac);
    char default_short[5]{};
    snprintf(default_short, sizeof(default_short), "%02x%02x", mac[4], mac[5]);
    if (owner.short_name[0] && strcmp(owner.short_name, default_short) != 0)
        snprintf(ble_name, sizeof(ble_name), "%s_%02x%02x", owner.short_name, mac[4], mac[5]);
    else
        snprintf(ble_name, sizeof(ble_name), "Meshtastic_%02x%02x", mac[4], mac[5]);

    BLEDevice::init(ble_name);
    BLEDevice::setMTU(517);
    const bool pin_required =
        config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    if (pin_required) {
        BLESecurity security;
        security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
        security.setCapability(ESP_IO_CAP_OUT);
        security.setStaticPIN(config.bluetooth.fixed_pin);
        security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    }
    server = BLEDevice::createServer();
    if (!server) return false;
    server->setCallbacks(&server_callbacks);
    BLEService* service = server->createService(MESH_SERVICE_UUID);
    if (!service) return false;

    const uint32_t auth_write = pin_required ?
        (BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC |
         BLECharacteristic::PROPERTY_WRITE_AUTHEN) : BLECharacteristic::PROPERTY_WRITE;
    const uint32_t auth_read = pin_required ?
        (BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC |
         BLECharacteristic::PROPERTY_READ_AUTHEN) : BLECharacteristic::PROPERTY_READ;

    BLECharacteristic* write_ch = service->createCharacteristic(TORADIO_UUID, auth_write);
    BLECharacteristic* read_ch = service->createCharacteristic(FROMRADIO_UUID, auth_read);
    from_num = service->createCharacteristic(FROMNUM_UUID,
        auth_read | BLECharacteristic::PROPERTY_NOTIFY);
    if (!write_ch || !read_ch || !from_num) return false;
    write_ch->setCallbacks(&to_radio_callbacks);
    read_ch->setCallbacks(&from_radio_callbacks);
    from_num->addDescriptor(&from_num_cccd);
    uint8_t initial_num[4]{};
    from_num->setValue(initial_num, sizeof(initial_num));
    service->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(MESH_SERVICE_UUID);
    BLEAdvertisementData scan_response;
    scan_response.setName(ble_name);
    adv->setScanResponseData(scan_response);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    adv->start();

    Serial.printf("[MT-BLE] advertising %s (upstream PhoneAPI, %s)\n", ble_name,
                  pin_required ? "fixed-PIN pairing" : "no-PIN pairing");
    return true;
}

void service_phone() {
    if (!api) return;
    if (disconnect_pending.exchange(false, std::memory_order_acq_rel)) {
        api->close();
        xQueueReset(to_radio);
        xQueueReset(from_radio);
        read_requested.store(false, std::memory_order_release);
        if (!shutting_down.load(std::memory_order_acquire) && server)
            BLEDevice::startAdvertising();
    }
    // Do not let any BLE callback enter NodeDB, MeshService or PhoneAPI.
    ToRadioFrame input{};
    for (unsigned i=0; i<kToRadioQueueDepth &&
                       xQueueReceive(to_radio, &input, 0) == pdTRUE; ++i)
        api->handleToRadio(input.data, input.size);
    if (read_requested.exchange(false, std::memory_order_acq_rel)) {
        FromRadioFrame output{};
        output.size = static_cast<uint16_t>(api->getFromRadio(output.data));
        if (xQueueSend(from_radio, &output, 0) != pdTRUE)
            Serial.println("[MT-BLE] FromRadio handoff full");
    }
}

void snapshot_gps() {
#if ENV_INCLUDE_GPS == 1
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_gps_snapshot) < 0) return;
    next_gps_snapshot = now + 250;
    MeshInkMeshtasticGpsSnapshot snapshot{};
    snapshot.fix = meshink_gps_read_status();
    snapshot.enabled = meshink_gps_constellation_mode() != MeshInkGpsConstellationMode::None;
    snapshot.rtc_valid = meshink_rtc_valid();
    snapshot.gps_authoritative = meshink_rtc_gps_authoritative();
    snapshot.utc = snapshot.rtc_valid ? meshink_rtc_current_time() : 0;
    meshink_meshtastic_native_set_gps_snapshot(snapshot);
    meshink_meshtastic_native_gps_update();
#endif
}
} // namespace

bool meshink_meshtastic_companion_active() { return active; }

void meshink_meshtastic_request_companion() {
    Preferences prefs;
    if (!prefs.begin(kBootNamespace, false)) return;
    const bool written = prefs.putBool(kCompanionRequest, true) == 1;
    prefs.end();
    if (!written) return;
    // The native worker must finish its deferred flash saves before reboot.
    meshink_meshtastic_native_stop();
    delay(100);
    ESP.restart();
}

bool meshink_meshtastic_consume_companion_request() {
    Preferences prefs;
    if (!prefs.begin(kBootNamespace, false)) return false;
    const bool request = prefs.getBool(kCompanionRequest, false);
    if (request) prefs.remove(kCompanionRequest);
    prefs.end();
    return request;
}

void meshink_meshtastic_companion_setup() {
    active = true;
    ready = false;
    shutting_down.store(false, std::memory_order_release);

    // Match MeshCore: show one e-paper image and release its buffers and
    // shared GPIOs before the native SX1262 driver or Bluetooth starts.
    meshink_show_companion_notice();
    meshink_board_begin_companion();
    if (!meshink_storage_mount_internal_safe() || !meshink_backup_recover_pending()) {
        Serial.println("[MT-BLE] storage unavailable or interrupted restore; refusing headless start");
        return;
    }
    if (!meshink_message_store().begin())
        Serial.println("[MT-BLE] MeshInk message journal unavailable");
    if (!meshink_meshtastic_native_begin()) {
        Serial.println("[MT-BLE] native Meshtastic radio failed");
        return;
    }

    to_radio = xQueueCreate(kToRadioQueueDepth, sizeof(ToRadioFrame));
    from_radio = xQueueCreate(1, sizeof(FromRadioFrame));
    if (!to_radio || !from_radio) {
        Serial.println("[MT-BLE] BLE transport queue allocation failed");
        return;
    }
    api.reset(new(std::nothrow) CompanionPhoneAPI());
    if (!api || !start_ble()) {
        Serial.println("[MT-BLE] BLE service failed to start");
        return;
    }

#if ENV_INCLUDE_GPS == 1
    meshink_gps_probe_after_radio(true);
    meshink_gps_service_begin();
    meshink_gps_set_provider_enabled(
        meshink_gps_constellation_mode() != MeshInkGpsConstellationMode::None);
#endif
    meshink_board_boot_complete();
    meshink_performance_set_cpu_mhz(80);
    ready = true;
    Serial.println("[MT-BLE] headless companion READY");
}

void meshink_meshtastic_companion_loop() {
    if (!ready) { delay(50); return; }
    service_phone();
    meshink_gps_service_loop();
    meshink_gps_background_tick();
    meshink_rtc_tick();
    snapshot_gps();
    meshink_meshtastic_native_loop();
    // Official AdminModule schedules a reboot through this timestamp. The
    // upstream standalone main loop is intentionally absent from MeshInk.
    extern uint32_t rebootAtMsec;
    if (rebootAtMsec && static_cast<int32_t>(millis() - rebootAtMsec) >= 0) {
        meshink_meshtastic_companion_prepare_exit();
        ESP.restart();
    }
    delay(1); // Keep the ESP32-S3 idle watchdog serviced.
}

void meshink_meshtastic_companion_prepare_exit() {
    shutting_down.store(true, std::memory_order_release);
    read_requested.store(false, std::memory_order_release);
    if (server && connected.load(std::memory_order_acquire))
        server->disconnect(0);
    // Let the pending GATT disconnect/read callbacks complete before deinit.
    delay(100);
    BLEDevice::deinit(false);
    from_num = nullptr;
    server = nullptr;
    connected.store(false, std::memory_order_release);
    if (api) { api->close(); api.reset(); }
    meshink_meshtastic_native_stop();
#if ENV_INCLUDE_GPS == 1
    meshink_gps_shutdown();
#endif
    meshink_radio_power_off();
    meshink_board_companion_release_resources();
    SPIFFS.end();
    ready = false;
    active = false;
}

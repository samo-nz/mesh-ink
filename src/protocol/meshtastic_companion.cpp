#include "meshtastic_companion.h"
#include "meshtastic_runtime.h"
#include "mesh_protocol.h"
#include "../../lib/Meshtastic/src/BluetoothCommon.h"
#include "../../lib/Meshtastic/src/target_specific.h"
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
// Upstream NimBLE tolerates slow initial config reads for up to ~20s.
constexpr uint32_t kBleReadTimeoutMs = 20000;
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
char ble_name[30]{}; // 29-byte maximum scan-response name (incl. AD overhead)
char last_native_timezone[65]{};
uint32_t next_timezone_check=0;

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
        Serial.printf("[MT-BLE] GATT connected uptime=%lus heap=%u\n",
                      (unsigned long)(millis()/1000), (unsigned)ESP.getFreeHeap());
        connected.store(true, std::memory_order_release);
        disconnect_pending.store(false, std::memory_order_release);
    }
    void onDisconnect(BLEServer*) override {
        Serial.printf("[MT-BLE] GATT disconnected uptime=%lus\n",
                      (unsigned long)(millis()/1000));
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
        else
            Serial.printf("[MT-BLE] ToRadio write received %u bytes\n", (unsigned)frame.size);
    }
};
class FromRadioCallbacks final : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* ch) override {
        if (!from_radio || shutting_down.load(std::memory_order_acquire) ||
            !connected.load(std::memory_order_acquire)) {
            ch->setValue(static_cast<uint8_t*>(nullptr), 0);
            return;
        }
        Serial.println("[MT-BLE] FromRadio GATT read requested");
        read_requested.store(true, std::memory_order_release);
        FromRadioFrame frame{};
        // The Bluetooth task never calls PhoneAPI. The headless protocol loop
        // handles ToRadio first, then supplies this read. This preserves the
        // official write-then-read configuration handshake ordering.
        if (xQueueReceive(from_radio, &frame, pdMS_TO_TICKS(kBleReadTimeoutMs)) == pdTRUE) {
            ch->setValue(frame.data, frame.size);
            Serial.printf("[MT-BLE] FromRadio GATT read supplied %u bytes\n",
                          (unsigned)frame.size);
        }
        else {
            read_requested.store(false, std::memory_order_release);
            ch->setValue(static_cast<uint8_t*>(nullptr), 0);
            Serial.println("[MT-BLE] FromRadio read timeout");
        }
    }
};
// Arduino's ESP32 Bluedroid security callback is optional, but reporting
// actual SMP results makes a phone connection failure diagnosable without
// logging any secret passkeys or message contents.
class PairingCallbacks final : public BLESecurityCallbacks {
  public:
    uint32_t onPassKeyRequest() override {
        Serial.println("[MT-BLE] Pairing: passkey requested");
        return config.bluetooth.fixed_pin;
    }
    void onPassKeyNotify(uint32_t) override {
        Serial.println("[MT-BLE] Pairing: enter configured fixed PIN on phone");
    }
    bool onSecurityRequest() override {
        Serial.println("[MT-BLE] Pairing: security request accepted");
        return true;
    }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
        Serial.printf("[MT-BLE] Pairing: authentication %s reason=%u\n",
                      result.success?"OK":"FAILED", (unsigned)result.fail_reason);
    }
    bool onConfirmPIN(uint32_t) override {
        // No dynamic code display or user confirmation exists in this
        // deliberately headless mode. Reject numeric comparison.
        Serial.println("[MT-BLE] Pairing: numeric comparison unavailable");
        return false;
    }
};
ServerCallbacks server_callbacks;
ToRadioCallbacks to_radio_callbacks;
FromRadioCallbacks from_radio_callbacks;
PairingCallbacks pairing_callbacks;
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
    // The native Meshtastic owner wins after the Android app has customized
    // it. Until then use MeshInk's configured node name instead of advertising
    // Meshtastic_xxxx when the original on-device setup already named us.
    // This changes only the BLE alias, not Meshtastic's stored owner fields.
    const unsigned default_suffix = nodeDB ? (nodeDB->getNodeNum() & 0xffffU) : 0U;
    char default_owner[40]{};
    snprintf(default_owner, sizeof(default_owner), "Meshtastic %04x", default_suffix);
    const bool native_name_custom = owner.long_name[0] &&
        strcmp(owner.long_name, default_owner) != 0;
    Preferences ui_prefs;
    String meshink_name;
    if (ui_prefs.begin("t5-ui", true)) {
        meshink_name = ui_prefs.getString("name", "");
        ui_prefs.end();
    }
    const char* source = native_name_custom ? "native" :
                         meshink_name.length() ? "MeshInk" : "default";
    const char* desired = native_name_custom ? owner.long_name :
                          meshink_name.length() ? meshink_name.c_str() : default_owner;
    snprintf(ble_name, sizeof(ble_name), "%.29s", desired);
    Serial.printf("[MT-BLE] advertised name=%s source=%s\n", ble_name, source);

    BLEDevice::init(ble_name);
    BLEDevice::setMTU(517);
    const bool pin_required =
        config.bluetooth.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    if (pin_required) {
        BLESecurity security;
        // ESP32 Arduino's setStaticPIN() internally changes authentication
        // mode to SC_ONLY. Install the PIN *first*, then restore MITM+bonding.
        // Otherwise the encrypted-MITM GATT permissions can never be met.
        security.setStaticPIN(config.bluetooth.fixed_pin);
        security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
        security.setCapability(ESP_IO_CAP_OUT);
        security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        BLEDevice::setSecurityCallbacks(&pairing_callbacks);
    }
    server = BLEDevice::createServer();
    if (!server) return false;
    server->setCallbacks(&server_callbacks);
    BLEService* service = server->createService(MESH_SERVICE_UUID);
    if (!service) return false;

    // Bluedroid uses plain GATT characteristic properties and separate
    // access permissions. The *_ENC and *_AUTHEN properties are NimBLE-only.
    BLECharacteristic* write_ch =
        service->createCharacteristic(TORADIO_UUID, BLECharacteristic::PROPERTY_WRITE);
    BLECharacteristic* read_ch =
        service->createCharacteristic(FROMRADIO_UUID, BLECharacteristic::PROPERTY_READ);
    from_num = service->createCharacteristic(FROMNUM_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    if (!write_ch || !read_ch || !from_num) return false;
    if (pin_required) {
        write_ch->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
        read_ch->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
        from_num->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
    }
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
        Serial.println("[MT-BLE] closing PhoneAPI after disconnect; restart advertising");
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
        Serial.printf("[MT-BLE] PhoneAPI FromRadio generated %u bytes\n",
                      (unsigned)output.size);
        if (xQueueSend(from_radio, &output, 0) != pdTRUE)
            Serial.println("[MT-BLE] FromRadio handoff full");
    }
}

// Track app edits to Meshtastic's authoritative device.tzdef without
// starting any MeshInk UI services. NVS is updated immediately on change,
// not only on a graceful BOOT exit, so ordinary power-cycle/reboot also
// restores the right local timezone. No writes during the steady state.
void sync_native_timezone_to_meshink(bool immediate=false) {
    if (!active || !nodeDB) return;
    const uint32_t now=millis();
    if (!immediate && (int32_t)(now-next_timezone_check)<0)return;
    next_timezone_check=now+500UL;
    char rule[65]{};
    snprintf(rule,sizeof(rule),"%s",config.device.tzdef);
    if (!strcmp(rule,last_native_timezone))return;

    // Map exact supported presets back to their native MeshInk selection.
    // Any other POSIX string is losslessly preserved as the phone-app mode
    // (timezone index 9) instead of approximating or discarding DST rules.
    static constexpr const char* preset[]={
        nullptr,"NZST-12NZDT,M9.5.0,M4.1.0/3","UTC0",
        "AEST-10AEDT,M10.1.0,M4.1.0/3",
        "GMT0BST,M3.5.0/1,M10.5.0",
        "CET-1CEST,M3.5.0,M10.5.0/3",
        "PST8PDT,M3.2.0,M11.1.0",
        "EST5EDT,M3.2.0,M11.1.0"
    };
    uint8_t index=9;
    for(uint8_t i=1;i<sizeof(preset)/sizeof(preset[0]);++i)
        if(!strcmp(rule,preset[i])){index=i;break;}
    Preferences prefs;
    if(!prefs.begin("t5-ui",false)){
        Serial.println("[MT-TZ] ERROR: unable to open MeshInk preferences");
        return;
    }
    // Store the raw string before activating the imported selection.
    // This allows the UI to apply even rules not included in its picker.
    const bool stored_raw=rule[0]
        ?prefs.putString("mt_tz_rule",rule)==strlen(rule)
        :prefs.putString("mt_tz_rule",rule)==0;
    const bool stored_v2=prefs.putBool("tz_v2",true)==1;
    const bool stored_index=prefs.putUChar("timezone",index)==1;
    prefs.end();
    if(!stored_raw||!stored_v2||!stored_index){
        Serial.println("[MT-TZ] ERROR: timezone sync persistence failed");
        return;
    }
    snprintf(last_native_timezone,sizeof(last_native_timezone),"%s",rule);
    Serial.printf("[MT-TZ] phone -> MeshInk: index=%u rule=%s\\n",
                  (unsigned)index,rule);
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
    // Stop via the selected backend: it drains the network worker and native
    // flash saves on their owning task before we switch boot modes.
    mesh_protocol_prepare_shutdown();
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
    snprintf(last_native_timezone,sizeof(last_native_timezone),"%s",
             config.device.tzdef);
    next_timezone_check=millis()+500UL;

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
    sync_native_timezone_to_meshink();
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
    // Final sync also covers a timezone change immediately before BOOT.
    sync_native_timezone_to_meshink(true);
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

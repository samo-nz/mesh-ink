#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp32-hal-cpu.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#include <BLEAdvertising.h>
#include "../lib/MeshCore/examples/companion_radio/DataStore.cpp"
#include "../lib/MeshCore/examples/companion_radio/MyMesh.cpp"
#include "companion_runtime.h"
#include "companion_notice.h"
#include "local_mesh_runtime.h"
#include "ui_onboarding.h"
#include "t5_logging.h"
#include "hardware/board.h"
#include "hardware/gps.h"
#include "hardware/rtc.h"
#include "hardware/radio.h"

// Device-owned composition root for the unmodified upstream MeshCore companion
// classes. This is deliberately small so upstream updates remain easy to diff.
static MultiSerialInterface interface_manager;
static SerialBLEInterface bluetooth_interface;
static DataStore store(SPIFFS, meshink_rtc_meshcore());
static StdRNG fast_rng;
static SimpleMeshTables tables;
void local_mesh_on_frame(const uint8_t*, size_t);

class LocalSerial final : public BaseSerialInterface {
    bool enabled=false;
    uint8_t pending=0;
    uint8_t command[MAX_FRAME_SIZE+1]{};
    size_t command_len=0;
public:
    void enable() override { enabled=true; } void disable() override { enabled=false; }
    bool isEnabled() const override { return enabled; } bool isConnected() const override { return true; }
    bool isWriteBusy() const override { return false; }
    size_t writeFrame(const uint8_t* frame,size_t len) override { if(len==1&&frame[0]==0x83){if(pending<255)pending++;}else local_mesh_on_frame(frame,len);return len; }
    size_t checkRecvFrame(uint8_t* frame) override {
        if(command_len){const size_t len=command_len;memcpy(frame,command,len);command_len=0;return len;}
        if(!pending)return 0;pending--;frame[0]=10;return 1;
    }
    bool enqueue(const uint8_t* frame,size_t len){
        if(!frame||!len||len>sizeof(command)||command_len)return false;
        memcpy(command,frame,len);command_len=len;return true;
    }
};

static LocalSerial local_interface;
static bool local_runtime_ready=false;

static void companion_set_low_power_cpu() {
    static constexpr uint32_t COMPANION_CPU_MHZ=80;
    const bool accepted=setCpuFrequencyMhz(COMPANION_CPU_MHZ);
    const uint32_t actual=getCpuFrequencyMhz();
    Serial.printf("[T5-POWER] companion cpu target=%lu actual=%luMHz result=%s\n",
                  (unsigned long)COMPANION_CPU_MHZ,(unsigned long)actual,
                  accepted&&actual==COMPANION_CPU_MHZ?"OK":"ERROR");
}

static void companion_configure_ble_scan_response(const char* prefix,const char* node_name) {
    // Legacy BLE advertising/scan-response payloads are capped at 31 bytes.
    // MeshCore's default ESP32 helper duplicates the 128-bit UART service UUID
    // into the scan response along with name/TX power, which overflows that
    // budget and makes Bluedroid print "Partial data write into ADV". Keep the
    // service UUID in the primary advertisement; use the scan response only
    // for the discoverable device name.
    char scan_name[30]{};
    const int full_len=snprintf(scan_name,sizeof(scan_name),"%s%s",
                                prefix?prefix:"",node_name?node_name:"");
    BLEAdvertisementData scan_response;
    const bool truncated=full_len<0||full_len>=(int)sizeof(scan_name);
    if(truncated)scan_response.setShortName(scan_name);
    else scan_response.setName(scan_name);
    BLEDevice::getAdvertising()->setScanResponseData(scan_response);
    Serial.printf("[T5-BLE] scan response name='%s' kind=%s payload=%u/31 bytes\n",
                  scan_name,truncated?"short":"complete",
                  (unsigned)scan_response.getPayload().size());
}

MyMesh the_mesh(meshink_radio_meshcore(), fast_rng, meshink_rtc_meshcore(), tables, store);
MyMesh& t5_mesh() { return the_mesh; }
bool local_mesh_enqueue_command(const uint8_t* frame,size_t len){return local_interface.enqueue(frame,len);}

void companion_setup() {
    T5_DEBUGLN(T5_LOG_MESH,"[T5-BOOT] starting upstream MeshCore companion runtime");
    meshink_show_companion_notice();
    meshink_board_begin_companion();
    if (!meshink_radio_initialize()) {
        Serial.printf("[T5-BOOT] fatal: %s initialization failed\n",meshink_radio_name());
        while (true) delay(1000);
    }
    fast_rng.begin(meshink_radio_rng_seed());
    SPIFFS.begin(true);
    store.begin();
    the_mesh.begin(false);
    bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name,
                              the_mesh.getBLEPin());
    companion_configure_ble_scan_response(BLE_NAME_PREFIX,the_mesh.getNodePrefs()->node_name);
    interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
    the_mesh.startInterface(interface_manager);
    meshink_gps_service_begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    meshink_board_boot_complete();

    // Companion mode has no local UI/render workload. Once BLE, MeshCore,
    // radio and optional GPS setup have completed, 80 MHz is enough for the
    // steady-state service loop while keeping the ESP32-S3 peripheral/APB
    // domain at its normal rate. Test10 validates this on hardware.
    companion_set_low_power_cpu();
}

void companion_loop() {
    the_mesh.loop();
    interface_manager.loop();
    meshink_gps_service_loop();
    meshink_rtc_tick();

    // The cache64 release uses Arduino + ESP-IDF with the task watchdog
    // enabled. Unlike the local UI path, companion mode previously returned
    // here without ever blocking/yielding, so loopTask could starve IDLE1
    // until the watchdog fired. A 1 ms delay yields CPU1 to FreeRTOS while
    // keeping MeshCore/BLE servicing effectively continuous.
    delay(1);
}

static bool companion_persist_contact(const ContactInfo& contact) {
    return contact.type!=ADV_TYPE_NONE;
}

void companion_prepare_exit() {
    const uint32_t shutdown_started=millis();
    meshink_board_companion_exit_feedback_begin();

    const bool ble_connected=bluetooth_interface.isConnected();
    Serial.printf("[T5-BOOT] companion shutdown: BLE connected=%u; stopping MeshCore interface\n",
                  ble_connected?1U:0U);

    // Upstream SerialBLEInterface::disable() always calls disconnect(last_conn_id).
    // If the phone has already disconnected, that stale ID makes Bluedroid emit
    // "Unknown connection ID". Detach the transport from MeshCore instead of
    // issuing a redundant disconnect. BLEDevice::deinit() below then shuts the
    // controller/host stack down cleanly in both cases.
    if(ble_connected){
        interface_manager.disable();

        // Give the requested disconnect/GAP callback a short bounded window
        // to settle before shutting the Bluetooth stack down.
        const uint32_t settle_started=millis();
        while(millis()-settle_started<100){
            meshink_gps_service_loop();
            meshink_rtc_tick();
            delay(1);
        }
    }else{
        interface_manager.removeInterface(&bluetooth_interface);
        interface_manager.disable();
    }
    BLEDevice::deinit(false);

    Serial.println("[T5-BOOT] companion shutdown: saving MeshCore state");
    the_mesh.savePrefs();
    store.saveContacts(&the_mesh,companion_persist_contact);
    store.saveChannels(&the_mesh);

#if ENV_INCLUDE_GPS == 1
    meshink_gps_shutdown();
#endif

    Serial.println("[T5-BOOT] companion shutdown: powering radio down");
    meshink_radio_power_off();
    meshink_board_companion_release_resources();
    SPIFFS.end();

    Serial.printf("[T5-BOOT] companion shutdown complete elapsed=%lums\n",
                  (unsigned long)(millis()-shutdown_started));
    Serial.flush();
}

void local_mesh_setup() {
    T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] starting upstream MeshCore runtime; Bluetooth disabled");
    meshink_board_begin_local();
    const bool radio_ready=meshink_radio_initialize();
    if (!radio_ready) {
        const MeshInkRadioFailureClass failure=meshink_radio_classify_failure();
        Serial.printf("[T5-MESH] ERROR: %s unavailable; failure-class=%u\n",meshink_radio_name(),(unsigned)failure);
        ui_show_radio_failure(failure);
        return;
    }
    fast_rng.begin(meshink_radio_rng_seed());
    // Probe without formatting, so an existing filesystem gets the fast
    // "STARTING UP..." splash. Only show "INITIALISING STORAGE..." if the
    // partition does not mount and the original format-on-failure path is
    // actually necessary (first install or filesystem recovery).
    bool storage_mounted=SPIFFS.begin(false);
    if(!storage_mounted){
        Serial.println("[T5-STORE] SPIFFS mount failed; showing storage initialization splash");
        ui_show_storage_initializing();
        storage_mounted=SPIFFS.begin(true);
    }
    Serial.printf("[T5-STORE] SPIFFS mount result=%s\n",storage_mounted?"OK":"FAILED");
    store.begin(); the_mesh.begin(true); the_mesh.startInterface(local_interface);
    meshink_gps_service_begin();
#if ENV_INCLUDE_GPS == 1
    // MeshCore defaults GPS off even though the receiver on this board shares
    // the always-on LoRa rail. For a NEW local-UI setup, default the SOFTWARE
    // GPS provider to ON with continuous reads (interval=0). Do not override
    // any completed setup's GPS preferences, including an explicit OFF.
    // Remember the one-time initialization so an incomplete setup that has
    // since changed GPS settings is not reset on its next boot.
    Preferences initial_gps;
    if(initial_gps.begin("t5-ui",false)){
        const bool configured=initial_gps.getBool("complete",false);
        const bool default_applied=initial_gps.getBool("gps_default_v1",false);
        if(!configured&&!default_applied){
            auto* settings=the_mesh.getNodePrefs();
            settings->gps_enabled=1;
            settings->gps_interval=0;
            the_mesh.savePrefs();
            initial_gps.putBool("gps_default_v1",true);
            Serial.println("[T5-BOOT] new setup GPS default: enabled, continuous");
        }
        initial_gps.end();
    }
    the_mesh.applyGpsPrefs();
#endif
    local_mesh_runtime_begin();
    ui_use_data_provider(local_mesh_provider());
    ui_mesh_ready();
    ui_apply_initial_radio_preset(); // fix first boot's displayed-vs-active radio mismatch
    local_runtime_ready=true;
    Serial.printf("[T5-BOOT] MeshCore ready: contacts=%d\n",the_mesh.getNumContacts());
    T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] ready name='%s' contacts=%d\n",the_mesh.getNodeName(),the_mesh.getNumContacts());
}
bool local_mesh_is_running(){return local_runtime_ready;}

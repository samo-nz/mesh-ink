#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#include "../lib/MeshCore/examples/companion_radio/DataStore.cpp"
#include "../lib/MeshCore/examples/companion_radio/MyMesh.cpp"
#include "companion_runtime.h"
#include "local_mesh_runtime.h"
#include "ui_onboarding.h"

// Device-owned composition root for the unmodified upstream MeshCore companion
// classes. This is deliberately small so upstream updates remain easy to diff.
static MultiSerialInterface interface_manager;
static SerialBLEInterface bluetooth_interface;
static DataStore store(SPIFFS, rtc_clock);
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
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);
MyMesh& t5_mesh() { return the_mesh; }
bool local_mesh_enqueue_command(const uint8_t* frame,size_t len){return local_interface.enqueue(frame,len);}

void companion_setup() {
    Serial.println("[T5-BOOT] starting upstream MeshCore companion runtime");
    board.begin();
    if (!radio_init()) {
        Serial.println("[T5-BOOT] fatal: SX1262 initialization failed");
        while (true) delay(1000);
    }
    fast_rng.begin(radio_driver.getRngSeed());
    SPIFFS.begin(true);
    store.begin();
    the_mesh.begin(false);
    bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name,
                              the_mesh.getBLEPin());
    interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
    the_mesh.startInterface(interface_manager);
    sensors.begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    board.onBootComplete();
}

void companion_loop() {
    the_mesh.loop();
    interface_manager.loop();
    sensors.loop();
    rtc_clock.tick();
}

void local_mesh_setup() {
    Serial.println("[T5-MESH] starting upstream MeshCore runtime; Bluetooth disabled");
    board.beginLocal();
    bool radio_ready=false;
    for(uint8_t attempt=1;attempt<=3&&!radio_ready;++attempt){
        radio_ready=radio_init();
        if(!radio_ready){Serial.printf("[T5-MESH] SX1262 initialization attempt %u/3 failed; retrying\n",attempt);delay(500);}
    }
    if (!radio_ready) { Serial.println("[T5-MESH] ERROR: SX1262 unavailable; stopping on hardware failure screen");ui_show_radio_failure();return; }
    fast_rng.begin(radio_driver.getRngSeed());
    SPIFFS.begin(true); store.begin(); the_mesh.begin(true); the_mesh.startInterface(local_interface);
    sensors.begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    local_mesh_runtime_begin();
    ui_use_data_provider(local_mesh_provider());
    ui_mesh_ready();
    local_runtime_ready=true;
    Serial.printf("[T5-MESH] ready name='%s' contacts=%d\n",the_mesh.getNodeName(),the_mesh.getNumContacts());
}
bool local_mesh_is_running(){return local_runtime_ready;}

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
void local_mesh_on_direct(const ContactInfo&, uint32_t, const char*);
void local_mesh_on_channel(const mesh::GroupChannel&, uint32_t, const char*);
static bool local_ui_runtime = false;

class T5Mesh final : public MyMesh {
public:
    using MyMesh::MyMesh;
protected:
    void onMessageRecv(const ContactInfo& from, mesh::Packet* packet, uint32_t timestamp, const char* text) override {
        if(local_ui_runtime)local_mesh_on_direct(from,timestamp,text);else MyMesh::onMessageRecv(from,packet,timestamp,text);
    }
    void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* packet, uint32_t timestamp, const uint8_t* prefix, const char* text) override {
        if(local_ui_runtime)local_mesh_on_direct(from,timestamp,text);else MyMesh::onSignedMessageRecv(from,packet,timestamp,prefix,text);
    }
    void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* packet, uint32_t timestamp, const char* text) override {
        if(local_ui_runtime)local_mesh_on_channel(channel,timestamp,text);else MyMesh::onChannelMessageRecv(channel,packet,timestamp,text);
    }
};

class LocalSerial final : public BaseSerialInterface {
    bool enabled=false;
public:
    void enable() override { enabled=true; } void disable() override { enabled=false; }
    bool isEnabled() const override { return enabled; } bool isConnected() const override { return false; }
    bool isWriteBusy() const override { return false; }
    size_t writeFrame(const uint8_t[],size_t len) override { return len; }
    size_t checkRecvFrame(uint8_t[]) override { return 0; }
};

static LocalSerial local_interface;
static T5Mesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);
MyMesh& t5_mesh() { return the_mesh; }

void companion_setup() {
    local_ui_runtime = false;
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
    local_ui_runtime = true;
    Serial.println("[T5-MESH] starting upstream MeshCore runtime; Bluetooth disabled");
    board.beginLocal();
    if (!radio_init()) { Serial.println("[T5-MESH] fatal: SX1262 initialization failed"); return; }
    fast_rng.begin(radio_driver.getRngSeed());
    SPIFFS.begin(true); store.begin(); the_mesh.begin(true); the_mesh.startInterface(local_interface);
    sensors.begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    ui_use_data_provider(local_mesh_provider());
    Serial.printf("[T5-MESH] ready name='%s' contacts=%d\n",the_mesh.getNodeName(),the_mesh.getNumContacts());
}

#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#include "../lib/MeshCore/examples/companion_radio/MyMesh.cpp"
#include "companion_runtime.h"

// Device-owned composition root for the unmodified upstream MeshCore companion
// classes. This is deliberately small so upstream updates remain easy to diff.
static MultiSerialInterface interface_manager;
static SerialBLEInterface bluetooth_interface;
static DataStore store(SPIFFS, rtc_clock);
static StdRNG fast_rng;
static SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

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

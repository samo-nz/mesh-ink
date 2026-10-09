// Headless Meshtastic platform glue. All board power policy remains in MeshInk.
#include "../../lib/Meshtastic/src/configuration.h"
#include "../../lib/Meshtastic/src/main.h"
#include "../../lib/Meshtastic/src/sleep.h"
#include "../../lib/Meshtastic/src/target_specific.h"
#include "../hardware/power.h"
#include <esp_efuse.h>
#include <esp_mac.h>
#include <esp_efuse_table.h>
#include <cstring>

// MeshInk already owns USB CDC. Use Meshtastic's genuine RedirectablePrint
// on MeshInk's existing Serial, without constructing StreamAPI/SerialConsole.
RedirectablePrint &meshink_meshtastic_debug_port(){
    static RedirectablePrint logger(&Serial);
    static bool initialized=false;
    if(!initialized){logger.rpInit();initialized=true;}
    return logger;
}

meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();
meshtastic::BluetoothStatus *bluetoothStatus = new meshtastic::BluetoothStatus();
// Only the upstream optional HardwareRNG guard reads this pointer; it is
// correctly null because MeshInk owns USB and has no Meshtastic SerialConsole.
// Native LOG_* always uses meshink_meshtastic_debug_port(), never this pointer.
SerialConsole *console=nullptr;
ScanI2C::DeviceAddress screen_found{}; // default I2CPort::NO_I2C

// Native radio subscribes to these lifecycle events. MeshInk controls sleep.
Observable<void *> preflightSleep;
Observable<void *> notifyDeepSleep;
Observable<void *> notifyReboot;

void getMacAddr(uint8_t *mac) {
    if(!mac)return;
    if(esp_efuse_mac_get_default(mac)!=ESP_OK)memset(mac,0,6);
}

bool getDeviceId(uint8_t *deviceId) {
    if(!deviceId)return false;
    // Match the official ESP32-S3 eFuse identity implementation.
    uint32_t uniqueId[4]{};
    if(esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID,
                                 uniqueId,sizeof(uniqueId)*8)!=ESP_OK)
        return false;
    memcpy(deviceId,uniqueId,sizeof(uniqueId));
    return true;
}

bool powerHAL_isPowerLevelSafe() {
    // Do not replicate board voltage or debounce thresholds here.
    MeshInkPowerCriticalState state{};
    return !meshink_power_poll_critical(state);
}

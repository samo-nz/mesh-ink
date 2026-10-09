// Headless Meshtastic platform glue. All board power policy remains in MeshInk.
#include "../../lib/Meshtastic/src/configuration.h"
#include "../../lib/Meshtastic/src/main.h"
#include "../../lib/Meshtastic/src/sleep.h"
#include "../../lib/Meshtastic/src/target_specific.h"
#include "../hardware/power.h"
#include <esp_efuse.h>
#include <esp_efuse_mac.h>
#include <esp_efuse_table.h>
#include <cstring>

// Meshtastic has no serial transport on this boot: MeshInk owns USB CDC.
// The upstream DEBUG_PORT fallback checks this pointer before using it.
class SerialConsole;
SerialConsole *console=nullptr;

meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();
meshtastic::BluetoothStatus *bluetoothStatus = new meshtastic::BluetoothStatus();
ScanI2C::DeviceAddress screen_found = ScanI2C::ADDRESS_NONE;

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

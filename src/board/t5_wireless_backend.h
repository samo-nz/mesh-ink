#pragma once

#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include "../hardware/wireless_types.h"

namespace meshink_t5_wireless_detail {

inline bool wifi_driver_off() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    return esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT;
}

inline bool bluetooth_controller_off() {
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
}

inline bool bluetooth_host_off() {
    return esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED;
}

inline void force_wifi_off() {
    const esp_err_t stopped = esp_wifi_stop();
    if(stopped != ESP_OK && stopped != ESP_ERR_WIFI_NOT_INIT) {
        // Continue to deinit/verify. Final state, not the intermediate error,
        // is the power-policy contract exposed to the application.
    }
    const esp_err_t deinitialized = esp_wifi_deinit();
    (void)deinitialized;
}

inline void force_bluetooth_off() {
    esp_bluedroid_status_t host = esp_bluedroid_get_status();
    if(host == ESP_BLUEDROID_STATUS_ENABLED)
        esp_bluedroid_disable();
    host = esp_bluedroid_get_status();
    if(host == ESP_BLUEDROID_STATUS_INITIALIZED)
        esp_bluedroid_deinit();

    esp_bt_controller_status_t controller = esp_bt_controller_get_status();
    if(controller == ESP_BT_CONTROLLER_STATUS_ENABLED)
        esp_bt_controller_disable();
    controller = esp_bt_controller_get_status();
    if(controller == ESP_BT_CONTROLLER_STATUS_INITED)
        esp_bt_controller_deinit();
}

} // namespace meshink_t5_wireless_detail

inline MeshInkWirelessState meshink_wireless_read_state() {
    using namespace meshink_t5_wireless_detail;
    MeshInkWirelessState state{};
    state.wifi_off = wifi_driver_off();
    state.bluetooth_controller_off = bluetooth_controller_off();
    state.bluetooth_host_off = bluetooth_host_off();
    return state;
}

inline MeshInkWirelessState meshink_wireless_force_wifi_off() {
    meshink_t5_wireless_detail::force_wifi_off();
    return meshink_wireless_read_state();
}

inline MeshInkWirelessState meshink_wireless_force_local_radios_off() {
    meshink_t5_wireless_detail::force_wifi_off();
    meshink_t5_wireless_detail::force_bluetooth_off();
    return meshink_wireless_read_state();
}

#pragma once

// Board-independent view of the ESP-class local wireless state.
// "off" means the corresponding software/controller stack is deinitialized,
// which is the state MeshInk requires while the standalone UI is running.
struct MeshInkWirelessState {
    bool wifi_off = false;
    bool bluetooth_controller_off = false;
    bool bluetooth_host_off = false;
};

inline bool meshink_wireless_local_radios_off(const MeshInkWirelessState& state) {
    return state.wifi_off &&
           state.bluetooth_controller_off &&
           state.bluetooth_host_off;
}

inline bool meshink_wireless_companion_radios_ready(const MeshInkWirelessState& state) {
    return state.wifi_off &&
           !state.bluetooth_controller_off &&
           !state.bluetooth_host_off;
}

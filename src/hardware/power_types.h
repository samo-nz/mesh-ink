#pragma once
#include <stdint.h>

enum class MeshInkPowerOffReason : uint8_t {
    User = 0,
    LowBattery = 1
};

enum class MeshInkChargeState : uint8_t {
    Unknown = 0,
    Idle,
    Charging,
    Full
};

struct MeshInkPowerStatus {
    bool battery_voltage_valid = false;
    uint16_t battery_mv = 0;
    bool battery_percent_valid = false;
    uint8_t battery_percent = 0;
    MeshInkChargeState charge_state = MeshInkChargeState::Unknown;
    bool external_power = false;
};

struct MeshInkPowerCriticalState {
    bool critical = false;
    bool battery_mv_valid = false;
    uint16_t battery_mv = 0;
};

// User-facing wake guidance is board-owned because the physical control that
// restores power may not be readable or controllable by the running firmware.
struct MeshInkPowerWakeInfo {
    const char* confirm_battery = "";
    const char* confirm_external = "";
    const char* off_battery_line1 = "";
    const char* off_battery_line2 = "";
    const char* off_external_line1 = "";
    const char* off_external_line2 = "";
};

inline bool meshink_power_is_charging(MeshInkChargeState state) {
    return state == MeshInkChargeState::Charging;
}

inline const char* meshink_power_charge_state_name(MeshInkChargeState state) {
    switch(state) {
        case MeshInkChargeState::Idle:return "idle";
        case MeshInkChargeState::Charging:return "charging";
        case MeshInkChargeState::Full:return "full";
        default:return "unknown";
    }
}

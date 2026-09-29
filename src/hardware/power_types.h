#pragma once
#include <stdint.h>

enum class MeshInkPowerOffReason : uint8_t {
    User = 0,
    LowBattery = 1
};

struct MeshInkPowerStatus {
    bool battery_voltage_valid = false;
    uint16_t battery_mv = 0;
    bool battery_percent_valid = false;
    uint8_t battery_percent = 0;
    bool charger_valid = false;
    uint8_t charge_state = 0;  // BQ25896: 0 idle, 1 precharge, 2 fast, 3 done
    bool external_power = false;
};

inline bool meshink_power_is_charging(uint8_t charge_state) {
    return charge_state == 1 || charge_state == 2;
}

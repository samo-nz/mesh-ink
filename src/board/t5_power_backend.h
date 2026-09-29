#pragma once

#include "../hardware/power_types.h"

void meshink_power_frontlight_begin();
void meshink_power_frontlight_set(uint8_t percent);

bool meshink_power_read_battery_mv(uint16_t& millivolts);
bool meshink_power_read_battery_percent(uint8_t& percent);
bool meshink_power_read_charge_state(uint8_t& state);
bool meshink_power_external_present();
bool meshink_power_read_status(MeshInkPowerStatus& status);

// Restore the battery path if a previous ship-mode request left BATFET disabled.
void meshink_power_recover_boot_path();

// Requests board-level ship mode. If external power keeps the MCU alive after
// the BATFET command, the backend enters deep sleep with BOOT as wake source.
[[noreturn]] void meshink_power_enter_ship_mode(MeshInkPowerOffReason reason);

#pragma once

#include "../hardware/power_types.h"

void meshink_power_frontlight_begin();
void meshink_power_frontlight_set(uint8_t percent);

bool meshink_power_read_battery_mv(uint16_t& millivolts);
bool meshink_power_read_battery_percent(uint8_t& percent);
bool meshink_power_read_charge_state(MeshInkChargeState& state);
bool meshink_power_external_present();
bool meshink_power_read_status(MeshInkPowerStatus& status);

// Battery topology, chemistry and cutoff/debounce policy belong to the board
// backend. Application code receives only the resulting critical state.
bool meshink_power_boot_critical(MeshInkPowerCriticalState& state);
bool meshink_power_poll_critical(MeshInkPowerCriticalState& state);
bool meshink_power_begin_minimal_bus();
void meshink_power_end_minimal_bus();
MeshInkPowerSleepCheck meshink_power_deep_sleep_check(MeshInkPowerCriticalState& state);

// Board-owned user guidance for restoring power after shutdown/deep sleep.
const MeshInkPowerWakeInfo& meshink_power_wake_info();

// Tiny RTC-retained UI handoff used only across deep-sleep standby. The
// backend validates that the current boot really came from deep sleep and
// consumes the value once, so ordinary resets/cold boots still start normally.
void meshink_power_retain_ui_tab(uint8_t tab);
bool meshink_power_get_retained_ui_tab(uint8_t& tab);
void meshink_power_clear_retained_ui_tab();

// Board-specific battery-gauge profile startup.
void meshink_power_prepare_board();

// Restore the battery path if a previous ship-mode request left BATFET disabled.
void meshink_power_recover_boot_path();

// Requests board-level ship mode. If external power keeps the MCU alive after
// the BATFET command, the backend enters deep sleep with BOOT as wake source.
[[noreturn]] void meshink_power_enter_ship_mode(MeshInkPowerOffReason reason);

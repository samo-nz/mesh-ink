#pragma once

#include "board_profile.h"

inline const char* meshink_board_name() {
    return T5_BOARD_LABEL;
}

inline bool meshink_board_has_gps() {
    return T5_HAS_GPS != 0;
}

void meshink_board_begin_companion();
void meshink_board_start_local_radio_settle();
void meshink_board_begin_local();
void meshink_board_begin_local_rx_wake(bool packet_wake);
void meshink_board_boot_complete();
bool meshink_board_woke_from_radio();
bool meshink_board_woke_from_primary_button();
bool meshink_board_woke_from_timer();
bool meshink_board_radio_irq_asserted();
bool meshink_board_service_asserted_radio_irq();
void meshink_board_restore_deep_sleep_wake_pads();
void meshink_board_prepare_retained_aux_wake();
void meshink_board_release_retained_radio_holds();
bool meshink_board_enter_deep_sleep_standby();
bool meshink_board_return_to_retained_deep_sleep();

// RTC-retained early-wake transcript. Append calls are no-ops unless a
// deep-sleep interval armed the buffer; replay never clears it.
void meshink_board_wake_log_append(const char* line);
void meshink_board_wake_log_appendf(const char* format,...);
void meshink_board_wake_log_replay();

void meshink_board_companion_exit_feedback_begin();
void meshink_board_companion_release_resources();

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
void meshink_board_begin_local_rx_wake();
void meshink_board_boot_complete();
bool meshink_board_woke_from_radio();
bool meshink_board_woke_from_primary_button();
bool meshink_board_enter_deep_sleep_standby();
void meshink_board_companion_exit_feedback_begin();
void meshink_board_companion_release_resources();

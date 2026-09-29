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
void meshink_board_boot_complete();
void meshink_board_companion_exit_feedback_begin();
void meshink_board_companion_release_resources();

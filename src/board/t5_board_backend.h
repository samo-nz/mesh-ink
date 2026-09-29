#pragma once

#include "board_profile.h"

inline const char* meshink_board_name() {
    return T5_BOARD_LABEL;
}

inline bool meshink_board_has_gps() {
    return T5_HAS_GPS != 0;
}

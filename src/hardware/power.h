#pragma once

#include "power_types.h"

// Compile-time power backend selection. UI/application code owns policy such
// as brightness, standby timing and low-battery thresholds; the selected board
// backend owns charger/gauge registers, frontlight PWM and ship-mode mechanics.
#ifndef MESHINK_POWER_BACKEND_HEADER
#define MESHINK_POWER_BACKEND_HEADER "board/t5_power_backend.h"
#endif

#include MESHINK_POWER_BACKEND_HEADER

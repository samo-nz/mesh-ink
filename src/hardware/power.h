#pragma once

#include "power_types.h"

// Compile-time power backend selection. UI/application code owns presentation
// and user policy such as brightness/standby timing. The selected board backend
// owns battery topology/chemistry, critical-battery policy, charger/gauge
// interpretation, frontlight PWM and ship-mode mechanics.
#ifndef MESHINK_POWER_BACKEND_HEADER
#define MESHINK_POWER_BACKEND_HEADER "board/t5_power_backend.h"
#endif

#include MESHINK_POWER_BACKEND_HEADER

#pragma once

#include "touch_types.h"

// Compile-time touch backend selection, mirroring hardware/display.h.
// UI/application code includes only this file; board profiles may replace the
// controller implementation without exposing controller registers or pins.
#ifndef MESHINK_TOUCH_BACKEND_HEADER
#define MESHINK_TOUCH_BACKEND_HEADER "board/t5_touch_backend.h"
#endif

#include MESHINK_TOUCH_BACKEND_HEADER

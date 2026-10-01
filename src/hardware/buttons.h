#pragma once

// Compile-time button backend selection. Application code owns interaction
// policy (short press, hold duration, standby/exit actions); the board backend
// owns physical pins, polarity and the label printed on the enclosure.
#ifndef MESHINK_BUTTONS_BACKEND_HEADER
#define MESHINK_BUTTONS_BACKEND_HEADER "board/t5_buttons_backend.h"
#endif

#include MESHINK_BUTTONS_BACKEND_HEADER

#pragma once
#include <stdint.h>

namespace mesh { class RTCClock; }

// Board-independent clock service. MeshInk consumes time/validity through this
// surface; MeshCore receives only its own RTCClock base reference.
#ifndef MESHINK_RTC_BACKEND_HEADER
#define MESHINK_RTC_BACKEND_HEADER "board/t5_rtc_backend.h"
#endif

#include MESHINK_RTC_BACKEND_HEADER

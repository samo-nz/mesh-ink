#pragma once
#include <stdint.h>

// Compile-time CPU/performance backend selection. MeshInk application code
// expresses race-to-idle policy in MHz; the selected board/platform backend
// owns the SDK calls needed to read or change the processor clock.
#ifndef MESHINK_PERFORMANCE_BACKEND_HEADER
#define MESHINK_PERFORMANCE_BACKEND_HEADER "board/t5_performance_backend.h"
#endif

#include MESHINK_PERFORMANCE_BACKEND_HEADER

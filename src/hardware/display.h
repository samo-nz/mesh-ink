#pragma once

#include "display_types.h"

// Compile-time backend selection. There is deliberately no virtual dispatch:
// the selected backend can inline hot drawing operations exactly as before.
#ifndef MESHINK_DISPLAY_BACKEND_HEADER
#define MESHINK_DISPLAY_BACKEND_HEADER "board/t5_display_backend.h"
#endif

#include MESHINK_DISPLAY_BACKEND_HEADER

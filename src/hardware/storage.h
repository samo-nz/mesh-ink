#pragma once

// Board-selectable removable-storage service. Application/map code sees only
// read-only file operations; the selected backend owns pins, buses and clocks.
#ifndef MESHINK_STORAGE_BACKEND_HEADER
#define MESHINK_STORAGE_BACKEND_HEADER "board/t5_storage_backend.h"
#endif

#include MESHINK_STORAGE_BACKEND_HEADER

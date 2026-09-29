#pragma once

#include "wireless_types.h"

// Compile-time local wireless backend selection. Boot/runtime code deals only
// in the generic state above; the board backend owns Wi-Fi/Bluetooth SDK calls.
#ifndef MESHINK_WIRELESS_BACKEND_HEADER
#define MESHINK_WIRELESS_BACKEND_HEADER "board/t5_wireless_backend.h"
#endif

#include MESHINK_WIRELESS_BACKEND_HEADER

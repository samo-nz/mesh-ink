#pragma once

#include "gps_types.h"

// Compile-time GPS backend selection. Application code uses only this surface;
// the selected board backend owns UARTs, receiver commands, pins and rails.
#ifndef MESHINK_GPS_BACKEND_HEADER
#define MESHINK_GPS_BACKEND_HEADER "board/t5_gps_backend.h"
#endif

#include MESHINK_GPS_BACKEND_HEADER

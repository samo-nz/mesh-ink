#pragma once

#include "../hardware/gps_types.h"

const char* meshink_gps_backend_name();
const char* meshink_gps_tuning_note();
void meshink_gps_background_tick();
MeshInkGpsConstellationMode meshink_gps_constellation_mode();
bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode);
void meshink_gps_shutdown();

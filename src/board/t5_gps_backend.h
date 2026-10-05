#pragma once

#include "../hardware/gps_types.h"

const char* meshink_gps_backend_name();
const char* meshink_gps_tuning_note();
void meshink_gps_prepare_runtime();
void meshink_gps_service_begin();
void meshink_gps_service_loop();
void meshink_gps_set_provider_enabled(bool enabled);
MeshInkGpsStatus meshink_gps_read_status();
void meshink_gps_background_tick();
MeshInkGpsConstellationMode meshink_gps_constellation_mode();
bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode);
void meshink_gps_shutdown();

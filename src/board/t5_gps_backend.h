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
bool meshink_gps_power_test_start(MeshInkGpsPowerExperiment experiment);
void meshink_gps_power_test_tick();
bool meshink_gps_power_test_busy();
bool meshink_gps_power_test_preserves_receiver_state();
bool meshink_gps_power_test_replay_last();
bool meshink_gps_power_matrix_replay_last();
bool meshink_gps_diagnostic_run(MeshInkGpsDiagnosticAction action);
void meshink_gps_enter_standby_power_mode();
void meshink_gps_leave_standby_power_mode();
MeshInkGpsConstellationMode meshink_gps_constellation_mode();
bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode);
void meshink_gps_shutdown();

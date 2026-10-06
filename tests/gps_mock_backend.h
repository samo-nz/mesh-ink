#pragma once

inline unsigned meshink_gps_mock_ticks=0;
inline unsigned meshink_gps_mock_service_loops=0;
inline bool meshink_gps_mock_started=false;
inline bool meshink_gps_mock_provider_enabled=false;
inline bool meshink_gps_mock_shutdown=false;
inline bool meshink_gps_mock_power_test_busy=false;
inline bool meshink_gps_mock_power_replayed=false;
inline bool meshink_gps_mock_diagnostic_ran=false;
inline MeshInkGpsDiagnosticAction meshink_gps_mock_diagnostic_action=
    MeshInkGpsDiagnosticAction::PassiveUartScan;
inline bool meshink_gps_mock_standby=false;
inline MeshInkGpsPowerExperiment meshink_gps_mock_experiment=
    MeshInkGpsPowerExperiment::GpsOnly;
inline MeshInkGpsStatus meshink_gps_mock_status{};
inline MeshInkGpsConstellationMode meshink_gps_mock_mode=
    MeshInkGpsConstellationMode::Unchanged;

inline const char* meshink_gps_backend_name(){return "mock";}
inline const char* meshink_gps_tuning_note(){return "mock GPS tuning note";}
inline void meshink_gps_service_begin(){meshink_gps_mock_started=true;}
inline void meshink_gps_service_loop(){++meshink_gps_mock_service_loops;}
inline void meshink_gps_set_provider_enabled(bool enabled){meshink_gps_mock_provider_enabled=enabled;}
inline MeshInkGpsStatus meshink_gps_read_status(){return meshink_gps_mock_status;}
inline void meshink_gps_background_tick(){++meshink_gps_mock_ticks;}
inline bool meshink_gps_power_test_start(MeshInkGpsPowerExperiment experiment){
    meshink_gps_mock_experiment=experiment;
    meshink_gps_mock_power_test_busy=true;
    return true;
}
inline void meshink_gps_power_test_tick(){}
inline bool meshink_gps_power_test_busy(){return meshink_gps_mock_power_test_busy;}
inline bool meshink_gps_power_test_replay_last(){meshink_gps_mock_power_replayed=true;return true;}
inline bool meshink_gps_diagnostic_run(MeshInkGpsDiagnosticAction action){
    meshink_gps_mock_diagnostic_action=action;
    meshink_gps_mock_diagnostic_ran=true;
    return true;
}
inline void meshink_gps_enter_standby_power_mode(){meshink_gps_mock_standby=true;}
inline void meshink_gps_leave_standby_power_mode(){meshink_gps_mock_standby=false;}
inline MeshInkGpsConstellationMode meshink_gps_constellation_mode(){
    return meshink_gps_mock_mode;
}
inline bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode){
    if(!meshink_gps_constellation_mode_valid(mode))return false;
    meshink_gps_mock_mode=mode;
    return true;
}
inline void meshink_gps_shutdown(){meshink_gps_mock_shutdown=true;}

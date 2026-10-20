#pragma once

inline unsigned meshink_gps_mock_ticks=0;
inline unsigned meshink_gps_mock_service_loops=0;
inline bool meshink_gps_mock_started=false;
inline bool meshink_gps_mock_provider_enabled=false;
inline bool meshink_gps_mock_shutdown=false;
inline MeshInkGpsStatus meshink_gps_mock_status{};
inline MeshInkGpsConstellationMode meshink_gps_mock_mode=
    MeshInkGpsConstellationMode::GpsBeiDou;
inline bool meshink_gps_mock_deep_sleep_power_save=false;

inline const char* meshink_gps_backend_name(){return "mock";}
inline const char* meshink_gps_tuning_note(){return "mock GPS tuning note";}
inline void meshink_gps_service_begin(){meshink_gps_mock_started=true;}
inline void meshink_gps_service_loop(){++meshink_gps_mock_service_loops;}
inline void meshink_gps_set_provider_enabled(bool enabled){meshink_gps_mock_provider_enabled=enabled;}
inline MeshInkGpsStatus meshink_gps_read_status(){return meshink_gps_mock_status;}
inline void meshink_gps_background_tick(){++meshink_gps_mock_ticks;}
inline MeshInkGpsConstellationMode meshink_gps_constellation_mode(){
    return meshink_gps_mock_mode;
}
inline bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode){
    if(!meshink_gps_constellation_mode_valid(mode))return false;
    meshink_gps_mock_mode=mode;
    return true;
}
inline bool meshink_gps_deep_sleep_power_save(){return meshink_gps_mock_deep_sleep_power_save;}
inline bool meshink_gps_set_deep_sleep_power_save(bool enabled){
    meshink_gps_mock_deep_sleep_power_save=enabled;
    return true;
}
inline void meshink_gps_shutdown(){meshink_gps_mock_shutdown=true;}

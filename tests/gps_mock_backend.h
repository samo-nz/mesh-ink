#pragma once

inline unsigned meshink_gps_mock_ticks=0;
inline bool meshink_gps_mock_shutdown=false;
inline MeshInkGpsConstellationMode meshink_gps_mock_mode=
    MeshInkGpsConstellationMode::Unchanged;

inline const char* meshink_gps_backend_name(){return "mock";}
inline const char* meshink_gps_tuning_note(){return "mock GPS tuning note";}
inline void meshink_gps_background_tick(){++meshink_gps_mock_ticks;}
inline MeshInkGpsConstellationMode meshink_gps_constellation_mode(){
    return meshink_gps_mock_mode;
}
inline bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode){
    if(!meshink_gps_constellation_mode_valid(mode))return false;
    meshink_gps_mock_mode=mode;
    return true;
}
inline void meshink_gps_shutdown(){meshink_gps_mock_shutdown=true;}

#define MESHINK_GPS_BACKEND_HEADER "gps_mock_backend.h"
#include "hardware/gps.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main(){
    assert(std::strcmp(meshink_gps_backend_name(),"mock")==0);
    assert(std::strcmp(meshink_gps_tuning_note(),"mock GPS tuning note")==0);
    assert(meshink_gps_constellation_mode()==
           MeshInkGpsConstellationMode::Unchanged);

    auto mode=meshink_gps_next_constellation_mode(
        MeshInkGpsConstellationMode::Unchanged);
    assert(mode==MeshInkGpsConstellationMode::GpsOnly);
    mode=meshink_gps_next_constellation_mode(mode);
    assert(mode==MeshInkGpsConstellationMode::GpsGlonass);
    mode=meshink_gps_next_constellation_mode(mode);
    assert(mode==MeshInkGpsConstellationMode::GpsBeiDou);
    mode=meshink_gps_next_constellation_mode(mode);
    assert(mode==MeshInkGpsConstellationMode::GpsBeiDouGlonass);
    mode=meshink_gps_next_constellation_mode(mode);
    assert(mode==MeshInkGpsConstellationMode::GpsOnly);

    assert(meshink_gps_set_constellation_mode(
        MeshInkGpsConstellationMode::GpsOnly));
    assert(meshink_gps_constellation_mode()==
           MeshInkGpsConstellationMode::GpsOnly);

    meshink_gps_background_tick();
    assert(meshink_gps_mock_ticks==1);

    meshink_gps_shutdown();
    assert(meshink_gps_mock_shutdown);

    std::cout << "PASS: generic GPS contract compiles and runs with a non-T5 backend.\n";
    return 0;
}

#define MESHINK_GPS_BACKEND_HEADER "gps_mock_backend.h"
#include "hardware/gps.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main(){
    assert(std::strcmp(meshink_gps_backend_name(),"mock")==0);
    assert(std::strcmp(meshink_gps_tuning_note(),"mock GPS tuning note")==0);
    assert(meshink_gps_constellation_mode()==
           MeshInkGpsConstellationMode::GpsBeiDou);

    assert(meshink_gps_constellation_mode_valid(
        MeshInkGpsConstellationMode::None));
    assert(meshink_gps_constellation_mode_valid(
        MeshInkGpsConstellationMode::GpsOnly));
    assert(meshink_gps_constellation_mode_valid(
        MeshInkGpsConstellationMode::BeiDouOnly));
    assert(meshink_gps_constellation_mode_valid(
        MeshInkGpsConstellationMode::GlonassOnly));
    assert(meshink_gps_constellation_mode_valid(
        MeshInkGpsConstellationMode::BeiDouGlonass));

    auto mode=MeshInkGpsConstellationMode::GpsBeiDou;
    MeshInkGpsConstellationMode next=mode;
    assert(meshink_gps_constellation_enabled(mode,MeshInkGpsConstellation::Gps));
    assert(meshink_gps_constellation_enabled(mode,MeshInkGpsConstellation::BeiDou));
    assert(!meshink_gps_constellation_enabled(mode,MeshInkGpsConstellation::Glonass));

    assert(meshink_gps_constellation_mode_set(
        mode,MeshInkGpsConstellation::Gps,false,next));
    assert(next==MeshInkGpsConstellationMode::BeiDouOnly);

    mode=next;
    assert(meshink_gps_constellation_mode_set(
        mode,MeshInkGpsConstellation::BeiDou,false,next));
    assert(next==MeshInkGpsConstellationMode::None);

    mode=next;
    assert(meshink_gps_constellation_mode_set(
        mode,MeshInkGpsConstellation::Glonass,true,next));
    assert(next==MeshInkGpsConstellationMode::GlonassOnly);

    assert(meshink_gps_set_constellation_mode(
        MeshInkGpsConstellationMode::None));
    assert(meshink_gps_constellation_mode()==
           MeshInkGpsConstellationMode::None);

    meshink_gps_service_begin();
    assert(meshink_gps_mock_started);
    meshink_gps_service_loop();
    assert(meshink_gps_mock_service_loops==1);
    meshink_gps_set_provider_enabled(true);
    assert(meshink_gps_mock_provider_enabled);

    meshink_gps_mock_status.available=true;
    meshink_gps_mock_status.valid=true;
    meshink_gps_mock_status.satellites=7;
    meshink_gps_mock_status.latitude=-43123456;
    meshink_gps_mock_status.longitude=172654321;
    meshink_gps_mock_status.timestamp=123456;
    const MeshInkGpsStatus status=meshink_gps_read_status();
    assert(status.available&&status.valid);
    assert(status.satellites==7);
    assert(status.latitude==-43123456);
    assert(status.longitude==172654321);
    assert(status.timestamp==123456);

    meshink_gps_background_tick();
    assert(meshink_gps_mock_ticks==1);

    meshink_gps_shutdown();
    assert(meshink_gps_mock_shutdown);

    std::cout << "PASS: generic GPS contract supports disabled plus all constellation masks.\n";
    return 0;
}

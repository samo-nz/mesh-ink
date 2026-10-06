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

    assert(meshink_gps_power_test_start(MeshInkGpsPowerExperiment::OnlineUpgradeWait));
    assert(meshink_gps_power_test_busy());
    assert(meshink_gps_mock_experiment==MeshInkGpsPowerExperiment::OnlineUpgradeWait);
    meshink_gps_mock_power_test_busy=false;
    assert(meshink_gps_power_test_replay_last());
    assert(meshink_gps_mock_power_replayed);
    meshink_gps_enter_standby_power_mode();
    assert(meshink_gps_mock_standby);
    meshink_gps_leave_standby_power_mode();
    assert(!meshink_gps_mock_standby);

    meshink_gps_shutdown();
    assert(meshink_gps_mock_shutdown);

    std::cout << "PASS: generic GPS contract compiles and runs with a non-T5 backend.\n";
    return 0;
}

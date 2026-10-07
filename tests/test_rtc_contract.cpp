namespace mesh { class RTCClock {}; }
#define MESHINK_RTC_BACKEND_HEADER "rtc_mock_backend.h"
#include "hardware/rtc.h"

#include <cassert>
#include <iostream>

int main(){
    assert(&meshink_rtc_meshcore()==&meshink_rtc_mock_clock);
    meshink_rtc_begin();
    assert(meshink_rtc_mock_started);
    meshink_rtc_tick();
    assert(meshink_rtc_mock_ticks==1);

    meshink_rtc_mock_time=123456789U;
    meshink_rtc_mock_valid=true;
    meshink_rtc_mock_source=MeshInkTimeSource::HardwareRtc;
    assert(meshink_rtc_current_time()==123456789U);
    assert(meshink_rtc_valid());
    assert(meshink_rtc_time_source()==MeshInkTimeSource::HardwareRtc);

    meshink_rtc_expect_companion_time(222222222U);
    assert(meshink_rtc_mock_expected_companion==222222222U);

    assert(meshink_rtc_set_manual_time(333333333U));
    assert(meshink_rtc_current_time()==333333333U);
    assert(meshink_rtc_time_source()==MeshInkTimeSource::Manual);
    assert(!meshink_rtc_gps_authoritative());

    std::cout << "PASS: generic RTC contract supports manual time and source metadata.\n";
    return 0;
}

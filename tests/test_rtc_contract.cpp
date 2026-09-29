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
    assert(meshink_rtc_current_time()==123456789U);
    assert(meshink_rtc_valid());
    std::cout << "PASS: generic RTC contract compiles and runs with a non-T5 backend.\n";
    return 0;
}

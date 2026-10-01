#pragma once
#include <stdint.h>

namespace mesh { class RTCClock; }

mesh::RTCClock& meshink_rtc_meshcore();
void meshink_rtc_begin();
void meshink_rtc_tick();
uint32_t meshink_rtc_current_time();
bool meshink_rtc_valid();

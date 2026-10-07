#pragma once
#include <stdint.h>
#include "../hardware/rtc_types.h"

namespace mesh { class RTCClock; }

mesh::RTCClock& meshink_rtc_meshcore();
void meshink_rtc_begin();
void meshink_rtc_tick();
uint32_t meshink_rtc_current_time();
bool meshink_rtc_valid();
bool meshink_rtc_set_manual_time(uint32_t utc);
bool meshink_rtc_set_time_mode(MeshInkTimeMode mode);
MeshInkTimeMode meshink_rtc_time_mode();
void meshink_rtc_expect_companion_time(uint32_t utc);
MeshInkTimeSource meshink_rtc_time_source();
bool meshink_rtc_gps_authoritative();

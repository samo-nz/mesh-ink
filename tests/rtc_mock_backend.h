#pragma once

inline mesh::RTCClock meshink_rtc_mock_clock;
inline bool meshink_rtc_mock_started=false;
inline unsigned meshink_rtc_mock_ticks=0;
inline uint32_t meshink_rtc_mock_time=0;
inline bool meshink_rtc_mock_valid=false;
inline uint32_t meshink_rtc_mock_expected_companion=0;
inline MeshInkTimeSource meshink_rtc_mock_source=MeshInkTimeSource::Unknown;
inline bool meshink_rtc_mock_gps_authoritative=false;

inline mesh::RTCClock& meshink_rtc_meshcore(){return meshink_rtc_mock_clock;}
inline void meshink_rtc_begin(){meshink_rtc_mock_started=true;}
inline void meshink_rtc_tick(){++meshink_rtc_mock_ticks;}
inline uint32_t meshink_rtc_current_time(){return meshink_rtc_mock_time;}
inline bool meshink_rtc_valid(){return meshink_rtc_mock_valid;}
inline bool meshink_rtc_set_manual_time(uint32_t utc){
    meshink_rtc_mock_time=utc;
    meshink_rtc_mock_valid=true;
    meshink_rtc_mock_source=MeshInkTimeSource::Manual;
    meshink_rtc_mock_gps_authoritative=false;
    return true;
}
inline void meshink_rtc_expect_companion_time(uint32_t utc){
    meshink_rtc_mock_expected_companion=utc;
}
inline MeshInkTimeSource meshink_rtc_time_source(){return meshink_rtc_mock_source;}
inline bool meshink_rtc_gps_authoritative(){return meshink_rtc_mock_gps_authoritative;}

#pragma once

inline mesh::RTCClock meshink_rtc_mock_clock;
inline bool meshink_rtc_mock_started=false;
inline unsigned meshink_rtc_mock_ticks=0;
inline uint32_t meshink_rtc_mock_time=0;
inline bool meshink_rtc_mock_valid=false;

inline mesh::RTCClock& meshink_rtc_meshcore(){return meshink_rtc_mock_clock;}
inline void meshink_rtc_begin(){meshink_rtc_mock_started=true;}
inline void meshink_rtc_tick(){++meshink_rtc_mock_ticks;}
inline uint32_t meshink_rtc_current_time(){return meshink_rtc_mock_time;}
inline bool meshink_rtc_valid(){return meshink_rtc_mock_valid;}

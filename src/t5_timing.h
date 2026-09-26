#pragma once
#include <stdint.h>

#ifndef T5_TIMING_DIAGNOSTICS
#define T5_TIMING_DIAGNOSTICS 0
#endif

enum class T5TimingSection : uint8_t { Idle=0, Mesh=1, Ui=2, Display=3 };

#if T5_TIMING_DIAGNOSTICS
void t5_timing_begin();
void t5_timing_touch_reset();
uint32_t t5_timing_touch_begin();
void t5_timing_touch_end(uint32_t started_us);
uint32_t t5_timing_cycle_begin();
void t5_timing_cycle_end(uint32_t started_us);
uint32_t t5_timing_section_begin(T5TimingSection section);
void t5_timing_section_end(T5TimingSection section,uint32_t started_us);
uint32_t t5_timing_display_begin();
void t5_timing_display_end(uint32_t started_us);
void t5_timing_service();
#else
inline void t5_timing_begin(){}
inline void t5_timing_touch_reset(){}
inline uint32_t t5_timing_touch_begin(){return 0;}
inline void t5_timing_touch_end(uint32_t){}
inline uint32_t t5_timing_cycle_begin(){return 0;}
inline void t5_timing_cycle_end(uint32_t){}
inline uint32_t t5_timing_section_begin(T5TimingSection){return 0;}
inline void t5_timing_section_end(T5TimingSection,uint32_t){}
inline uint32_t t5_timing_display_begin(){return 0;}
inline void t5_timing_display_end(uint32_t){}
inline void t5_timing_service(){}
#endif

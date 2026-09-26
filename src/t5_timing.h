#pragma once
#include <stdint.h>

#ifndef T5_TIMING_DIAGNOSTICS
#define T5_TIMING_DIAGNOSTICS 0
#endif

enum class T5TimingSection : uint8_t { Idle=0, Mesh=1, Ui=2, Display=3 };
enum class T5UiAction : uint8_t {
    None=0, Touch=1, TextRefresh=2, StatusPoll=3,
    StatusRefresh=4, ToastRefresh=5, MessageAlert=6, Other=7
};

#if T5_TIMING_DIAGNOSTICS
void t5_timing_begin();
void t5_timing_touch_reset();
uint32_t t5_timing_touch_begin();
void t5_timing_touch_end(uint32_t started_us);
void t5_timing_note_touch_queue_drop();
uint32_t t5_timing_cycle_begin();
void t5_timing_cycle_end(uint32_t started_us);
uint32_t t5_timing_section_begin(T5TimingSection section);
void t5_timing_section_end(T5TimingSection section,uint32_t started_us);
uint32_t t5_timing_display_begin();
void t5_timing_display_end(uint32_t started_us);
void t5_timing_set_ui_context(const char* screen,bool keyboard,bool landscape,bool standby);
void t5_timing_set_ui_action(T5UiAction action);
void t5_timing_note_ui_draw(uint32_t elapsed_us);
void t5_timing_note_chat_draw(uint32_t history_us,uint32_t keyboard_us);
void t5_timing_note_ui_status(uint32_t elapsed_us);
void t5_timing_note_text_wait(uint32_t wait_ms);
void t5_timing_note_ui_input(uint32_t elapsed_us,uint32_t age_ms,uint32_t queue_depth);
void t5_timing_note_refresh(uint8_t requested_mode,uint8_t actual_mode);
void t5_timing_service();
#else
inline void t5_timing_begin(){}
inline void t5_timing_touch_reset(){}
inline uint32_t t5_timing_touch_begin(){return 0;}
inline void t5_timing_touch_end(uint32_t){}
inline void t5_timing_note_touch_queue_drop(){}
inline uint32_t t5_timing_cycle_begin(){return 0;}
inline void t5_timing_cycle_end(uint32_t){}
inline uint32_t t5_timing_section_begin(T5TimingSection){return 0;}
inline void t5_timing_section_end(T5TimingSection,uint32_t){}
inline uint32_t t5_timing_display_begin(){return 0;}
inline void t5_timing_display_end(uint32_t){}
inline void t5_timing_set_ui_context(const char*,bool,bool,bool){}
inline void t5_timing_set_ui_action(T5UiAction){}
inline void t5_timing_note_ui_draw(uint32_t){}
inline void t5_timing_note_chat_draw(uint32_t,uint32_t){}
inline void t5_timing_note_ui_status(uint32_t){}
inline void t5_timing_note_text_wait(uint32_t){}
inline void t5_timing_note_ui_input(uint32_t,uint32_t,uint32_t){}
inline void t5_timing_note_refresh(uint8_t,uint8_t){}
inline void t5_timing_service(){}
#endif

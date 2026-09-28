#pragma once

#include <stdint.h>
#include "board/board_profile.h"

#if T5_BOARD_H752_01
#include <epdiy.h>
#else
#error "Real H752 display backend not enabled yet"
#endif

void t5_display_init();
void t5_display_deinit();
void t5_display_set_rotation(enum EpdRotation rotation);
enum EpdRotation t5_display_get_rotation();
void t5_display_set_pixel_clock_mhz(int mhz);
int t5_display_width();
int t5_display_height();
float t5_display_ambient_temperature();

EpdiyHighlevelState t5_display_hl_init();
uint8_t* t5_display_framebuffer(EpdiyHighlevelState* state);
void t5_display_set_all_white(EpdiyHighlevelState* state);
EpdDrawError t5_display_update_screen(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature);
EpdDrawError t5_display_update_area(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature,EpdRect area);

void t5_display_poweron();
void t5_display_poweroff();
void t5_display_clear();
void t5_display_draw_pixel(int x,int y,uint8_t color,uint8_t* framebuffer);
void t5_display_draw_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer);
void t5_display_fill_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer);

#pragma once

#include <stdint.h>
#include "board/board_profile.h"

#if T5_BOARD_H752_01
#include <epdiy.h>

// Keep the field-tested H752-01 path header-inline. CI restores PlatformIO
// build caches between testing commits; inline forwarding avoids a stale
// source-manifest ever omitting a newly introduced backend translation unit.
// These functions intentionally do nothing except forward to the exact EPDiy
// calls used by MeshInk before the board-backend refactor.
inline void t5_display_init(){epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);}
inline void t5_display_deinit(){epd_deinit();}
inline void t5_display_set_rotation(enum EpdRotation rotation){epd_set_rotation(rotation);}
inline enum EpdRotation t5_display_get_rotation(){return epd_get_rotation();}
inline void t5_display_set_pixel_clock_mhz(int mhz){epd_set_lcd_pixel_clock_MHz(mhz);}
inline int t5_display_width(){return epd_width();}
inline int t5_display_height(){return epd_height();}
inline float t5_display_ambient_temperature(){return epd_ambient_temperature();}

inline EpdiyHighlevelState t5_display_hl_init(){return epd_hl_init(EPD_BUILTIN_WAVEFORM);}
inline uint8_t* t5_display_framebuffer(EpdiyHighlevelState* state){return epd_hl_get_framebuffer(state);}
inline void t5_display_set_all_white(EpdiyHighlevelState* state){epd_hl_set_all_white(state);}
inline EpdDrawError t5_display_update_screen(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature){
    return epd_hl_update_screen(state,mode,temperature);
}
inline EpdDrawError t5_display_update_area(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature,EpdRect area){
    return epd_hl_update_area(state,mode,temperature,area);
}

inline void t5_display_poweron(){epd_poweron();}
inline void t5_display_poweroff(){epd_poweroff();}
inline void t5_display_clear(){epd_clear();}
inline void t5_display_draw_pixel(int x,int y,uint8_t color,uint8_t* framebuffer){
    epd_draw_pixel(x,y,color,framebuffer);
}
inline void t5_display_draw_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer){
    epd_draw_rect(rect,color,framebuffer);
}
inline void t5_display_fill_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer){
    epd_fill_rect(rect,color,framebuffer);
}
#else
#error "Real H752 display backend not enabled yet"
#endif

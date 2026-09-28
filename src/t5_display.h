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
#include <string.h>
#include <esp_heap_caps.h>
#include <h752_legacy_epd_bridge.h>

// Minimal compatibility surface used by MeshInk. The original H752 driver
// stores a physical 960x540 packed 4-bpp framebuffer; these definitions let
// the UI keep using the same high-level state and rotation vocabulary without
// pulling EPDiy into the old-board build.
typedef struct {
    int x;
    int y;
    int width;
    int height;
} EpdRect;

enum EpdRotation {
    EPD_ROT_LANDSCAPE = 0,
    EPD_ROT_PORTRAIT = 1,
    EPD_ROT_INVERTED_LANDSCAPE = 2,
    EPD_ROT_INVERTED_PORTRAIT = 3,
};

enum EpdDrawMode {
    MODE_DU = 0,
    MODE_GC16 = 1,
    MODE_GL16 = 2,
};

enum EpdDrawError {
    EPD_DRAW_SUCCESS = 0,
    EPD_DRAW_FAILED_ALLOC = 0x10,
};

struct EpdiyHighlevelState {
    uint8_t* front_fb;
    uint8_t* back_fb;
    uint8_t* difference_fb;
    uint8_t* dirty_lines;
    uint8_t* dirty_columns;
};

static constexpr int T5_H752_EPD_WIDTH=960;
static constexpr int T5_H752_EPD_HEIGHT=540;
static constexpr size_t T5_H752_FB_BYTES=(size_t)T5_H752_EPD_WIDTH*T5_H752_EPD_HEIGHT/2U;

inline EpdRotation& t5_h752_rotation_state(){
    static EpdRotation rotation=EPD_ROT_INVERTED_PORTRAIT;
    return rotation;
}
inline bool& t5_h752_panel_valid(){
    static bool valid=false;
    return valid;
}
inline uint8_t* t5_h752_alloc_framebuffer(){
    uint8_t* ptr=(uint8_t*)heap_caps_malloc(T5_H752_FB_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!ptr)ptr=(uint8_t*)heap_caps_malloc(T5_H752_FB_BYTES,MALLOC_CAP_8BIT);
    return ptr;
}
inline void t5_h752_map_point(int x,int y,int& px,int& py){
    switch(t5_h752_rotation_state()){
        case EPD_ROT_LANDSCAPE: px=x;py=y;break;
        case EPD_ROT_PORTRAIT: px=T5_H752_EPD_WIDTH-1-y;py=x;break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            px=T5_H752_EPD_WIDTH-1-x;py=T5_H752_EPD_HEIGHT-1-y;break;
        case EPD_ROT_INVERTED_PORTRAIT:
        default: px=y;py=T5_H752_EPD_HEIGHT-1-x;break;
    }
}
inline H752LegacyRect t5_h752_map_rect(EpdRect rect){
    switch(t5_h752_rotation_state()){
        case EPD_ROT_LANDSCAPE:
            return {rect.x,rect.y,rect.width,rect.height};
        case EPD_ROT_PORTRAIT:
            return {T5_H752_EPD_WIDTH-(rect.y+rect.height),rect.x,rect.height,rect.width};
        case EPD_ROT_INVERTED_LANDSCAPE:
            return {T5_H752_EPD_WIDTH-(rect.x+rect.width),
                    T5_H752_EPD_HEIGHT-(rect.y+rect.height),rect.width,rect.height};
        case EPD_ROT_INVERTED_PORTRAIT:
        default:
            return {rect.y,T5_H752_EPD_HEIGHT-(rect.x+rect.width),rect.height,rect.width};
    }
}

inline void t5_display_init(){epd_init();t5_h752_panel_valid()=false;}
inline void t5_display_deinit(){
    // The official H752 driver does not expose a complete esp_lcd/RMT teardown.
    // H752 LoRa uses different pins from the display bus, so leaving the
    // display peripheral initialized is safer than an unverified partial free.
}
inline void t5_display_set_rotation(enum EpdRotation rotation){t5_h752_rotation_state()=rotation;}
inline enum EpdRotation t5_display_get_rotation(){return t5_h752_rotation_state();}
inline void t5_display_set_pixel_clock_mhz(int){/* fixed at 10 MHz by official H752 i80 driver */}
inline int t5_display_width(){return T5_H752_EPD_WIDTH;}
inline int t5_display_height(){return T5_H752_EPD_HEIGHT;}
inline float t5_display_ambient_temperature(){return 25.0f;}

inline EpdiyHighlevelState t5_display_hl_init(){
    EpdiyHighlevelState state{};
    state.front_fb=t5_h752_alloc_framebuffer();
    state.back_fb=t5_h752_alloc_framebuffer();
    if(state.front_fb)memset(state.front_fb,0xFF,T5_H752_FB_BYTES);
    if(state.back_fb)memset(state.back_fb,0x00,T5_H752_FB_BYTES);
    return state;
}
inline uint8_t* t5_display_framebuffer(EpdiyHighlevelState* state){
    return state?state->front_fb:nullptr;
}
inline void t5_display_set_all_white(EpdiyHighlevelState* state){
    if(state&&state->front_fb)memset(state->front_fb,0xFF,T5_H752_FB_BYTES);
}

inline EpdDrawError t5_display_update_screen(EpdiyHighlevelState* state,EpdDrawMode,int){
    if(!state||!state->front_fb||!state->back_fb)return EPD_DRAW_FAILED_ALLOC;
    if(t5_h752_panel_valid()&&memcmp(state->front_fb,state->back_fb,T5_H752_FB_BYTES)==0)
        return EPD_DRAW_SUCCESS;

    // Correctness-first H752 path: the official legacy waveform draws black /
    // grayscale onto a white panel, so erase before every changed frame. This
    // is intentionally conservative until a real H752 can validate faster
    // differential transitions.
    epd_clear();
    H752LegacyRect full{0,0,T5_H752_EPD_WIDTH,T5_H752_EPD_HEIGHT};
    epd_draw_grayscale_image(full,state->front_fb);
    memcpy(state->back_fb,state->front_fb,T5_H752_FB_BYTES);
    t5_h752_panel_valid()=true;
    return EPD_DRAW_SUCCESS;
}
inline EpdDrawError t5_display_update_area(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature,EpdRect){
    // Safe fallback: preserve correctness by refreshing the complete physical
    // panel. Partial H752 waveforms need hardware validation before enabling.
    return t5_display_update_screen(state,mode,temperature);
}

inline void t5_display_poweron(){epd_poweron();}
inline void t5_display_poweroff(){epd_poweroff();}
inline void t5_display_clear(){epd_clear();t5_h752_panel_valid()=false;}
inline void t5_display_draw_pixel(int x,int y,uint8_t color,uint8_t* framebuffer){
    int px=0,py=0;t5_h752_map_point(x,y,px,py);
    epd_draw_pixel(px,py,color,framebuffer);
}
inline void t5_display_draw_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer){
    const H752LegacyRect p=t5_h752_map_rect(rect);
    epd_draw_rect(p.x,p.y,p.width,p.height,color,framebuffer);
}
inline void t5_display_fill_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer){
    const H752LegacyRect p=t5_h752_map_rect(rect);
    epd_fill_rect(p.x,p.y,p.width,p.height,color,framebuffer);
}
#endif

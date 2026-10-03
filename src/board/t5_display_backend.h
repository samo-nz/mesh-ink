#pragma once

#include <epdiy.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/display_types.h"
#include "board/t5_packed_framebuffer.h"

// Field-tested LILYGO T5 H752-01 / EPDiy v7 backend.
//
// Physical panel storage is 960x540 at packed 4 bpp. MeshInk normally presents
// a 540x960 inverted-portrait logical viewport. Those facts belong here rather
// than in UI or Maps code.
struct MeshInkDisplayState {
    EpdiyHighlevelState native{};
};

inline MeshInkDisplayGeometry meshink_display_geometry() {
    return {960,540,540,960,4};
}

inline int meshink_display_physical_width() {
    return meshink_display_geometry().physical_width;
}
inline int meshink_display_physical_height() {
    return meshink_display_geometry().physical_height;
}
inline size_t meshink_display_framebuffer_bytes() {
    const MeshInkDisplayGeometry g=meshink_display_geometry();
    return (size_t)g.physical_width*(size_t)g.physical_height*
           (size_t)g.framebuffer_bits_per_pixel/8U;
}

inline EpdRotation meshink_display_native_rotation(MeshInkRotation rotation) {
    switch(rotation) {
        case MeshInkRotation::Portrait:return EPD_ROT_PORTRAIT;
        case MeshInkRotation::InvertedLandscape:return EPD_ROT_INVERTED_LANDSCAPE;
        case MeshInkRotation::InvertedPortrait:return EPD_ROT_INVERTED_PORTRAIT;
        case MeshInkRotation::Landscape:
        default:return EPD_ROT_LANDSCAPE;
    }
}
inline MeshInkRotation meshink_display_rotation_from_native(EpdRotation rotation) {
    switch(rotation) {
        case EPD_ROT_PORTRAIT:return MeshInkRotation::Portrait;
        case EPD_ROT_INVERTED_LANDSCAPE:return MeshInkRotation::InvertedLandscape;
        case EPD_ROT_INVERTED_PORTRAIT:return MeshInkRotation::InvertedPortrait;
        case EPD_ROT_LANDSCAPE:
        default:return MeshInkRotation::Landscape;
    }
}
inline EpdDrawMode meshink_display_native_refresh_mode(MeshInkRefreshMode mode) {
    switch(mode) {
        case MeshInkRefreshMode::Gray16:return MODE_GC16;
        case MeshInkRefreshMode::FastGray16:return MODE_GL16;
        case MeshInkRefreshMode::Direct:
        default:return MODE_DU;
    }
}
inline EpdRect meshink_display_native_rect(MeshInkRect rect) {
    return {rect.x,rect.y,rect.width,rect.height};
}

inline void meshink_display_init() {
    epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);
    // Field-tested H752-01 EPDiy timing. Application code must not select
    // panel clocks; each display backend owns its controller tuning.
    epd_set_lcd_pixel_clock_MHz(17);
}
inline void meshink_display_deinit(){epd_deinit();}
inline void meshink_display_set_rotation(MeshInkRotation rotation) {
    epd_set_rotation(meshink_display_native_rotation(rotation));
}
inline void meshink_display_set_orientation(MeshInkOrientation orientation) {
    // H752-01 panel is mounted so the application's portrait is EPDiy's
    // inverted portrait, while application landscape matches EPDiy landscape.
    epd_set_rotation(orientation==MeshInkOrientation::Landscape
        ? EPD_ROT_LANDSCAPE : EPD_ROT_INVERTED_PORTRAIT);
}
inline MeshInkRotation meshink_display_get_rotation() {
    return meshink_display_rotation_from_native(epd_get_rotation());
}
inline int meshink_display_logical_width() {
    const MeshInkDisplayGeometry g=meshink_display_geometry();
    const MeshInkRotation r=meshink_display_get_rotation();
    return (r==MeshInkRotation::Portrait||r==MeshInkRotation::InvertedPortrait)
        ? g.physical_height:g.physical_width;
}
inline int meshink_display_logical_height() {
    const MeshInkDisplayGeometry g=meshink_display_geometry();
    const MeshInkRotation r=meshink_display_get_rotation();
    return (r==MeshInkRotation::Portrait||r==MeshInkRotation::InvertedPortrait)
        ? g.physical_width:g.physical_height;
}
inline int meshink_display_portrait_width(){return meshink_display_geometry().portrait_width;}
inline int meshink_display_portrait_height(){return meshink_display_geometry().portrait_height;}
inline float meshink_display_ambient_temperature(){return epd_ambient_temperature();}

inline MeshInkDisplayState meshink_display_state_init() {
    MeshInkDisplayState state{};
    state.native=epd_hl_init(EPD_BUILTIN_WAVEFORM);
    return state;
}
inline uint8_t* meshink_display_framebuffer(MeshInkDisplayState* state) {
    return state?epd_hl_get_framebuffer(&state->native):nullptr;
}
inline void meshink_display_set_all_white(MeshInkDisplayState* state) {
    if(state)epd_hl_set_all_white(&state->native);
}
inline void meshink_display_fill_framebuffer(MeshInkDisplayState* state,uint8_t packed_color) {
    uint8_t* fb=meshink_display_framebuffer(state);
    if(fb)memset(fb,packed_color,meshink_display_framebuffer_bytes());
}
inline void meshink_display_invalidate_previous(MeshInkDisplayState* state) {
    if(!state||!state->native.front_fb||!state->native.back_fb)return;
    const size_t bytes=meshink_display_framebuffer_bytes();
    for(size_t i=0;i<bytes;++i)
        state->native.back_fb[i]=(uint8_t)~state->native.front_fb[i];
}
inline void meshink_display_release_state(MeshInkDisplayState* state) {
    if(!state)return;
    heap_caps_free(state->native.front_fb);
    heap_caps_free(state->native.back_fb);
    heap_caps_free(state->native.difference_fb);
    free(state->native.dirty_lines);
    heap_caps_free(state->native.dirty_columns);
    state->native={};
}

inline MeshInkDisplayResult meshink_display_update_screen(
    MeshInkDisplayState* state,MeshInkRefreshMode mode,int temperature) {
    if(!state)return -1;
    return (MeshInkDisplayResult)epd_hl_update_screen(
        &state->native,meshink_display_native_refresh_mode(mode),temperature);
}
inline MeshInkDisplayResult meshink_display_update_area(
    MeshInkDisplayState* state,MeshInkRefreshMode mode,int temperature,MeshInkRect area) {
    if(!state)return -1;
    return (MeshInkDisplayResult)epd_hl_update_area(
        &state->native,meshink_display_native_refresh_mode(mode),temperature,
        meshink_display_native_rect(area));
}

inline void meshink_display_poweron(){epd_poweron();}
inline void meshink_display_poweroff(){epd_poweroff();}
inline void meshink_display_clear(){epd_clear();}
inline void meshink_display_draw_pixel(int x,int y,uint8_t color,uint8_t* framebuffer) {
    epd_draw_pixel(x,y,color,framebuffer);
}
inline uint8_t meshink_display_read_logical_gray8(
    const uint8_t* framebuffer,int logical_x,int logical_y) {
    if(!framebuffer)return 0xFF;
    const int physical_width=meshink_display_physical_width();
    const int physical_height=meshink_display_physical_height();
    int physical_x=logical_x,physical_y=logical_y;
    switch(meshink_display_get_rotation()) {
        case MeshInkRotation::Portrait:
            physical_x=physical_width-1-logical_y;
            physical_y=logical_x;
            break;
        case MeshInkRotation::InvertedLandscape:
            physical_x=physical_width-1-logical_x;
            physical_y=physical_height-1-logical_y;
            break;
        case MeshInkRotation::InvertedPortrait:
            physical_x=logical_y;
            physical_y=physical_height-1-logical_x;
            break;
        case MeshInkRotation::Landscape:
        default:
            break;
    }
    if(physical_x<0||physical_y<0||
       physical_x>=physical_width||physical_y>=physical_height)return 0xFF;
    const size_t row_bytes=(size_t)physical_width/2U;
    const uint8_t packed=
        framebuffer[(size_t)physical_y*row_bytes+((unsigned)physical_x>>1)];
    const uint8_t gray4=(physical_x&1)?(packed>>4):(packed&0x0FU);
    return (uint8_t)(gray4*17U);
}
inline void meshink_display_draw_rect(MeshInkRect rect,uint8_t color,uint8_t* framebuffer) {
    epd_draw_rect(meshink_display_native_rect(rect),color,framebuffer);
}
inline void meshink_display_fill_rect(MeshInkRect rect,uint8_t color,uint8_t* framebuffer) {
    // The local UI is monochrome. Bypass EPDiy's per-rectangle drawing path
    // for black/white fills and write the packed 4bpp framebuffer directly.
    // This is especially important for rounded panels, which are composed from
    // many one-pixel-high fill calls. Preserve EPDiy as the exact fallback for
    // grayscale colours or an unavailable framebuffer.
    if(framebuffer&&(color==0x00U||color==0xFFU)) {
        meshink_t5_packed::fill_logical_gray4(
            framebuffer,
            meshink_display_physical_width(),
            meshink_display_physical_height(),
            meshink_display_get_rotation(),
            rect,
            color==0x00U?0x00U:0x0FU);
        return;
    }
    epd_fill_rect(meshink_display_native_rect(rect),color,framebuffer);
}

inline void meshink_display_fill_rounded_rect(
    MeshInkRect rect,int radius,uint8_t color,uint8_t* framebuffer) {
    if(framebuffer&&(color==0x00U||color==0xFFU)) {
        meshink_t5_packed::fill_logical_rounded_gray4(
            framebuffer,
            meshink_display_physical_width(),
            meshink_display_physical_height(),
            meshink_display_get_rotation(),
            rect,
            radius,
            color==0x00U?0x00U:0x0FU);
        return;
    }

    // Generic grayscale fallback preserves the established UI geometry.
    if(rect.width<=0||rect.height<=0)return;
    const int max_radius=(rect.width<rect.height?rect.width:rect.height)/2;
    int r=radius;
    if(r<0)r=0;
    if(r>max_radius)r=max_radius;
    if(!r) {
        meshink_display_fill_rect(rect,color,framebuffer);
        return;
    }
    meshink_display_fill_rect(
        {rect.x,rect.y+r,rect.width,rect.height-2*r},color,framebuffer);
    for(int row=0;row<r;++row) {
        const int inset=meshink_t5_packed::rounded_row_inset(r,row);
        const int span=rect.width-2*inset;
        if(span<=0)continue;
        meshink_display_fill_rect(
            {rect.x+inset,rect.y+row,span,1},color,framebuffer);
        meshink_display_fill_rect(
            {rect.x+inset,rect.y+rect.height-1-row,span,1},color,framebuffer);
    }
}

// Maps bulk compositor. It preserves the current T5 cache64 fast path while
// keeping EPDiy packing, physical dimensions and rotation out of map_tiles.cpp.
inline void meshink_display_blit_gray4_dithered(
    uint8_t* framebuffer,const MeshInkGray4DitherBlit& blit) {
    if(!framebuffer||!blit.source||!blit.dither_masks||
       blit.source_width<=0||blit.source_rect.width<=0||
       blit.source_rect.height<=0||blit.destination_rect.width<=0||
       blit.destination_rect.height<=0)return;

    const int dest_x0=blit.destination_rect.x;
    const int dest_y0=blit.destination_rect.y;
    const int dest_x1=dest_x0+blit.destination_rect.width;
    const int dest_y1=dest_y0+blit.destination_rect.height;
    const int clip_x1=blit.clip_rect.x+blit.clip_rect.width;
    const int clip_y1=blit.clip_rect.y+blit.clip_rect.height;
    const int x0=dest_x0>blit.clip_rect.x?dest_x0:blit.clip_rect.x;
    const int y0=dest_y0>blit.clip_rect.y?dest_y0:blit.clip_rect.y;
    const int x1=dest_x1<clip_x1?dest_x1:clip_x1;
    const int y1=dest_y1<clip_y1?dest_y1:clip_y1;
    if(x0>=x1||y0>=y1)return;

    unsigned shift_x=0,shift_y=0;
    int scaled_w=blit.source_rect.width;
    int scaled_h=blit.source_rect.height;
    while(scaled_w<blit.destination_rect.width&&
          scaled_w>0&&(scaled_w<<1)<=blit.destination_rect.width) {
        scaled_w<<=1;++shift_x;
    }
    while(scaled_h<blit.destination_rect.height&&
          scaled_h>0&&(scaled_h<<1)<=blit.destination_rect.height) {
        scaled_h<<=1;++shift_y;
    }
    const bool power2_scale=
        scaled_w==blit.destination_rect.width&&
        scaled_h==blit.destination_rect.height;

    const auto source_x=[&](int px)->int {
        const int offset=px-dest_x0;
        return blit.source_rect.x+(power2_scale
            ? (offset>>shift_x)
            : (offset*blit.source_rect.width)/blit.destination_rect.width);
    };
    const auto source_y=[&](int py)->int {
        const int offset=py-dest_y0;
        return blit.source_rect.y+(power2_scale
            ? (offset>>shift_y)
            : (offset*blit.source_rect.height)/blit.destination_rect.height);
    };
    const auto black_at=[&](int px,int py)->bool {
        const int sx=source_x(px),sy=source_y(py);
        const size_t offset=(size_t)sy*(size_t)blit.source_width+(size_t)sx;
        const uint8_t packed=blit.source[offset>>1];
        const unsigned level=(offset&1U)?(packed&0x0FU):(packed>>4);
        const int world_x=blit.world_origin_x+(px-dest_x0);
        const int world_y=blit.world_origin_y+(py-dest_y0);
        const unsigned phase=(((unsigned)world_y&3U)<<2)|
                             ((unsigned)world_x&3U);
        return (blit.dither_masks[level]&(1U<<phase))!=0;
    };

    if(meshink_display_get_rotation()==MeshInkRotation::InvertedPortrait) {
        // H752-01/ED047TC1 packed-4bpp fast path. In inverted portrait,
        // logical (x,y) maps to physical (y, physical_height-1-x).
        const int physical_width=meshink_display_physical_width();
        const int physical_height=meshink_display_physical_height();
        const size_t row_bytes=(size_t)physical_width/2U;
        for(int px=x0;px<x1;++px) {
            const int physical_y=physical_height-px-1;
            uint8_t* out_row=framebuffer+(size_t)physical_y*row_bytes;
            int py=y0;
            if(py&1) {
                uint8_t& out=out_row[(unsigned)py>>1];
                out=(uint8_t)((out&0x0FU)|(black_at(px,py)?0x00U:0xF0U));
                ++py;
            }
            for(;py+1<y1;py+=2) {
                const uint8_t low=black_at(px,py)?0x00U:0x0FU;
                const uint8_t high=black_at(px,py+1)?0x00U:0xF0U;
                out_row[(unsigned)py>>1]=(uint8_t)(low|high);
            }
            if(py<y1) {
                uint8_t& out=out_row[(unsigned)py>>1];
                out=(uint8_t)((out&0xF0U)|(black_at(px,py)?0x00U:0x0FU));
            }
        }
        return;
    }

    // Generic backend fallback: preserve the same world-anchored dithering
    // using horizontal runs rather than one display call per pixel.
    for(int py=y0;py<y1;++py) {
        int run_x=x0;
        bool black=black_at(x0,py);
        for(int px=x0+1;px<x1;++px) {
            const bool next=black_at(px,py);
            if(next!=black) {
                meshink_display_fill_rect({run_x,py,px-run_x,1},
                                          black?0x00:0xFF,framebuffer);
                run_x=px;black=next;
            }
        }
        meshink_display_fill_rect({run_x,py,x1-run_x,1},
                                  black?0x00:0xFF,framebuffer);
    }
}

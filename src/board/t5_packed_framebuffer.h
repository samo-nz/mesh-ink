#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "hardware/display_types.h"

// H752-01 / ED047TC1 framebuffer helpers.
//
// EPDiy stores two 4-bit pixels per byte with the even physical X pixel in
// the low nibble and the odd physical X pixel in the high nibble. Application
// drawing normally uses an inverted-portrait logical viewport, but every
// axis-aligned logical rectangle remains axis-aligned after the 90/180 degree
// transforms below.
//
// These helpers contain no EPDiy dependency so the packing/rotation rules can
// be exhaustively host-tested.
namespace meshink_t5_packed {

inline bool map_logical_rect(MeshInkRect rect,MeshInkRotation rotation,
                             int physical_width,int physical_height,
                             MeshInkRect& physical) {
    if(physical_width<=0||physical_height<=0||
       rect.width<=0||rect.height<=0)return false;

    const bool portrait=rotation==MeshInkRotation::Portrait||
                        rotation==MeshInkRotation::InvertedPortrait;
    const int logical_width=portrait?physical_height:physical_width;
    const int logical_height=portrait?physical_width:physical_height;

    long long x0=rect.x;
    long long y0=rect.y;
    long long x1=x0+(long long)rect.width;
    long long y1=y0+(long long)rect.height;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>logical_width)x1=logical_width;
    if(y1>logical_height)y1=logical_height;
    if(x0>=x1||y0>=y1)return false;

    switch(rotation) {
        case MeshInkRotation::Portrait:
            physical={(int)(physical_width-y1),(int)x0,
                      (int)(y1-y0),(int)(x1-x0)};
            break;
        case MeshInkRotation::InvertedLandscape:
            physical={(int)(physical_width-x1),(int)(physical_height-y1),
                      (int)(x1-x0),(int)(y1-y0)};
            break;
        case MeshInkRotation::InvertedPortrait:
            physical={(int)y0,(int)(physical_height-x1),
                      (int)(y1-y0),(int)(x1-x0)};
            break;
        case MeshInkRotation::Landscape:
        default:
            physical={(int)x0,(int)y0,(int)(x1-x0),(int)(y1-y0)};
            break;
    }
    return physical.x>=0&&physical.y>=0&&physical.width>0&&physical.height>0&&
           physical.x+physical.width<=physical_width&&
           physical.y+physical.height<=physical_height;
}

inline void fill_physical_gray4(uint8_t* framebuffer,int physical_width,
                                int physical_height,MeshInkRect rect,
                                uint8_t gray4) {
    if(!framebuffer||physical_width<=0||physical_height<=0||
       rect.width<=0||rect.height<=0)return;

    int x0=rect.x,y0=rect.y;
    int x1=rect.x+rect.width,y1=rect.y+rect.height;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>physical_width)x1=physical_width;
    if(y1>physical_height)y1=physical_height;
    if(x0>=x1||y0>=y1)return;

    gray4&=0x0FU;
    const uint8_t packed=(uint8_t)(gray4|(uint8_t)(gray4<<4));
    const size_t row_bytes=((size_t)physical_width+1U)/2U;

    for(int y=y0;y<y1;++y) {
        uint8_t* row=framebuffer+(size_t)y*row_bytes;
        int x=x0;

        // Odd physical X owns the high nibble of its byte.
        if(x&1) {
            const size_t byte=(unsigned)x>>1;
            row[byte]=(uint8_t)((row[byte]&0x0FU)|(uint8_t)(gray4<<4));
            ++x;
        }

        // The interior consists entirely of whole pixel pairs.
        const int pair_count=(x1-x)/2;
        if(pair_count>0) {
            memset(row+((unsigned)x>>1),packed,(size_t)pair_count);
            x+=pair_count*2;
        }

        // A final even physical X owns the low nibble.
        if(x<x1) {
            const size_t byte=(unsigned)x>>1;
            row[byte]=(uint8_t)((row[byte]&0xF0U)|gray4);
        }
    }
}

inline bool fill_logical_gray4(uint8_t* framebuffer,int physical_width,
                               int physical_height,MeshInkRotation rotation,
                               MeshInkRect logical,uint8_t gray4) {
    MeshInkRect physical{};
    if(!map_logical_rect(logical,rotation,physical_width,physical_height,physical))
        return false;
    fill_physical_gray4(framebuffer,physical_width,physical_height,physical,gray4);
    return true;
}

} // namespace meshink_t5_packed

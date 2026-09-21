#pragma once
#include <stdint.h>

struct MapRenderResult { bool sd_ready; uint16_t tiles; uint16_t reused; };
MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,int height,
                                 double latitude,double longitude,uint8_t zoom);

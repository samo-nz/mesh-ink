#pragma once
#include <stdint.h>

// "tiles" counts successfully decoded screen tiles. "native" counts tiles
// found at the requested zoom; the remainder are upscaled parent tiles.
// "missing" counts screen tiles without any supported parent on the SD card.
struct MapRenderResult {
    bool sd_ready;
    uint16_t tiles;
    uint16_t reused;
    uint16_t native = 0;
    uint16_t missing = 0;
    uint8_t min_source_zoom = 0;
    uint8_t max_source_zoom = 0;
};
MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,int height,
                                 double latitude,double longitude,uint8_t zoom);

#pragma once
#include <stdint.h>

// "tiles" counts successfully decoded screen tiles. "native" counts tiles
// found at the requested zoom; the remainder are upscaled parent tiles.
// "missing" counts screen tiles without any supported parent on the SD card.
struct MapRenderResult {
    bool sd_ready;
    uint16_t tiles;
    uint16_t reused;
    uint16_t native;
    uint16_t missing;
    uint8_t min_source_zoom;
    uint8_t max_source_zoom;
    // Decoded source PNGs are cached as packed 4-bit (16-level) grayscale.
    // RAM hits are tile requests served without decoding a PNG.
    // disk_decodes counts successful new PNG decodes in this render.
    // sd_checks counts card metadata checks for candidate tile paths.
    uint16_t ram_hits;
    uint16_t disk_decodes;
    uint16_t sd_checks;
};
MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,int height,
                                 double latitude,double longitude,uint8_t zoom);

// Probes a removable card and retries a failed mount without rebooting.
// Call before reusing a cached map framebuffer or periodically while on Maps.
bool map_tiles_media_ready();
// Changes whenever the old card is unmounted or a new mount succeeds.
// UI must discard cached map images when this value changes.
uint32_t map_tiles_media_epoch();

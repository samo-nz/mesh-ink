#pragma once
#include <stdint.h>

// A PNG byte range in a local PMTiles v3 archive. The archive remains on SD;
// the caller passes this range to PNGdec's existing file callbacks.
struct PmtilesPngRange {
    uint32_t offset;
    uint32_t length;
};

// Returns false for a missing tile or an unsupported/invalid archive.
// Only unwrapped 256x256 PNG raster tiles are supported (not vector tiles).
bool pmtiles_find_png(const char* path, int zoom, int x, int y,
                      PmtilesPngRange& range);

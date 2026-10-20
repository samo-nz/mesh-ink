#pragma once
#include <stdint.h>
#include "hardware/storage.h"


struct PmtilesPerfStats {
    uint32_t archive_open_us = 0;
    uint32_t prepare_us = 0;
    uint32_t metadata_seek_us = 0;
    uint32_t metadata_read_us = 0;
    uint32_t metadata_bytes = 0;
    uint32_t inflate_us = 0;
    uint32_t index_parse_us = 0;
    uint16_t metadata_reads = 0;
    uint16_t leaf_loads = 0;
};

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

// Keep the archive handle open while a map viewport requests many tiles.
// Always end a frame before unmounting an SD card.
void pmtiles_begin_frame();
void pmtiles_end_frame();
// Open and prepare one archive ahead of the first map frame. The read-only
// handle/root index are retained and reused by later pmtiles_begin_frame().
bool pmtiles_warm_archive(const char* path);
// Borrow the archive file already opened for this render. Valid only until
// pmtiles_end_frame()/pmtiles_reset(); the caller must NOT close this handle.
MeshInkStorageFile* pmtiles_frame_file(const char* path);
// Archives that fail to open/index are disabled for the current SD mount.
// A missing individual tile does not disable its containing archive.
bool pmtiles_archive_failed(const char* path);
void pmtiles_disable_archive(const char* path);
bool pmtiles_had_io_error();
// Per-frame cold-path instrumentation. Categories may be nested; callers use
// these to separate archive metadata costs from tile PNG range I/O.
PmtilesPerfStats pmtiles_perf_stats();
// Discard all file handles and directory indexes on media failure/remount.
void pmtiles_reset();

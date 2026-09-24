#pragma once
#include <stdint.h>
#include <SD.h> // platform's File is an fs::File alias; do not forward-declare class File

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
// Borrow the archive file already opened for this render. Valid only until
// pmtiles_end_frame()/pmtiles_reset(); the caller must NOT close this handle.
File* pmtiles_frame_file(const char* path);
// SD seek/read/open failures must not be treated as permanently missing tiles.
bool pmtiles_had_io_error();
// Discard all file handles and directory indexes on media failure/remount.
void pmtiles_reset();

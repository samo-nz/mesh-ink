#include "pmtiles_reader.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
// ESP32-S3's tinfl_decompress() resolves to the built-in ROM function
// (0x40000828), NOT to the externally installed richgel999/miniz library.
// Its tinfl_decompressor layout is different. Using <miniz.h> allocated an
// 8,364-byte state buffer for a ROM function expecting a larger structure,
// overwriting heap metadata during nontrivial gzip directory decoding.
#ifdef ESP32
#include <esp32s3/rom/miniz.h>
static_assert(sizeof(tinfl_decompressor) >= 9000,
              "Use the ESP32-S3 ROM tinfl_decompressor layout");
#else
#include <miniz.h> // Host regression tests use a zlib-backed tinfl stub.
#endif
#include <string.h>
#include <stdlib.h>

// PMTiles v3: https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md
// Directories can be gzip compressed. Read only the indexed directory and
// requested tile range; never copy an archive into RAM.
namespace {
constexpr size_t MAX_DIRECTORY_BYTES = 512 * 1024;
constexpr uint32_t MAX_ROOT_BYTES = 16384;
constexpr uint32_t MAX_DIRECTORY_HOPS = 4;
struct Entry {
    uint64_t id;
    uint64_t offset;
    uint32_t length;
    uint32_t run;
};
struct Directory {
    Entry* entries = nullptr;
    size_t count = 0;
};
struct Archive {
    uint64_t root_offset = 0, root_length = 0;
    uint64_t leaf_offset = 0, leaf_length = 0;
    uint64_t tile_offset = 0, tile_length = 0;
    uint32_t file_size = 0;
    uint8_t compression = 0;
    uint8_t min_zoom = 0, max_zoom = 0;
    bool supported = false;
};
Archive archive;
Directory root, leaf;
char cached_path[160]{};
bool prepared = false;
uint64_t cached_leaf_offset = UINT64_MAX;
uint64_t cached_leaf_length = 0;

uint64_t little64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
uint32_t little32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void clear_directory(Directory& d) {
    free(d.entries);
    d.entries = nullptr;
    d.count = 0;
}
bool within(uint64_t start, uint64_t length, uint64_t total) {
    return start <= total && length <= total - start;
}
// Stage SD reads in bounded, aligned internal RAM rather than passing
// potentially unaligned PSRAM allocations directly to the SD driver.
bool read_at(File& file, uint64_t start, uint8_t* dst, size_t n) {
    if (!dst || start > UINT32_MAX || !file.seek((uint32_t)start))
        return false;
    alignas(4) static uint8_t stage[256];
    while (n) {
        const size_t chunk = n < sizeof(stage) ? n : sizeof(stage);
        if (file.read(stage, chunk) != chunk) return false;
        memcpy(dst, stage, chunk);
        dst += chunk;
        n -= chunk;
    }
    return true;
}
bool read_varint(const uint8_t*& cursor, const uint8_t* end, uint64_t& out) {
    out = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        if (cursor == end) return false;
        const uint8_t b = *cursor++;
        if (shift == 63 && b > 1) return false;
        if (shift > 63) return false;
        out |= (uint64_t)(b & 127) << shift;
        if (!(b & 128)) return true;
    }
    return false;
}
void* map_alloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
bool expand_gzip(const uint8_t* in, size_t in_size, uint8_t*& output,
                 size_t& output_size) {
    if (in_size < 18 || in[0] != 0x1f || in[1] != 0x8b ||
        in[2] != 8 || (in[3] & 0xe0)) return false;
    size_t pos = 10;
    const uint8_t flags = in[3];
    if (flags & 4) {
        if (pos + 2 > in_size - 8) return false;
        const size_t extra = (size_t)in[pos] | ((size_t)in[pos + 1] << 8);
        pos += 2;
        if (extra > in_size - 8 - pos) return false;
        pos += extra;
    }
    if (flags & 8) {
        while (pos < in_size - 8 && in[pos]) ++pos;
        if (pos == in_size - 8) return false;
        ++pos;
    }
    if (flags & 16) {
        while (pos < in_size - 8 && in[pos]) ++pos;
        if (pos == in_size - 8) return false;
        ++pos;
    }
    if (flags & 2) {
        if (pos + 2 > in_size - 8) return false;
        pos += 2; // optional gzip header CRC
    }
    if (pos >= in_size - 8) return false;
    output_size = little32(in + in_size - 4);
    if (!output_size || output_size > MAX_DIRECTORY_BYTES) return false;
    // Small directory output stays in internal RAM; larger directories
    // use bounded PSRAM. Always use the ROM-compatible decoder state below.
    constexpr size_t OUTPUT_GUARD = 32;
    constexpr size_t INTERNAL_DECODE_LIMIT = 16 * 1024;
    if (output_size > SIZE_MAX - OUTPUT_GUARD) return false;
    const bool internal_output = output_size <= INTERNAL_DECODE_LIMIT;
    output = (uint8_t*)(internal_output
        ? heap_caps_malloc(output_size + OUTPUT_GUARD,
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
        : map_alloc(output_size + OUTPUT_GUARD));
    if (!output) {
        Serial.printf("[T5-PMT] gzip output allocation failed (%u bytes)\n",
                      (unsigned)output_size);
        return false;
    }
    memset(output + output_size, 0xa5, OUTPUT_GUARD);
    // tinfl_decompress_mem_to_mem() puts this large struct on loopTask's
    // stack. The low-level API lets us keep it on the internal heap.
    tinfl_decompressor* decoder = (tinfl_decompressor*)heap_caps_malloc(
        sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!decoder) {
        Serial.printf("[T5-PMT] gzip internal scratch allocation failed (%u bytes)\n",
                      (unsigned)sizeof(tinfl_decompressor));
        free(output);
        output = nullptr;
        return false;
    }
    tinfl_init(decoder);
    size_t input_length = in_size - 8 - pos;
    size_t actual = output_size;
    const tinfl_status status = tinfl_decompress(
        decoder, in + pos, &input_length, output, output, &actual,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    for (size_t i = 0; i < OUTPUT_GUARD; ++i) {
        if (output[output_size + i] != 0xa5) {
            Serial.printf("[T5-PMT] gzip output exceeded buffer at +%u\n",
                          (unsigned)i);
            abort();
        }
    }
    free(decoder);
    if (status != TINFL_STATUS_DONE || actual != output_size ||
        (uint32_t)mz_crc32(MZ_CRC32_INIT, output, actual) !=
            little32(in + in_size - 8)) {
        free(output);
        output = nullptr;
        return false;
    }
    return true;
}
bool parse_directory(File& file, uint64_t start, uint64_t size,
                     Directory& output) {
    clear_directory(output);
    if (!size || size > MAX_DIRECTORY_BYTES ||
        !within(start, size, archive.file_size)) return false;
    // Keep ordinary compressed directories (including this map's 5,769 B
    // leaf) in internal RAM to isolate SD reads from PSRAM heap writes.
    // Larger directories retain the existing bounded PSRAM fallback.
    uint8_t* compressed = size <= 8192
        ? (uint8_t*)heap_caps_malloc((size_t)size,
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
        : nullptr;
    if (!compressed) compressed = (uint8_t*)map_alloc((size_t)size);
    if (!compressed) return false;
    if (!read_at(file, start, compressed, (size_t)size)) {
        free(compressed);
        return false;
    }
    uint8_t* decoded = compressed;
    size_t decoded_size = (size_t)size;
    if (archive.compression == 2) {
        decoded = nullptr;
        if (!expand_gzip(compressed, (size_t)size, decoded, decoded_size)) {
            free(compressed);
            return false;
        }
        free(compressed);
    }
    const uint8_t* cur = decoded;
    const uint8_t* end = decoded + decoded_size;
    uint64_t count = 0;
    bool ok = read_varint(cur, end, count) && count &&
              count <= decoded_size / 4 &&
              count <= SIZE_MAX / sizeof(Entry);
    if (!ok) { free(decoded); return false; }
    Entry* entries = (Entry*)map_alloc((size_t)count * sizeof(Entry));
    if (!entries) { free(decoded); return false; }
    memset(entries, 0, (size_t)count * sizeof(Entry));
    uint64_t id = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        uint64_t delta = 0;
        ok = read_varint(cur, end, delta) && delta <= UINT64_MAX - id;
        if (ok) { id += delta; entries[i].id = id; }
    }
    for (size_t i = 0; ok && i < count; ++i) {
        uint64_t run = 0;
        ok = read_varint(cur, end, run) && run <= UINT32_MAX;
        if (ok) entries[i].run = (uint32_t)run;
    }
    for (size_t i = 0; ok && i < count; ++i) {
        uint64_t length = 0;
        ok = read_varint(cur, end, length) && length &&
             length <= UINT32_MAX;
        if (ok) entries[i].length = (uint32_t)length;
    }
    uint64_t previous_end = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        uint64_t encoded = 0;
        ok = read_varint(cur, end, encoded);
        if (!ok) break;
        const uint64_t offset = encoded == 0 && i ? previous_end :
                                encoded ? encoded - 1 : UINT64_MAX;
        ok = offset != UINT64_MAX &&
             offset <= UINT64_MAX - entries[i].length;
        if (ok) {
            entries[i].offset = offset;
            previous_end = offset + entries[i].length;
        }
    }
    free(decoded);
    if (!ok) { free(entries); return false; }
    output.entries = entries;
    output.count = (size_t)count;
    return true;
}
bool prepare(File& file, const char* path) {
    if (strlen(path) >= sizeof(cached_path)) return false;
    if (strcmp(path, cached_path) == 0 && prepared) return archive.supported;
    clear_directory(root);
    clear_directory(leaf);
    cached_leaf_offset = UINT64_MAX;
    cached_leaf_length = 0;
    archive = Archive{};
    strcpy(cached_path, path);
    prepared = true;
    archive.file_size = file.size(); // ESP32 Arduino SD seeks use 32-bit offsets.
    if (archive.file_size < 127) return false;
    uint8_t header[127];
    if (!read_at(file, 0, header, sizeof(header)) ||
        memcmp(header, "PMTiles", 7) || header[7] != 3) return false;
    archive.root_offset = little64(header + 8);
    archive.root_length = little64(header + 16);
    archive.leaf_offset = little64(header + 40);
    archive.leaf_length = little64(header + 48);
    archive.tile_offset = little64(header + 56);
    archive.tile_length = little64(header + 64);
    archive.compression = header[97];
    archive.min_zoom = header[100];
    archive.max_zoom = header[101];
    if (header[99] != 2 || header[98] != 1 ||
        (archive.compression != 1 && archive.compression != 2) ||
        archive.min_zoom > archive.max_zoom ||
        archive.root_length > MAX_ROOT_BYTES ||
        !within(archive.root_offset, archive.root_length, archive.file_size) ||
        !within(archive.leaf_offset, archive.leaf_length, archive.file_size) ||
        !within(archive.tile_offset, archive.tile_length, archive.file_size))
        return false;
    Serial.printf("[T5-PMT] open %s root=%llu bytes compression=%u\n",
                  path, (unsigned long long)archive.root_length,
                  (unsigned)archive.compression);
    archive.supported = parse_directory(file, archive.root_offset,
                                        archive.root_length, root);
    return archive.supported;
}
// Use the PMTiles Hilbert ordering, not a row-major or TMS tile address.
uint64_t tile_id(int zoom, uint32_t x, uint32_t y) {
    const uint32_t n = 1U << zoom;
    uint64_t id = ((1ULL << (2 * zoom)) - 1) / 3;
    for (uint32_t s = n / 2; s; s /= 2) {
        const uint32_t rx = (x & s) ? 1 : 0;
        const uint32_t ry = (y & s) ? 1 : 0;
        id += (uint64_t)s * s * ((3 * rx) ^ ry);
        if (!ry) {
            if (rx) { x = s - 1 - x; y = s - 1 - y; }
            const uint32_t temp = x; x = y; y = temp;
        }
    }
    return id;
}
const Entry* select_entry(const Directory& d, uint64_t id) {
    size_t low = 0, high = d.count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (d.entries[mid].id <= id) low = mid + 1;
        else high = mid;
    }
    return low ? &d.entries[low - 1] : nullptr;
}
} // namespace

bool pmtiles_find_png(const char* path, int zoom, int x, int y,
                      PmtilesPngRange& range) {
    range = {};
    if (!path || zoom < 0 || zoom > 24 ||
        x < 0 || y < 0 || (uint32_t)x >= (1U << zoom) ||
        (uint32_t)y >= (1U << zoom)) return false;
    File file = SD.open(path, FILE_READ);
    if (!file || !prepare(file, path) ||
        zoom < archive.min_zoom || zoom > archive.max_zoom) {
        if (file) file.close();
        return false;
    }
    const uint64_t id = tile_id(zoom, (uint32_t)x, (uint32_t)y);
    const Directory* directory = &root;
    for (uint32_t hop = 0; hop < MAX_DIRECTORY_HOPS; ++hop) {
        const Entry* entry = select_entry(*directory, id);
        if (!entry) break;
        if (entry->run) {
            if (id >= entry->id && id - entry->id < entry->run &&
                within(entry->offset, entry->length, archive.tile_length)) {
                const uint64_t absolute = archive.tile_offset + entry->offset;
                if (absolute <= UINT32_MAX &&
                    within(absolute, entry->length, archive.file_size)) {
                    range.offset = (uint32_t)absolute;
                    range.length = entry->length;
                    file.close();
                    return true;
                }
            }
            break;
        }
        if (!within(entry->offset, entry->length, archive.leaf_length))
            break;
        if (cached_leaf_offset != entry->offset ||
            cached_leaf_length != entry->length) {
            clear_directory(leaf);
            cached_leaf_offset = entry->offset;
            cached_leaf_length = entry->length;
            Serial.printf("[T5-PMT] leaf for z=%d x=%d y=%d offset=%llu length=%u\n",
                          zoom, x, y, (unsigned long long)entry->offset,
                          (unsigned)entry->length);
            if (!parse_directory(file, archive.leaf_offset + entry->offset,
                                 entry->length, leaf)) break;
        }
        directory = &leaf;
    }
    file.close();
    return false;
}

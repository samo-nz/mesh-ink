#include "pmtiles_reader.h"
#include <SD.h>
#include <zlib.h>
#include <assert.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>

std::map<std::string, std::vector<uint8_t>> mock_sd;
MockSD SD;
using Bytes = std::vector<uint8_t>;
void varint(Bytes& b, uint64_t value) {
    while (value >= 128) { b.push_back((uint8_t)value | 128); value >>= 7; }
    b.push_back((uint8_t)value);
}
void le64(Bytes& b, size_t pos, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) b[pos + i] = (uint8_t)(value >> (8 * i));
}
Bytes directory(uint64_t id, uint32_t length, uint32_t run) {
    Bytes b;
    varint(b, 1); varint(b, id); varint(b, run);
    varint(b, length); varint(b, 1); // offset 0 + 1
    return b;
}
Bytes gzip(const Bytes& raw) {
    z_stream z{};
    assert(deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                        15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    Bytes b(compressBound((uLong)raw.size()) + 64);
    z.next_in = (Bytef*)raw.data();
    z.avail_in = (uInt)raw.size();
    z.next_out = b.data();
    z.avail_out = (uInt)b.size();
    assert(deflate(&z, Z_FINISH) == Z_STREAM_END);
    b.resize(z.total_out);
    deflateEnd(&z);
    return b;
}
Bytes make_archive(bool compressed, bool leaf, uint8_t type=2,
                   uint8_t tile_compression=1) {
    const Bytes tile{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    Bytes leaf_dir = leaf ? directory(1, (uint32_t)tile.size(), 4) : Bytes{};
    if (compressed && leaf) leaf_dir = gzip(leaf_dir);
    Bytes root = directory(1, leaf ? (uint32_t)leaf_dir.size()
                                   : (uint32_t)tile.size(), leaf ? 0 : 4);
    if (compressed) root = gzip(root);
    Bytes b(127, 0);
    const char magic[] = "PMTiles";
    for (size_t i=0; i<7; ++i) b[i] = magic[i];
    b[7] = 3;
    le64(b, 8, 127);
    le64(b, 16, root.size());
    le64(b, 40, 127 + root.size());
    le64(b, 48, leaf_dir.size());
    le64(b, 56, 127 + root.size() + leaf_dir.size());
    le64(b, 64, tile.size());
    b[97] = compressed ? 2 : 1;
    b[98] = tile_compression;
    b[99] = type;
    b[100] = 0; b[101] = 2;
    b.insert(b.end(), root.begin(), root.end());
    b.insert(b.end(), leaf_dir.begin(), leaf_dir.end());
    b.insert(b.end(), tile.begin(), tile.end());
    return b;
}
void assert_range(const char* name, int z, int x, int y) {
    PmtilesPngRange r{};
    assert(pmtiles_find_png(name,z,x,y,r));
    const Bytes& a = mock_sd.at(name);
    assert(r.length == 8 && r.offset + r.length <= a.size());
    assert(a[r.offset] == 0x89 && a[r.offset + 1] == 'P');
}
int main() {
    mock_sd["/maps/plain.pmtiles"] = make_archive(false,false);
    mock_sd["/maps/gzip.pmtiles"] = make_archive(true,true);
    mock_sd["/maps/vector.pmtiles"] = make_archive(true,false,1);
    mock_sd["/maps/wrapped.pmtiles"] = make_archive(true,false,2,2);
    assert_range("/maps/plain.pmtiles",1,0,0); // PMTiles ID 1
    assert_range("/maps/plain.pmtiles",1,0,1); // PMTiles ID 2
    assert_range("/maps/plain.pmtiles",1,1,1); // PMTiles ID 3
    assert_range("/maps/plain.pmtiles",1,1,0); // PMTiles ID 4
    assert_range("/maps/gzip.pmtiles",1,1,0);  // root -> gzip leaf
    assert_range("/maps/gzip.pmtiles",1,0,0);  // repeat cached leaf
    PmtilesPngRange range{};
    assert(!pmtiles_find_png("/maps/gzip.pmtiles",0,0,0,range));
    assert(!pmtiles_find_png("/maps/gzip.pmtiles",2,0,0,range));
    assert(!pmtiles_find_png("/maps/vector.pmtiles",1,0,0,range));
    assert(!pmtiles_find_png("/maps/wrapped.pmtiles",1,0,0,range));
    assert_range("/maps/plain.pmtiles",1,1,0); // archive cache switching
    // One map frame reuses an open archive while alternating nearby tile
    // lookups, and a removed/missing archive must be reported as I/O failure.
    pmtiles_begin_frame();
    assert_range("/maps/gzip.pmtiles",1,1,0);
    assert_range("/maps/gzip.pmtiles",1,0,0);
    assert(!pmtiles_had_io_error());
    pmtiles_end_frame();
    PmtilesPngRange disconnected{};
    pmtiles_begin_frame();
    assert(!pmtiles_find_png("/maps/disconnected.pmtiles",1,0,0,disconnected));
    assert(pmtiles_had_io_error());
    pmtiles_end_frame();
    pmtiles_reset();
    assert_range("/maps/gzip.pmtiles",1,1,0); // directory rebuilt after remount
    std::cout << "PMTiles plain/gzip root+leaf, run ranges, omissions,"
                 " unsupported formats and archive switching: PASS\n";
}

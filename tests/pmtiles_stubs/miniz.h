#pragma once
#include <zlib.h>
#include <stddef.h>
#include <stdint.h>
#define MZ_CRC32_INIT 0
static inline uint32_t mz_crc32(uint32_t crc, const unsigned char* p, size_t n) {
    return (uint32_t)crc32(crc, p, (uInt)n);
}
static inline size_t tinfl_decompress_mem_to_mem(
    void* output, size_t capacity, const void* input, size_t length, int) {
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK) return (size_t)-1;
    stream.next_in = (Bytef*)input;
    stream.avail_in = (uInt)length;
    stream.next_out = (Bytef*)output;
    stream.avail_out = (uInt)capacity;
    const int status = inflate(&stream, Z_FINISH);
    const size_t result = status == Z_STREAM_END ? stream.total_out : (size_t)-1;
    inflateEnd(&stream);
    return result;
}

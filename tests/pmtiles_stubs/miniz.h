#pragma once
#include <zlib.h>
#include <stddef.h>
#include <stdint.h>
#define MZ_CRC32_INIT 0
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
typedef enum { TINFL_STATUS_FAILED = -1, TINFL_STATUS_DONE = 0 } tinfl_status;
struct tinfl_decompressor { z_stream stream; };
static inline void tinfl_init(tinfl_decompressor* p) {
    p->stream = z_stream{};
}
static inline tinfl_status tinfl_decompress(tinfl_decompressor* p,
      const uint8_t* input, size_t* input_size,
      uint8_t*, uint8_t* output, size_t* output_size, int) {
    if (inflateInit2(&p->stream, -15) != Z_OK)
        return TINFL_STATUS_FAILED;
    p->stream.next_in = (Bytef*)input;
    p->stream.avail_in = (uInt)*input_size;
    p->stream.next_out = output;
    p->stream.avail_out = (uInt)*output_size;
    const int status = inflate(&p->stream, Z_FINISH);
    *input_size = p->stream.total_in;
    *output_size = p->stream.total_out;
    inflateEnd(&p->stream);
    return status == Z_STREAM_END ? TINFL_STATUS_DONE : TINFL_STATUS_FAILED;
}
static inline uint32_t mz_crc32(uint32_t crc, const unsigned char* p, size_t n) {
    return (uint32_t)crc32(crc, p, (uInt)n);
}

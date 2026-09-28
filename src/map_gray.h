#pragma once
#include <stdint.h>

namespace meshink_map_gray {

// Preserve MeshInk's existing map luminance exactly. RGB565 inputs are
// weighted in their native 5/6/5 precision, then quantized to the 16-level
// source cache used by the world-anchored dither.
inline uint8_t level_from_rgb565(uint16_t colour) {
    const unsigned raw=(((((unsigned)colour>>11)&31U)*77U+
                         (((unsigned)colour>>5)&63U)*75U+
                         ((unsigned)colour&31U)*29U)>>5);
    const unsigned clamped=raw>255U?255U:raw;
    const unsigned level=(clamped+8U)/17U;
    return (uint8_t)(level>15U?15U:level);
}

// Equivalent to converting RGB888 -> RGB565 first and then calling the
// function above, but without materializing the intermediate RGB565 pixel.
// Low RGB bits discarded by RGB565 are intentionally ignored so output is
// bit-for-bit identical to the established map palette.
inline uint8_t level_from_rgb888(uint8_t r,uint8_t g,uint8_t b) {
    const unsigned raw=((((unsigned)(r>>3)*77U)+
                         ((unsigned)(g>>2)*75U)+
                         ((unsigned)(b>>3)*29U))>>5);
    const unsigned clamped=raw>255U?255U:raw;
    const unsigned level=(clamped+8U)/17U;
    return (uint8_t)(level>15U?15U:level);
}

} // namespace meshink_map_gray

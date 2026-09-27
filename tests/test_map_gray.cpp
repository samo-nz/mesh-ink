#include "map_gray.h"
#include <cassert>
#include <cstdio>

using namespace meshink_map_gray;

int main() {
    // Exhaust every RGB565 colour. For each quantization bucket, test both
    // the lowest and highest RGB888 values that map to it.
    for(unsigned r5=0;r5<32;++r5) {
        for(unsigned g6=0;g6<64;++g6) {
            for(unsigned b5=0;b5<32;++b5) {
                const uint16_t rgb565=(uint16_t)((r5<<11)|(g6<<5)|b5);
                const uint8_t expected=level_from_rgb565(rgb565);
                assert(level_from_rgb888((uint8_t)(r5<<3),
                                         (uint8_t)(g6<<2),
                                         (uint8_t)(b5<<3))==expected);
                assert(level_from_rgb888((uint8_t)((r5<<3)|7U),
                                         (uint8_t)((g6<<2)|3U),
                                         (uint8_t)((b5<<3)|7U))==expected);
            }
        }
    }
    std::puts("Direct RGB888 to 4-bit map grayscale matches RGB565 path");
}

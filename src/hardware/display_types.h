#pragma once

#include <stdint.h>
#include "orientation.h"

// Board-independent display vocabulary used by MeshInk application code.
// Backends translate these concepts to their controller/driver-specific API.
struct MeshInkRect {
    int x;
    int y;
    int width;
    int height;
};

enum class MeshInkRotation : uint8_t {
    Landscape = 0,
    Portrait,
    InvertedLandscape,
    InvertedPortrait,
};

enum class MeshInkRefreshMode : uint8_t {
    Direct = 0,      // fast binary transition (EPDiy DU on the T5)
    Gray16,          // full grayscale clean/update (EPDiy GC16)
    FastGray16,      // faster grayscale transition (EPDiy GL16)
};

using MeshInkDisplayResult = int32_t;

struct MeshInkDisplayGeometry {
    int physical_width;
    int physical_height;
    int portrait_width;
    int portrait_height;
    uint8_t framebuffer_bits_per_pixel;
};

// Bulk 4-bit grayscale source blit used by Maps. The display backend owns
// physical framebuffer packing and may implement an accelerated path.
struct MeshInkGray4DitherBlit {
    const uint8_t* source;
    int source_width;
    MeshInkRect source_rect;
    MeshInkRect destination_rect;
    MeshInkRect clip_rect;
    int world_origin_x;
    int world_origin_y;
    const uint16_t* dither_masks;
};

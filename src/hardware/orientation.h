#pragma once
#include <stdint.h>

// Application-visible screen orientation. Board/display/touch backends map
// this logical orientation to their physical panel/controller mounting.
enum class MeshInkOrientation : uint8_t {
    Portrait = 0,
    Landscape = 1,
};

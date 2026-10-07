#pragma once
#include <stdint.h>

enum class MeshInkTimeSource : uint8_t {
    Unknown = 0,
    HardwareRtc = 1,
    Protocol = 2,
    Companion = 3,
    Gps = 4,
    Manual = 5
};

enum class MeshInkTimeMode : uint8_t {
    Auto = 0,
    Manual = 1
};

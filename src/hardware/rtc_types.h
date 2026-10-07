#pragma once
#include <stdint.h>

enum class MeshInkTimeSource : uint8_t {
    Unknown = 0,
    HardwareRtc = 1,
    MeshCore = 2,
    Companion = 3,
    Gps = 4,
    Manual = 5
};

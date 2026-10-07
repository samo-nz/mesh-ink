#pragma once
#include <stdint.h>

// Board-independent GNSS constellation vocabulary. Values are a generic
// three-bit mask; board backends translate the selected systems into their
// receiver-specific configuration commands.
enum class MeshInkGpsConstellation : uint8_t {
    Gps = 0x01,
    BeiDou = 0x02,
    Glonass = 0x04
};

enum class MeshInkGpsConstellationMode : uint8_t {
    None = 0,
    GpsOnly = 1,
    BeiDouOnly = 2,
    GpsBeiDou = 3,
    GlonassOnly = 4,
    GpsGlonass = 5,
    BeiDouGlonass = 6,
    GpsBeiDouGlonass = 7
};

inline bool meshink_gps_constellation_mode_valid(MeshInkGpsConstellationMode mode) {
    const uint8_t bits=static_cast<uint8_t>(mode);
    return bits>=1U&&bits<=7U;
}

inline bool meshink_gps_constellation_enabled(
    MeshInkGpsConstellationMode mode,MeshInkGpsConstellation constellation) {
    return (static_cast<uint8_t>(mode)&static_cast<uint8_t>(constellation))!=0;
}

// Build a new non-empty constellation mask. Returning false when the requested
// change would clear the final enabled system lets every UI enforce the same
// hardware-agnostic "at least one constellation" rule.
inline bool meshink_gps_constellation_mode_set(
    MeshInkGpsConstellationMode current,MeshInkGpsConstellation constellation,
    bool enabled,MeshInkGpsConstellationMode& next) {
    uint8_t bits=static_cast<uint8_t>(current);
    const uint8_t bit=static_cast<uint8_t>(constellation);
    if(enabled)bits|=bit;
    else bits&=(uint8_t)~bit;
    if(bits<1U||bits>7U)return false;
    next=static_cast<MeshInkGpsConstellationMode>(bits);
    return true;
}

struct MeshInkGpsStatus {
    bool available = false;
    bool valid = false;
    bool waiting_time_sync = true;
    int32_t satellites = 0;
    long latitude = 0;
    long longitude = 0;
    uint32_t timestamp = 0;
};

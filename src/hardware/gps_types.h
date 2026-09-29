#pragma once
#include <stdint.h>

// Board-independent GPS tuning vocabulary. Numeric values intentionally
// preserve MeshInk's existing saved preference values; backends interpret
// the semantic mode rather than exposing receiver command syntax to the app.
enum class MeshInkGpsConstellationMode : uint8_t {
    Unchanged = 0,
    GpsOnly = 1,
    GpsBeiDou = 3,
    GpsGlonass = 5,
    GpsBeiDouGlonass = 7
};

inline bool meshink_gps_constellation_mode_valid(MeshInkGpsConstellationMode mode) {
    switch(mode) {
        case MeshInkGpsConstellationMode::Unchanged:
        case MeshInkGpsConstellationMode::GpsOnly:
        case MeshInkGpsConstellationMode::GpsBeiDou:
        case MeshInkGpsConstellationMode::GpsGlonass:
        case MeshInkGpsConstellationMode::GpsBeiDouGlonass:
            return true;
        default:
            return false;
    }
}

// Preserve the field-tested on-device cycle order exactly.
inline MeshInkGpsConstellationMode meshink_gps_next_constellation_mode(
    MeshInkGpsConstellationMode mode) {
    switch(mode) {
        case MeshInkGpsConstellationMode::Unchanged:
            return MeshInkGpsConstellationMode::GpsOnly;
        case MeshInkGpsConstellationMode::GpsOnly:
            return MeshInkGpsConstellationMode::GpsGlonass;
        case MeshInkGpsConstellationMode::GpsGlonass:
            return MeshInkGpsConstellationMode::GpsBeiDou;
        case MeshInkGpsConstellationMode::GpsBeiDou:
            return MeshInkGpsConstellationMode::GpsBeiDouGlonass;
        default:
            return MeshInkGpsConstellationMode::GpsOnly;
    }
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

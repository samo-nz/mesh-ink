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

enum class MeshInkGpsPowerExperiment : uint8_t {
    GpsOnly = 0,
    BeiDouOnly,
    GlonassOnly,
    NmeaEvery9,
    NmeaOff,
    GpsOnlyNmeaOff,
    UartHighImpedance,
    RfOff,
    RfOffUartHighImpedance,
    CasicTimedStandby60s,
    SlowFix5s,
    SlowFix10s
};

inline const char* meshink_gps_power_experiment_name(MeshInkGpsPowerExperiment experiment) {
    switch(experiment) {
        case MeshInkGpsPowerExperiment::GpsOnly:return "GPS ONLY";
        case MeshInkGpsPowerExperiment::BeiDouOnly:return "BEIDOU ONLY";
        case MeshInkGpsPowerExperiment::GlonassOnly:return "GLONASS ONLY";
        case MeshInkGpsPowerExperiment::NmeaEvery9:return "NMEA EVERY 9 FIXES";
        case MeshInkGpsPowerExperiment::NmeaOff:return "NMEA OFF";
        case MeshInkGpsPowerExperiment::GpsOnlyNmeaOff:return "GPS ONLY + NMEA OFF";
        case MeshInkGpsPowerExperiment::UartHighImpedance:return "UART HIGH-Z";
        case MeshInkGpsPowerExperiment::RfOff:return "CASIC RF OFF";
        case MeshInkGpsPowerExperiment::RfOffUartHighImpedance:return "RF OFF + UART HIGH-Z";
        case MeshInkGpsPowerExperiment::CasicTimedStandby60s:return "CASIC STANDBY 60S";
        case MeshInkGpsPowerExperiment::SlowFix5s:return "5 SECOND FIX INTERVAL";
        case MeshInkGpsPowerExperiment::SlowFix10s:return "10 SECOND FIX INTERVAL";
        default:return "UNKNOWN";
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

#pragma once
#include "../hardware/gps_types.h"
#include <stdint.h>

// Snapshot is produced only by MeshInk's UI/GNSS owner task. The official
// networking worker must not concurrently read the mutable MicroNMEA parser.
struct MeshInkMeshtasticGpsSnapshot {
    MeshInkGpsStatus fix{};
    bool enabled=false;
    bool rtc_valid=false;
    bool gps_authoritative=false;
    uint32_t utc=0;
};
void meshink_meshtastic_native_set_gps_snapshot(const MeshInkMeshtasticGpsSnapshot& snapshot);

// Called by the selected protocol backend after MeshInk's board/storage startup.
// Upstream Meshtastic exclusively owns the networking engine while selected.
bool meshink_meshtastic_native_begin();
void meshink_meshtastic_native_loop();
// Feed upstream position networking from MeshInk's existing GNSS service.
void meshink_meshtastic_native_gps_update();
void meshink_meshtastic_native_stop();

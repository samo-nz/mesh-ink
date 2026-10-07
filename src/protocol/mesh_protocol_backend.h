#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../ui_data.h"
#include "../hardware/gps_types.h"
#include "../hardware/rtc_types.h"

enum MeshInkProtocolCapability : uint32_t {
    MESHINK_PROTOCOL_CAP_DISCOVERY   = 1u << 0,
    MESHINK_PROTOCOL_CAP_ADVERTISE   = 1u << 1,
    MESHINK_PROTOCOL_CAP_COMPANION   = 1u << 2,
    MESHINK_PROTOCOL_CAP_DIAGNOSTICS = 1u << 3,
    MESHINK_PROTOCOL_CAP_PRIVACY     = 1u << 4,
    MESHINK_PROTOCOL_CAP_PATH_HASH   = 1u << 5,
};

struct MeshInkProtocolDescriptor {
    uint8_t id = 0;
    const char* name = "UNKNOWN";
    const char* core_name = "UNKNOWN";
    const char* core_version = "";
    uint32_t capabilities = 0;
};

struct MeshInkProtocolBackend {
    MeshInkProtocolDescriptor descriptor{};

    // Boot/runtime lifecycle.
    void (*setup)() = nullptr;
    bool (*setup_rx_wake)() = nullptr;
    bool (*setup_button_wake)() = nullptr;
    void (*loop)() = nullptr;
    void (*service_startup)() = nullptr;
    void (*rx_wake_loop)() = nullptr;
    bool (*rx_wake_promoted)() = nullptr;
    void (*prepare_interactive_services)() = nullptr;
    bool (*promote_to_ui)(const char* source) = nullptr;
    bool (*enter_deep_sleep_standby)() = nullptr;
    bool (*is_running)() = nullptr;
    void (*flush_now)() = nullptr;
    void (*prepare_shutdown)() = nullptr;

    // Optional companion/transport mode owned by this protocol helper.
    void (*request_companion_mode)() = nullptr;
    bool (*consume_companion_request)() = nullptr;
    void (*companion_setup)() = nullptr;
    void (*companion_loop)() = nullptr;
    void (*companion_prepare_exit)() = nullptr;

    // Shared UI/data surface.
    UiDataProvider* (*provider)() = nullptr;
    void (*refresh_ui_data)() = nullptr;
    bool (*send_active)(const char* text) = nullptr;
    bool (*send_advert)(bool flood) = nullptr;

    // Identity/radio settings.
    bool (*apply_radio)(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t path_hash_mode) = nullptr;
    void (*apply_name)(const char* name) = nullptr;
    bool (*name_character_allowed)(char c) = nullptr;
    size_t (*node_name_max_length)() = nullptr;
    const char* (*node_name)() = nullptr;
    const char* (*radio_summary)() = nullptr;
    bool (*radio_matches)(float frequency_mhz, float bandwidth_khz, uint8_t spreading_factor,
                          uint8_t coding_rate, uint8_t path_hash_bytes) = nullptr;
    void (*cycle_path_hash)() = nullptr;
    uint8_t (*path_hash_mode)() = nullptr;

    // GPS/location behavior.
    void (*apply_gps)(bool enabled) = nullptr;
    bool (*gps_enabled)() = nullptr;
    bool (*gps_fix)() = nullptr;
    uint32_t (*gps_interval)() = nullptr;
    bool (*gps_advert_location)() = nullptr;
    bool (*my_location)(long& latitude, long& longitude) = nullptr;
    void (*cycle_gps_interval)() = nullptr;
    MeshInkGpsConstellationMode (*gps_constellation_mode)() = nullptr;
    bool (*gps_set_constellation_mode)(MeshInkGpsConstellationMode mode) = nullptr;
    bool (*gps_deep_sleep_power_save)() = nullptr;
    bool (*gps_set_deep_sleep_power_save)(bool enabled) = nullptr;
    const char* (*gps_tuning_note)() = nullptr;
    void (*toggle_gps_advert_location)() = nullptr;

    // Time is shared UI behavior; the protocol helper may feed the RTC.
    uint32_t (*current_time)() = nullptr;
    bool (*time_valid)() = nullptr;
    bool (*set_manual_time)(uint32_t utc) = nullptr;
    bool (*set_time_mode)(MeshInkTimeMode mode) = nullptr;
    MeshInkTimeMode (*time_mode)() = nullptr;
    MeshInkTimeSource (*time_source)() = nullptr;
    bool (*gps_time_authoritative)() = nullptr;

    // Optional protocol-specific settings/diagnostics.
    const char* (*privacy_value)(uint8_t item) = nullptr;
    void (*toggle_privacy)(uint8_t item) = nullptr;
    bool (*request_diagnostics)() = nullptr;
    bool (*diagnostics_busy)() = nullptr;
    const char* (*diagnostics_core)() = nullptr;
    const char* (*diagnostics_radio)() = nullptr;
    const char* (*diagnostics_packets)() = nullptr;

    uint16_t (*direct_unread_total)() = nullptr;
    uint16_t (*channel_unread_total)() = nullptr;
};

// Protocol helpers register into generic slots. The dispatcher treats missing
// weak slots as unavailable, so another upstream core/helper can be added
// without changing the shared UI or boot flow.
const MeshInkProtocolBackend* meshink_protocol_backend_slot_1() __attribute__((weak));
const MeshInkProtocolBackend* meshink_protocol_backend_slot_2() __attribute__((weak));
const MeshInkProtocolBackend* meshink_protocol_backend_slot_3() __attribute__((weak));
const MeshInkProtocolBackend* meshink_protocol_backend_slot_4() __attribute__((weak));

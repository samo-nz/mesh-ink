#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../ui_data.h"
#include "../hardware/gps_types.h"

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

enum class MeshInkProtocolSettingResult : uint8_t {
    Unchanged = 0,
    Saved,
    RestartRequired,
    RestartNow,
    Failed,
};

struct MeshInkProtocolSettingItem {
    uint16_t id;
    const char* title;
    const char* value;
    bool editable;
};

struct MeshInkRadioPreset {
    // Keep this a C++11 aggregate; the ESP32 Arduino toolchain does not treat
    // a default-member-initialized record as aggregate-initializable.
    const char* title;
    const char* detail;
    uint32_t frequency_khz;
    float bandwidth_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    uint8_t path_hash_bytes;
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
    const char* (*radio_summary)() = nullptr;
    bool (*radio_matches)(float frequency_mhz, float bandwidth_khz, uint8_t spreading_factor,
                          uint8_t coding_rate, uint8_t path_hash_bytes) = nullptr;
    void (*cycle_path_hash)() = nullptr;
    uint8_t (*path_hash_mode)() = nullptr;

    // MeshInk owns the physical GPS receiver and RTC. A protocol helper only
    // synchronizes protocol state when the shared receiver mode changes, and
    // exposes protocol-specific position publication policy/scheduling.
    void (*gps_mode_changed)(MeshInkGpsConstellationMode mode) = nullptr;
    // Meshtastic-specific location publication UI; not the physical GPS.
    bool (*location_sharing_get)(bool& enabled,bool& public_approximate,uint8_t& interval) = nullptr;
    bool (*location_sharing_set)(bool enabled,bool public_approximate,uint8_t interval) = nullptr;
    uint32_t (*gps_interval)() = nullptr;
    bool (*gps_advert_location)() = nullptr;
    bool (*my_location)(long& latitude, long& longitude) = nullptr;
    void (*cycle_gps_interval)() = nullptr;
    void (*toggle_gps_advert_location)() = nullptr;

    // Protocol Settings rows. Shared UI owns layout/navigation while each
    // helper owns the meaning, value and mutation of protocol-specific rows.
    size_t (*settings_count)() = nullptr;
    bool (*settings_item)(size_t index, MeshInkProtocolSettingItem& item) = nullptr;
    MeshInkProtocolSettingResult (*settings_activate)(uint16_t id) = nullptr;

    // Radio preset metadata belongs to its protocol helper, not the shared UI.
    size_t (*radio_preset_count)() = nullptr;
    const MeshInkRadioPreset* (*radio_preset_at)(size_t index) = nullptr;

    // First-run radio wizard: optional protocol-owned region/preset enumeration.
    size_t (*setup_region_count)() = nullptr;
    const char* (*setup_region_name)(size_t index) = nullptr;
    size_t (*setup_preset_count)() = nullptr;
    const char* (*setup_preset_name)(size_t index) = nullptr;
    size_t (*setup_preset_count_for_region)(size_t region) = nullptr;
    const char* (*setup_preset_name_for_region)(size_t region,size_t index) = nullptr;
    int (*setup_preset_index_for_region)(size_t region,size_t index) = nullptr;
    bool (*setup_validate_radio)(size_t region,float frequency_mhz,float bandwidth_khz,
                                 uint8_t spreading_factor,uint8_t coding_rate,
                                 uint8_t path_hash_bytes,uint8_t power_dbm) = nullptr;
    bool (*setup_commit_radio)(size_t region, size_t preset, uint8_t hops) = nullptr;
    bool (*setup_save_tx_power)(uint8_t dbm) = nullptr;
    uint8_t (*setup_current_tx_power)() = nullptr;

    // Legacy capability hooks retained for protocol internals/compatibility.
    const char* (*privacy_value)(uint8_t item) = nullptr;
    void (*toggle_privacy)(uint8_t item) = nullptr;
    bool (*request_diagnostics)() = nullptr;
    bool (*diagnostics_busy)() = nullptr;
    const char* (*diagnostics_core)() = nullptr;
    const char* (*diagnostics_radio)() = nullptr;
    const char* (*diagnostics_packets)() = nullptr;
};

// Protocol helpers register into generic slots. Build targets enable only the
// slots they actually compile, so another upstream core/helper can be added
// without changing the shared UI or boot flow.
const MeshInkProtocolBackend* meshink_protocol_backend_slot_1();
const MeshInkProtocolBackend* meshink_protocol_backend_slot_2();
const MeshInkProtocolBackend* meshink_protocol_backend_slot_3();
const MeshInkProtocolBackend* meshink_protocol_backend_slot_4();

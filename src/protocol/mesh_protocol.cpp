#include <Arduino.h>
#include <Preferences.h>

#include "mesh_protocol.h"

#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
#include "../hardware/rtc.h"
#if ENV_INCLUDE_GPS == 1
#include "../hardware/gps.h"
#endif
#endif

namespace {

using BackendSlot = const MeshInkProtocolBackend* (*)();

static BackendSlot backend_slots[] = {
#if defined(MESHINK_PROTOCOL_SLOT_1_ENABLED)
    meshink_protocol_backend_slot_1,
#else
    nullptr,
#endif
#if defined(MESHINK_PROTOCOL_SLOT_2_ENABLED)
    meshink_protocol_backend_slot_2,
#else
    nullptr,
#endif
#if defined(MESHINK_PROTOCOL_SLOT_3_ENABLED)
    meshink_protocol_backend_slot_3,
#else
    nullptr,
#endif
#if defined(MESHINK_PROTOCOL_SLOT_4_ENABLED)
    meshink_protocol_backend_slot_4,
#else
    nullptr,
#endif
};

static const MeshInkProtocolBackend& fallback_backend() {
    static const MeshInkProtocolBackend value = [] {
        MeshInkProtocolBackend b{};
        b.descriptor.id = 0;
        b.descriptor.name = "NO PROTOCOL";
        b.descriptor.core_name = "NO CORE";
        b.descriptor.core_version = "";
        b.descriptor.capabilities = 0;
        return b;
    }();
    return value;
}

static const MeshInkProtocolBackend* slot_backend(size_t index) {
    if (index >= sizeof(backend_slots) / sizeof(backend_slots[0])) return nullptr;
    BackendSlot slot = backend_slots[index];
    return slot ? slot() : nullptr;
}

static const MeshInkProtocolBackend* first_backend() {
    for (size_t i = 0; i < sizeof(backend_slots) / sizeof(backend_slots[0]); ++i) {
        if (const auto* backend = slot_backend(i)) return backend;
    }
    return &fallback_backend();
}

static uint8_t selected_protocol_id() {
    Preferences prefs;
    if (!prefs.begin("mesh-protocol", true)) return 0;
    const uint8_t id = prefs.getUChar("active", 0);
    prefs.end();
    return id;
}

static const MeshInkProtocolBackend* find_backend(uint8_t id) {
    if (!id) return nullptr;
    for (size_t i = 0; i < sizeof(backend_slots) / sizeof(backend_slots[0]); ++i) {
        const auto* backend = slot_backend(i);
        if (backend && backend->descriptor.id == id) return backend;
    }
    return nullptr;
}

static const MeshInkProtocolBackend& active_backend() {
    static const MeshInkProtocolBackend* cached = nullptr;
    if (!cached) {
        cached = find_backend(selected_protocol_id());
        if (!cached) cached = first_backend();
    }
    return *cached;
}

static const char* safe_text(const char* value) {
    return value ? value : "";
}

}  // namespace

const MeshInkProtocolDescriptor& mesh_protocol_descriptor() {
    return active_backend().descriptor;
}

const char* mesh_protocol_name() {
    return safe_text(active_backend().descriptor.name);
}

const char* mesh_protocol_core_name() {
    return safe_text(active_backend().descriptor.core_name);
}

const char* mesh_protocol_core_version() {
    return safe_text(active_backend().descriptor.core_version);
}

bool mesh_protocol_has(uint32_t capability) {
    return (active_backend().descriptor.capabilities & capability) == capability;
}

size_t mesh_protocol_available_count() {
    size_t count = 0;
    for (size_t i = 0; i < sizeof(backend_slots) / sizeof(backend_slots[0]); ++i)
        if (slot_backend(i)) ++count;
    return count;
}

const MeshInkProtocolDescriptor* mesh_protocol_available(size_t index) {
    size_t found = 0;
    for (size_t i = 0; i < sizeof(backend_slots) / sizeof(backend_slots[0]); ++i) {
        const auto* backend = slot_backend(i);
        if (!backend) continue;
        if (found++ == index) return &backend->descriptor;
    }
    return nullptr;
}

bool mesh_protocol_select_for_next_boot(uint8_t protocol_id) {
    if (!find_backend(protocol_id)) return false;
    Preferences prefs;
    if (!prefs.begin("mesh-protocol", false)) return false;
    const bool ok = prefs.putUChar("active", protocol_id) == 1;
    prefs.end();
    return ok;
}

bool mesh_protocol_restart_into(uint8_t protocol_id) {
    if (!mesh_protocol_select_for_next_boot(protocol_id)) return false;
    mesh_protocol_flush_now();
    delay(100);
    ESP.restart();
    return true;
}

void mesh_protocol_setup() {
    if (active_backend().setup) active_backend().setup();
}

bool mesh_protocol_setup_rx_wake() {
    return active_backend().setup_rx_wake ? active_backend().setup_rx_wake() : false;
}

bool mesh_protocol_setup_button_wake() {
    return active_backend().setup_button_wake ? active_backend().setup_button_wake() : false;
}

void mesh_protocol_loop() {
    if (active_backend().loop) active_backend().loop();
}

void mesh_protocol_service_startup() {
    if (active_backend().service_startup) active_backend().service_startup();
}

void mesh_protocol_rx_wake_loop() {
    if (active_backend().rx_wake_loop) active_backend().rx_wake_loop();
}

bool mesh_protocol_rx_wake_promoted() {
    return active_backend().rx_wake_promoted ? active_backend().rx_wake_promoted() : false;
}

void mesh_protocol_prepare_interactive_services() {
    if (active_backend().prepare_interactive_services) active_backend().prepare_interactive_services();
}

bool mesh_protocol_promote_to_ui(const char* source) {
    return active_backend().promote_to_ui ? active_backend().promote_to_ui(source) : false;
}

bool mesh_protocol_enter_deep_sleep_standby() {
    return active_backend().enter_deep_sleep_standby
        ? active_backend().enter_deep_sleep_standby()
        : false;
}

bool mesh_protocol_is_running() {
    return active_backend().is_running ? active_backend().is_running() : false;
}

void mesh_protocol_flush_now() {
    if (active_backend().flush_now) active_backend().flush_now();
}

void mesh_protocol_prepare_shutdown() {
    if (active_backend().prepare_shutdown) active_backend().prepare_shutdown();
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    meshink_gps_shutdown();
#endif
}

void mesh_protocol_request_companion_mode() {
    if (active_backend().request_companion_mode) active_backend().request_companion_mode();
}

bool mesh_protocol_consume_companion_request() {
    return active_backend().consume_companion_request
        ? active_backend().consume_companion_request()
        : false;
}

void mesh_protocol_companion_setup() {
    if (active_backend().companion_setup) active_backend().companion_setup();
}

void mesh_protocol_companion_loop() {
    if (active_backend().companion_loop) active_backend().companion_loop();
}

void mesh_protocol_companion_prepare_exit() {
    if (active_backend().companion_prepare_exit) active_backend().companion_prepare_exit();
}

UiDataProvider* mesh_protocol_provider() {
    return active_backend().provider ? active_backend().provider() : nullptr;
}

void mesh_protocol_refresh_ui_data() {
    if (active_backend().refresh_ui_data) active_backend().refresh_ui_data();
}

bool mesh_protocol_send_active(const char* text) {
    return active_backend().send_active ? active_backend().send_active(text) : false;
}

bool mesh_protocol_send_advert(bool flood) {
    return active_backend().send_advert ? active_backend().send_advert(flood) : false;
}

bool mesh_protocol_apply_radio(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t path_hash_mode) {
    return active_backend().apply_radio
        ? active_backend().apply_radio(freq, bw, sf, cr, path_hash_mode)
        : false;
}

void mesh_protocol_apply_name(const char* name) {
    if (active_backend().apply_name) active_backend().apply_name(name);
}

bool mesh_protocol_name_character_allowed(char c) {
    if (active_backend().name_character_allowed)
        return active_backend().name_character_allowed(c);
    return c >= 32 && c < 127;
}

size_t mesh_protocol_node_name_max_length() {
    return active_backend().node_name_max_length
        ? active_backend().node_name_max_length()
        : 20;
}

const char* mesh_protocol_radio_summary() {
    return active_backend().radio_summary ? safe_text(active_backend().radio_summary()) : "";
}

bool mesh_protocol_radio_matches(float frequency_mhz, float bandwidth_khz, uint8_t spreading_factor,
                                 uint8_t coding_rate, uint8_t path_hash_bytes) {
    return active_backend().radio_matches
        ? active_backend().radio_matches(frequency_mhz, bandwidth_khz, spreading_factor,
                                         coding_rate, path_hash_bytes)
        : false;
}

void mesh_protocol_cycle_path_hash() {
    if (active_backend().cycle_path_hash) active_backend().cycle_path_hash();
}

uint8_t mesh_protocol_path_hash_mode() {
    return active_backend().path_hash_mode ? active_backend().path_hash_mode() : 0;
}

void mesh_protocol_apply_gps(bool enabled) {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    const auto current = meshink_gps_constellation_mode();
    const auto requested = enabled
        ? (current == MeshInkGpsConstellationMode::None
            ? MeshInkGpsConstellationMode::GpsBeiDou
            : current)
        : MeshInkGpsConstellationMode::None;
    (void)mesh_protocol_gps_set_constellation_mode(requested);
#else
    (void)enabled;
#endif
}

bool mesh_protocol_gps_enabled() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    return meshink_gps_constellation_mode() != MeshInkGpsConstellationMode::None;
#else
    return false;
#endif
}

bool mesh_protocol_gps_fix() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    return meshink_gps_read_status().valid;
#else
    return false;
#endif
}

uint32_t mesh_protocol_gps_interval() {
    return active_backend().gps_interval ? active_backend().gps_interval() : 0;
}

bool mesh_protocol_gps_advert_location() {
    return active_backend().gps_advert_location
        ? active_backend().gps_advert_location()
        : false;
}

bool mesh_protocol_my_location(long& latitude, long& longitude) {
    return active_backend().my_location
        ? active_backend().my_location(latitude, longitude)
        : false;
}

void mesh_protocol_cycle_gps_interval() {
    if (active_backend().cycle_gps_interval) active_backend().cycle_gps_interval();
}

MeshInkGpsConstellationMode mesh_protocol_gps_constellation_mode() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    return meshink_gps_constellation_mode();
#else
    return MeshInkGpsConstellationMode::None;
#endif
}

bool mesh_protocol_gps_set_constellation_mode(MeshInkGpsConstellationMode mode) {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    if (!meshink_gps_set_constellation_mode(mode)) return false;
    if (active_backend().gps_mode_changed) active_backend().gps_mode_changed(mode);
    return true;
#else
    (void)mode;
    return false;
#endif
}

bool mesh_protocol_gps_deep_sleep_power_save() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    return meshink_gps_deep_sleep_power_save();
#else
    return false;
#endif
}

bool mesh_protocol_gps_set_deep_sleep_power_save(bool enabled) {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    return meshink_gps_set_deep_sleep_power_save(enabled);
#else
    (void)enabled;
    return false;
#endif
}

const char* mesh_protocol_gps_tuning_note() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED) && ENV_INCLUDE_GPS == 1
    return safe_text(meshink_gps_tuning_note());
#else
    return "";
#endif
}

void mesh_protocol_toggle_gps_advert_location() {
    if (active_backend().toggle_gps_advert_location)
        active_backend().toggle_gps_advert_location();
}

uint32_t mesh_protocol_current_time() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_current_time();
#else
    return 0;
#endif
}

bool mesh_protocol_time_valid() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_valid();
#else
    return false;
#endif
}

bool mesh_protocol_set_manual_time(uint32_t utc) {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_set_manual_time(utc);
#else
    (void)utc;
    return false;
#endif
}

bool mesh_protocol_set_time_mode(MeshInkTimeMode mode) {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_set_time_mode(mode);
#else
    (void)mode;
    return false;
#endif
}

MeshInkTimeMode mesh_protocol_time_mode() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_time_mode();
#else
    return MeshInkTimeMode::Auto;
#endif
}

MeshInkTimeSource mesh_protocol_time_source() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_time_source();
#else
    return MeshInkTimeSource::Unknown;
#endif
}

bool mesh_protocol_gps_time_authoritative() {
#if defined(MESHINK_DEVICE_SERVICES_ENABLED)
    return meshink_rtc_gps_authoritative();
#else
    return false;
#endif
}

const char* mesh_protocol_privacy_value(uint8_t item) {
    return active_backend().privacy_value
        ? safe_text(active_backend().privacy_value(item))
        : "NOT AVAILABLE";
}

void mesh_protocol_toggle_privacy(uint8_t item) {
    if (active_backend().toggle_privacy) active_backend().toggle_privacy(item);
}

bool mesh_protocol_request_diagnostics() {
    return active_backend().request_diagnostics
        ? active_backend().request_diagnostics()
        : false;
}

bool mesh_protocol_diagnostics_busy() {
    return active_backend().diagnostics_busy
        ? active_backend().diagnostics_busy()
        : false;
}

const char* mesh_protocol_diagnostics_core() {
    return active_backend().diagnostics_core
        ? safe_text(active_backend().diagnostics_core())
        : "NOT AVAILABLE";
}

const char* mesh_protocol_diagnostics_radio() {
    return active_backend().diagnostics_radio
        ? safe_text(active_backend().diagnostics_radio())
        : "NOT AVAILABLE";
}

const char* mesh_protocol_diagnostics_packets() {
    return active_backend().diagnostics_packets
        ? safe_text(active_backend().diagnostics_packets())
        : "NOT AVAILABLE";
}

uint16_t mesh_protocol_direct_unread_total() {
    UiDataProvider* provider = mesh_protocol_provider();
    return provider ? provider->direct_unread_total() : 0;
}

uint16_t mesh_protocol_channel_unread_total() {
    UiDataProvider* provider = mesh_protocol_provider();
    return provider ? provider->channel_unread_total() : 0;
}

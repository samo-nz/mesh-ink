#include <Arduino.h>
#include <Preferences.h>

#include "mesh_protocol_backend.h"
#include "../local_mesh_runtime.h"
#include "../companion_runtime.h"
#include "../../include/meshcore_version.h"

namespace {

static void request_companion_mode() {
    if (local_mesh_is_running()) local_mesh_flush_contacts_save_now();

    Preferences mode;
    if (mode.begin("t5-boot", false)) {
        mode.putBool("companion_once", true);
        mode.end();
    }

    delay(150);
    ESP.restart();
}

static bool consume_companion_request() {
    Preferences mode;
    if (!mode.begin("t5-boot", false)) return false;
    const bool requested = mode.getBool("companion_once", false);
    if (requested) mode.remove("companion_once");
    mode.end();
    return requested;
}

static const MeshInkProtocolBackend& backend() {
    static const MeshInkProtocolBackend value = [] {
        MeshInkProtocolBackend b{};
        b.descriptor.id = 1;
        b.descriptor.name = "MESHCORE";
        b.descriptor.core_name = "MeshCore";
        b.descriptor.core_version = MESHCORE_RELEASE " (" MESHCORE_REVISION ")";
        b.descriptor.capabilities =
            MESHINK_PROTOCOL_CAP_DISCOVERY |
            MESHINK_PROTOCOL_CAP_ADVERTISE |
            MESHINK_PROTOCOL_CAP_COMPANION |
            MESHINK_PROTOCOL_CAP_DIAGNOSTICS |
            MESHINK_PROTOCOL_CAP_PRIVACY |
            MESHINK_PROTOCOL_CAP_PATH_HASH;

        b.setup = local_mesh_setup;
        b.setup_rx_wake = local_mesh_setup_rx_wake;
        b.setup_button_wake = local_mesh_setup_button_wake;
        b.loop = local_mesh_loop;
        b.service_startup = local_mesh_service_startup;
        b.rx_wake_loop = local_mesh_rx_wake_loop;
        b.rx_wake_promoted = local_mesh_rx_wake_promoted;
        b.prepare_interactive_services = local_mesh_prepare_interactive_services;
        b.promote_to_ui = local_mesh_promote_to_ui;
        b.enter_deep_sleep_standby = local_mesh_enter_deep_sleep_standby;
        b.is_running = local_mesh_is_running;
        b.flush_now = local_mesh_flush_contacts_save_now;
        b.prepare_shutdown = local_mesh_prepare_shutdown;

        b.request_companion_mode = request_companion_mode;
        b.consume_companion_request = consume_companion_request;
        b.companion_setup = companion_setup;
        b.companion_loop = companion_loop;
        b.companion_prepare_exit = companion_prepare_exit;

        b.provider = local_mesh_provider;
        b.refresh_ui_data = local_mesh_refresh_ui_data;
        b.send_active = local_mesh_send_active;
        b.send_advert = local_mesh_send_advert;

        b.apply_radio = local_mesh_apply_radio;
        b.apply_name = local_mesh_apply_name;
        b.node_name = local_mesh_node_name;
        b.radio_summary = local_mesh_radio_summary;
        b.radio_matches = local_mesh_radio_matches;
        b.cycle_path_hash = local_mesh_cycle_path_hash;
        b.path_hash_mode = local_mesh_path_hash_mode;

        b.apply_gps = local_mesh_apply_gps;
        b.gps_enabled = local_mesh_gps_enabled;
        b.gps_fix = local_mesh_gps_fix;
        b.gps_interval = local_mesh_gps_interval;
        b.gps_advert_location = local_mesh_gps_advert_location;
        b.my_location = local_mesh_my_location;
        b.cycle_gps_interval = local_mesh_cycle_gps_interval;
        b.gps_constellation_mode = local_mesh_gps_constellation_mode;
        b.gps_set_constellation_mode = local_mesh_gps_set_constellation_mode;
        b.gps_deep_sleep_power_save = local_mesh_gps_deep_sleep_power_save;
        b.gps_set_deep_sleep_power_save = local_mesh_gps_set_deep_sleep_power_save;
        b.gps_tuning_note = local_mesh_gps_tuning_note;
        b.toggle_gps_advert_location = local_mesh_toggle_gps_advert_location;

        b.current_time = local_mesh_current_time;
        b.time_valid = local_mesh_time_valid;
        b.set_manual_time = local_mesh_set_manual_time;
        b.set_time_mode = local_mesh_set_time_mode;
        b.time_mode = local_mesh_time_mode;
        b.time_source = local_mesh_time_source;
        b.gps_time_authoritative = local_mesh_gps_time_authoritative;

        b.privacy_value = local_mesh_privacy_value;
        b.toggle_privacy = local_mesh_toggle_privacy;
        b.request_diagnostics = local_mesh_request_diagnostics;
        b.diagnostics_busy = local_mesh_diagnostics_busy;
        b.diagnostics_core = local_mesh_diagnostics_core;
        b.diagnostics_radio = local_mesh_diagnostics_radio;
        b.diagnostics_packets = local_mesh_diagnostics_packets;

        b.direct_unread_total = local_mesh_direct_unread_total;
        b.channel_unread_total = local_mesh_channel_unread_total;
        return b;
    }();
    return value;
}

}  // namespace

const MeshInkProtocolBackend* meshink_protocol_backend_slot_1() {
    return &backend();
}

#include <Arduino.h>
#include <Preferences.h>

#include "mesh_protocol_backend.h"
#include "../local_mesh_runtime.h"
#include "../companion_runtime.h"
#include "../../include/meshcore_version.h"

namespace {

static bool name_character_allowed(char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';
}

static size_t node_name_max_length() {
    return 20;
}

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
        b.name_character_allowed = name_character_allowed;
        b.node_name_max_length = node_name_max_length;
        b.radio_summary = local_mesh_radio_summary;
        b.radio_matches = local_mesh_radio_matches;
        b.cycle_path_hash = local_mesh_cycle_path_hash;
        b.path_hash_mode = local_mesh_path_hash_mode;

        b.gps_mode_changed = local_mesh_sync_gps_mode;
        b.gps_interval = local_mesh_gps_interval;
        b.gps_advert_location = local_mesh_gps_advert_location;
        b.my_location = local_mesh_my_location;
        b.cycle_gps_interval = local_mesh_cycle_gps_interval;
        b.toggle_gps_advert_location = local_mesh_toggle_gps_advert_location;


        b.privacy_value = local_mesh_privacy_value;
        b.toggle_privacy = local_mesh_toggle_privacy;
        b.request_diagnostics = local_mesh_request_diagnostics;
        b.diagnostics_busy = local_mesh_diagnostics_busy;
        b.diagnostics_core = local_mesh_diagnostics_core;
        b.diagnostics_radio = local_mesh_diagnostics_radio;
        b.diagnostics_packets = local_mesh_diagnostics_packets;

        return b;
    }();
    return value;
}

}  // namespace

const MeshInkProtocolBackend* meshink_protocol_backend_slot_1() {
    return &backend();
}

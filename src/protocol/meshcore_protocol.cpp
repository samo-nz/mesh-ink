#include <Arduino.h>
#include <Preferences.h>

#include "mesh_protocol_backend.h"
#include "../local_mesh_runtime.h"
#include "../companion_runtime.h"
#include "../../include/meshcore_version.h"

namespace {


static constexpr MeshInkRadioPreset CORE_RADIO_PRESETS[] = {
    {"KEEP CURRENT","NO RADIO CHANGES",0,0.0f,0,0,0},
    {"AUSTRALIA","915.800 / SF10 / BW250 / CR5",915800,250.0f,10,5,1},
    {"AUSTRALIA NARROW","916.575 / SF7 / BW62.5 / CR8",916575,62.5f,7,8,1},
    {"AUSTRALIA MID","915.075 / SF9 / BW125 / CR5",915075,125.0f,9,5,1},
    {"AUSTRALIA SA WA","923.125 / SF8 / BW62.5 / CR8",923125,62.5f,8,8,1},
    {"AUSTRALIA QLD","923.125 / SF8 / BW62.5 / CR5",923125,62.5f,8,5,1},
    {"BRAZIL","923.125 / SF8 / BW62.5 / CR8",923125,62.5f,8,8,1},
    {"CANADA","910.525 / SF7 / BW62.5 / CR5 / 3B",910525,62.5f,7,5,3},
    {"COSTA RICA","910.525 / SF11 / BW125 / CR5",910525,125.0f,11,5,1},
    {"EU UK NARROW","869.618 / SF8 / BW62.5 / CR8",869618,62.5f,8,8,1},
    {"EU UK DEPRECATED","869.525 / SF11 / BW250 / CR5",869525,250.0f,11,5,1},
    {"CZECH NARROW","869.432 / SF7 / BW62.5 / CR5",869432,62.5f,7,5,1},
    {"EU 433 LONG RANGE","433.650 / SF11 / BW250 / CR5",433650,250.0f,11,5,1},
    {"EU 433 NARROW","433.650 / SF8 / BW62.5 / CR8",433650,62.5f,8,8,1},
    {"HUNGARY","869.618 / SF7 / BW62.5 / CR5 / 2B",869618,62.5f,7,5,2},
    {"NETHERLANDS","869.618 / SF7 / BW62.5 / CR5",869618,62.5f,7,5,1},
    {"NL LIMBURG","869.618 / SF8 / BW62.5 / CR8 / 2B",869618,62.5f,8,8,2},
    {"NZ NARROW","917.375 / SF7 / BW62.5 / CR5 / 2B",917375,62.5f,7,5,2},
    {"NZ GISBORNE","917.375 / SF11 / BW250 / CR5 / 1B",917375,250.0f,11,5,1},
    {"PORTUGAL 433","433.375 / SF9 / BW62.5 / CR6",433375,62.5f,9,6,1},
    {"PORTUGAL 868","869.618 / SF7 / BW62.5 / CR6",869618,62.5f,7,6,1},
    {"SLOVAKIA","869.618 / SF7 / BW62.5 / CR5 / 2B",869618,62.5f,7,5,2},
    {"SWITZERLAND","869.618 / SF8 / BW62.5 / CR8",869618,62.5f,8,8,1},
    {"USA","910.525 / SF7 / BW62.5 / CR5",910525,62.5f,7,5,1},
    {"USA PHILLYMESH","902.250 / SF11 / BW500 / CR5 / 2B",902250,500.0f,11,5,2},
    {"USA SOCAL","927.875 / SF7 / BW62.5 / CR5 / 3B",927875,62.5f,7,5,3},
    {"VIETNAM NARROW","920.250 / SF8 / BW62.5 / CR5",920250,62.5f,8,5,1},
    {"VIETNAM DEPRECATED","920.250 / SF11 / BW250 / CR5",920250,250.0f,11,5,1},
};

static constexpr const char* CORE_SETUP_REGIONS[]={
    "NEW ZEALAND","AUSTRALIA","EUROPE / UK","NORTH AMERICA",
    "BRAZIL / ASIA","CUSTOM / OTHER"
};
static size_t core_radio_count(){return sizeof(CORE_RADIO_PRESETS)/sizeof(CORE_RADIO_PRESETS[0]);}
static const MeshInkRadioPreset* core_radio_at(size_t index){
    return index<core_radio_count()?&CORE_RADIO_PRESETS[index]:nullptr;
}
static size_t core_region_count(){return sizeof(CORE_SETUP_REGIONS)/sizeof(CORE_SETUP_REGIONS[0]);}
static const char* core_region_name(size_t index){
    return index<core_region_count()?CORE_SETUP_REGIONS[index]:"";
}
static bool core_preset_matches_region(size_t index,size_t region){
    if(index==0||index>=core_radio_count())return false;
    switch(region){
        case 0:return index==17||index==18;
        case 1:return index>=1&&index<=5;
        case 2:return (index>=9&&index<=16)||(index>=19&&index<=22);
        case 3:return index==7||index==8||(index>=23&&index<=25);
        case 4:return index==6||index==26||index==27;
        default:return false;
    }
}
static size_t core_setup_preset_count(size_t region){
    size_t count=1; // CUSTOM, initialized with no inherited radio settings.
    for(size_t i=1;i<core_radio_count();++i)
        if(core_preset_matches_region(i,region))++count;
    return count;
}
static int core_setup_preset_at(size_t region,size_t visible){
    if(visible==0)return -1;
    size_t n=1;
    for(size_t i=1;i<core_radio_count();++i){
        if(!core_preset_matches_region(i,region))continue;
        if(n++==visible)return (int)i;
    }
    return -2;
}
static const char* core_setup_preset_label(size_t region,size_t visible){
    const int index=core_setup_preset_at(region,visible);
    return index==-1?"CUSTOM / MANUAL":
           index>=0?CORE_RADIO_PRESETS[index].title:"";
}

static bool name_character_allowed(char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';
}

static size_t node_name_max_length() {
    return 20;
}

enum : uint16_t {
    MESHCORE_SETTING_PATH_HASH = 1,
    MESHCORE_SETTING_GPS_INTERVAL = 2,
    MESHCORE_SETTING_AUTO_ADD_CONTACTS = 10,
    MESHCORE_SETTING_AUTO_ADD_HOPS = 11,
    MESHCORE_SETTING_ADVERT_LOCATION = 12,
    MESHCORE_SETTING_BASE_TELEMETRY = 13,
    MESHCORE_SETTING_LOCATION_TELEMETRY = 14,
    MESHCORE_SETTING_PACKET_REPEATING = 15,
};

static size_t protocol_settings_count() {
    return 8;
}

static bool protocol_settings_item(size_t index, MeshInkProtocolSettingItem& item) {
    item = MeshInkProtocolSettingItem{};
    switch (index) {
        case 0:
            item = {MESHCORE_SETTING_PATH_HASH, "PATH HASH MODE",
                    local_mesh_path_hash_mode()==0?"1 BYTE":
                    local_mesh_path_hash_mode()==1?"2 BYTES":"3 BYTES", true};
            return true;
        case 1: {
            static char interval[24];
            const uint32_t seconds=local_mesh_gps_interval();
            if(!seconds) strcpy(interval,"CONTINUOUS");
            else if(seconds<60) snprintf(interval,sizeof(interval),"%lu SECONDS",(unsigned long)seconds);
            else snprintf(interval,sizeof(interval),"%lu MINUTES",(unsigned long)(seconds/60));
            item = {MESHCORE_SETTING_GPS_INTERVAL, "GPS PUBLISH INTERVAL", interval, true};
            return true;
        }
        case 2:
            item = {MESHCORE_SETTING_AUTO_ADD_CONTACTS, "AUTO ADD CONTACTS",
                    local_mesh_privacy_value(0), true};
            return true;
        case 3:
            item = {MESHCORE_SETTING_AUTO_ADD_HOPS, "AUTO ADD MAX HOPS",
                    local_mesh_privacy_value(1), true};
            return true;
        case 4:
            item = {MESHCORE_SETTING_ADVERT_LOCATION, "ADVERTISE LOCATION",
                    local_mesh_privacy_value(2), true};
            return true;
        case 5:
            item = {MESHCORE_SETTING_BASE_TELEMETRY, "BASE TELEMETRY",
                    local_mesh_privacy_value(3), true};
            return true;
        case 6:
            item = {MESHCORE_SETTING_LOCATION_TELEMETRY, "LOCATION TELEMETRY",
                    local_mesh_privacy_value(4), true};
            return true;
        case 7:
            item = {MESHCORE_SETTING_PACKET_REPEATING, "PACKET REPEATING",
                    local_mesh_privacy_value(5), true};
            return true;
        default:
            return false;
    }
}

static MeshInkProtocolSettingResult activate_protocol_setting(uint16_t id) {
    switch (id) {
        case MESHCORE_SETTING_PATH_HASH:
            local_mesh_cycle_path_hash();
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_GPS_INTERVAL:
            local_mesh_cycle_gps_interval();
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_AUTO_ADD_CONTACTS:
            local_mesh_toggle_privacy(0);
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_AUTO_ADD_HOPS:
            local_mesh_toggle_privacy(1);
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_ADVERT_LOCATION:
            local_mesh_toggle_privacy(2);
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_BASE_TELEMETRY:
            local_mesh_toggle_privacy(3);
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_LOCATION_TELEMETRY:
            local_mesh_toggle_privacy(4);
            return MeshInkProtocolSettingResult::Saved;
        case MESHCORE_SETTING_PACKET_REPEATING:
            local_mesh_toggle_privacy(5);
            return MeshInkProtocolSettingResult::Saved;
        default:
            return MeshInkProtocolSettingResult::Failed;
    }
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

        b.radio_preset_count=core_radio_count;
        b.radio_preset_at=core_radio_at;
        b.setup_region_count=core_region_count;
        b.setup_region_name=core_region_name;
        b.setup_preset_count_for_region=core_setup_preset_count;
        b.setup_preset_name_for_region=core_setup_preset_label;
        b.setup_preset_index_for_region=core_setup_preset_at;

        b.apply_radio = local_mesh_apply_radio;
        b.setup_save_tx_power = local_mesh_save_tx_power;
        b.setup_current_tx_power = local_mesh_tx_power;
        b.apply_name = local_mesh_apply_name;
        b.name_character_allowed = name_character_allowed;
        b.node_name_max_length = node_name_max_length;
        b.radio_summary = local_mesh_radio_summary;
        b.radio_matches = local_mesh_radio_matches;
        b.cycle_path_hash = local_mesh_cycle_path_hash;
        b.path_hash_mode = local_mesh_path_hash_mode;

        b.settings_count = protocol_settings_count;
        b.settings_item = protocol_settings_item;
        b.settings_activate = activate_protocol_setting;

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

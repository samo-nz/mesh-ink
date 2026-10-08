#include <Arduino.h>
#include <esp_random.h>
#include <Preferences.h>
#include "hardware/display.h"
#include "hardware/storage.h"
#include "hardware/touch.h"
#include "hardware/power.h"
#include "hardware/performance.h"
#include "hardware/buttons.h"
#include "hardware/board.h"
#include <esp_heap_caps.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <SPIFFS.h>
#include "ui_onboarding.h"
#include "channel_key.h"
#include "ui_data.h"
#include "protocol/mesh_protocol.h"
#include "map_tiles.h"
#include "map_gestures.h"
#include "ui_layout.h"
#include "t5_logging.h"
#include "keyboard_geometry.h"
#include "message_limits.h"
#include "message_store.h"
#include "backup_restore.h"
#include "fonts/inter_15_regular.h"
#include "fonts/inter_20_regular.h"
#include "fonts/inter_25_regular.h"
#include "fonts/inter_30_regular.h"
#include "fonts/inter_50_digits.h"
#include "meshink_logo_bitmap.h"  // generated from original PNG at build time

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.3.0"
#endif

// UI milestone 0.1.0: standalone onboarding. Bluetooth, radio and GPS are not
// started in this target. Saved values are device-owned and will be handed to
// the active protocol helper when the full firmware runtime is attached.
static constexpr char UI_VERSION[] = T5_FIRMWARE_VERSION;

struct Glyph { char c; uint8_t r[7]; };
static constexpr Glyph FONT[] = {
 {' ',{0,0,0,0,0,0,0}},{'-',{0,0,0,31,0,0,0}},{'.',{0,0,0,0,0,6,6}},
 {'<',{1,2,4,8,4,2,1}},{'>',{16,8,4,2,4,8,16}},{'_',{0,0,0,0,0,0,31}},
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},
 {'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
 {'9',{14,17,17,15,1,1,14}},
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
 {'I',{31,4,4,4,4,4,31}},{'J',{7,2,2,2,2,18,12}},
 {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}}
 ,{'a',{0,0,14,1,15,17,15}},{'b',{16,16,30,17,17,17,30}}
 ,{'c',{0,0,14,17,16,17,14}},{'d',{1,1,15,17,17,17,15}}
 ,{'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}}
 ,{'g',{0,0,15,17,15,1,14}},{'h',{16,16,30,17,17,17,17}}
 ,{'i',{4,0,12,4,4,4,14}},{'j',{2,0,6,2,2,18,12}}
 ,{'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}}
 ,{'m',{0,0,26,21,21,21,21}},{'n',{0,0,30,17,17,17,17}}
 ,{'o',{0,0,14,17,17,17,14}},{'p',{0,0,30,17,30,16,16}}
 ,{'q',{0,0,15,17,15,1,1}},{'r',{0,0,22,25,16,16,16}}
 ,{'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}}
 ,{'u',{0,0,17,17,17,19,13}},{'v',{0,0,17,17,17,10,4}}
 ,{'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}}
 ,{'y',{0,0,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}}
 ,{'!',{4,4,4,4,4,0,4}},{'?',{14,17,1,2,4,0,4}}
 ,{'@',{14,17,23,21,23,16,14}},{'#',{10,31,10,10,31,10,0}}
 ,{'$',{4,15,20,14,5,30,4}},{'%',{24,25,2,4,8,19,3}}
 ,{'&',{12,18,20,8,21,18,13}},{'*',{0,21,14,31,14,21,0}}
 ,{'(',{2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}}
 ,{'+',{0,4,4,31,4,4,0}},{'=',{0,0,31,0,31,0,0}}
 ,{'/',{1,2,4,8,16,0,0}},{'\\',{16,8,4,2,1,0,0}}
 ,{':',{0,6,6,0,6,6,0}},{';',{0,6,6,0,6,4,8}}
 ,{',',{0,0,0,0,6,4,8}},{'\'',{4,4,8,0,0,0,0}}
 ,{'"',{10,10,0,0,0,0,0}},{'[',{14,8,8,8,8,8,14}}
 ,{']',{14,2,2,2,2,2,14}},{'{',{2,4,4,8,4,4,2}}
 ,{'}',{8,4,4,2,4,4,8}}
};

static MeshInkDisplayState display;
static uint8_t* fb = nullptr;

static bool screenshot_write_u16(MeshInkStorageFile& file,uint16_t value){
    const uint8_t bytes[2]={(uint8_t)(value&0xFFU),(uint8_t)(value>>8)};
    return file.write(bytes,sizeof(bytes))==sizeof(bytes);
}
static bool screenshot_write_u32(MeshInkStorageFile& file,uint32_t value){
    const uint8_t bytes[4]={
        (uint8_t)(value&0xFFU),(uint8_t)((value>>8)&0xFFU),
        (uint8_t)((value>>16)&0xFFU),(uint8_t)((value>>24)&0xFFU)
    };
    return file.write(bytes,sizeof(bytes))==sizeof(bytes);
}
static uint8_t screenshot_gray8_at(int logical_x,int logical_y){
    return meshink_display_read_logical_gray8(fb,logical_x,logical_y);
}
bool ui_save_screenshot(char* path_out,size_t path_len){
    if(path_out&&path_len)path_out[0]=0;
    if(!fb||!map_tiles_media_ready()){
        Serial.println("[T5-SHOT] SD/framebuffer unavailable");
        return false;
    }

    char path[20]{};
    bool found=false;
    for(unsigned i=1;i<=9999;++i){
        snprintf(path,sizeof(path),"/SHOT%04u.BMP",i);
        if(!meshink_storage_exists(path)){found=true;break;}
    }
    if(!found){
        Serial.println("[T5-SHOT] no free screenshot filename");
        return false;
    }

    MeshInkStorageFile file=meshink_storage_open_write(path);
    if(!file){
        Serial.printf("[T5-SHOT] open failed: %s\n",path);
        return false;
    }

    const int width=meshink_display_logical_width();
    const int height=meshink_display_logical_height();
    const uint32_t row_stride=(uint32_t)((width+3)&~3);
    const uint32_t palette_bytes=256U*4U;
    const uint32_t pixel_offset=14U+40U+palette_bytes;
    const uint32_t image_bytes=row_stride*(uint32_t)height;
    const uint32_t file_bytes=pixel_offset+image_bytes;
    bool ok=true;

    const uint8_t signature[2]={'B','M'};
    ok=ok&&file.write(signature,sizeof(signature))==sizeof(signature);
    ok=ok&&screenshot_write_u32(file,file_bytes);
    ok=ok&&screenshot_write_u16(file,0)&&screenshot_write_u16(file,0);
    ok=ok&&screenshot_write_u32(file,pixel_offset);

    ok=ok&&screenshot_write_u32(file,40);
    ok=ok&&screenshot_write_u32(file,(uint32_t)width);
    ok=ok&&screenshot_write_u32(file,(uint32_t)(-(int32_t)height)); // top-down BMP
    ok=ok&&screenshot_write_u16(file,1);
    ok=ok&&screenshot_write_u16(file,8);
    ok=ok&&screenshot_write_u32(file,0);
    ok=ok&&screenshot_write_u32(file,image_bytes);
    ok=ok&&screenshot_write_u32(file,2835)&&screenshot_write_u32(file,2835);
    ok=ok&&screenshot_write_u32(file,256)&&screenshot_write_u32(file,0);

    for(unsigned i=0;ok&&i<256;++i){
        const uint8_t entry[4]={(uint8_t)i,(uint8_t)i,(uint8_t)i,0};
        ok=file.write(entry,sizeof(entry))==sizeof(entry);
    }

    static uint8_t row[960];
    if(row_stride>sizeof(row))ok=false;
    for(int y=0;ok&&y<height;++y){
        for(int x=0;x<width;++x)row[x]=screenshot_gray8_at(x,y);
        for(uint32_t x=(uint32_t)width;x<row_stride;++x)row[x]=0xFF;
        ok=file.write(row,row_stride)==row_stride;
    }
    file.flush();
    file.close();

    if(!ok){
        Serial.printf("[T5-SHOT] write failed: %s\n",path);
        return false;
    }
    if(path_out&&path_len){
        strncpy(path_out,path,path_len-1);
        path_out[path_len-1]=0;
    }
    Serial.printf("[T5-SHOT] saved %s %dx%d\n",path,width,height);
    return true;
}
static Preferences prefs;
static char node_name[21] = "MeshInk-";
static uint8_t selected_preset = 17;
static bool saved = false;
static bool replace_name_on_type = false;
static bool keyboard_visible = true;
static bool keyboard_upper = true;
static bool keyboard_symbols = false;
static bool message_keyboard_case_dirty = false;
static bool keyboard_landscape = false;
static bool standby_restore_landscape = false;
static uint16_t status_unread = 0;
static uint16_t status_channel_unread = 0;
static bool status_gps_enabled = false;
static bool status_gps_fix = false;
static MeshInkGpsError status_gps_error = MeshInkGpsError::None;
static int16_t status_gps_satellites = 0;
static int16_t status_gps_satellites_bar = 0;
static long status_gps_latitude = 0;
static long status_gps_longitude = 0;
static uint32_t status_gps_timestamp = 0;
// Retain the last genuine fix when GPS is searching/off, including across
// reboots. Never mistake placeholder (0,0) or "no fix" for a new location.
static bool map_has_last_gps_position=false;
static long map_last_gps_latitude=0,map_last_gps_longitude=0;
static bool map_last_gps_saved=false;
static long map_saved_gps_latitude=0,map_saved_gps_longitude=0;
static uint32_t map_last_gps_save_ms=0;
static int16_t status_battery = -1;
static MeshInkChargeState status_charge_state=MeshInkChargeState::Unknown;
static int8_t status_hour = -1;
static int8_t status_minute = -1;
// Content changes still use the existing screen redraw path. Status-only
// changes are kept separate so the 48 px bar can use a small DU update.
static bool status_dirty = false;
static bool status_bar_dirty = false;
static int16_t status_bar_painted_minute = -1;
static int16_t status_bar_painted_slot = -1;
static bool toast_visible = false;
static uint32_t toast_until = 0;
static char toast_message[32] = {};
static bool setup_complete = false;
static bool toast_opens_main = false;
static bool keyboard_message_mode = false;
static bool keyboard_password_mode = false;
static bool save_remote_password = false;
static char compose_text[MESHINK_MESSAGE_TEXT_BYTES] = {};
static char remote_password[16] = {};
static UiDataProvider* ui_data = nullptr;
static size_t selected_contact = 0;
static size_t selected_channel = 0;
static constexpr size_t LIST_ITEMS_PER_PAGE = 5;
static size_t contacts_page = 0;
static size_t channels_page = 0;
static size_t channel_manage_page = 0;
static char channel_form_name[32]{};
static char channel_form_key_hex[33]{};
static bool channel_form_key_field=false;
static char channel_delete_title[42]{};
static size_t channel_delete_index=0;
static size_t discovery_page = 0;
static size_t protocol_settings_page = 0;
static uint8_t chat_page = 0;
static constexpr size_t CHAT_PAGE_ANCHORS=250;
static uint16_t chat_page_starts[CHAT_PAGE_ANCHORS]{};
static size_t chat_page_snapshot_count=(size_t)-1;
static int chat_page_snapshot_current_available=-1;
static int chat_page_snapshot_history_available=-1;
static uint16_t chat_page_known=0;
static void reset_chat_paging(){
    chat_page=0;
    chat_page_snapshot_count=(size_t)-1;
    chat_page_snapshot_current_available=-1;
    chat_page_snapshot_history_available=-1;
    chat_page_known=0;
}
static uint8_t timezone_index = 1;
static int16_t custom_timezone_minutes=0;
static char auto_timezone_label[28]="UTC";
static char auto_timezone_rule[80]="UTC0";
static uint16_t manual_time_year=2026;
static uint8_t manual_time_month=1;
static uint8_t manual_time_day=1;
static uint8_t manual_time_hour=12;
static uint8_t manual_time_minute=0;
static bool mesh_is_ready = false;
enum class FrontlightMode:uint8_t{On=0,NightTimer=1,Off=2};
static FrontlightMode frontlight_mode=FrontlightMode::On;
static uint8_t frontlight_timeout_index=2;
static uint8_t frontlight_brightness=30;
static uint16_t night_start_minutes=20*60;
static uint16_t night_end_minutes=7*60;
static bool frontlight_lit=false;
static uint32_t frontlight_deadline=0;
static uint8_t night_edit_field=0;
static QueueHandle_t touch_queue=nullptr;
static TaskHandle_t touch_task_handle=nullptr;
static bool text_refresh_pending=false;
static uint32_t text_refresh_after=0;
static uint32_t text_refresh_queued_at=0;
static bool touch_enabled=true;
static bool standby_active=false;
static bool hardware_failure=false;
static uint8_t standby_timeout_index=1;
static bool deep_sleep_standby=false;
static bool deep_sleep_pending=false;
static uint32_t deep_sleep_retry_at=0;
static uint32_t last_user_activity=0;
static bool status_wake_light=false;
static bool message_alert_active=false;
static bool headless_ui_state=false;
static bool headless_alert_requested=false;
static bool headless_display_session=false;
static bool display_session_active=false;
static bool quick_panel_active=false;
static bool quick_panel_restore_landscape=false;
static volatile bool quick_slider_dragging=false;
static volatile bool display_slider_dragging=false;
static volatile uint8_t quick_slider_preview=30;
static uint8_t message_alert_phase=0;
static uint32_t message_alert_deadline=0;
static uint32_t message_alert_cooldown_until=0;
enum class Screen : uint8_t {
    Welcome, SetupName, SetupRegion, SetupPreset, SetupRadio, SetupReview, SetupCancel,
    Presets, CompanionConfirm, ShutdownConfirm,
    Contacts, ContactChat, ContactDetails,
    Channels, ChannelChat, ChannelManage, ChannelCreate, ChannelDelete,
    Maps, Discovery, More, AdvertMenu, Diagnostics,
    Settings, ProtocolSelect, ProtocolSettings, BackupOptions, BackupFiles, BackupConfirm, BackupResult,
    GpsSettings, GpsTuning, DateTime, ManualTime, Timezone, CustomTimezone, DisplaySettings, NightSchedule, Help, About
};
static Screen screen = Screen::Welcome;
static Screen setup_cancel_from=Screen::SetupName;
static Screen backup_return_screen=Screen::ProtocolSettings;
static bool backup_restore_mode=false,backup_from_setup=false,backup_restore_pending=false;
static uint8_t backup_flags=7,backup_available=7;
static MeshInkBackupInfo backup_entries[24]{};
static size_t backup_count=0,backup_page=0;
static char backup_filename[32]{},backup_result_message[84]{};
// Separate protocol completion flags; a legacy completed installation is preserved.
static bool setup_meshcore_done=false, setup_meshtastic_done=false, setup_any_done=false;
static uint8_t setup_return_protocol=0;
static uint8_t setup_protocol_choice=0;
static uint8_t setup_region=0,setup_region_page=0,setup_preset_page=0;
static int setup_radio_preset=-1;
static uint8_t setup_hops=3,setup_sf=0,setup_cr=0,setup_hash=0,setup_edit_field=0;
static float setup_bw=0;
static char setup_freq[18]{},setup_power[8]{};
static bool setup_is_screen(Screen value){
    return value==Screen::Welcome||value==Screen::SetupName||value==Screen::SetupRegion||
           value==Screen::SetupPreset||value==Screen::SetupRadio||
           value==Screen::SetupReview||value==Screen::SetupCancel;
}
static bool setup_protocol_done(uint8_t id){
    return id==2?setup_meshtastic_done:(id==1&&setup_meshcore_done);
}
static bool setup_is_meshcore(){return mesh_protocol_descriptor().id==1;}

static uint8_t retained_wake_tab=0;
static bool retained_wake_tab_valid=false;
static Screen preset_return_screen = Screen::Welcome;
static uint8_t preset_page = 3;
static bool details_from_discovery=false;
static uint8_t details_page=0;

// Deep-sleep wake restores only the four real bottom-navigation tabs. Nested
// pages intentionally collapse to their parent tab so wake always returns to
// a stable place rather than recreating transient editor/detail state.
static uint8_t retained_tab_for_screen(Screen value){
    switch(value){
        case Screen::Contacts:
        case Screen::ContactChat:
            return 1;
        case Screen::ContactDetails:
            return details_from_discovery?4:1;
        case Screen::Channels:
        case Screen::ChannelChat:
        case Screen::ChannelManage:
        case Screen::ChannelCreate:
        case Screen::ChannelDelete:
            return 2;
        case Screen::Maps:
            return 3;
        case Screen::Welcome:
        case Screen::SetupName:
        case Screen::SetupRegion:
        case Screen::SetupPreset:
        case Screen::SetupRadio:
        case Screen::SetupReview:
        case Screen::SetupCancel:
        case Screen::Presets:
            return 1;
        default:
            return 4;
    }
}
static Screen screen_for_retained_tab(uint8_t tab){
    switch(tab){
        case 2:return Screen::Channels;
        case 3:return Screen::Maps;
        case 4:return Screen::More;
        default:return Screen::Contacts;
    }
}
enum class NodeInfoPage:uint8_t{Overview=0,Status,Telemetry,Path};
static bool node_has_capability(uint32_t capabilities,UiNodeCapability capability){
    return ui_node_has_capability(capabilities,capability);
}
static uint8_t node_info_page_count(uint32_t capabilities){
    uint8_t count=1;
    if(node_has_capability(capabilities,UI_NODE_CAP_STATUS))++count;
    if(node_has_capability(capabilities,UI_NODE_CAP_TELEMETRY))++count;
    if(node_has_capability(capabilities,UI_NODE_CAP_PATH)||
       node_has_capability(capabilities,UI_NODE_CAP_TRACE))++count;
    return count;
}
static NodeInfoPage node_info_page(uint32_t capabilities,uint8_t page){
    if(page==0)return NodeInfoPage::Overview;
    uint8_t next=1;
    if(node_has_capability(capabilities,UI_NODE_CAP_STATUS)&&page==next++)return NodeInfoPage::Status;
    if(node_has_capability(capabilities,UI_NODE_CAP_TELEMETRY)&&page==next++)return NodeInfoPage::Telemetry;
    if((node_has_capability(capabilities,UI_NODE_CAP_PATH)||
        node_has_capability(capabilities,UI_NODE_CAP_TRACE))&&page==next)return NodeInfoPage::Path;
    return NodeInfoPage::Overview;
}
// Screen-edge geometry is derived from the logical portrait surface. On the
// T5 this remains exactly 540x960 with a 48 px status bar and 60 px bottom nav.
static inline const MeshInkUiLayout& portrait_layout(){
    static const MeshInkUiLayout layout=
        meshink_make_ui_layout(meshink_display_portrait_width(),
                               meshink_display_portrait_height());
    return layout;
}
static inline void set_ui_orientation(MeshInkOrientation orientation){
    meshink_display_set_orientation(orientation);
    meshink_touch_set_orientation(orientation);
}
static inline int map_top(){return portrait_layout().map_top;}
static inline int map_bottom(){return portrait_layout().map_bottom;}
static inline int map_centre_x(){return portrait_layout().map_centre_x;}
static inline int map_centre_y(){return portrait_layout().map_centre_y;}
static inline int ui_x(int reference_x){return meshink_ui_ref_x(portrait_layout(),reference_x);}
static inline int ui_y(int reference_y){return meshink_ui_ref_y(portrait_layout(),reference_y);}
static inline int ui_w(int reference_w){return meshink_ui_ref_w(portrait_layout(),reference_w);}
static inline int ui_h(int reference_h){return meshink_ui_ref_h(portrait_layout(),reference_h);}
static inline MeshInkUiRect ui_rect(int x,int y,int w,int h){
    return meshink_ui_ref_rect(portrait_layout(),x,y,w,h);
}
static double map_latitude=-41.2865,map_longitude=174.7762;
static uint8_t map_zoom=12;
static bool map_imperial=false;
// The background contains only decoded tiles and the map header; markers and
// controls are applied after copying it, never baked into the cache.
static uint8_t* map_base_cache=nullptr;
static size_t map_base_bytes=0;
static bool map_base_valid=false;
static double map_base_lat=0,map_base_lon=0;
static uint8_t map_base_zoom=0;
static MapRenderResult map_base_result{};
static uint32_t map_base_media_epoch=0;
// The own-position bullseye is drawn over the map, never stored in the
// raster base cache. The last drawn screen position supports low-rate GPS
// marker updates without refreshing the e-paper for every GPS sample.
static bool map_device_marker_visible=false;
static int map_device_marker_x=0,map_device_marker_y=0;
struct MapMarkerHit {int16_t x,y;size_t index;UiNodeRole role;};
static MapMarkerHit map_marker_hits[50]{};
static size_t map_marker_hit_count=0;
static bool map_cache_hit() {
    return map_base_valid&&map_base_cache&&map_base_zoom==map_zoom&&
        fabs(map_base_lat-map_latitude)<0.00000001&&fabs(map_base_lon-map_longitude)<0.00000001;
}

static const char* map_source_badge(const MapRenderResult& result) {
    if(!result.sd_ready||!result.tiles)return "---";
    // Prefer the requested/native zoom source. Parent fallbacks often represent
    // intentionally omitted blank/ocean tiles and should not contaminate the
    // badge for an otherwise-native viewport.
    if(result.native_pmtiles&&result.native_loose)return "MIX";
    if(result.native_pmtiles)return "PMT";
    if(result.native_loose)return "PNG";
    // No native tiles at this zoom: report which lower-resolution source is
    // being enlarged to fill the viewport.
    if(result.parent_pmtiles&&result.parent_loose)return "MIX";
    if(result.parent_pmtiles)return "E-M";
    if(result.parent_loose)return "E-P";
    return "---";
}
// GPS-capable boards prefer the live/retained receiver fix. Boards without
// GPS use the active protocol helper's configured static "My Location" coordinates.
static bool map_device_position(long& latitude,long& longitude,bool& current_fix){
    if(meshink_board_has_gps()){
        current_fix=status_gps_enabled&&status_gps_fix;
        if(current_fix &&
           status_gps_latitude>=-85051100L&&status_gps_latitude<=85051100L &&
           status_gps_longitude>=-180000000L&&status_gps_longitude<=180000000L){
            latitude=status_gps_latitude;longitude=status_gps_longitude;return true;
        }
        current_fix=false;
        if(!map_has_last_gps_position)return false;
        latitude=map_last_gps_latitude;longitude=map_last_gps_longitude;
        return true;
    }
    current_fix=false;
    return mesh_protocol_my_location(latitude,longitude);
}
static bool centre_map_on_device(){
    long latitude=0,longitude=0;bool current_fix=false;
    if(!map_device_position(latitude,longitude,current_fix))return false;
    map_latitude=latitude/1000000.0;map_longitude=longitude/1000000.0;
    return true;
}

static constexpr uint8_t PRESETS_PER_PAGE = 5;

struct TimezoneChoice { const char* label; const char* detail; const char* rule; };
static constexpr uint8_t TIMEZONE_AUTO=0;
static constexpr uint8_t TIMEZONE_CUSTOM=8;
static constexpr TimezoneChoice TIMEZONES[] = {
 {"AUTO (GPS-DERIVED)","USES LAST RESOLVED ZONE UNTIL NEXT FIX",nullptr},
 {"NEW ZEALAND","NZST / NZDT AUTOMATIC","NZST-12NZDT,M9.5.0,M4.1.0/3"},
 {"UTC","COORDINATED UNIVERSAL TIME","UTC0"},
 {"AUSTRALIA EAST","AEST / AEDT AUTOMATIC","AEST-10AEDT,M10.1.0,M4.1.0/3"},
 {"UNITED KINGDOM","GMT / BST AUTOMATIC","GMT0BST,M3.5.0/1,M10.5.0"},
 {"CENTRAL EUROPE","CET / CEST AUTOMATIC","CET-1CEST,M3.5.0,M10.5.0/3"},
 {"US PACIFIC","PST / PDT AUTOMATIC","PST8PDT,M3.2.0,M11.1.0"},
 {"US EASTERN","EST / EDT AUTOMATIC","EST5EDT,M3.2.0,M11.1.0"},
 {"CUSTOM UTC OFFSET","FIXED OFFSET - NO DST",nullptr},
};
static constexpr uint8_t TIMEZONE_COUNT=sizeof(TIMEZONES)/sizeof(TIMEZONES[0]);

static void fixed_timezone_text(int16_t east_minutes,char* label,size_t label_len,char* rule,size_t rule_len){
    const int total=constrain((int)east_minutes,-12*60,14*60);
    const char sign=total<0?'-':'+';
    const int magnitude=abs(total);
    snprintf(label,label_len,"UTC%c%02d:%02d",sign,magnitude/60,magnitude%60);
    if(total==0)snprintf(rule,rule_len,"UTC0");
    else snprintf(rule,rule_len,"UTC%c%d:%02d",total>0?'-':'+',magnitude/60,magnitude%60);
}
static bool timezone_box(double lat,double lon,double min_lat,double max_lat,double min_lon,double max_lon){
    return lat>=min_lat&&lat<=max_lat&&lon>=min_lon&&lon<=max_lon;
}
static void resolve_gps_timezone(long latitude,long longitude,char* label,size_t label_len,char* rule,size_t rule_len){
    const double lat=latitude/1000000.0,lon=longitude/1000000.0;
    auto set=[&](const char* name,const char* tz){snprintf(label,label_len,"%s",name);snprintf(rule,rule_len,"%s",tz);};
    if(timezone_box(lat,lon,-45.5,-43.0,-178.5,-175.0)){set("CHATHAM ISLANDS","CHAST-12:45CHADT,M9.5.0/2:45,M4.1.0/3:45");return;}
    if(timezone_box(lat,lon,-48.5,-33.0,165.0,180.0)){set("NEW ZEALAND","NZST-12NZDT,M9.5.0,M4.1.0/3");return;}
    if(timezone_box(lat,lon,-29.5,-10.0,138.0,154.5)){set("QUEENSLAND","AEST-10");return;}
    if(timezone_box(lat,lon,-39.5,-25.0,129.0,141.0)){set("AUSTRALIA CENTRAL","ACST-9:30ACDT,M10.1.0,M4.1.0/3");return;}
    if(timezone_box(lat,lon,-26.0,-10.0,129.0,138.0)){set("NORTHERN TERRITORY","ACST-9:30");return;}
    if(timezone_box(lat,lon,-36.0,-13.0,112.0,129.0)){set("AUSTRALIA WEST","AWST-8");return;}
    if(timezone_box(lat,lon,-44.5,-28.0,140.0,154.5)){set("AUSTRALIA EAST","AEST-10AEDT,M10.1.0,M4.1.0/3");return;}
    if(timezone_box(lat,lon,49.0,61.5,-11.0,3.0)){set("UNITED KINGDOM","GMT0BST,M3.5.0/1,M10.5.0");return;}
    if(timezone_box(lat,lon,34.0,72.0,22.0,40.0)){set("EASTERN EUROPE","EET-2EEST,M3.5.0/3,M10.5.0/4");return;}
    if(timezone_box(lat,lon,35.0,72.0,3.0,22.0)){set("CENTRAL EUROPE","CET-1CEST,M3.5.0,M10.5.0/3");return;}
    if(timezone_box(lat,lon,18.0,23.5,-161.5,-154.0)){set("HAWAII","HST10");return;}
    if(timezone_box(lat,lon,50.0,72.0,-170.0,-130.0)){set("ALASKA","AKST9AKDT,M3.2.0,M11.1.0");return;}
    if(timezone_box(lat,lon,24.0,50.5,-125.0,-66.0)){
        if(timezone_box(lat,lon,31.0,37.5,-115.0,-109.0)){set("ARIZONA","MST7");return;}
        if(lon<-114.0){set("US PACIFIC","PST8PDT,M3.2.0,M11.1.0");return;}
        if(lon<-101.0){set("US MOUNTAIN","MST7MDT,M3.2.0,M11.1.0");return;}
        if(lon<-85.0){set("US CENTRAL","CST6CDT,M3.2.0,M11.1.0");return;}
        set("US EASTERN","EST5EDT,M3.2.0,M11.1.0");return;
    }
    if(timezone_box(lat,lon,33.0,43.0,124.0,131.5)){set("KOREA","KST-9");return;}
    if(timezone_box(lat,lon,30.0,46.0,129.0,146.0)){set("JAPAN","JST-9");return;}
    if(timezone_box(lat,lon,26.0,31.5,80.0,89.0)){set("NEPAL","NPT-5:45");return;}
    if(timezone_box(lat,lon,23.0,38.0,60.0,78.0)){set("PAKISTAN","PKT-5");return;}
    if(timezone_box(lat,lon,20.0,27.0,88.0,93.0)){set("BANGLADESH","BST-6");return;}
    if(timezone_box(lat,lon,6.0,36.0,68.0,98.0)){set("INDIA","IST-5:30");return;}
    if(timezone_box(lat,lon,18.0,54.0,73.0,135.0)){set("CHINA","CST-8");return;}
    if(timezone_box(lat,lon,-1.5,8.0,99.0,120.0)){set("SINGAPORE / MALAYSIA","SGT-8");return;}
    if(timezone_box(lat,lon,4.0,22.0,116.0,127.0)){set("PHILIPPINES","PST-8");return;}
    if(timezone_box(lat,lon,9.0,29.0,92.0,102.0)){set("MYANMAR","MMT-6:30");return;}
    if(timezone_box(lat,lon,5.0,24.0,97.0,109.0)){set("SE ASIA","ICT-7");return;}
    if(timezone_box(lat,lon,-12.0,7.5,95.0,106.0)){set("INDONESIA WEST","WIB-7");return;}
    if(timezone_box(lat,lon,-12.0,7.5,106.0,120.0)){set("INDONESIA CENTRAL","WITA-8");return;}
    if(timezone_box(lat,lon,-12.0,7.5,120.0,141.0)){set("INDONESIA EAST","WIT-9");return;}
    if(timezone_box(lat,lon,-36.0,-22.0,16.0,33.0)){set("SOUTH AFRICA","SAST-2");return;}
    if(timezone_box(lat,lon,22.0,27.0,51.0,57.0)){set("GULF","GST-4");return;}
    if(timezone_box(lat,lon,24.0,40.0,44.0,64.0)){set("IRAN","IRST-3:30");return;}
    if(timezone_box(lat,lon,-56.0,-21.0,-73.0,-53.0)){set("ARGENTINA / URUGUAY","ART3");return;}
    int minutes=(int)lround(lon*4.0/15.0)*15;
    minutes=constrain(minutes,-12*60,14*60);
    char fixed_label[24]{};
    fixed_timezone_text((int16_t)minutes,fixed_label,sizeof(fixed_label),rule,rule_len);
    snprintf(label,label_len,"GPS OFFSET %s",fixed_label);
}
static const char* active_timezone_rule(){
    if(timezone_index==TIMEZONE_AUTO)return auto_timezone_rule;
    static char custom_rule[32]{};
    if(timezone_index==TIMEZONE_CUSTOM){
        char ignored[24]{};
        fixed_timezone_text(custom_timezone_minutes,ignored,sizeof(ignored),custom_rule,sizeof(custom_rule));
        return custom_rule;
    }
    return TIMEZONES[timezone_index].rule;
}
static void apply_timezone(){
    const char* rule=active_timezone_rule();
    setenv("TZ",rule&&rule[0]?rule:"UTC0",1);tzset();
    T5_DEBUGF(T5_LOG_UI,"[T5-TIME] display timezone index=%u rule=%s\n",(unsigned)timezone_index,rule?rule:"UTC0");
}
static void persist_timezone_selection(){
    Preferences zone;if(zone.begin("t5-ui",false)){
        zone.putUChar("timezone",timezone_index);zone.putBool("tz_v2",true);
        zone.putInt("custom_tz_min",(int32_t)custom_timezone_minutes);zone.end();
    }
}
static void persist_auto_timezone(){
    Preferences zone;if(zone.begin("t5-ui",false)){
        zone.putString("auto_tz_label",auto_timezone_label);
        zone.putString("auto_tz_rule",auto_timezone_rule);zone.end();
    }
}
static void update_auto_timezone_from_gps(long latitude,long longitude){
    char label[sizeof(auto_timezone_label)]{},rule[sizeof(auto_timezone_rule)]{};
    resolve_gps_timezone(latitude,longitude,label,sizeof(label),rule,sizeof(rule));
    if(!strcmp(label,auto_timezone_label)&&!strcmp(rule,auto_timezone_rule))return;
    snprintf(auto_timezone_label,sizeof(auto_timezone_label),"%s",label);
    snprintf(auto_timezone_rule,sizeof(auto_timezone_rule),"%s",rule);
    persist_auto_timezone();
    if(timezone_index==TIMEZONE_AUTO){
        apply_timezone();status_dirty=true;status_bar_dirty=true;
        T5_DEBUGF(T5_LOG_UI,"[T5-TIME] GPS timezone resolved %s\n",auto_timezone_label);
    }
}
static void seed_auto_timezone_from_selection(uint8_t previous){
    if(previous==TIMEZONE_AUTO)return;
    if(previous==TIMEZONE_CUSTOM){
        char label[24]{};
        fixed_timezone_text(custom_timezone_minutes,label,sizeof(label),auto_timezone_rule,sizeof(auto_timezone_rule));
        snprintf(auto_timezone_label,sizeof(auto_timezone_label),"%s",label);
    }else if(previous<TIMEZONE_COUNT&&TIMEZONES[previous].rule){
        snprintf(auto_timezone_label,sizeof(auto_timezone_label),"%s",TIMEZONES[previous].label);
        snprintf(auto_timezone_rule,sizeof(auto_timezone_rule),"%s",TIMEZONES[previous].rule);
    }
    persist_auto_timezone();
}
static void timezone_display_label(char* out,size_t len){
    if(!out||!len)return;
    if(timezone_index==TIMEZONE_AUTO)snprintf(out,len,"AUTO (%s)",auto_timezone_label);
    else if(timezone_index==TIMEZONE_CUSTOM){
        char label[24]{},rule[32]{};
        fixed_timezone_text(custom_timezone_minutes,label,sizeof(label),rule,sizeof(rule));
        snprintf(out,len,"CUSTOM %s",label);
    }else snprintf(out,len,"%s",TIMEZONES[timezone_index].label);
}

static constexpr uint32_t FRONTLIGHT_TIMEOUTS[]={5000,10000,15000,30000,0};
static constexpr uint32_t STANDBY_TIMEOUTS[]={300000,600000,900000,0};
static const char* frontlight_mode_name(){return frontlight_mode==FrontlightMode::On?"ON":frontlight_mode==FrontlightMode::NightTimer?"NIGHT TIMER":"OFF";}
static const char* frontlight_timeout_name(){static const char* names[]={"5 SECONDS","10 SECONDS","15 SECONDS","30 SECONDS","ALWAYS ON"};return names[min((uint8_t)4,frontlight_timeout_index)];}
static const char* standby_timeout_name(){static const char* names[]={"5 MINUTES","10 MINUTES","15 MINUTES","NEVER"};return names[min((uint8_t)3,standby_timeout_index)];}
static bool night_window_active(){const uint16_t now=status_hour<0?0:(uint16_t)(status_hour*60+status_minute);return night_start_minutes<=night_end_minutes?(now>=night_start_minutes&&now<night_end_minutes):(now>=night_start_minutes||now<night_end_minutes);}
static bool frontlight_allowed(){return frontlight_mode==FrontlightMode::On||(frontlight_mode==FrontlightMode::NightTimer&&night_window_active());}
static void frontlight_drive(bool on){frontlight_lit=on&&frontlight_allowed();meshink_power_frontlight_set(frontlight_lit?frontlight_brightness:0);}
static void frontlight_preview(uint8_t level){
    // Live PWM feedback while either brightness slider is being dragged.
    // Do not alter persisted brightness or redraw e-paper until release.
    frontlight_lit=level>0;
    meshink_power_frontlight_set(level);
    const uint32_t timeout=FRONTLIGHT_TIMEOUTS[min((uint8_t)4,frontlight_timeout_index)];
    frontlight_deadline=timeout?millis()+timeout:0;
}
static void frontlight_event(){if(!frontlight_allowed()){frontlight_drive(false);frontlight_deadline=0;return;}frontlight_drive(true);const uint32_t timeout=FRONTLIGHT_TIMEOUTS[min((uint8_t)4,frontlight_timeout_index)];frontlight_deadline=timeout?millis()+timeout:0;}
static void frontlight_service(){if(message_alert_active||quick_slider_dragging||display_slider_dragging)return;if(frontlight_mode==FrontlightMode::Off||(frontlight_mode==FrontlightMode::NightTimer&&!night_window_active())){if(frontlight_lit)frontlight_drive(false);return;}if(frontlight_lit&&frontlight_deadline&&(int32_t)(millis()-frontlight_deadline)>=0){frontlight_deadline=0;frontlight_drive(false);T5_DEBUGLN(T5_LOG_UI,"[T5-LIGHT] timeout; frontlight off");}}
static void save_frontlight_settings(){Preferences light;if(light.begin("t5-ui",false)){light.putUChar("light_mode",(uint8_t)frontlight_mode);light.putUChar("light_timeout",frontlight_timeout_index);light.putUChar("light_level",frontlight_brightness);light.putUChar("standby_timeout",standby_timeout_index);light.putBool("deep_standby",deep_sleep_standby);light.putUShort("night_start",night_start_minutes);light.putUShort("night_end",night_end_minutes);light.end();}}

// Keep the original five-field single-touch event compatible with all UI
// screens. Maps alone can add a completed two-finger gesture and tap timing.
struct QueuedTap{
    int16_t x=0,y=0,dx=0,dy=0;
    bool home=false;
    uint8_t map_pinch=0;
    int8_t zoom_steps=0;
    uint16_t hold_ms=0;
    uint8_t map_sampled=0;
    uint32_t queued_at_ms=0;
    QueuedTap()=default;
    // Arduino's C++11 toolchain requires an explicit constructor here once
    // the map-only fields have default initializers. Preserve all existing
    // five-argument single-touch event construction unchanged.
    QueuedTap(int16_t px,int16_t py,int16_t pdx,int16_t pdy,bool is_home):
        x(px),y(py),dx(pdx),dy(pdy),home(is_home),queued_at_ms(millis()) {}
};

struct MapTapSequence{
    uint8_t count=0;
    int16_t first_x=0,first_y=0;
    int16_t last_x=0,last_y=0;
    uint32_t last_at=0;
};
static MapTapSequence map_taps{};
static uint32_t navigation_touch_cutoff_ms=0;

static constexpr uint32_t UI_IDLE_CPU_MHZ=80;
static constexpr uint32_t UI_RENDER_CPU_MHZ=240;
static bool ui_boot_cpu_active=false;

static uint32_t ui_post_render_cpu_target(){
    return ui_boot_cpu_active?UI_RENDER_CPU_MHZ:UI_IDLE_CPU_MHZ;
}

static bool set_cpu_target(uint32_t mhz,const char* reason){
    const bool accepted=meshink_performance_set_cpu_mhz(mhz);const uint32_t actual=meshink_performance_cpu_mhz();
    if(!accepted||actual!=mhz)Serial.printf("[T5-ERROR] CPU target=%lu actual=%lu MHz reason=%s\n",(unsigned long)mhz,(unsigned long)actual,reason);
    return accepted&&actual==mhz;
}

struct MeshInkCpuBoostScope {
    uint32_t previous_mhz;
    bool restore;
    explicit MeshInkCpuBoostScope(bool enabled,const char* reason):
        previous_mhz(meshink_performance_cpu_mhz()),restore(false){
        if(enabled&&previous_mhz<UI_RENDER_CPU_MHZ)
            restore=set_cpu_target(UI_RENDER_CPU_MHZ,reason);
    }
    ~MeshInkCpuBoostScope(){
        if(restore)set_cpu_target(previous_mhz,"ui-draw-complete");
    }
};

struct Preset {
    const char* title;
    const char* detail;
    uint32_t frequency_khz;  // integer kHz avoids parsing and rounding displayed MHz
    float bandwidth_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    uint8_t path_hash_bytes;  // 0 only for KEEP CURRENT, otherwise 1..3
};
static constexpr Preset PRESETS[] = {
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
static constexpr uint8_t PRESET_COUNT = sizeof(PRESETS)/sizeof(PRESETS[0]);

static const char* active_radio_label(){
    auto matches=[](const Preset& preset){
        return preset.path_hash_bytes&&mesh_protocol_radio_matches(
            preset.frequency_khz/1000.0f,preset.bandwidth_khz,
            preset.spreading_factor,preset.coding_rate,preset.path_hash_bytes);
    };
    if(selected_preset<PRESET_COUNT&&matches(PRESETS[selected_preset]))
        return PRESETS[selected_preset].title;
    for(uint8_t i=1;i<PRESET_COUNT;++i)
        if(matches(PRESETS[i]))return PRESETS[i].title;
    return mesh_protocol_radio_summary();
}

static bool apply_selected_preset() {
    if(selected_preset>=PRESET_COUNT)return false;
    const Preset& preset=PRESETS[selected_preset];
    if(preset.path_hash_bytes==0)return true; // KEEP CURRENT never alters the radio
    // Apply the *same typed values* displayed by the preset selector.
    // Protocol helpers expose path-hash mode as 0/1/2 for a 1/2/3-byte hash.
    const bool applied=mesh_protocol_apply_radio(preset.frequency_khz/1000.0f,
        preset.bandwidth_khz,preset.spreading_factor,preset.coding_rate,
        preset.path_hash_bytes-1);
    if(!applied)Serial.printf("[T5-ERROR] radio preset '%s' could not be applied\n",preset.title);
    else T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] radio preset '%s' applied\n",preset.title);
    return applied;
}


// The same setup screens serve both protocols. Protocol one maps its existing
// radio presets into short regional lists; Leaf enumerates its native regions.
static constexpr const char* SETUP_PROTO1_REGIONS[] = {
    "NEW ZEALAND","AUSTRALIA","EUROPE / UK","NORTH AMERICA",
    "BRAZIL / ASIA","CUSTOM / OTHER"
};
static size_t setup_region_count(){
    return setup_is_meshcore()
        ?sizeof(SETUP_PROTO1_REGIONS)/sizeof(SETUP_PROTO1_REGIONS[0])
        :mesh_protocol_setup_region_count();
}
static const char* setup_region_label(size_t index){
    if(setup_is_meshcore())
        return index<setup_region_count()?SETUP_PROTO1_REGIONS[index]:"";
    return mesh_protocol_setup_region_name(index);
}
static bool setup_preset_in_region(size_t index,size_t region){
    if(index==0||index>=PRESET_COUNT)return false;
    switch(region){
        case 0:return index==17||index==18;
        case 1:return index>=1&&index<=5;
        case 2:return (index>=9&&index<=16)||(index>=19&&index<=22);
        case 3:return index==7||index==8||(index>=23&&index<=25);
        case 4:return index==6||index==26||index==27;
        default:return false; // Custom region is deliberately preset-free.
    }
}
static int setup_core_preset_at(size_t visible_index){
    if(visible_index==0)return -1; // Custom from scratch, not a cloned preset.
    size_t seen=1;
    for(size_t i=1;i<PRESET_COUNT;++i){
        if(!setup_preset_in_region(i,setup_region))continue;
        if(seen++==visible_index)return (int)i;
    }
    return -2;
}
static size_t setup_preset_count(){
    if(!setup_is_meshcore())return mesh_protocol_setup_preset_count();
    size_t count=1; // Custom always present.
    for(size_t i=1;i<PRESET_COUNT;++i)if(setup_preset_in_region(i,setup_region))++count;
    return count;
}
static const char* setup_preset_label(size_t index){
    if(!setup_is_meshcore())return mesh_protocol_setup_preset_name(index);
    const int preset=setup_core_preset_at(index);
    return preset==-1?"CUSTOM / MANUAL":preset>=0?PRESETS[preset].title:"";
}
static void setup_load_core_preset(int index){
    setup_radio_preset=index;
    setup_bw=0;setup_sf=0;setup_cr=0;setup_hash=0;
    setup_freq[0]=0;setup_power[0]=0;setup_edit_field=0;
    if(index<=0||index>=PRESET_COUNT)return;
    const Preset& preset=PRESETS[index];
    snprintf(setup_freq,sizeof(setup_freq),"%.3f",(double)preset.frequency_khz/1000.0);
    setup_bw=preset.bandwidth_khz;
    setup_sf=preset.spreading_factor;
    setup_cr=preset.coding_rate;
    setup_hash=preset.path_hash_bytes;
    const uint8_t power=mesh_protocol_setup_current_tx_power();
    snprintf(setup_power,sizeof(setup_power),"%u",(unsigned)(power?power:20));
}
static bool setup_radio_valid(){
    if(!setup_freq[0]||!setup_power[0]||!setup_bw||!setup_sf||!setup_cr||!setup_hash)return false;
    char *freq_end=nullptr,*power_end=nullptr;
    const double frequency=strtod(setup_freq,&freq_end);
    const long power=strtol(setup_power,&power_end,10);
    return freq_end!=setup_freq&&!*freq_end&&frequency>=150.0&&frequency<=960.0&&
           power_end!=setup_power&&!*power_end&&power>=2&&power<=22&&
           setup_bw>=7.0f&&setup_bw<=500.0f&&
           setup_sf>=5&&setup_sf<=12&&setup_cr>=5&&setup_cr<=8&&setup_hash>=1&&setup_hash<=3;
}
static void setup_initialize_draft(){
    setup_region=0;setup_region_page=0;setup_preset_page=0;
    setup_hops=3;setup_radio_preset=-1;
    setup_freq[0]=0;setup_power[0]=0;
    setup_bw=0;setup_sf=0;setup_cr=0;setup_hash=0;
    setup_edit_field=0;
    // Leaf's ANZ value is not assumed to be index zero in its region listing.
    if(!setup_is_meshcore())
        for(size_t i=0;i<setup_region_count();++i)
            if(!strcmp(setup_region_label(i),"ANZ")){setup_region=(uint8_t)i;break;}
}
static bool setup_finish(){
    if(!node_name[0])return false;
    if(setup_is_meshcore()){
        if(!setup_radio_valid())return false;
        const float freq=(float)strtod(setup_freq,nullptr);
        if(!mesh_protocol_apply_radio(freq,setup_bw,setup_sf,setup_cr,setup_hash-1))return false;
        if(!mesh_protocol_setup_save_tx_power((uint8_t)strtoul(setup_power,nullptr,10)))return false;
        if(setup_radio_preset>0)selected_preset=(uint8_t)setup_radio_preset;
        else selected_preset=0; // KEEP CURRENT is only the settings-picker sentinel.
    }else if(!mesh_protocol_setup_commit_radio(setup_region,(size_t)setup_radio_preset,setup_hops)){
        return false;
    }
    Preferences commit;
    if(!commit.begin("t5-ui",false))return false;
    commit.putString("name",node_name);
    commit.putUChar("preset_v2",selected_preset);
    commit.putBool("complete",true); // preserve legacy readers
    commit.putBool("name_migrated",true);
    commit.putBool(setup_is_meshcore()?"setup_mc":"setup_mst",true);
    commit.putUChar("setup_return",0);
    commit.putUChar("setup_choice",0);
    commit.end();
    setup_complete=true;
    setup_any_done=true;
    if(setup_is_meshcore())setup_meshcore_done=true;else setup_meshtastic_done=true;
    // Restart with the confirmed parameters so both protocols initialize the
    // same way they will on every subsequent boot.
    return mesh_protocol_restart_into(mesh_protocol_descriptor().id);
}

static const uint8_t* glyph(char c) {
    for (const auto& g : FONT) if (g.c == c) return g.r;
    for (const auto& g : FONT) if (g.c == '?') return g.r;
    return FONT[0].r;
}

static void text(const char* s, int x, int y, int scale, uint8_t color = 0, bool bold = false) {
    while (*s) {
        const uint8_t* rows = glyph(*s++);
        for (int ry=0; ry<7; ++ry) for (int rx=0; rx<5; ++rx)
            if (rows[ry] & (1 << (4-rx)))
                for (int dy=0; dy<scale; ++dy) for (int dx=0; dx<scale; ++dx)
                    { meshink_display_draw_pixel(x+rx*scale+dx, y+ry*scale+dy, color, fb);
                      if (bold) meshink_display_draw_pixel(x+rx*scale+dx+1, y+ry*scale+dy, color, fb); }
        x += 6*scale;
    }
}

static void centred(const char* s, int y, int scale, uint8_t color = 0, bool bold = false) {
    text(s, (meshink_display_logical_width() - (int)strlen(s)*6*scale)/2, y, scale, color, bold);
}

// Primary UI typography uses three built-in 1-bit Inter raster sizes.
// Small technical/status text deliberately stays on the original 5x7 bitmap.
// The Inter bitmaps contain only black/white coverage: no grayscale edge
// pixels are introduced, preserving the proven DU e-paper refresh behaviour.
static void ui_glyph_bounds(const uint8_t* rows,int& left,int& right) {
    left=5;right=-1;
    for(int rx=0;rx<5;++rx){
        const uint8_t mask=(uint8_t)(1U<<(4-rx));
        for(int ry=0;ry<7;++ry)if(rows[ry]&mask){
            left=min(left,rx);right=max(right,rx);break;
        }
    }
}

struct UiSmoothFont {
    const MeshInkFontData* font;
    uint8_t baseline_from_top;
    uint8_t visual_height;
    uint8_t line_step;
};
static UiSmoothFont ui_smooth_font(int scale) {
    // Every primary tier is a native FreeType raster. Nothing is enlarged at
    // runtime, which keeps baselines, curves and descenders internally stable.
    if(scale>=11)return {&inter_50_digits,77,79,81};
    if(scale>=6)return {&inter_30_regular,46,46,65};
    if(scale==5)return {&inter_25_regular,38,38,55};
    if(scale==4)return {&inter_20_regular,31,31,44};
    return {&inter_15_regular,23,23,34};
}
static const MeshInkFontGlyph* ui_smooth_glyph(const MeshInkFontData* font,uint32_t codepoint) {
    if(!font)return nullptr;
    for(uint32_t i=0;i<font->intervalCount;++i){
        const MeshInkFontUnicodeInterval& interval=font->intervals[i];
        if(codepoint>=interval.first&&codepoint<=interval.last)
            return &font->glyph[interval.offset+(codepoint-interval.first)];
        if(codepoint<interval.first)break;
    }
    if(codepoint!='?')return ui_smooth_glyph(font,'?');
    return nullptr;
}
static int ui_smooth_char_advance(char c,int scale) {
    const UiSmoothFont face=ui_smooth_font(scale);
    const MeshInkFontGlyph* glyph_data=ui_smooth_glyph(face.font,(uint8_t)c);
    if(!glyph_data)return 0;
    return max(1,(int)((glyph_data->advanceX+8)>>4));
}
static int ui_legacy_char_advance(char c,int scale) {
    if(c==' ')return 3*scale;
    const uint8_t* rows=glyph(c);int left=0,right=4;ui_glyph_bounds(rows,left,right);
    const int pixels=right>=left?(right-left+1):3;
    return pixels*scale+scale;
}
static int ui_char_advance(char c,int scale) {
    return scale>=3?ui_smooth_char_advance(c,scale):ui_legacy_char_advance(c,scale);
}
static int ui_text_height(int scale) {
    if(scale<3)return 7*scale;
    return ui_smooth_font(scale).visual_height;
}
static int ui_text_line_step(int scale) {
    if(scale<3)return 7*scale+8;
    return ui_smooth_font(scale).line_step;
}
static int ui_text_width_n(const char* s,size_t n,int scale) {
    if(!s||!n)return 0;
    int width=0;
    for(size_t i=0;i<n&&s[i]&&s[i]!='\n';++i)width+=ui_char_advance(s[i],scale);
    return width&&scale<3?width-scale:width;
}
static int ui_text_width(const char* s,int scale) {
    return s?ui_text_width_n(s,strlen(s),scale):0;
}
static void ui_smooth_text(const char* s,int x,int y,int scale,uint8_t color,bool bold) {
    if(!s)return;
    const UiSmoothFont face=ui_smooth_font(scale);
    const int baseline=y+face.baseline_from_top;
    while(*s&&*s!='\n'){
        const char c=*s++;
        const MeshInkFontGlyph* glyph_data=ui_smooth_glyph(face.font,(uint8_t)c);
        if(!glyph_data)continue;
        const int gx=x+glyph_data->left;
        const int gy=baseline-glyph_data->top;
        for(int sy=0;sy<glyph_data->height;++sy){
            for(int sx=0;sx<glyph_data->width;++sx){
                const uint32_t bit=(uint32_t)sy*glyph_data->width+(uint32_t)sx;
                const uint8_t packed=face.font->bitmap[glyph_data->dataOffset+(bit>>3)];
                if(!(packed&(uint8_t)(0x80U>>(bit&7))))continue;
                meshink_display_draw_pixel(gx+sx,gy+sy,color,fb);
                if(bold)meshink_display_draw_pixel(gx+sx+1,gy+sy,color,fb);
            }
        }
        x+=ui_smooth_char_advance(c,scale);
    }
}
static void ui_smooth_text_clipped(const char* s,int x,int y,int scale,
                                   uint8_t color,bool bold,
                                   int clip_left,int clip_top,
                                   int clip_right,int clip_bottom) {
    if(!s||clip_right<=clip_left||clip_bottom<=clip_top)return;
    const UiSmoothFont face=ui_smooth_font(scale);
    const int baseline=y+face.baseline_from_top;
    while(*s&&*s!='\n'){
        const char c=*s++;
        const MeshInkFontGlyph* glyph_data=ui_smooth_glyph(face.font,(uint8_t)c);
        if(!glyph_data)continue;
        const int gx=x+glyph_data->left;
        const int gy=baseline-glyph_data->top;
        for(int sy=0;sy<glyph_data->height;++sy){
            const int py=gy+sy;
            if(py<clip_top||py>=clip_bottom)continue;
            for(int sx=0;sx<glyph_data->width;++sx){
                const int px=gx+sx;
                if(px<clip_left||px>=clip_right)continue;
                const uint32_t bit=(uint32_t)sy*glyph_data->width+(uint32_t)sx;
                const uint8_t packed=face.font->bitmap[glyph_data->dataOffset+(bit>>3)];
                if(!(packed&(uint8_t)(0x80U>>(bit&7))))continue;
                meshink_display_draw_pixel(px,py,color,fb);
                if(bold&&px+1<clip_right)meshink_display_draw_pixel(px+1,py,color,fb);
            }
        }
        x+=ui_smooth_char_advance(c,scale);
    }
}
static void ui_text(const char* s,int x,int y,int scale,uint8_t color=0,bool bold=false) {
    if(!s)return;
    if(scale>=3){
        ui_smooth_text(s,x,y,scale,color,bold);
        return;
    }
    while(*s&&*s!='\n'){
        const char c=*s++;
        if(c==' '){x+=3*scale;continue;}
        const uint8_t* rows=glyph(c);int left=0,right=4;ui_glyph_bounds(rows,left,right);
        if(right<left){x+=3*scale;continue;}
        for(int ry=0;ry<7;++ry)for(int rx=left;rx<=right;++rx)
            if(rows[ry]&(1U<<(4-rx)))
                for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx){
                    meshink_display_draw_pixel(x+(rx-left)*scale+dx,y+ry*scale+dy,color,fb);
                    if(bold)meshink_display_draw_pixel(x+(rx-left)*scale+dx+1,y+ry*scale+dy,color,fb);
                }
        x+=(right-left+1)*scale+scale;
    }
}
static void ui_text_fit(const char* value,int x,int y,int max_width,int scale,
                        uint8_t color=0,bool bold=false) {
    if(!value||max_width<=0)return;
    if(ui_text_width(value,scale)<=max_width){ui_text(value,x,y,scale,color,bold);return;}
    char clipped[64]{};size_t out=0;int width=0;
    const int dots=ui_text_width("...",scale);
    while(value[out]&&out<sizeof(clipped)-4){
        const int advance=ui_char_advance(value[out],scale);
        if(width+advance+dots>max_width)break;
        clipped[out]=value[out];width+=advance;++out;
    }
    clipped[out++]='.';clipped[out++]='.';clipped[out++]='.';clipped[out]=0;
    ui_text(clipped,x,y,scale,color,bold);
}
static void ui_centred_fit(const char* value,int y,int max_width,int scale,
                           uint8_t color=0,bool bold=false) {
    if(!value||max_width<=0)return;
    if(ui_text_width(value,scale)<=max_width){
        ui_text(value,(meshink_display_logical_width()-ui_text_width(value,scale))/2,
                y,scale,color,bold);
        return;
    }
    char clipped[64]{};size_t out=0;int width=0;
    const int dots=ui_text_width("...",scale);
    while(value[out]&&out<sizeof(clipped)-4){
        const int advance=ui_char_advance(value[out],scale);
        if(width+advance+dots>max_width)break;
        clipped[out]=value[out];width+=advance;++out;
    }
    clipped[out++]='.';clipped[out++]='.';clipped[out++]='.';clipped[out]=0;
    const int clipped_width=ui_text_width(clipped,scale);
    ui_text(clipped,(meshink_display_logical_width()-clipped_width)/2,
            y,scale,color,bold);
}
static int ui_text_max_line_width(const char* value,int scale) {
    if(!value||!*value)return 0;
    int widest=0,current=0;
    for(const char* p=value;;++p){
        if(!*p||*p=='\n'){
            if(current>0&&scale<3)current-=scale; // legacy bitmap keeps one trailing scale-wide gap
            widest=max(widest,current);current=0;
            if(!*p)break;
            continue;
        }
        current+=ui_char_advance(*p,scale);
    }
    return widest;
}
static size_t ui_wrap_take(const char* value,int max_width,int scale) {
    if(!value||!*value)return 0;
    size_t count=0,last_space=0;int width=0;
    while(value[count]&&value[count]!='\n'){
        const int advance=ui_char_advance(value[count],scale);
        if(count&&width+advance>max_width)break;
        if(!count&&advance>max_width)return 1;
        width+=advance;
        if(value[count]==' ')last_space=count;
        ++count;
    }
    if(!value[count]||value[count]=='\n')return count;
    return last_space?last_space:count;
}
static int ui_wrapped_line_count(const char* value,int max_width,int scale) {
    if(!value||!*value)return 1;
    int lines=0;const char* cursor=value;
    while(*cursor){
        while(*cursor==' ')++cursor;
        if(*cursor=='\n'){++cursor;++lines;continue;}
        if(!*cursor)break;
        size_t take=ui_wrap_take(cursor,max_width,scale);
        if(!take)take=1;
        cursor+=take;while(*cursor==' ')++cursor;if(*cursor=='\n')++cursor;
        ++lines;
    }
    return max(1,lines);
}
static void ui_draw_wrapped(const char* value,int x,int y,int max_width,int scale,
                            uint8_t color,bool bold,int max_lines) {
    if(!value)return;
    const char* cursor=value;
    for(int row=0;row<max_lines&&*cursor;++row){
        while(*cursor==' ')++cursor;
        if(*cursor=='\n'){++cursor;continue;}
        size_t take=ui_wrap_take(cursor,max_width,scale);
        if(!take)take=1;
        char line_text[160]{};const size_t copy=min(take,sizeof(line_text)-4);
        memcpy(line_text,cursor,copy);
        size_t trim=strlen(line_text);
        while(trim&&line_text[trim-1]==' ')line_text[--trim]=0;

        const char* next=cursor+take;
        while(*next==' ')++next;
        if(*next=='\n')++next;
        const bool truncated=(row==max_lines-1)&&*next;
        if(truncated){
            const int dots=ui_text_width("...",scale);
            while(trim&&ui_text_width(line_text,scale)+dots>max_width)
                line_text[--trim]=0;
            if(trim<sizeof(line_text)-4){
                line_text[trim++]='.';line_text[trim++]='.';line_text[trim++]='.';
                line_text[trim]=0;
            }
            ui_text(line_text,x,y+row*ui_text_line_step(scale),scale,color,bold);
        }else{
            ui_text(line_text,x,y+row*ui_text_line_step(scale),scale,color,bold);
        }
        cursor=next;
    }
}
static void ui_draw_wrapped_tail(const char* value,int x,int y,int max_width,
                                 int max_height,int scale,uint8_t color,bool bold) {
    if(!value||!*value||max_width<=0||max_height<=0)return;
    struct WrappedLine { const char* start; size_t len; };
    WrappedLine lines[32]{};
    int line_count=0;
    const char* cursor=value;
    while(*cursor&&line_count<(int)(sizeof(lines)/sizeof(lines[0]))){
        while(*cursor==' ')++cursor;
        if(!*cursor)break;
        if(*cursor=='\n'){lines[line_count++]={cursor,0};++cursor;continue;}
        size_t take=ui_wrap_take(cursor,max_width,scale);if(!take)take=1;
        lines[line_count++]={cursor,take};cursor+=take;
        while(*cursor==' ')++cursor;
        if(*cursor=='\n')++cursor;
    }
    if(!line_count)return;
    const int glyph_height=ui_text_height(scale);
    const int line_step=ui_text_line_step(scale);
    if(max_height<glyph_height)return;
    const int visible_lines=1+(max_height-glyph_height)/line_step;
    const int first=max(0,line_count-visible_lines);
    int row=0;
    for(int i=first;i<line_count;++i,++row){
        char line_text[MESHINK_MESSAGE_TEXT_BYTES]{};
        const size_t copy=min(lines[i].len,sizeof(line_text)-1);
        if(copy)memcpy(line_text,lines[i].start,copy);
        size_t trim=copy;while(trim&&line_text[trim-1]==' ')line_text[--trim]=0;
        ui_text(line_text,x,y+row*line_step,scale,color,bold);
    }
}

struct UiComposeLine { const char* start; size_t len; };

static int ui_compose_wrap_lines(const char* value,int max_width,int scale,
                                 UiComposeLine* lines,int capacity) {
    if(!value||!lines||capacity<=0)return 0;
    if(!*value){lines[0]={value,0};return 1;}
    int count=0;
    const char* cursor=value;
    while(count<capacity){
        const char* start=cursor;
        const char* scan=cursor;
        const char* last_space=nullptr;
        int width=0;
        while(*scan&&*scan!='\n'){
            const int advance=ui_char_advance(*scan,scale);
            if(scan>start&&width+advance>max_width)break;
            width+=advance;
            if(*scan==' ')last_space=scan;
            ++scan;
        }
        if(*scan=='\n'){
            lines[count++]={start,(size_t)(scan-start)};
            cursor=scan+1;
            if(!*cursor&&count<capacity)lines[count++]={cursor,0};
            if(!*cursor)break;
            continue;
        }
        if(!*scan){
            lines[count++]={start,(size_t)(scan-start)};
            break;
        }

        // If the character that overflowed is itself a space, put the caret
        // on the next line rather than silently discarding the user's space.
        if(*scan==' '){
            lines[count++]={start,(size_t)(scan-start)};
            cursor=scan+1;
            if(!*cursor&&count<capacity)lines[count++]={cursor,0};
            if(!*cursor)break;
            continue;
        }

        // Prefer a word boundary when one exists. The separator space remains
        // in compose_text, while rendering begins after it on the next line.
        if(last_space&&last_space>start){
            lines[count++]={start,(size_t)(last_space-start)};
            cursor=last_space+1;
        }else{
            lines[count++]={start,(size_t)(scan-start)};
            cursor=scan;
        }
        if(!*cursor&&count<capacity){
            lines[count++]={cursor,0};
            break;
        }
    }
    return max(1,count);
}

static void ui_draw_compose_tail(const char* value,int x,int y,int max_width,
                                 int max_height,int scale) {
    if(!value||max_width<=0||max_height<=0)return;
    UiComposeLine lines[64]{};
    const int line_count=ui_compose_wrap_lines(
        value,max_width,scale,lines,(int)(sizeof(lines)/sizeof(lines[0])));
    if(!line_count)return;

    const int glyph_height=ui_text_height(scale);
    const int line_step=ui_text_line_step(scale);
    if(max_height<glyph_height)return;

    // One line remains vertically comfortable. Once wrapping starts, keep the
    // newest line near the bottom but lift it slightly clear of the rounded
    // field edge so descenders and the lower glyph row are never clipped.
    const int multiline_lift=max(8,glyph_height/3);
    const int current_y=line_count<=1
        ?y+(max_height-glyph_height)/2
        :y+max_height-glyph_height-multiline_lift;
    for(int i=line_count-1;i>=0;--i){
        const int line_y=current_y-(line_count-1-i)*line_step;
        if(line_y+glyph_height<=y)break;
        if(line_y>=y+max_height)continue;
        char line_text[MESHINK_MESSAGE_TEXT_BYTES]{};
        const size_t copy=min(lines[i].len,sizeof(line_text)-1);
        if(copy)memcpy(line_text,lines[i].start,copy);
        ui_smooth_text_clipped(line_text,x,line_y,scale,0,false,
                               x,y,x+max_width,y+max_height);
    }

    // Keep the caret non-blinking and restrained, but large enough to remain
    // legible after a direct e-paper update and to make trailing spaces obvious.
    const UiComposeLine& current=lines[line_count-1];
    const int caret_advance=ui_text_width_n(current.start,current.len,scale);
    constexpr int caret_width=2;
    const int caret_x=max(x,min(x+max_width-caret_width,x+caret_advance));
    const int caret_height=min(max_height-4,max(14,(glyph_height*3)/2));
    const int centred_caret_y=current_y+(glyph_height-caret_height)/2;
    const int caret_y=max(y+2,min(y+max_height-2-caret_height,centred_caret_y));
    meshink_display_fill_rect({caret_x,caret_y,caret_width,caret_height},0,fb);
}

// Rounded surfaces are composed directly in the selected display backend.
// The T5 backend preserves these exact integer corner pixels while choosing
// packed-framebuffer-friendly spans for the current rotation.
static void rounded_fill(int x,int y,int w,int h,int radius,uint8_t color) {
    if(w<=0||h<=0)return;
    meshink_display_fill_rounded_rect({x,y,w,h},radius,color,fb);
}
static void rounded_box(int x,int y,int w,int h,int radius,bool selected=false) {
    if(selected){rounded_fill(x,y,w,h,radius,0);return;}
    rounded_fill(x,y,w,h,radius,0);
    const int border=max(2,ui_w(2));
    rounded_fill(x+border,y+border,w-2*border,h-2*border,
                 max(1,radius-border),0xFF);
}
static void rounded_box(const MeshInkUiRect& rect,int radius,bool selected=false) {
    rounded_box(rect.x,rect.y,rect.width,rect.height,radius,selected);
}

static void ui_centred(const char* value,int y,int scale,uint8_t color=0,bool bold=false) {
    ui_text(value,(meshink_display_logical_width()-ui_text_width(value,scale))/2,
            y,scale,color,bold);
}
static void ui_action_button(const char* label,const MeshInkUiRect& rect,bool selected=false) {
    const int radius=max(ui_w(12),ui_h(12));
    rounded_box(rect,radius,selected);
    int scale=3;
    if(ui_text_width(label,scale)>rect.width-ui_w(24))scale=2;
    const int label_width=ui_text_width(label,scale);
    ui_text(label,rect.x+(rect.width-label_width)/2,
            rect.y+(rect.height-ui_text_height(scale))/2,scale,selected?0xFF:0,true);
}
static void ui_section_card(const MeshInkUiRect& rect) {
    rounded_box(rect,max(ui_w(14),ui_h(14)),false);
}

static meshink_keyboard::Metrics keyboard_metrics(bool landscape) {
    const meshink_keyboard::Tuning tuning={
        landscape?MESHINK_KEYBOARD_LANDSCAPE_X_OFFSET:MESHINK_KEYBOARD_PORTRAIT_X_OFFSET,
        landscape?MESHINK_KEYBOARD_LANDSCAPE_Y_OFFSET:MESHINK_KEYBOARD_PORTRAIT_Y_OFFSET,
        MESHINK_KEYBOARD_KEY_HEIGHT_DELTA,
        MESHINK_KEYBOARD_ROW_GAP_DELTA
    };
    return meshink_keyboard::make_metrics(
        landscape?meshink_display_portrait_height():meshink_display_portrait_width(),
        landscape?meshink_display_portrait_width():meshink_display_portrait_height(),
        landscape,tuning);
}


static void key(const char* label,const meshink_keyboard::Rect& rect) {
    rounded_box(rect.x,rect.y,rect.width,rect.height,max(ui_w(7),ui_h(7)),false);
    int scale=3;
    if(ui_text_width(label,scale)+ui_w(8)>rect.width)scale=2;
    ui_text(label,rect.x+(rect.width-ui_text_width(label,scale))/2,
            rect.y+(rect.height-ui_text_height(scale))/2,scale,0,true);
}

static void draw_keyboard() {
    const auto metrics=keyboard_metrics(false);
    const char* numbers="1234567890";
    const auto digits=meshink_keyboard::numbers(metrics);
    for(int i=0;numbers[i];++i){
        char label[2]={numbers[i],0};
        key(label,{digits.start+i*digits.pitch,metrics.number_top,digits.width,metrics.key_height});
    }
    const char* letter_rows_upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    const char* letter_rows_lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbol_rows[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    const char** rows=keyboard_symbols?symbol_rows:(keyboard_upper?letter_rows_upper:letter_rows_lower);
    for(int r=0;r<3;++r){
        const auto layout=meshink_keyboard::letters(metrics,r,(int)strlen(rows[r]));
        const int y=metrics.letter_top+r*metrics.row_step;
        for(int i=0;rows[r][i];++i){
            char label[2]={rows[r][i],0};
            key(label,{layout.start+i*layout.pitch,y,layout.width,metrics.key_height});
        }
    }
    key(keyboard_symbols?"ABC":(keyboard_upper?"abc":"#+="),metrics.mode_key);
    key("DEL",metrics.delete_key);
    key(screen==Screen::ChannelCreate?"HIDE":"LAND",metrics.orientation_key);
    if(screen==Screen::SetupName||screen==Screen::SetupRadio){
        key("ENTER",metrics.wide_action_key);
    }else if(screen==Screen::ChannelCreate){
        key("DONE",metrics.wide_action_key);
    }else if(keyboard_message_mode||keyboard_password_mode){
        // Space is valid for message/password entry. Match familiar phone
        // keyboards with a wide space bar and an isolated action at right.
        key("SPACE",metrics.space_key);
        key(keyboard_password_mode?"LOGIN":"SEND",metrics.action_key);
    }else if(mesh_protocol_name_character_allowed(' ')){
        key("SPACE",metrics.space_key);
        key("SAVE",metrics.action_key);
    }else{
        key("SAVE",metrics.wide_action_key);
    }
    if(keyboard_message_mode)message_keyboard_case_dirty=false;
}

static void landscape_key(const char* label,const meshink_keyboard::Rect& rect){
    key(label,rect);
}
static const char** active_keyboard_rows(){
    static const char* upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    static const char* lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    static const char* symbols[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    return keyboard_symbols?symbols:(keyboard_upper?upper:lower);
}
static void draw_landscape_keyboard(){
    const auto metrics=keyboard_metrics(true);
    meshink_display_set_all_white(&display);
    const char* value=keyboard_password_mode?remote_password:(keyboard_message_mode?compose_text:node_name);
    rounded_box(metrics.entry.x,metrics.entry.y,metrics.entry.width,metrics.entry.height,
                max(ui_w(10),ui_h(10)),false);
    const int inset_x=meshink_keyboard::scale_axis(16,metrics.width,960);
    const int inset_y=meshink_keyboard::scale_axis(12,metrics.height,540);
    const int entry_x=metrics.entry.x+inset_x;
    const int entry_y=metrics.entry.y+inset_y;
    const int entry_width=metrics.entry.width-2*inset_x;
    const int entry_height=metrics.entry.height-2*inset_y;
    if(keyboard_message_mode&&value[0])
        ui_draw_compose_tail(value,entry_x,entry_y,entry_width,entry_height,4);
    else
        ui_draw_wrapped_tail(value[0]?value:(keyboard_password_mode?"Enter password":"Enter text"),
                             entry_x,entry_y,entry_width,entry_height,4,0,false);
    const char* numbers="1234567890";
    const auto digits=meshink_keyboard::numbers(metrics);
    for(int i=0;i<10;++i){
        char label[2]={numbers[i],0};
        landscape_key(label,{digits.start+i*digits.pitch,metrics.number_top,digits.width,metrics.key_height});
    }
    const char** rows=active_keyboard_rows();
    for(int r=0;r<3;++r){
        const auto layout=meshink_keyboard::letters(metrics,r,(int)strlen(rows[r]));
        const int y=metrics.letter_top+r*metrics.row_step;
        for(int i=0;rows[r][i];++i){
            char label[2]={rows[r][i],0};
            landscape_key(label,{layout.start+i*layout.pitch,y,layout.width,metrics.key_height});
        }
    }
    landscape_key(keyboard_symbols?"ABC":(keyboard_upper?"abc":"#+="),metrics.mode_key);
    landscape_key("DEL",metrics.delete_key);
    landscape_key("PORTRAIT",metrics.orientation_key);
    landscape_key(mesh_protocol_name_character_allowed(' ')||keyboard_message_mode||keyboard_password_mode?"SPACE":"",metrics.space_key);
    landscape_key(keyboard_password_mode?"LOGIN":(keyboard_message_mode?"SEND":"DONE"),metrics.action_key);
    if(keyboard_message_mode)message_keyboard_case_dirty=false;
}

// Drawing and touch use the same scaled metrics. Gaps are divided at their
// midpoint, so scaling cannot make a visible key type its neighbour.
static bool keyboard_character_at(int x,int y,bool landscape,char& character){
    const auto metrics=keyboard_metrics(landscape);
    if(meshink_keyboard::in_row(y,metrics.number_top,metrics)){
        // No controls flank the number row. Give 1/0 ownership of the blank
        // outer margins too, matching the physical edge tolerance already
        // used by the alphabetic home row.
        const int i=meshink_keyboard::key_index_edge_extended(
            meshink_keyboard::numbers(metrics),x,metrics.width);
        if(i<0)return false;
        character="1234567890"[i];
        return true;
    }
    const char** rows=active_keyboard_rows();
    for(int r=0;r<3;++r){
        const int top=metrics.letter_top+r*metrics.row_step;
        if(!meshink_keyboard::in_row(y,top,metrics))continue;
        const auto layout=meshink_keyboard::letters(
            metrics,r,(int)strlen(rows[r]));
        // The top and home rows have no side controls, so their first/last
        // characters own the unused screen-edge margins. Keep the third row
        // strict because MODE and DEL deliberately own its outer regions.
        const bool owns_screen_edges=r<2;
        const int i=owns_screen_edges
            ? meshink_keyboard::key_index_edge_extended(
                layout,x,metrics.width)
            : meshink_keyboard::key_index(layout,x);
        if(i<0)return false;
        character=rows[r][i];
        return true;
    }
    return false;
}

static void line(int x0,int y0,int x1,int y1,uint8_t color=0) {
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    while(true){meshink_display_draw_pixel(x0,y0,color,fb);if(x0==x1&&y0==y1)break;const int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}

static void draw_status_disc(int cx,int cy,int radius,uint8_t color=0) {
    rounded_fill(cx-radius,cy-radius,radius*2+1,radius*2+1,radius,color);
}
static void draw_status_bold_line(int x0,int y0,int x1,int y1,int radius=2,uint8_t color=0) {
    for(int oy=-radius;oy<=radius;++oy)for(int ox=-radius;ox<=radius;++ox)
        if(ox*ox+oy*oy<=radius*radius)line(x0+ox,y0+oy,x1+ox,y1+oy,color);
    draw_status_disc(x0,y0,radius,color);
    draw_status_disc(x1,y1,radius,color);
}

static void draw_target_icon(int x,int y,bool disabled) {
    // Modern GPS target: rounded four-pixel ring, bold centre and short ticks.
    rounded_fill(x+3,y+3,25,25,12,0);
    rounded_fill(x+7,y+7,17,17,8,0xFF);
    draw_status_disc(x+15,y+15,4,0);
    meshink_display_fill_rect({x+13,y,5,7},0,fb);
    meshink_display_fill_rect({x+13,y+24,5,7},0,fb);
    meshink_display_fill_rect({x,y+13,7,5},0,fb);
    meshink_display_fill_rect({x+24,y+13,7,5},0,fb);
    if(disabled)draw_status_bold_line(x+3,y+3,x+27,y+27,3,0);
}

static void draw_search_icon(int x,int y) {
    // A true circular magnifying glass rather than the old square outline.
    rounded_fill(x+2,y+2,22,22,11,0);
    rounded_fill(x+7,y+7,12,12,6,0xFF);
    draw_status_bold_line(x+20,y+20,x+29,y+29,3,0);
}

static void draw_gps_error_icon(int x,int y) {
    // Warning ring: a working GPS setting with a receiver/backend fault must
    // never be visually confused with ordinary satellite searching.
    rounded_fill(x+3,y+3,25,25,12,0);
    rounded_fill(x+7,y+7,17,17,8,0xFF);
    draw_status_bold_line(x+15,y+8,x+15,y+18,2,0);
    draw_status_disc(x+15,y+24,2,0);
}

static void draw_envelope_icon(int x,int y) {
    // Rounded, heavy envelope silhouette with a bold folded flap.
    rounded_fill(x,y+5,30,22,5,0);
    rounded_fill(x+4,y+9,22,14,2,0xFF);
    draw_status_bold_line(x+4,y+9,x+15,y+18,2,0);
    draw_status_bold_line(x+26,y+9,x+15,y+18,2,0);
}

static void draw_channel_status_icon(int x,int y) {
    // Compact two-person channel/group mark; solid shapes stay legible on DU.
    draw_status_disc(x+9,y+9,5,0);
    draw_status_disc(x+21,y+9,5,0);
    rounded_fill(x+2,y+16,14,11,6,0);
    rounded_fill(x+14,y+16,14,11,6,0);
}

static void draw_standby_envelope_icon(int x,int y) {
    // Large 120x80 envelope for the standby summary card.
    const int w=ui_w(120),h=ui_h(80),stroke=max(ui_w(5),ui_h(5));
    meshink_display_fill_rect({x,y,w,stroke},0,fb);
    meshink_display_fill_rect({x,y+h-stroke,w,stroke},0,fb);
    meshink_display_fill_rect({x,y,stroke,h},0,fb);
    meshink_display_fill_rect({x+w-stroke,y,stroke,h},0,fb);
    const int cx=x+w/2;
    for(int d=-stroke/2;d<=stroke/2;++d) {
        line(x+stroke,y+stroke+d,cx,y+ui_h(44)+d);
        line(x+w-stroke-1,y+stroke+d,cx,y+ui_h(44)+d);
    }
}

static void fill_standby_disc(int cx,int cy,int radius) {
    const int rr=radius*radius;
    for(int dy=-radius;dy<=radius;++dy) {
        int dx=radius;
        while(dx>0&&dx*dx+dy*dy>rr)--dx;
        meshink_display_fill_rect({cx-dx,cy+dy,dx*2+1,1},0,fb);
    }
}

static void draw_standby_channel_icon(int x,int y) {
    // Three-person silhouette matching the standby mockup while remaining
    // entirely within the generic display primitive boundary.
    const int centre_x=x+ui_w(63);
    fill_standby_disc(centre_x,y+ui_h(21),ui_w(18));
    fill_standby_disc(x+ui_w(24),y+ui_h(31),ui_w(13));
    fill_standby_disc(x+ui_w(102),y+ui_h(31),ui_w(13));

    meshink_display_fill_rect({x+ui_w(38),y+ui_h(46),ui_w(50),ui_h(34)},0,fb);
    meshink_display_fill_rect({x+ui_w(31),y+ui_h(59),ui_w(64),ui_h(21)},0,fb);
    meshink_display_fill_rect({x+ui_w(8),y+ui_h(52),ui_w(28),ui_h(28)},0,fb);
    meshink_display_fill_rect({x+ui_w(90),y+ui_h(52),ui_w(28),ui_h(28)},0,fb);
}

static void draw_standby_card(const MeshInkUiRect& rect) {
    rounded_box(rect,max(ui_w(22),ui_h(22)),false);
}

static void standby_centred(const char* value,const MeshInkUiRect& rect,int y,int scale,bool bold=true) {
    const int width=ui_text_width(value,scale);
    ui_text(value,rect.x+(rect.width-width)/2,y,scale,0,bold);
}

static void draw_battery_icon(int x,int y,int level=-1) {
    meshink_display_draw_rect({x,y+6,31,18},0,fb);meshink_display_fill_rect({x+31,y+11,4,8},0,fb);
    if(level<0)level=status_battery;
    if(level>0){
        const int fill=(level*27)/100;
        meshink_display_fill_rect({x+2,y+8,fill,14},0,fb);
    }
    if(meshink_power_is_charging(status_charge_state)){
        meshink_display_fill_rect({x+10,y+6,14,17},0xFF,fb);
        // Wide, bold lightning bolt for the low-resolution status bar.
        for(int d=-2;d<=2;++d){line(x+22+d,y+5,x+12+d,y+16);line(x+12+d,y+16,x+20+d,y+16);line(x+20+d,y+16,x+10+d,y+27);}
    }
}

static void draw_status_bar() {
    MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-status-draw");
    const MeshInkUiLayout& layout=portrait_layout();
    const int status_height=layout.status_height;
    meshink_display_fill_rect({0,0,layout.width,status_height},0xFF,fb);
    meshink_display_draw_rect({0,0,layout.width,status_height},0,fb);
    auto compact_count=[](uint16_t value,char out[5]){
        if(value>99)strcpy(out,"99+");
        else snprintf(out,5,"%u",(unsigned)value);
    };
    int left=ui_x(6);
    if(meshink_board_has_gps()){
        if(!status_gps_enabled)draw_target_icon(ui_x(6),ui_y(9),true);
        else if(status_gps_error!=MeshInkGpsError::None)draw_gps_error_icon(ui_x(6),ui_y(9));
        else if(status_gps_fix)draw_target_icon(ui_x(6),ui_y(9),false);
        else draw_search_icon(ui_x(6),ui_y(9));
        left=ui_x(46);
        // All numeric status values use the same primary face/size as the
        // clock and battery percentage. If both unread classes are present,
        // reserve the left half for their two counters and keep only the GPS
        // state icon so the enlarged numbers cannot collide with the clock.
        if(!standby_active&&status_gps_enabled&&status_gps_error==MeshInkGpsError::None&&status_gps_fix&&
           !(status_unread&&status_channel_unread)) {
            char satellites[4];
            snprintf(satellites,sizeof(satellites),"%d",max(0,min(99,(int)status_gps_satellites_bar)));
            text(satellites,ui_x(43),ui_y(13),3,0,true);
            left=ui_x(43)+ui_text_width(satellites,3)+ui_w(8);
        }
    }
    if(status_unread){
        draw_envelope_icon(left,ui_y(9));left+=ui_w(36);
        char count[5];compact_count(status_unread,count);
        text(count,left,ui_y(13),3,0,true);
        left+=ui_text_width(count,3)+ui_w(6);
    }
    if(status_channel_unread){
        draw_channel_status_icon(left,ui_y(9));left+=ui_w(34);
        char count[5];compact_count(status_channel_unread,count);
        text(count,left,ui_y(13),3,0,true);
    }
    char clock_text[8];
    if(status_hour>=0&&status_minute>=0){
        const int safe_hour=max(0,min(23,(int)status_hour));
        const int safe_minute=max(0,min(59,(int)status_minute));
        snprintf(clock_text,sizeof(clock_text),"%02d:%02d",safe_hour,safe_minute);
    }else snprintf(clock_text,sizeof(clock_text),"--:--");
    centred(clock_text,ui_y(13),3,0,true);
    char battery[8];
    if(status_battery>=0)snprintf(battery,sizeof(battery),"%d%%",status_battery);
    else snprintf(battery,sizeof(battery),"--%%");
    const int battery_x=layout.width-ui_w(10)-(int)strlen(battery)*18;
    draw_battery_icon(battery_x-ui_w(43),ui_y(8),status_battery);
    text(battery,battery_x,ui_y(13),3,0,true);
    const int16_t painted_minute=(status_hour>=0&&status_minute>=0)
        ?(int16_t)(status_hour*60+status_minute):-1;
    status_bar_painted_minute=painted_minute;
    status_bar_painted_slot=painted_minute>=0?(int16_t)(painted_minute/5):-1;
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] status-bar clock=%02d:%02d battery=%d%% direct=%u channel=%u gps=%s\n",
        status_hour,status_minute,status_battery,status_unread,status_channel_unread,
        status_gps_enabled?(status_gps_error!=MeshInkGpsError::None?"error":(status_gps_fix?"fix":"searching")):"off");
}

// Share the same small black notification style between ordinary settings
// toasts and the synchronous Maps loading message (which has no timeout).
static MeshInkRect toast_message_rect(const char* message) {
    const int scale=3;
    const int natural=ui_text_width(message,scale)+ui_w(48);
    const int w=min(portrait_layout().width-ui_w(24),max(ui_w(300),natural));
    return {(portrait_layout().width-w)/2,ui_y(640),w,ui_h(72)};
}
static void draw_toast_message(const char* message) {
    MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-toast-draw");
    const int scale=3,r=ui_w(12);
    const MeshInkRect rect=toast_message_rect(message);
    rounded_fill(rect.x,rect.y,rect.width,rect.height,r,0);
    ui_centred_fit(message,rect.y+ui_h(25),rect.width-ui_w(24),scale,0xFF,true);
}
static void draw_toast() {
    if(toast_visible)draw_toast_message(toast_message);
}

static void show_toast(const char* message) {
    strncpy(toast_message,message,sizeof(toast_message)-1);toast_message[sizeof(toast_message)-1]=0;
    toast_visible=true;toast_until=millis()+1500;
}

// First-run setup now uses the same step-by-step screens for both protocols.
// The former single-page welcome form has been removed.

static void draw_presets() {
    meshink_display_set_all_white(&display);
    draw_status_bar();
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect back_rect=meshink_preset_back_rect(layout);
    ui_action_button("< BACK",back_rect,false);
    ui_centred("RADIO PRESETS",ui_y(92),4,0,true);
    const int first=preset_page*PRESETS_PER_PAGE;
    for (int row=0; row<PRESETS_PER_PAGE; ++row) {
        const int index=first+row; if(index>=PRESET_COUNT) break;
        const MeshInkUiRect row_rect=meshink_preset_row_rect(layout,row);
        rounded_box(row_rect,max(ui_w(13),ui_h(13)),index==selected_preset);
        const uint8_t color=index==selected_preset?0xFF:0;
        ui_text_fit(PRESETS[index].title,layout.content_text_x,row_rect.y+ui_h(11),
                    row_rect.width-ui_w(32),3,color,true);
        const int detail_width=row_rect.width-ui_w(32);
        const int detail_scale=ui_text_width(PRESETS[index].detail,3)<=detail_width?3:2;
        ui_text_fit(PRESETS[index].detail,layout.content_text_x,row_rect.y+ui_h(57),
                    detail_width,detail_scale,color,false);
    }
    const MeshInkUiRect prev_rect=meshink_preset_prev_rect(layout);
    const MeshInkUiRect next_rect=meshink_preset_next_rect(layout);
    ui_action_button("PREV",prev_rect,preset_page==0);
    const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
    ui_action_button("NEXT",next_rect,preset_page+1>=page_count);
    char page_text[20];snprintf(page_text,sizeof(page_text),"PAGE %u OF %u",preset_page+1,page_count);
    ui_centred(page_text,ui_y(890),2,0,true);
}

static void draw_companion_confirm() {
    meshink_display_set_all_white(&display);draw_status_bar();
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect cancel=meshink_confirm_left_rect(layout,500);
    const MeshInkUiRect start=meshink_confirm_right_rect(layout,500);
    ui_centred("BLUETOOTH",ui_y(120),5,0,true);ui_centred("COMPANION MODE",ui_y(180),4,0,true);
    ui_centred("THE LOCAL UI WILL CLOSE",ui_y(300),2);ui_centred("UNTIL THE DEVICE RESTARTS",ui_y(335),2);
    ui_action_button("CANCEL",cancel,false);
    ui_action_button("START",start,true);
}

static void draw_shutdown_confirm() {
    meshink_display_set_all_white(&display);draw_status_bar();
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect cancel=meshink_confirm_left_rect(layout,650);
    const MeshInkUiRect shutdown=meshink_confirm_right_rect(layout,650);
    ui_centred("SHUT DOWN",ui_y(120),5,0,true);
    ui_centred("FULL BATTERY POWER CUT",ui_y(245),3,0,true);
    ui_centred("THE DEVICE WILL STOP",ui_y(305),3,0,true);
    ui_centred("RECEIVING MESSAGES",ui_y(350),3,0,true);
    const MeshInkPowerWakeInfo& wake=meshink_power_wake_info();
    ui_centred_fit(wake.confirm_battery,ui_y(445),layout.width-ui_w(32),3,0,true);
    ui_centred_fit(wake.confirm_external,ui_y(500),layout.width-ui_w(32),3,0,true);
    ui_action_button("CANCEL",cancel,false);
    ui_action_button("SHUT DOWN",shutdown,true);
}

static void draw_bottom_nav(int selected) {
    static const char* labels[]={"CONTACTS","CHANNELS","MAPS","MORE"};
    const MeshInkUiLayout& layout=portrait_layout();
    meshink_display_fill_rect({0,layout.bottom_nav_top,layout.width,layout.bottom_nav_height},0xFF,fb);
    meshink_display_fill_rect({0,layout.bottom_nav_top,layout.width,ui_h(2)},0,fb);
    for(int i=0;i<4;++i){
        const int left=i*layout.tab_width;
        const MeshInkUiRect tab={left+ui_w(5),layout.bottom_nav_top+ui_h(7),
                                 layout.tab_width-ui_w(10),layout.bottom_nav_height-ui_h(13)};
        const bool active=i==selected;
        if(active)rounded_box(tab,max(ui_w(11),ui_h(11)),true);
        const uint8_t color=active?0xFF:0;
        const int width=ui_text_width(labels[i],2);
        ui_text(labels[i],left+(layout.tab_width-width)/2,
                layout.bottom_nav_top+ui_h(21),2,color,true);
        const bool unread=(i==0&&status_unread)||(i==1&&status_channel_unread);
        if(unread){
            const int d=ui_w(10);
            rounded_fill(left+layout.tab_width-ui_w(18),layout.bottom_nav_top+ui_h(10),
                         d,d,d/2,color);
        }
    }
}

static void draw_app_header(const char* title,bool back=false,const char* action=nullptr) {
    meshink_display_set_all_white(&display);draw_status_bar();
    const MeshInkUiLayout& layout=portrait_layout();
    if(back){
        const MeshInkUiRect back_rect=meshink_header_back_rect(layout);
        rounded_box(back_rect,max(ui_w(10),ui_h(10)),true);
        ui_text("<",back_rect.x+ui_w(19),layout.header_text_y,3,0xFF,true);
    }
    const int title_guard=back
        ?2*(layout.header_back_x+layout.header_button_width+ui_w(12))
        :2*ui_w(24);
    ui_centred_fit(title,layout.header_title_y,layout.width-title_guard,4,0,true);
    if(action){
        const MeshInkUiRect action_rect=meshink_header_action_rect(layout);
        ui_action_button(action,action_rect,true);
    }
}

static const char* node_role_label(UiNodeRole role){
    switch(role){
        case UiNodeRole::Client:return "CLIENT";
        case UiNodeRole::Relay:return "RELAY";
        case UiNodeRole::Service:return "SERVICE";
        case UiNodeRole::Sensor:return "SENSOR";
        default:return "UNKNOWN";
    }
}
static void thick_line(int x1,int y1,int x2,int y2){
    for(int d=-1;d<=1;++d){line(x1+d,y1,x2+d,y2);line(x1,y1+d,x2,y2+d);}
}
static void thick_rect(int x,int y,int w,int h){
    for(int d=0;d<3;++d)meshink_display_draw_rect({x+d,y+d,w-2*d,h-2*d},0,fb);
}
static void draw_node_role_icon(UiNodeRole role,int x,int y){
    if(role==UiNodeRole::Client){thick_rect(x,y+3,28,20);thick_line(x+6,y+23,x+3,y+29);thick_line(x+6,y+23,x+12,y+23);}
    else if(role==UiNodeRole::Relay){
        // Relay node: tapered mast plus two signal arcs on each side.
        meshink_display_fill_rect({x+12,y+5,5,6},0,fb);
        thick_line(x+14,y+8,x+8,y+30);thick_line(x+14,y+8,x+20,y+30);
        thick_line(x+8,y+30,x+20,y+30);line(x+10,y+22,x+18,y+22);line(x+11,y+17,x+17,y+17);
        thick_line(x+10,y+7,x+6,y+10);thick_line(x+6,y+10,x+6,y+16);thick_line(x+6,y+16,x+10,y+19);
        thick_line(x+18,y+7,x+22,y+10);thick_line(x+22,y+10,x+22,y+16);thick_line(x+22,y+16,x+18,y+19);
        thick_line(x+6,y+4,x+1,y+8);thick_line(x+1,y+8,x+1,y+18);thick_line(x+1,y+18,x+6,y+22);
        thick_line(x+22,y+4,x+27,y+8);thick_line(x+27,y+8,x+27,y+18);thick_line(x+27,y+18,x+22,y+22);
    }
    else if(role==UiNodeRole::Service){
        // Service node: simple house silhouette with one window and door.
        thick_line(x+2,y+14,x+14,y+3);thick_line(x+14,y+3,x+26,y+14);
        thick_line(x+5,y+12,x+5,y+31);thick_line(x+23,y+12,x+23,y+31);
        thick_line(x+5,y+31,x+23,y+31);
        meshink_display_fill_rect({x+9,y+16,5,5},0,fb);
        thick_rect(x+15,y+21,6,10);
    }
    else if(role==UiNodeRole::Sensor){thick_rect(x+2,y+5,25,23);meshink_display_fill_rect({x+12,y+10,6,6},0,fb);thick_line(x+14,y+15,x+7,y+23);thick_line(x+14,y+15,x+22,y+20);}
    else {thick_rect(x+2,y+3,25,27);ui_text("?",x+8,y+8,2,0,true);}
}
static void draw_list_entry(const UiListEntry& item,int y,int subtitle_scale=3) {
    const MeshInkUiLayout& layout=portrait_layout();
    rounded_box(layout.outer_margin,y,layout.outer_width,layout.list_row_height,
                max(ui_w(12),ui_h(12)));
    const bool typed=item.role!=UiNodeRole::Unknown;
    const int title_x=typed?layout.content_text_x+ui_w(42):layout.content_text_x;
    if(typed)draw_node_role_icon(item.role,layout.content_text_x,y+ui_h(15));

    const int time_width=ui_text_width(item.time,2);
    const int time_x=layout.content_right-layout.text_inset-time_width;
    ui_text(item.time,time_x,y+ui_h(19),2,0,true);
    ui_text_fit(item.title,title_x,y+ui_h(15),
                max(ui_w(120),time_x-title_x-ui_w(16)),3,0,true);

    // Body copy is deliberately scale 3 on the 540 px T5. Proportional
    // advances keep two useful lines in the existing 142 px row without
    // shrinking the text back to the old small metadata size.
    ui_draw_wrapped(item.subtitle,layout.content_text_x,y+ui_h(57),
                    layout.outer_width-2*layout.text_inset,subtitle_scale,0,false,2);

    if(item.unread){
        char unread[5];snprintf(unread,sizeof(unread),"%u",(unsigned)item.unread);
        const int badge_h=ui_h(26);
        const int badge_w=max(ui_w(34),ui_text_width(unread,2)+ui_w(18));
        const int badge_x=layout.content_right-layout.text_inset-badge_w;
        const int badge_y=y+layout.list_row_height-ui_h(32);
        rounded_box(badge_x,badge_y,badge_w,badge_h,badge_h/2,true);
        ui_text(unread,badge_x+(badge_w-ui_text_width(unread,2))/2,
                badge_y+ui_h(6),2,0xFF,true);
    }
}

static size_t list_page_count(size_t count) {
    return count ? (count+LIST_ITEMS_PER_PAGE-1)/LIST_ITEMS_PER_PAGE : 1;
}

static void clamp_list_page(size_t& page,size_t count) {
    const size_t pages=list_page_count(count);
    if(page>=pages)page=pages-1;
}

static void draw_page_arrow(int centre_x,int centre_y,bool up) {
    // A compact swipe-direction hint beside the page counter. Three-pixel
    // strokes remain legible on e-paper without consuming another row.
    meshink_display_fill_rect({centre_x-1,centre_y-7,3,15},0,fb);
    const int tip_y=up?centre_y-9:centre_y+9;
    const int wing_y=up?centre_y-2:centre_y+2;
    for(int d=-1;d<=1;++d){
        line(centre_x,tip_y+d,centre_x-7,wing_y+d);
        line(centre_x,tip_y+d,centre_x+7,wing_y+d);
    }
}

static void draw_page_indicator(size_t page,size_t pages,int y) {
    if(pages<=1)return;
    char page_text[24];
    snprintf(page_text,sizeof(page_text),"PAGE %u OF %u",(unsigned)(page+1),(unsigned)pages);
    const int text_width=ui_text_width(page_text,2);
    const int text_left=(meshink_display_logical_width()-text_width)/2;
    ui_centred(page_text,y,2,0,true);
    if(page>0)draw_page_arrow(text_left-ui_w(24),y+ui_h(7),false);
    if(page+1<pages)draw_page_arrow(text_left+text_width+ui_w(24),y+ui_h(7),true);
}

static void draw_list_page_footer(size_t page,size_t count) {
    draw_page_indicator(page,list_page_count(count),portrait_layout().list_footer_y);
}

static void draw_contacts() {
    draw_app_header("CONTACTS");
    // The model is fully populated before ui_use_data_provider() attaches it.
    // A missing provider means STARTUP, not a completed empty contact list.
    if(!ui_data)ui_centred("LOADING CONTACT INFO..",ui_y(300),3,0,true);
    else {
        const size_t count=ui_data->contact_count();
        clamp_list_page(contacts_page,count);
        if(!count)ui_centred("NO SAVED CONTACTS",ui_y(300),3,0,true);
        else {
            const size_t first=contacts_page*LIST_ITEMS_PER_PAGE;
            for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row)
                draw_list_entry(ui_data->contact(first+row),portrait_layout().list_top+row*portrait_layout().list_row_stride,2);
            draw_list_page_footer(contacts_page,count);
        }
    }
    draw_bottom_nav(0);
}

static void draw_channels() {
    draw_app_header("CHANNELS",false,"MANAGE");
    if(!ui_data)ui_centred("NO CONFIGURED CHANNELS",ui_y(300),3,0,true);
    else {
        const size_t count=ui_data->channel_count();
        clamp_list_page(channels_page,count);
        if(!count)ui_centred("NO CONFIGURED CHANNELS",ui_y(300),3,0,true);
        else {
            const size_t first=channels_page*LIST_ITEMS_PER_PAGE;
            for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row)
                draw_list_entry(ui_data->channel(first+row),portrait_layout().list_top+row*portrait_layout().list_row_stride,2);
            draw_list_page_footer(channels_page,count);
        }
    }
    draw_bottom_nav(1);
}

// Channel management stays protocol-neutral; Leaf exposes a safe UI preview.
static void draw_channel_manage() {
    draw_app_header("MANAGE CHANNELS",true,"ADD");
    if(!ui_data){ui_centred("CHANNELS UNAVAILABLE",ui_y(310),3,0,true);return;}
    const size_t count=ui_data->channel_count();
    clamp_list_page(channel_manage_page,count);
    if(!count)ui_centred("NO CHANNELS CONFIGURED",ui_y(300),3,0,true);
    else{
        const size_t first=channel_manage_page*LIST_ITEMS_PER_PAGE;
        for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row)
            draw_list_entry(ui_data->channel(first+row),
                portrait_layout().list_top+row*portrait_layout().list_row_stride,2);
        draw_list_page_footer(channel_manage_page,count);
    }
    if(!ui_data->channel_management_available())
        ui_centred("SECONDARY CHANNELS COMING SOON",ui_y(796),2,0,true);
}

static void draw_channel_create() {
    draw_app_header("ADD CHANNEL",true,"ADD");
    const auto name_rect=ui_rect(22,162,496,82);
    const auto key_rect=ui_rect(22,303,496,82);
    ui_text("CHANNEL NAME",ui_x(26),ui_y(128),2,0,true);
    rounded_box(name_rect,max(ui_w(12),ui_h(12)),!channel_form_key_field&&keyboard_visible);
    ui_text_fit(channel_form_name[0]?channel_form_name:"Tap to enter name",
                name_rect.x+ui_w(15),name_rect.y+ui_h(20),name_rect.width-ui_w(30),3,
                !channel_form_key_field&&keyboard_visible?0xFF:0,true);
    ui_text("PRIVATE KEY (32 HEX DIGITS)",ui_x(26),ui_y(268),2,0,true);
    rounded_box(key_rect,max(ui_w(12),ui_h(12)),channel_form_key_field&&keyboard_visible);
    char private_label[36]{};
    if(channel_form_key_hex[0])
        snprintf(private_label,sizeof(private_label),"KEY ENTERED: %u DIGITS",(unsigned)strlen(channel_form_key_hex));
    else strcpy(private_label,"Generate a random key");
    ui_text_fit(private_label,key_rect.x+ui_w(15),key_rect.y+ui_h(20),key_rect.width-ui_w(30),3,
                channel_form_key_field&&keyboard_visible?0xFF:0,true);
    ui_centred("LEAVE KEY BLANK FOR A NEW PRIVATE KEY",ui_y(417),2,0,false);
    if(ui_data&&!ui_data->channel_management_available())
        ui_centred("MESHTASTIC: COMING SOON",ui_y(465),2,0,true);
    if(keyboard_visible)draw_keyboard();
    else ui_action_button("SHOW KEYBOARD",ui_rect(55,520,430,70),false);
}

static void draw_channel_delete() {
    draw_app_header("REMOVE CHANNEL",true);
    ui_centred("REMOVE THIS CHANNEL?",ui_y(188),4,0,true);
    ui_centred_fit(channel_delete_title,ui_y(268),portrait_layout().section_width,3,0,true);
    ui_centred("WARNING: CHANNEL KEY WILL BE LOST",ui_y(360),2,0,true);
    ui_draw_wrapped("Removing this channel deletes its key from MeshInk. "
                    "You cannot rejoin without the exact same key. "
                    "This cannot be undone.",
                    portrait_layout().section_margin,ui_y(405),
                    portrait_layout().section_width,3,0,false,5);
    if(ui_data&&!ui_data->channel_removable(channel_delete_index))
        ui_centred("DEFAULT CHANNEL CANNOT BE REMOVED",ui_y(594),2,0,true);
    ui_action_button("CANCEL",meshink_confirm_left_rect(portrait_layout(),650),false);
    ui_action_button("YES, REMOVE",meshink_confirm_right_rect(portrait_layout(),650),true);
}

// Bold radio-tower marker for repeaters. Keep it deliberately simple so the
// symbol survives DU refresh and remains obvious when scanning coverage.
static void draw_map_repeater_marker(int x,int y) {
    meshink_display_fill_rect({x-13,y-14,27,29},0xFF,fb);
    // Heavy mast / tripod.
    meshink_display_fill_rect({x-2,y-8,5,15},0,fb);
    thick_line(x,y-5,x-7,y+11);thick_line(x,y-5,x+7,y+11);
    meshink_display_fill_rect({x-8,y+9,17,4},0,fb);
    // Two bold broadcast arcs per side, with a deliberate white gap around
    // the tower legs/base so the radio waves remain visually separate.
    thick_line(x-5,y-7,x-9,y-4);thick_line(x-9,y-4,x-9,y+1);thick_line(x-9,y+1,x-8,y+3);
    thick_line(x+5,y-7,x+9,y-4);thick_line(x+9,y-4,x+9,y+1);thick_line(x+9,y+1,x+8,y+3);
    thick_line(x-9,y-10,x-12,y-7);thick_line(x-12,y-7,x-12,y+3);thick_line(x-12,y+3,x-10,y+6);
    thick_line(x+9,y-10,x+12,y-7);thick_line(x+12,y-7,x+12,y+3);thick_line(x+12,y+3,x+10,y+6);
}

static bool project_device_on_map(long latitude,long longitude,int& sx,int& sy);

// Node positions are stable; labels are ranked and moved around them.  Keep the
// solver intentionally bounded: at most 50 markers and 12 candidates per label.
static void draw_map_nodes() {
    map_marker_hit_count=0;
    if(!ui_data)return;
    const MeshInkUiLayout& layout=portrait_layout();
    const double world=256.0*(1U<<map_zoom);
    const double centre_x=(map_longitude+180.0)/360.0*world;
    const double rad=map_latitude*PI/180.0;
    const double centre_y=(1.0-log(tan(rad)+1.0/cos(rad))/PI)*world/2.0;
    size_t count=0;
    for(size_t i=0;i<ui_data->map_node_count()&&count<50;++i) {
        UiMapNode node{};if(!ui_data->map_node(i,node))continue;
        const double x=(node.longitude/1000000.0+180.0)/360.0*world;
        double delta_x=x-centre_x;
        if(delta_x>world/2)delta_x-=world;
        if(delta_x<-world/2)delta_x+=world;
        const double lat=node.latitude/1000000.0,r=lat*PI/180.0;
        const double y=(1.0-log(tan(r)+1.0/cos(r))/PI)*world/2.0;
        const int sx=(int)lround(map_centre_x()+delta_x),sy=(int)lround(map_centre_y()+y-centre_y);
        if(sx<11||sx>layout.width-11||sy<map_top()+11||sy>map_bottom()-11)continue;
        map_marker_hits[count++]={(int16_t)sx,(int16_t)sy,i,node.role};
    }

    struct Bounds {int16_t x,y,w,h;};
    struct RankedLabel {uint8_t marker;int32_t score;};
    Bounds occupied[50]{};size_t occupied_count=0;
    RankedLabel ranked[50]{};
    const uint32_t now=(uint32_t)time(nullptr);

    // Requested GPS positions and repeaters matter most.  Recency and screen
    // centre then make the remaining greedy choices deterministic and useful.
    for(size_t i=0;i<count;++i) {
        const auto& marker=map_marker_hits[i];
        UiMapNode node{};if(!ui_data->map_node(marker.index,node))continue;
        int32_t score=0;
        if(node.gps_from_reply)score+=4000000;
        if(node.role==UiNodeRole::Relay)score+=3000000;
        if(node.advertised_at&&now>=node.advertised_at) {
            const uint32_t age=min((uint32_t)1000000,now-node.advertised_at);
            score+=(int32_t)(1000000-age);
        }
        const int centre_distance=abs(marker.x-map_centre_x())+abs(marker.y-map_centre_y());
        score+=max(0,100000-centre_distance*100);
        ranked[i]={(uint8_t)i,score};
    }
    for(size_t i=1;i<count;++i) {
        const RankedLabel key=ranked[i];size_t j=i;
        while(j&&ranked[j-1].score<key.score){ranked[j]=ranked[j-1];--j;}
        ranked[j]=key;
    }

    // Deliberately thin labels as the viewport covers more territory. Important
    // GPS/repeater labels may exceed the normal budget when collision-free.
    const size_t label_budget=map_zoom<=7?6:map_zoom<=9?10:map_zoom<=11?16:map_zoom<=13?24:40;
    const bool compact_labels=map_zoom<=10;
    size_t labels_drawn=0;
    const int label_bottom=ui_y(766);

    // Reserve the exact own-location bullseye before solving labels. The marker
    // is drawn after labels, so without this reservation it can erase text.
    int own_marker_x=0,own_marker_y=0;
    bool own_marker_reserved=false;
    long own_latitude=0,own_longitude=0;bool own_current_fix=false;
    if(map_device_position(own_latitude,own_longitude,own_current_fix)&&
       project_device_on_map(own_latitude,own_longitude,own_marker_x,own_marker_y)&&
       own_marker_x>=20&&own_marker_x<=layout.width-20&&
       own_marker_y>=map_top()+20&&own_marker_y<=map_bottom()-21)
        own_marker_reserved=true;

    for(size_t order=0;order<count;++order) {
        const auto& n=map_marker_hits[ranked[order].marker];
        UiMapNode node{};if(!ui_data->map_node(n.index,node))continue;
        const bool important=node.gps_from_reply||node.role==UiNodeRole::Relay;
        if(labels_drawn>=label_budget&&!important)continue;

        char short_name[19]{};strncpy(short_name,node.name,sizeof(short_name)-1);
        char age[16]{};
        if(node.gps_from_reply){
            const uint32_t seconds=(uint32_t)(millis()-node.gps_received_millis)/1000U;
            if(seconds<3600)snprintf(age,sizeof(age),"GPS %lum",(unsigned long)(seconds/60));
            else if(seconds<86400)snprintf(age,sizeof(age),"GPS %luh",(unsigned long)(seconds/3600));
            else snprintf(age,sizeof(age),"GPS %lud",(unsigned long)(seconds/86400));
        }else if(!node.advertised_at||now<node.advertised_at)strcpy(age,"ADV ?");
        else {
            const uint32_t seconds=now-node.advertised_at;
            if(seconds<3600)snprintf(age,sizeof(age),"ADV %lum",(unsigned long)(seconds/60));
            else if(seconds<86400)snprintf(age,sizeof(age),"ADV %luh",(unsigned long)(seconds/3600));
            else snprintf(age,sizeof(age),"ADV %lud",(unsigned long)(seconds/86400));
        }
        const int h=compact_labels?18:34;
        const int text_w=compact_labels?ui_text_width(short_name,2):
            max(ui_text_width(short_name,2),ui_text_width(age,2));
        const int w=min(230,max(48,text_w+8));

        int best_x=0,best_y=0,best_cost=0x7fffffff;
        for(uint8_t candidate=0;candidate<12;++candidate) {
            int x=0,y=0;
            switch(candidate){
                case 0:x=n.x+14;y=n.y-h/2;break;
                case 1:x=n.x-14-w;y=n.y-h/2;break;
                case 2:x=n.x-w/2;y=n.y-14-h;break;
                case 3:x=n.x-w/2;y=n.y+14;break;
                case 4:x=n.x+12;y=n.y-12-h;break;
                case 5:x=n.x-12-w;y=n.y-12-h;break;
                case 6:x=n.x+12;y=n.y+12;break;
                case 7:x=n.x-12-w;y=n.y+12;break;
                case 8:x=n.x+28;y=n.y-h/2;break;
                case 9:x=n.x-28-w;y=n.y-h/2;break;
                case 10:x=n.x-w/2;y=n.y-28-h;break;
                default:x=n.x-w/2;y=n.y+28;break;
            }
            if(x<3||x+w>layout.width-3||y<map_top()+3||y+h>label_bottom)continue;

            bool overlap=false;
            for(size_t control=0;control<3&&!overlap;++control){
                const MeshInkUiRect r=meshink_map_control_rect(layout,(int)control);
                if(x<r.x+r.width+5&&x+w+5>r.x&&y<r.y+r.height+5&&y+h+5>r.y)overlap=true;
            }
            // Protect every true node position, not just labels already placed.
            for(size_t j=0;j<count&&!overlap;++j){
                const auto& m=map_marker_hits[j];
                const int radius=m.role==UiNodeRole::Relay?14:9;
                if(x<m.x+radius+3&&x+w>m.x-radius-3&&
                   y<m.y+radius+3&&y+h>m.y-radius-3)overlap=true;
            }
            // The own-location bullseye uses a 35x35 white backing. Give it a
            // few extra pixels so text and leader lines stay visually separate.
            if(!overlap&&own_marker_reserved&&
               x<own_marker_x+22&&x+w>own_marker_x-22&&
               y<own_marker_y+22&&y+h>own_marker_y-22)
                overlap=true;
            for(size_t j=0;j<occupied_count&&!overlap;++j)
                if(x<occupied[j].x+occupied[j].w+5&&x+w+5>occupied[j].x&&
                   y<occupied[j].y+occupied[j].h+4&&y+h+4>occupied[j].y)
                    overlap=true;
            if(overlap)continue;

            int cost=(candidate<8?candidate:20+candidate)*20;
            const int edge=min(min(x,layout.width-(x+w)),
                               min(y-map_top(),label_bottom-(y+h)));
            if(edge<12)cost+=(12-edge)*8;
            if(cost<best_cost){best_cost=cost;best_x=x;best_y=y;}
        }
        if(best_cost==0x7fffffff)continue;

        // A short leader keeps displaced labels visually tied to their marker.
        const int target_x=max(best_x,min((int)n.x,best_x+w-1));
        const int target_y=max(best_y,min((int)n.y,best_y+h-1));
        thick_line(n.x,n.y,target_x,target_y);
        meshink_display_fill_rect({best_x,best_y,w,h},0xFF,fb);
        ui_text_fit(short_name,best_x+4,best_y+2,w-ui_w(8),2,0,true);
        if(!compact_labels)ui_text_fit(age,best_x+4,best_y+18,w-ui_w(8),2,0,true);
        occupied[occupied_count++]={(int16_t)best_x,(int16_t)best_y,(int16_t)w,(int16_t)h};
        ++labels_drawn;
    }

    // Draw true positions last. Ordinary nodes remain dots; repeaters get the
    // radio-tower glyph so they stand out immediately when assessing coverage.
    for(size_t i=0;i<count;++i) {
        const auto& n=map_marker_hits[i];
        if(n.role==UiNodeRole::Relay){
            draw_map_repeater_marker(n.x,n.y);
            continue;
        }
        meshink_display_fill_rect({n.x-6,n.y-6,13,13},0xFF,fb);
        for(int dy=-4;dy<=4;++dy) {
            const int half=abs(dy)==4?1:abs(dy)==3?3:4;
            meshink_display_fill_rect({n.x-half,n.y+dy,half*2+1,1},0,fb);
        }
    }
    map_marker_hit_count=count;
}
// Convert a GPS position to the same screen projection as map node markers.
static bool project_device_on_map(long latitude,long longitude,int& sx,int& sy) {
    if(latitude<-85051100L||latitude>85051100L||
       longitude<-180000000L||longitude>180000000L)return false;
    const double world=256.0*(1U<<map_zoom);
    const double centre_x=(map_longitude+180.0)/360.0*world;
    const double centre_rad=map_latitude*PI/180.0;
    const double centre_y=(1.0-log(tan(centre_rad)+1.0/cos(centre_rad))/PI)*world/2.0;
    const double x=(longitude/1000000.0+180.0)/360.0*world;
    double delta_x=x-centre_x;
    if(delta_x>world/2)delta_x-=world;
    if(delta_x<-world/2)delta_x+=world;
    const double r=latitude/1000000.0*PI/180.0;
    const double y=(1.0-log(tan(r)+1.0/cos(r))/PI)*world/2.0;
    sx=(int)lround(map_centre_x()+delta_x);
    sy=(int)lround(map_centre_y()+y-centre_y);
    return true;
}

// Reuse the previous centre bullseye as a true, label-free device marker.
// Do not display a misleading own-position marker without a valid GPS fix.
static void draw_device_location_marker() {
    map_device_marker_visible=false;
    long latitude=0,longitude=0;bool current_fix=false;
    if(!map_device_position(latitude,longitude,current_fix))return;
    int sx=0,sy=0;
    if(!project_device_on_map(latitude,longitude,sx,sy)||
       sx<20||sx>portrait_layout().width-20||sy<map_top()+20||sy>map_bottom()-21)return;
    // Same bold 30x30 crosshair as the GPS-fix status icon, at real
    // coordinates (not an always-centred marker). White backing stays
    // legible on dark map tiles; the icon works for last-known fixes too.
    meshink_display_fill_rect({sx-17,sy-17,35,35},0xFF,fb);
    draw_target_icon(sx-15,sy-15,false);
    map_device_marker_x=sx;map_device_marker_y=sy;
    map_device_marker_visible=true;
}

static void pan_map_by_pixels(int dx,int dy) {
    const double world=256.0*(1U<<map_zoom);
    double x=(map_longitude+180.0)/360.0*world-dx;
    x=fmod(fmod(x,world)+world,world);
    const double r=max(-85.0511,min(85.0511,map_latitude))*PI/180.0;
    const double cy=(1.0-log(tan(r)+1.0/cos(r))/PI)*world/2.0;
    const double y=max(0.0,min(world,cy-dy));
    map_longitude=x/world*360.0-180.0;
    map_latitude=atan(sinh(PI*(1.0-2.0*y/world)))*180.0/PI;
}
static void draw_maps() {
    const MeshInkUiLayout& layout=portrait_layout();
    const bool media_ready=map_tiles_media_ready();
    const uint32_t media_epoch=map_tiles_media_epoch();
    if(!media_ready||map_base_media_epoch!=media_epoch) {
        map_base_valid=false;
        map_base_media_epoch=media_epoch;
    }
    MapRenderResult result{};
    result.sd_ready=media_ready;
    result.min_source_zoom=map_zoom;
    result.max_source_zoom=map_zoom;
    if(media_ready&&map_cache_hit()) {
        result=map_base_result;
        memcpy(fb,map_base_cache,map_base_bytes);
        draw_status_bar();
    } else {
        meshink_display_set_all_white(&display);
        draw_status_bar();
        result=map_tiles_render(fb,0,map_top(),layout.width,map_bottom()-map_top(),
                                map_latitude,map_longitude,map_zoom);
        if(!result.sd_ready||map_base_media_epoch!=map_tiles_media_epoch()) {
            map_base_valid=false;
            map_base_media_epoch=map_tiles_media_epoch();
            if(!result.sd_ready)
                meshink_display_fill_rect({0,map_top(),layout.width,map_bottom()-map_top()},0xFF,fb);
        }
        if(result.sd_ready&&result.tiles) {
            const size_t bytes=meshink_display_framebuffer_bytes();
            if(!map_base_cache)map_base_cache=(uint8_t*)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(map_base_cache) {
                memcpy(map_base_cache,fb,bytes);map_base_bytes=bytes;
                map_base_lat=map_latitude;map_base_lon=map_longitude;map_base_zoom=map_zoom;map_base_result=result;map_base_valid=true;
                T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] cached %u bytes (%u tiles)\n",(unsigned)bytes,result.tiles);
            } else T5_DEBUGLN(T5_LOG_MAP,"[T5-MAP] PSRAM unavailable; uncached rendering");
        }
    }
    draw_map_nodes();
    draw_device_location_marker();

    if(result.sd_ready&&!result.tiles) {
        const MeshInkUiRect missing=ui_rect(18,774,232,30);
        meshink_display_fill_rect({missing.x,missing.y,missing.width,missing.height},0xFF,fb);
        ui_text("NO MAP TILES HERE",ui_x(22),ui_y(778),2,0,true);
    }
    char zoom[24];snprintf(zoom,sizeof(zoom),"ZOOM %u (%s)",map_zoom,map_source_badge(result));
    const int zoom_label_width=ui_text_width(zoom,2)+ui_w(8);
    meshink_display_fill_rect({ui_x(18),ui_y(812),zoom_label_width,ui_h(30)},0xFF,fb);
    ui_text(zoom,ui_x(22),ui_y(816),2,0,true);

    const MeshInkUiRect zoom_in=meshink_map_control_rect(layout,0);
    const MeshInkUiRect zoom_out=meshink_map_control_rect(layout,1);
    const MeshInkUiRect locate=meshink_map_control_rect(layout,2);
    rounded_box(zoom_in,max(ui_w(12),ui_h(12)),true);
    meshink_display_fill_rect({zoom_in.x+ui_w(20),zoom_in.y+ui_h(30),ui_w(26),ui_h(5)},0xFF,fb);
    meshink_display_fill_rect({zoom_in.x+ui_w(30),zoom_in.y+ui_h(20),ui_w(5),ui_h(26)},0xFF,fb);
    rounded_box(zoom_out,max(ui_w(12),ui_h(12)),true);
    meshink_display_fill_rect({zoom_out.x+ui_w(20),zoom_out.y+ui_h(30),ui_w(26),ui_h(5)},0xFF,fb);
    long own_latitude=0,own_longitude=0;bool own_current_fix=false;
    const bool has_own_location=map_device_position(own_latitude,own_longitude,own_current_fix);
    if(has_own_location){
        rounded_box(locate,max(ui_w(12),ui_h(12)),true);
        const int target_x=locate.x+ui_w(12),target_y=locate.y+ui_h(12);
        meshink_display_fill_rect({target_x+ui_w(6),target_y+ui_h(6),ui_w(33),ui_h(5)},0xFF,fb);
        meshink_display_fill_rect({target_x+ui_w(6),target_y+ui_h(34),ui_w(33),ui_h(5)},0xFF,fb);
        meshink_display_fill_rect({target_x+ui_w(6),target_y+ui_h(6),ui_w(5),ui_h(33)},0xFF,fb);
        meshink_display_fill_rect({target_x+ui_w(34),target_y+ui_h(6),ui_w(5),ui_h(33)},0xFF,fb);
        meshink_display_fill_rect({target_x+ui_w(18),target_y+ui_h(18),ui_w(9),ui_h(9)},0xFF,fb);
        meshink_display_fill_rect({target_x,target_y+ui_h(21),ui_w(45),ui_h(5)},0xFF,fb);
        meshink_display_fill_rect({target_x+ui_w(21),target_y,ui_w(5),ui_h(45)},0xFF,fb);
    }

    const double metres_per_pixel=cos(map_latitude*PI/180.0)*2.0*PI*6378137.0/(256.0*(1<<map_zoom));
    double target=metres_per_pixel*ui_w(120),nice=1.0;
    while(nice*10.0<=target)nice*=10.0;
    if(target/nice>=5)nice*=5;else if(target/nice>=2)nice*=2;
    int pixels=(int)(nice/metres_per_pixel);
    char scale[24];
    if(map_imperial){const double feet=nice*3.28084;if(feet>=5280)snprintf(scale,sizeof(scale),"%.1f MI",feet/5280.0);else snprintf(scale,sizeof(scale),"%.0f FT",feet);}
    else if(nice>=1000)snprintf(scale,sizeof(scale),"%.0f KM",nice/1000.0);else snprintf(scale,sizeof(scale),"%.0f M",nice);
    const int scale_backing_width=max(pixels+ui_w(12),ui_text_width(scale,2)+ui_w(16));
    meshink_display_fill_rect({ui_x(20),ui_y(850),scale_backing_width,ui_h(34)},0xFF,fb);
    line(ui_x(26),ui_y(872),ui_x(26)+pixels,ui_y(872));
    line(ui_x(26),ui_y(866),ui_x(26),ui_y(878));
    line(ui_x(26)+pixels,ui_y(866),ui_x(26)+pixels,ui_y(878));
    ui_text(scale,ui_x(28),ui_y(850),2,0,true);
    if(!result.sd_ready){
        const MeshInkUiRect warning=ui_rect(80,300,380,80);
        ui_section_card(warning);
        ui_centred("SD card / maps unavailable",ui_y(328),2,0,true);
    }
    draw_bottom_nav(2);
}

static void message_footer_text(const UiMessage& message,char out[72]) {
    const char* state=(message.network&&message.network[0])?message.network:"";
    if(!state[0]&&message.outgoing){
        switch(message.state){
            case UiMessageState::Sending:state="SENDING";break;
            case UiMessageState::Sent:state="SENT";break;
            case UiMessageState::Delivered:state="DELIVERED";break;
            case UiMessageState::Failed:state="FAILED";break;
            case UiMessageState::Retrying1:state="RETRYING 1/2";break;
            case UiMessageState::Retrying2:state="RETRYING 2/2";break;
            case UiMessageState::Retrying3:state="FINAL FLOOD";break;
            case UiMessageState::Retrying4:state="RETRYING 4/5";break;
            case UiMessageState::Retrying5:state="RETRYING 5/5";break;
            default:break;
        }
    }
    snprintf(out,72,"%s%s%s",message.time,state[0]?"  ":"",state);
}

struct MessageBubbleGeometry { int16_t x;int16_t width;int16_t height;int16_t text_width; };
struct ChatGeometryCache {
    MessageBubbleGeometry geometry[MESHINK_MESSAGE_CAPACITY]{};
    uint8_t valid[MESHINK_MESSAGE_CAPACITY]{};
    size_t count=(size_t)-1;
    uint32_t revision=0xFFFFFFFFUL;
};
static ChatGeometryCache chat_geometry_cache{};

static MessageBubbleGeometry message_bubble_geometry(const UiMessage& message) {
    char footer[72]{};message_footer_text(message,footer);
    const int screen_width=meshink_display_logical_width();
    const int pad=ui_w(18);
    const int max_width=ui_w(456);
    const int min_width=ui_w(240);
    const int body_natural=min(max_width-2*pad,ui_text_max_line_width(message.text,3));
    const int footer_natural=ui_text_width(footer,2);
    const int width=min(max_width,max(min_width,
        max(body_natural+2*pad,footer_natural+2*pad)));
    const int text_width=max(ui_w(80),width-2*pad);
    const int lines=min(16,ui_wrapped_line_count(message.text,text_width,3));
    const int height=max(ui_h(96),lines*ui_text_line_step(3)+ui_h(58));
    const int margin=ui_x(12);
    const int x=message.outgoing?screen_width-margin-width:margin;
    const MessageBubbleGeometry geometry={
        (int16_t)x,(int16_t)width,(int16_t)height,(int16_t)text_width
    };
    return geometry;
}

static void chat_geometry_sync(size_t count){
    const uint32_t revision=ui_data?ui_data->active_message_revision():0;
    if(chat_geometry_cache.count==count&&chat_geometry_cache.revision==revision)return;
    memset(chat_geometry_cache.valid,0,sizeof(chat_geometry_cache.valid));
    chat_geometry_cache.count=count;
    chat_geometry_cache.revision=revision;
    // Page boundaries are still discovered lazily. Invalidate only the tiny
    // anchor list when message layout could have changed.
    chat_page_snapshot_count=(size_t)-1;
    chat_page_known=0;
}

static MessageBubbleGeometry chat_message_geometry(size_t index){
    if(index>=MESHINK_MESSAGE_CAPACITY||!ui_data)
        return {0,0,0,0};
    if(!chat_geometry_cache.valid[index]){
        chat_geometry_cache.geometry[index]=
            message_bubble_geometry(ui_data->active_message(index));
        chat_geometry_cache.valid[index]=1;
    }
    return chat_geometry_cache.geometry[index];
}

static void draw_message_bubble(const UiMessage& message,int y,
                                const MessageBubbleGeometry& geometry) {
    const int radius=max(ui_w(14),ui_h(14));
    rounded_box(geometry.x,y,geometry.width,geometry.height,radius,message.outgoing);
    const uint8_t color=message.outgoing?0xFF:0;
    ui_draw_wrapped(message.text,geometry.x+ui_w(18),y+ui_h(14),
                    geometry.text_width,3,color,false,16);

    char footer[72]{};message_footer_text(message,footer);
    const int footer_width=ui_text_width(footer,2);
    const int footer_max=geometry.width-ui_w(28);
    if(footer_width<=footer_max)
        ui_text(footer,geometry.x+geometry.width-ui_w(14)-footer_width,
                y+geometry.height-ui_h(27),2,color,true);
    else
        ui_text_fit(footer,geometry.x+ui_w(14),y+geometry.height-ui_h(27),
                    footer_max,2,color,true);
}

static size_t chat_fill_backwards(size_t end,int available){
    size_t candidate=end;int used=0;
    while(candidate>0){
        const int h=chat_message_geometry(candidate-1).height;
        const int needed=h+(used?ui_h(8):0);
        if(used+needed>available)break;
        used+=needed;--candidate;
        if(used>=available)break;
    }
    return candidate;
}

static void chat_page_bounds_lazy(size_t count,int current_available,int history_available,
                                  uint8_t requested,size_t& first,size_t& end,bool& has_older){
    first=end=count;has_older=false;
    if(!count)return;

    if(chat_page_snapshot_count!=count||
       chat_page_snapshot_current_available!=current_available||
       chat_page_snapshot_history_available!=history_available){
        chat_page_snapshot_count=count;
        chat_page_snapshot_current_available=current_available;
        chat_page_snapshot_history_available=history_available;
        chat_page_known=0;
        chat_page=0;
    }
    if(!chat_page_known){
        // Page 1/current leaves room for the normal bottom taskbar.
        chat_page_starts[0]=(uint16_t)chat_fill_backwards(count,current_available);
        chat_page_known=1;
    }
    while(requested>=chat_page_known&&chat_page_known<CHAT_PAGE_ANCHORS&&
          chat_page_starts[chat_page_known-1]>0){
        const size_t previous_start=chat_page_starts[chat_page_known-1];
        // Older pages intentionally hide the taskbar and use that space for
        // additional message history.
        chat_page_starts[chat_page_known]=(uint16_t)
            chat_fill_backwards(previous_start,history_available);
        ++chat_page_known;
    }
    if(requested>=chat_page_known){
        chat_page=(uint8_t)(chat_page_known-1);
        requested=chat_page;
    }
    first=chat_page_starts[requested];
    end=requested?chat_page_starts[requested-1]:count;
    has_older=first>0;
}

static void draw_chat_page_indicator(size_t page,bool has_older,int y){
    if(page==0&&!has_older)return;
    char page_text[20];
    snprintf(page_text,sizeof(page_text),"PAGE %u",(unsigned)(page+1));
    const int text_width=ui_text_width(page_text,2);
    const int text_left=(meshink_display_logical_width()-text_width)/2;
    ui_centred(page_text,y,2,0,true);
    // Chat history follows the message stack: older content is above the
    // current/newest page, so swipe down to reveal it and swipe up to return.
    if(page>0)draw_page_arrow(text_left-ui_w(24),y+ui_h(7),true);
    if(has_older)draw_page_arrow(text_left+text_width+ui_w(24),y+ui_h(7),false);
}

static int chat_compose_top(){
    const auto metrics=keyboard_metrics(false);
    return portrait_layout().bottom_nav_top-metrics.key_height-ui_h(12);
}
static int chat_history_bottom_current(){
    return chat_compose_top()-ui_h(88);
}
static int chat_history_bottom_paged(){
    // Older pages have neither compose box nor taskbar. Leave only a slim
    // footer band for PAGE N/arrows and give the rest back to message history.
    return portrait_layout().height-ui_h(62);
}
static int chat_history_available_current(){
    return chat_history_bottom_current()-ui_h(126);
}
static int chat_history_available_paged(){
    return chat_history_bottom_paged()-ui_h(126);
}
static void draw_compose_entry(const meshink_keyboard::Metrics& metrics){
    rounded_box(metrics.entry.x,metrics.entry.y,metrics.entry.width,metrics.entry.height,
                max(ui_w(12),ui_h(12)));
    const int inset_x=meshink_keyboard::scale_axis(16,metrics.width,540);
    const int inset_y=meshink_keyboard::scale_axis(8,metrics.height,960);
    const int text_x=metrics.entry.x+inset_x;
    const int text_y=metrics.entry.y+inset_y;
    const int text_width=metrics.entry.width-2*inset_x;
    const int text_height=metrics.entry.height-2*inset_y;
    if(compose_text[0])
        ui_draw_compose_tail(compose_text,text_x,text_y,text_width,text_height,3);
    else
        ui_text("Write a message...",text_x,
                metrics.entry.y+(metrics.entry.height-ui_text_height(3))/2,3,0,false);
}

static void draw_chat(bool channel) {
    draw_app_header(ui_data?ui_data->active_title():(channel?"CHANNEL":"CONTACT"),true,channel?nullptr:"INFO");
    const size_t count=ui_data?ui_data->active_message_count():0;
    chat_geometry_sync(count);
    const bool keyboard=keyboard_visible&&keyboard_message_mode;
    const auto keyboard_layout=keyboard_metrics(false);
    size_t first=count,end=count;bool has_older=false;
    if(count){
        if(keyboard)
            first=chat_fill_backwards(count,keyboard_layout.history_bottom-ui_h(126));
        else
            chat_page_bounds_lazy(count,chat_history_available_current(),
                                  chat_history_available_paged(),
                                  chat_page,first,end,has_older);
    }
    if(!count)ui_centred("NO MESSAGES YET",ui_y(300),3,0,true);
    else{
        int y=ui_y(126);
        for(size_t i=first;i<end;++i){
            const UiMessage& message=ui_data->active_message(i);
            const MessageBubbleGeometry geometry=chat_message_geometry(i);
            draw_message_bubble(message,y,geometry);
            y+=geometry.height+ui_h(8);
        }
    }
    if(keyboard){
        draw_compose_entry(keyboard_layout);
        draw_keyboard();
    }else{
        const MeshInkUiLayout& layout=portrait_layout();
        const bool history_page=chat_page>0;
        if(history_page){
            // History pages are read-only views: no composer and no taskbar.
            draw_chat_page_indicator(chat_page,has_older,layout.height-ui_h(38));
        }else{
            const int compose_y=chat_compose_top();
            draw_chat_page_indicator(chat_page,has_older,compose_y-ui_h(48));
            rounded_box(layout.outer_margin,compose_y,layout.outer_width,keyboard_layout.key_height,
                        max(ui_w(12),ui_h(12)));
            const char* prompt=compose_text[0]?compose_text:"Write a message...";
            ui_text_fit(prompt,layout.content_text_x,compose_y+ui_h(17),
                        layout.outer_width-2*layout.text_inset,3,0,compose_text[0]);
        }
    }
}

static void draw_message_entry_fast() {
    MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-message-entry-draw");
    // Typing does not change the chat history. Avoid rebuilding the status
    // bar, message bubbles and bottom navigation for every character.
    const auto metrics=keyboard_metrics(false);
    draw_compose_entry(metrics);
    if(message_keyboard_case_dirty){
        draw_keyboard();
        message_keyboard_case_dirty=false;
    }
}

static void draw_contact_details() {
    draw_app_header("NODE INFO",true);UiNodeDetails node{};
    if(!ui_data||!ui_data->active_node_details(node)){ui_centred("NODE DETAILS UNAVAILABLE",ui_y(300),3,0,true);return;}
    const uint8_t pages=node_info_page_count(node.capabilities);if(details_page>=pages)details_page=pages-1;
    const NodeInfoPage page=node_info_page(node.capabilities,details_page);
    ui_centred_fit(node.name,ui_y(126),portrait_layout().section_width,4,0,true);
    ui_centred((node.role_label&&node.role_label[0])?node.role_label:node_role_label(node.role),ui_y(174),2,0,true);
    auto action_button=[](const char* label,const MeshInkUiRect& rect,bool selected=false) {
        ui_action_button(label,rect,selected);
    };
    auto request_label=[&](UiNodeInfoRequest request,const char* idle) {
        return node.request_active?(node.request_type==request?"REQUESTING...":"REQUEST BUSY"):idle;
    };
    const bool login_required=node_has_capability(node.capabilities,UI_NODE_CAP_LOGIN);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect map_action=meshink_node_map_rect(layout);
    const MeshInkUiRect full_action=meshink_node_action_rect(layout);
    const MeshInkUiRect left_action=meshink_node_left_action_rect(layout);
    const MeshInkUiRect right_action=meshink_node_right_action_rect(layout);

    if(page!=NodeInfoPage::Overview&&!node.saved_contact){
        ui_text("ADD CONTACT FIRST",layout.section_margin,ui_y(250),3,0,true);
        ui_draw_wrapped("Remote requests require this node to be saved as a contact.",
                        layout.section_margin,ui_y(304),layout.section_width,2,0,false,4);
        draw_page_indicator(details_page,pages,ui_y(770));
        return;
    }
    if(page==NodeInfoPage::Overview){
        ui_text("OVERVIEW",layout.section_margin,ui_y(206),3,0,true);
        ui_text("LAST ADVERT",layout.section_margin,ui_y(258),2,0,true);
        ui_text_fit(node.advert_age,layout.detail_value_x,ui_y(254),
                    layout.width-layout.detail_value_x-layout.section_margin,3,0,false);
        ui_text("ROUTE",layout.section_margin,ui_y(326),2,0,true);
        ui_text_fit(node.route,layout.detail_value_x,ui_y(322),
                    layout.width-layout.detail_value_x-layout.section_margin,3,0,false);
        ui_text("POSITION",layout.section_margin,ui_y(394),2,0,true);
        const int overview_position_width=
            layout.width-layout.detail_value_x-layout.section_margin;
        const int overview_position_y=ui_y(390);
        const int overview_position_lines=min(
            2,ui_wrapped_line_count(node.position,overview_position_width,3));
        ui_draw_wrapped(node.position,layout.detail_value_x,overview_position_y,
                        overview_position_width,3,0,false,2);
        const int overview_source_y=max(
            ui_y(448),overview_position_y+
            overview_position_lines*ui_text_line_step(3)+ui_h(8));
        ui_text_fit(node.position_source,layout.detail_value_x,overview_source_y,
                    overview_position_width,2,0,false);
        const int overview_last_seen_y=max(
            ui_y(476),overview_source_y+ui_text_height(2)+ui_h(18));
        ui_text("LAST HEARD",layout.section_margin,overview_last_seen_y+ui_h(4),2,0,true);
        ui_text_fit(node.last_seen,layout.detail_value_x,overview_last_seen_y,
                    overview_position_width,3,0,false);
        const int overview_identity_y=max(
            ui_y(522),overview_last_seen_y+ui_text_height(3)+ui_h(20));
        ui_text("IDENTITY",layout.section_margin,overview_identity_y+ui_h(4),2,0,true);
        ui_text_fit(node.identity,layout.detail_value_x,overview_identity_y,
                    overview_position_width,2,0,false);
        if(node.latitude||node.longitude)ui_action_button("OPEN POSITION ON MAP",map_action,false);
        if(node.saved_contact){action_button("CHAT",left_action);
            action_button("DELETE",right_action);}
        else action_button("ADD CONTACT",full_action,true);
    } else if(page==NodeInfoPage::Status){
        ui_text("NODE STATUS",layout.section_margin,ui_y(220),3,0,true);
        if(login_required&&!node.authenticated){
            ui_draw_wrapped("This protocol requires authentication before status can be requested.",
                            layout.section_margin,ui_y(282),layout.section_width,2,0,false,5);
            if(!strcmp(node.status,"LOGIN FAILED"))ui_text("LOGIN FAILED",layout.section_margin,ui_y(410),2,0,true);
            action_button(node.login_active?"LOGGING IN...":"ENTER PASSWORD",full_action,true);
        }else{
            int status_y=ui_y(258);
            if(login_required){
                char login_text[48];snprintf(login_text,sizeof(login_text),"LOGGED IN - %s",node.access_level?node.access_level:"UNKNOWN");
                ui_text(login_text,layout.section_margin,status_y,2,0,true);
                status_y=ui_y(294);
            }
            const int status_scale=ui_text_max_line_width(node.status,3)<=layout.section_width?3:2;
            ui_draw_wrapped(node.status,layout.section_margin,status_y,
                            layout.section_width,status_scale,0,true,14);
            action_button(request_label(UiNodeInfoRequest::Status,"REQUEST STATUS"),full_action,true);
        }
    } else if(page==NodeInfoPage::Telemetry){
        ui_text("TELEMETRY / POSITION",layout.section_margin,ui_y(220),3,0,true);
        if(login_required&&!node.authenticated){
            ui_draw_wrapped("This protocol requires authentication before telemetry can be requested.",
                            layout.section_margin,ui_y(282),layout.section_width,2,0,false,4);
            action_button(node.login_active?"LOGGING IN...":"ENTER PASSWORD",full_action,true);
        }else{
            const int telemetry_scale=
                ui_wrapped_line_count(node.telemetry,layout.section_width,3)<=4?3:2;
            ui_draw_wrapped(node.telemetry,layout.section_margin,ui_y(270),
                            layout.section_width,telemetry_scale,0,true,4);
            ui_text("TELEMETRY POSITION",layout.section_margin,ui_y(420),2,0,true);
            const int telemetry_position_y=ui_y(454);
            const int telemetry_position_lines=min(
                2,ui_wrapped_line_count(node.position,layout.section_width,3));
            ui_draw_wrapped(node.position,layout.section_margin,telemetry_position_y,
                            layout.section_width,3,0,true,2);
            const int telemetry_source_y=max(
                ui_y(522),telemetry_position_y+
                telemetry_position_lines*ui_text_line_step(3)+ui_h(8));
            ui_draw_wrapped(node.position_source,layout.section_margin,telemetry_source_y,
                            layout.section_width,2,0,false,1);
            if(node.latitude||node.longitude)ui_action_button("OPEN POSITION ON MAP",map_action,false);
            action_button(request_label(UiNodeInfoRequest::Telemetry,"REQUEST TELEMETRY"),full_action,true);
        }
    } else {
        const bool can_path=node_has_capability(node.capabilities,UI_NODE_CAP_PATH);
        const bool can_trace=node_has_capability(node.capabilities,UI_NODE_CAP_TRACE);
        int next_y=ui_y(220);
        if(can_path){
            ui_text("DISCOVERED PATH",layout.section_margin,next_y,3,0,true);
            const int path_text_y=next_y+ui_h(40);
            const int path_lines=min(3,ui_wrapped_line_count(node.path,layout.section_width,3));
            ui_draw_wrapped(node.path,layout.section_margin,path_text_y,
                            layout.section_width,3,0,true,3);
            next_y=max(ui_y(360),path_text_y+path_lines*ui_text_line_step(3)+ui_h(10));
        }
        if(can_trace){
            ui_text("TRACE ROUTE",layout.section_margin,next_y,3,0,true);
            const int trace_text_y=next_y+ui_h(40);
            const int trace_lines=min(9,ui_wrapped_line_count(node.trace,layout.section_width,2));
            ui_draw_wrapped(node.trace,layout.section_margin,trace_text_y,
                            layout.section_width,2,0,true,9);
            next_y=max(ui_y(620),trace_text_y+trace_lines*ui_text_line_step(2)+ui_h(10));
        }
        ui_text("SAVED ROUTE",layout.section_margin,next_y,2,0,true);
        ui_draw_wrapped(node.route,layout.section_margin,next_y+ui_h(34),
                        layout.section_width,3,0,true,2);
        if(can_path&&can_trace){
            action_button(request_label(UiNodeInfoRequest::Path,"DISCOVER PATH"),left_action,true);
            action_button(request_label(UiNodeInfoRequest::Trace,"TRACE ROUTE"),right_action,true);
        }else if(can_path){
            action_button(request_label(UiNodeInfoRequest::Path,"DISCOVER PATH"),full_action,true);
        }else if(can_trace){
            action_button(request_label(UiNodeInfoRequest::Trace,"TRACE ROUTE"),full_action,true);
        }
    }
    draw_page_indicator(details_page,pages,ui_y(770));
    if(keyboard_visible&&keyboard_password_mode){
        const auto metrics=keyboard_metrics(false);
        meshink_display_fill_rect({0,metrics.clear_top,layout.width,
                                   layout.height-metrics.clear_top},0xFF,fb);
        const MeshInkUiRect save_rect=meshink_password_save_rect(layout);
        const MeshInkUiRect checkbox={
            save_rect.x+ui_w(4),save_rect.y+ui_h(8),ui_w(28),ui_h(28)};
        rounded_box(checkbox,max(ui_w(6),ui_h(6)),save_remote_password);
        if(save_remote_password)ui_text("X",checkbox.x+ui_w(6),checkbox.y+ui_h(5),2,0xFF,true);
        ui_text("SAVE PASSWORD",save_rect.x+ui_w(46),save_rect.y+ui_h(14),2,0,true);
        rounded_box(metrics.entry.x,metrics.entry.y,metrics.entry.width,metrics.entry.height,
                    max(ui_w(12),ui_h(12)));
        ui_text_fit(remote_password[0]?remote_password:"Remote password",
                    metrics.entry.x+ui_w(16),metrics.entry.y+ui_h(20),
                    metrics.entry.width-ui_w(32),3,0,false);
        draw_keyboard();
    }
}

static void settings_row(const char* title,const char* subtitle,int y);

static void draw_discovery() {
    draw_app_header("DISCOVERED",true);
    if(!ui_data||!ui_data->advert_count()){
        discovery_page=0;
        ui_centred("NO ADVERTS HEARD",ui_y(300),3,0,true);
        ui_centred("SEND AN ADVERT OR WAIT",ui_y(350),2);
        return;
    }
    const size_t count=ui_data->advert_count();
    clamp_list_page(discovery_page,count);
    const size_t first=discovery_page*LIST_ITEMS_PER_PAGE;
    for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row)
        draw_list_entry(ui_data->advert(first+row),
                        portrait_layout().list_top+row*portrait_layout().list_row_stride);
    draw_list_page_footer(discovery_page,count);
}

enum class MoreAction:uint8_t{Settings,Discovery,Advertise,Companion,Diagnostics,Help};
struct MoreMenuItem{const char* title;const char* subtitle;MoreAction action;};

static size_t more_menu_items(MoreMenuItem out[6]){
    size_t count=0;
    out[count++]={"SETTINGS","Device and protocol configuration",MoreAction::Settings};
    if(mesh_protocol_has(MESHINK_PROTOCOL_CAP_DISCOVERY))
        out[count++]={"DISCOVERED ADVERTS","Recent nodes heard",MoreAction::Discovery};
    if(mesh_protocol_has(MESHINK_PROTOCOL_CAP_ADVERTISE))
        out[count++]={"ADVERTISE","Share this node on the mesh",MoreAction::Advertise};
    if(mesh_protocol_has(MESHINK_PROTOCOL_CAP_COMPANION))
        out[count++]={"BLUETOOTH COMPANION","Restart in protocol companion mode",MoreAction::Companion};
    if(mesh_protocol_has(MESHINK_PROTOCOL_CAP_DIAGNOSTICS))
        out[count++]={"DIAGNOSTICS","Live protocol and radio stats",MoreAction::Diagnostics};
    out[count++]={"HELP","Using MeshInk",MoreAction::Help};
    return count;
}

static int more_row_y(size_t index){return 130+(int)index*130;}

static void draw_more() {
    draw_app_header("MORE");
    MoreMenuItem items[6]{};
    const size_t count=more_menu_items(items);
    for(size_t i=0;i<count;++i)
        settings_row(items[i].title,items[i].subtitle,more_row_y(i));
    draw_bottom_nav(3);
}

static void draw_diagnostics() {
    draw_app_header("DIAGNOSTICS",true);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect core=meshink_outer_row_rect(layout,120,180);
    const MeshInkUiRect radio=meshink_outer_row_rect(layout,312,180);
    const MeshInkUiRect packets=meshink_outer_row_rect(layout,504,180);
    ui_section_card(core);ui_section_card(radio);ui_section_card(packets);
    ui_text("CORE",layout.content_text_x,core.y+ui_h(13),3,0,true);
    ui_draw_wrapped(mesh_protocol_diagnostics_core(),layout.content_text_x,core.y+ui_h(52),
                    core.width-ui_w(32),2,0,false,5);
    ui_text("RADIO",layout.content_text_x,radio.y+ui_h(13),3,0,true);
    ui_draw_wrapped(mesh_protocol_diagnostics_radio(),layout.content_text_x,radio.y+ui_h(52),
                    radio.width-ui_w(32),2,0,false,5);
    ui_text("PACKETS",layout.content_text_x,packets.y+ui_h(13),3,0,true);
    ui_draw_wrapped(mesh_protocol_diagnostics_packets(),layout.content_text_x,packets.y+ui_h(52),
                    packets.width-ui_w(32),2,0,false,5);
    const MeshInkUiRect action=meshink_node_action_rect(layout);
    const char* label=mesh_protocol_diagnostics_busy()?"REFRESHING...":"REFRESH STATS";
    ui_action_button(label,action,true);
}

static void draw_advert_menu() {
    draw_app_header("ADVERTISE",true);
    settings_row("ZERO HOP ADVERT","NEARBY NODES ONLY",180);settings_row("FLOOD ADVERT","SEND ACROSS THE MESH",320);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect note=meshink_outer_row_rect(layout,490,180);
    ui_section_card(note);
    ui_draw_wrapped("Advertising shares this node identity using the active protocol radio settings.",
                    layout.content_text_x,note.y+ui_h(22),note.width-ui_w(32),3,0,false,4);
}

static void settings_row(const char* title,const char* subtitle,int reference_y) {
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect row=meshink_outer_row_rect(layout,reference_y,112);
    ui_section_card(row);
    ui_text_fit(title,layout.content_text_x,row.y+ui_h(13),
                row.width-ui_w(76),3,0,true);
    const int subtitle_width=row.width-ui_w(76);
    const int subtitle_scale=ui_text_width(subtitle,3)<=subtitle_width?3:2;
    ui_text_fit(subtitle,layout.content_text_x,row.y+ui_h(55),
                subtitle_width,subtitle_scale,0,false);
    ui_text(">",layout.settings_arrow_x,row.y+ui_h(39),3,0,true);
}
static void settings_info_row(const char* title,const char* subtitle,int reference_y) {
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect row=meshink_outer_row_rect(layout,reference_y,112);
    ui_section_card(row);
    ui_text_fit(title,layout.content_text_x,row.y+ui_h(13),
                row.width-ui_w(32),3,0,true);
    const int subtitle_width=row.width-ui_w(32);
    const int subtitle_scale=ui_text_width(subtitle,3)<=subtitle_width?3:2;
    ui_text_fit(subtitle,layout.content_text_x,row.y+ui_h(55),
                subtitle_width,subtitle_scale,0,false);
}


static void setup_footer(const char* action="NEXT"){
    if(keyboard_visible)return;
    const MeshInkUiRect left=ui_rect(24,850,232,74);
    const MeshInkUiRect right=ui_rect(284,850,232,74);
    if(screen!=Screen::Welcome)ui_action_button("BACK",left,false);
    ui_action_button(action,right,true);
}
static void setup_progress(const char* title,uint8_t number){
    char header[48]{};
    snprintf(header,sizeof(header),"%s",title);
    draw_app_header(header,false,setup_any_done?"CANCEL":nullptr);
    char progress[32]{};
    snprintf(progress,sizeof(progress),"STEP %u OF 6  /  %s",
             (unsigned)number,mesh_protocol_name());
    ui_centred(progress,ui_y(94),2,0,true);
}
static void setup_list_pager(size_t current,size_t count){
    if(count<=5)return;
    const MeshInkUiRect left=ui_rect(24,744,232,65);
    const MeshInkUiRect right=ui_rect(284,744,232,65);
    ui_action_button("PREV",left,current==0);
    ui_action_button("MORE",right,(current+1)*5>=count);
}
static void draw_setup_protocol(){
    draw_app_header("WELCOME TO MESHINK",false);
    ui_centred("SELECT YOUR MESH PROTOCOL",ui_y(130),3,0,true);
    ui_centred("One protocol runs at a time.",ui_y(190),2,0,false);
    const size_t count=mesh_protocol_available_count();
    for(size_t i=0;i<count&&i<4;++i){
        const auto* descriptor=mesh_protocol_available(i);
        if(!descriptor)continue;
        settings_row(descriptor->name,
            descriptor->id==setup_protocol_choice?"SELECTED":"TAP TO SELECT",
            280+(int)i*145);
    }
    ui_centred("You can switch protocols later in Settings.",ui_y(720),2,0,false);
    ui_action_button("RESTORE FROM SD",ui_rect(60,760,420,62),false);
    setup_footer();
}
static void draw_setup_name(){
    setup_progress("NAME YOUR NODE",2);
    ui_text("NODE NAME",ui_x(28),ui_y(152),2,0,true);
    const MeshInkUiRect field=ui_rect(24,185,492,96);
    ui_section_card(field);
    ui_text_fit(node_name,field.x+ui_w(17),field.y+ui_h(24),
                field.width-ui_w(34),3,0,true);
    ui_centred("Letters, numbers and symbols. No spaces.",ui_y(330),2,0,false);
    if(keyboard_visible)draw_keyboard();
    else {
        ui_action_button("EDIT NAME",ui_rect(60,450,420,74),false);
        ui_centred("ENTER on keyboard closes it.",ui_y(610),2,0,false);
        ui_action_button("RESTORE FROM SD",ui_rect(60,720,420,70),false);
        setup_footer();
    }
}
static void draw_setup_region(){
    setup_progress("SELECT REGION",3);
    const size_t count=setup_region_count();
    const size_t offset=(size_t)setup_region_page*5;
    for(size_t i=offset;i<count&&i<offset+5;++i){
        const char* name=setup_region_label(i);
        settings_row(name,i==setup_region?"SELECTED":"TAP TO SELECT",
                     142+(int)(i-offset)*116);
    }
    setup_list_pager(setup_region_page,count);
    setup_footer();
}
static void draw_setup_preset(){
    setup_progress(setup_is_meshcore()?"RADIO PRESET":"MODEM PRESET",4);
    const size_t count=setup_preset_count();
    const size_t offset=(size_t)setup_preset_page*5;
    for(size_t i=offset;i<count&&i<offset+5;++i){
        const bool selected=setup_is_meshcore()
            ?setup_core_preset_at(i)==setup_radio_preset
            :(int)i==setup_radio_preset;
        settings_row(setup_preset_label(i),selected?"SELECTED":"TAP TO SELECT",
                     142+(int)(i-offset)*116);
    }
    setup_list_pager(setup_preset_page,count);
    setup_footer();
}
static void setup_compact_row(const char* title,const char* value,int y){
    const MeshInkUiRect rect=ui_rect(24,y,492,70);
    ui_section_card(rect);
    ui_text(title,rect.x+ui_w(13),rect.y+ui_h(13),2,0,true);
    ui_text_fit(value,rect.x+ui_w(248),rect.y+ui_h(15),
                rect.width-ui_w(272),2,0,true);
}
static void draw_setup_radio(){
    setup_progress("RADIO CONFIGURATION",5);
    if(!setup_is_meshcore()){
        settings_info_row("REGION",setup_region_label(setup_region),160);
        settings_info_row("MODEM",setup_preset_label((size_t)setup_radio_preset),298);
        char hops[24]{};
        snprintf(hops,sizeof(hops),"%u HOPS  /  TAP TO CHANGE",(unsigned)setup_hops);
        settings_row("HOP LIMIT",hops,436);
        ui_draw_wrapped("Meshtastic custom modem values require Leaf support; use a supported preset for this build.",
            ui_x(34),ui_y(620),ui_w(470),2,0,false,4);
        setup_footer();
        return;
    }
    ui_text("FREQUENCY (MHz)",ui_x(28),ui_y(145),2,0,true);
    const MeshInkUiRect freq=ui_rect(24,174,492,88);
    ui_section_card(freq);
    ui_text_fit(setup_freq[0]?setup_freq:"Tap to enter MHz",
                freq.x+ui_w(16),freq.y+ui_h(22),freq.width-ui_w(32),3,0,true);
    ui_text("TRANSMIT POWER (dBm)",ui_x(28),ui_y(288),2,0,true);
    const MeshInkUiRect power=ui_rect(24,317,492,88);
    ui_section_card(power);
    ui_text_fit(setup_power[0]?setup_power:"Tap to enter dBm",
                power.x+ui_w(16),power.y+ui_h(22),power.width-ui_w(32),3,0,true);
    if(keyboard_visible){draw_keyboard();return;}
    char bandwidth[24]{},spreading[20]{},coding[16]{},hash[16]{};
    if(setup_bw)snprintf(bandwidth,sizeof(bandwidth),"%.1f kHz",(double)setup_bw);
    else strcpy(bandwidth,"SELECT");
    if(setup_sf)snprintf(spreading,sizeof(spreading),"SF%u",(unsigned)setup_sf);
    else strcpy(spreading,"SELECT");
    if(setup_cr)snprintf(coding,sizeof(coding),"4/%u",(unsigned)setup_cr);
    else strcpy(coding,"SELECT");
    if(setup_hash)snprintf(hash,sizeof(hash),"%u BYTES",(unsigned)setup_hash);
    else strcpy(hash,"SELECT");
    setup_compact_row("BANDWIDTH",bandwidth,443);
    setup_compact_row("SPREADING FACTOR",spreading,524);
    setup_compact_row("CODING RATE",coding,605);
    setup_compact_row("PATH HASH",hash,686);
    setup_footer();
}
static void draw_setup_review(){
    setup_progress("REVIEW AND START",6);
    settings_info_row("PROTOCOL",mesh_protocol_name(),138);
    settings_info_row("NODE NAME",node_name,268);
    settings_info_row("REGION",setup_region_label(setup_region),398);
    if(setup_is_meshcore()){
        settings_info_row("RADIO",setup_radio_preset<0?"CUSTOM":
            PRESETS[setup_radio_preset].title,528);
        char summary[90]{};
        snprintf(summary,sizeof(summary),"%s MHz  /  SF%u  /  BW%.1f  /  CR4/%u  /  %sdBm",
            setup_freq,(unsigned)setup_sf,(double)setup_bw,
            (unsigned)setup_cr,setup_power);
        ui_text_fit(summary,ui_x(28),ui_y(699),ui_w(486),2,0,false);
    }else{
        settings_info_row("MODEM",setup_preset_label((size_t)setup_radio_preset),528);
        char details[40]{};
        snprintf(details,sizeof(details),"HOP LIMIT: %u",(unsigned)setup_hops);
        ui_centred(details,ui_y(710),2,0,false);
    }
    setup_footer("START MESHINK");
}

static void draw_backup_options(){
    draw_app_header(backup_restore_mode?"RESTORE FROM SD":"BACKUP TO SD",true);
    char label[40]{};
    snprintf(label,sizeof(label),"%s DATA ONLY",mesh_protocol_name());
    ui_centred(label,ui_y(122),3,0,true);
    if(backup_restore_mode)
        ui_text_fit(backup_filename,ui_x(24),ui_y(182),ui_w(492),2,0,false);
    else ui_centred("SELECT BACKUP CATEGORIES",ui_y(182),2,0,false);
    constexpr const char* names[]={"MESSAGES","NODES / CONTACTS","SETTINGS & IDENTITY"};
    constexpr uint8_t flags[]={1,2,4};
    for(int i=0;i<3;++i){
        ui_section_card(ui_rect(24,245+i*124,492,100));
        const bool on=(backup_flags&flags[i])!=0;
        const bool available=(backup_available&flags[i])!=0;
        rounded_box(ui_rect(42,263+i*124,53,53),ui_w(8),on);
        if(on)ui_text("X",ui_x(57),ui_y(275+i*124),3,0xFF,true);
        ui_text(names[i],ui_x(120),ui_y(277+i*124),3,available?0:0x88,true);
    }
    ui_draw_wrapped("WARNING: SD IS REMOVABLE. BACKUPS MAY CONTAIN PRIVATE IDENTITY KEYS, CHANNEL KEYS AND MESSAGES. KEEP YOUR CARD SECURE.",
        ui_x(30),ui_y(636),ui_w(475),2,0,false,5);
    if(backup_restore_mode){
        ui_action_button("CHOOSE FILE",ui_rect(24,811,232,80),false);
        ui_action_button("RESTORE",ui_rect(284,811,232,80),true);
    }else{
        ui_action_button("RESTORE SD",ui_rect(24,811,232,80),false);
        ui_action_button("BACKUP NOW",ui_rect(284,811,232,80),true);
    }
}
static void draw_backup_files(){
    draw_app_header("CHOOSE SD BACKUP",true);
    char subtitle[46]{};
    snprintf(subtitle,sizeof(subtitle),"%s / %u BACKUPS",
             mesh_protocol_name(),(unsigned)backup_count);
    ui_centred(subtitle,ui_y(112),2,0,true);
    if(!backup_count)ui_centred("NO COMPATIBLE SD BACKUPS",ui_y(340),3,0,true);
    const size_t start=backup_page*5;
    for(size_t i=start;i<backup_count&&i<start+5;++i){
        const MeshInkBackupInfo& entry=backup_entries[i];
        char details[72]{};
        snprintf(details,sizeof(details),"%s%s%s %lu BYTES",
            entry.categories&1?"MSG ":"",entry.categories&2?"NODES ":"",
            entry.categories&4?"SETTINGS ":"",(unsigned long)entry.bytes);
        settings_row(entry.filename,details,170+(int)(i-start)*119);
    }
    if(backup_count>5){
        ui_action_button("PREV",ui_rect(24,800,232,74),backup_page==0);
        ui_action_button("NEXT",ui_rect(284,800,232,74),(backup_page+1)*5>=backup_count);
    }
}
static void draw_backup_confirm(){
    draw_app_header("CONFIRM RESTORE",true);
    ui_centred("REPLACE SELECTED DATA?",ui_y(195),3,0,true);
    ui_draw_wrapped("Only the chosen categories for this protocol will be replaced. The SD backup and all other protocol data will be retained.",
        ui_x(36),ui_y(350),ui_w(470),3,0,false,6);
    ui_action_button("CANCEL",ui_rect(24,735,232,85),false);
    ui_action_button("RESTORE",ui_rect(284,735,232,85),true);
}
static void draw_backup_result(){
    draw_app_header("BACKUP / RESTORE",true);
    ui_draw_wrapped(backup_result_message,ui_x(32),ui_y(315),ui_w(476),3,0,true,8);
    ui_action_button("BACK",ui_rect(85,780,370,80),false);
}

static void draw_setup_cancel(){
    draw_app_header("CANCEL SETUP",true);
    ui_centred("RETURN TO YOUR PREVIOUS PROTOCOL?",ui_y(225),3,0,true);
    ui_draw_wrapped("This protocol will remain unconfigured. Your previous protocol and its settings will be retained.",
        ui_x(40),ui_y(355),ui_w(460),3,0,false,5);
    ui_action_button("CONTINUE SETUP",ui_rect(24,660,232,75),false);
    ui_action_button("YES, RETURN",ui_rect(284,660,232,75),true);
}

enum class SettingsAction:uint8_t{Protocol,ProtocolSettings,Gps,DateTime,DisplayPower,About};
struct SettingsMenuItem{const char* title;const char* subtitle;SettingsAction action;};

static size_t settings_menu_items(SettingsMenuItem out[6]){
    size_t count=0;
    out[count++]={"PROTOCOL",mesh_protocol_name(),SettingsAction::Protocol};
    out[count++]={"PROTOCOL SETTINGS","Identity, radio and protocol features",SettingsAction::ProtocolSettings};
    if(meshink_board_has_gps())
        out[count++]={"LOCATION & GPS","Position, receiver and sharing",SettingsAction::Gps};
    out[count++]={"DATE & TIME","Clock, source and timezone",SettingsAction::DateTime};
    out[count++]={"DISPLAY & POWER","Frontlight, refresh and standby",SettingsAction::DisplayPower};
    out[count++]={"ABOUT","Firmware, device and protocol info",SettingsAction::About};
    return count;
}

static int settings_menu_y(size_t index){return 118+(int)index*120;}

static void draw_settings() {
    draw_app_header("SETTINGS",true);
    SettingsMenuItem items[6]{};
    const size_t count=settings_menu_items(items);
    for(size_t i=0;i<count;++i)
        settings_row(items[i].title,items[i].subtitle,settings_menu_y(i));
}

static int protocol_select_row_y(size_t index){return 150+(int)index*150;}

static void draw_protocol_select() {
    draw_app_header("SELECT PROTOCOL",true);
    const size_t count=mesh_protocol_available_count();
    for(size_t i=0;i<count&&i<4;++i){
        const auto* protocol=mesh_protocol_available(i);
        if(!protocol)continue;
        char subtitle[72]{};
        if(protocol->id==mesh_protocol_descriptor().id)
            snprintf(subtitle,sizeof(subtitle),"ACTIVE / %s",protocol->core_name);
        else
            snprintf(subtitle,sizeof(subtitle),"%s / TAP TO SWITCH",protocol->core_name);
        settings_row(protocol->name,subtitle,protocol_select_row_y(i));
    }
    const MeshInkUiLayout& layout=portrait_layout();
    ui_draw_wrapped("Changing protocol restarts MeshInk. Messages and protocol settings are kept for switching back.",
                    layout.section_margin,ui_y(760),layout.section_width,2,0,false,4);
}

enum class ProtocolSettingsRowKind:uint8_t{NodeName,RadioPreset,Backend,Backup};

static size_t protocol_settings_total_count(){
    return 2+(mesh_protocol_supports_radio_presets()?1:0)+mesh_protocol_setting_count();
}

static bool protocol_settings_row(size_t index,ProtocolSettingsRowKind& kind,
                                  const char*& title,const char*& value,
                                  uint16_t& backend_id,bool& editable){
    backend_id=0;editable=true;
    if(index==0){
        kind=ProtocolSettingsRowKind::NodeName;
        title="NODE NAME";value=node_name;
        return true;
    }
    size_t cursor=1;
    if(mesh_protocol_supports_radio_presets()){
        if(index==cursor){
            kind=ProtocolSettingsRowKind::RadioPreset;
            title="RADIO PRESET";value=active_radio_label();
            return true;
        }
        ++cursor;
    }
    if(index-cursor==mesh_protocol_setting_count()){
        kind=ProtocolSettingsRowKind::Backup;
        title="BACKUP / RESTORE";value="SD CARD / PROTOCOL DATA";
        return true;
    }
    MeshInkProtocolSettingItem item{};
    if(!mesh_protocol_setting_item(index-cursor,item))return false;
    kind=ProtocolSettingsRowKind::Backend;
    title=item.title;value=item.value;backend_id=item.id;editable=item.editable;
    return true;
}

static int protocol_settings_row_y(size_t row){return 118+(int)row*136;}

static void draw_protocol_settings() {
    char header[36]{};
    snprintf(header,sizeof(header),"%s SETTINGS",mesh_protocol_name());
    draw_app_header(header,true);
    const size_t count=protocol_settings_total_count();
    clamp_list_page(protocol_settings_page,count);
    const size_t first=protocol_settings_page*LIST_ITEMS_PER_PAGE;
    for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
        ProtocolSettingsRowKind kind{};
        const char* title="";const char* value="";
        uint16_t id=0;bool editable=true;
        if(!protocol_settings_row(first+row,kind,title,value,id,editable))continue;
        if(editable)settings_row(title,value,protocol_settings_row_y(row));
        else settings_info_row(title,value,protocol_settings_row_y(row));
    }
    draw_list_page_footer(protocol_settings_page,count);
    if(keyboard_visible)draw_keyboard();
}

static void draw_protocol_name_fast() {
    MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-protocol-name-draw");
    settings_row("NODE NAME",node_name,protocol_settings_row_y(0));
}

static const char* gps_mode_label(){
    switch(mesh_protocol_gps_constellation_mode()){
        case MeshInkGpsConstellationMode::GpsOnly:return "GPS";
        case MeshInkGpsConstellationMode::BeiDouOnly:return "BEIDOU";
        case MeshInkGpsConstellationMode::GpsBeiDou:return "GPS + BEIDOU";
        case MeshInkGpsConstellationMode::GlonassOnly:return "GLONASS";
        case MeshInkGpsConstellationMode::GpsGlonass:return "GPS + GLONASS";
        case MeshInkGpsConstellationMode::BeiDouGlonass:return "BEIDOU + GLONASS";
        case MeshInkGpsConstellationMode::GpsBeiDouGlonass:return "GPS + BEIDOU + GLONASS";
        default:return "DISABLED";
    }
}
static void draw_gps_settings() {
    draw_app_header("LOCATION & GPS",true);
    settings_row("GPS MODE",gps_mode_label(),120);
    char fix[40]{};
    const char* current_status="DISABLED";
    if(mesh_protocol_gps_enabled()){
        switch(status_gps_error){
            case MeshInkGpsError::ModuleNotIdentified:current_status="ERROR - MODULE NOT IDENTIFIED";break;
            case MeshInkGpsError::NmeaUnavailable:current_status="ERROR - NO VALID NMEA";break;
            case MeshInkGpsError::ProviderUnavailable:current_status="ERROR - GPS PROVIDER UNAVAILABLE";break;
            default:
                snprintf(fix,sizeof(fix),status_gps_fix?"FIXED  %d SATELLITES":"SEARCHING  %d SATELLITES",status_gps_satellites);
                current_status=fix;
                break;
        }
    }
    settings_row("CURRENT STATUS",current_status,238);
    char position[64];if(status_gps_fix){const long alat=abs(status_gps_latitude),alon=abs(status_gps_longitude);snprintf(position,sizeof(position),"%c%ld.%06ld  %c%ld.%06ld",status_gps_latitude<0?'-':'+',alat/1000000,alat%1000000,status_gps_longitude<0?'-':'+',alon/1000000,alon%1000000);}else strcpy(position,"NO VALID POSITION");settings_row("LATITUDE / LONGITUDE",position,356);
    settings_row("DEEP SLEEP POWER SAVE",mesh_protocol_gps_deep_sleep_power_save()?"ON":"OFF",474);
}

static const char* gps_constellation_state(MeshInkGpsConstellation constellation){
    return meshink_gps_constellation_enabled(
        mesh_protocol_gps_constellation_mode(),constellation)?"ENABLED":"DISABLED";
}
static void draw_gps_tuning(){
    draw_app_header("GPS MODE",true);
    settings_row("GPS",gps_constellation_state(MeshInkGpsConstellation::Gps),118);
    settings_row("BEIDOU",gps_constellation_state(MeshInkGpsConstellation::BeiDou),238);
    settings_row("GLONASS",gps_constellation_state(MeshInkGpsConstellation::Glonass),358);
    const MeshInkUiLayout& layout=portrait_layout();
    ui_draw_wrapped("GPS is disabled when no satellite systems are selected.",
                    layout.section_margin,ui_y(510),layout.section_width,3,0,false,3);
}

static const char* const MONTH_NAMES[]={
    "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"
};
static bool manual_time_leap_year(uint16_t year){
    return (year%4U==0U&&year%100U!=0U)||(year%400U==0U);
}
static uint8_t manual_time_days_in_month(uint16_t year,uint8_t month){
    static constexpr uint8_t DAYS[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1||month>12)return 31;
    if(month==2&&manual_time_leap_year(year))return 29;
    return DAYS[month-1];
}
static void clamp_manual_time_day(){
    manual_time_day=min(manual_time_day,manual_time_days_in_month(
        manual_time_year,manual_time_month));
    if(manual_time_day<1)manual_time_day=1;
}
static void load_manual_time_draft(){
    time_t now=(time_t)mesh_protocol_current_time();
    struct tm local{};
    if(now>=(time_t)946684800&&localtime_r(&now,&local)&&
       local.tm_year+1900>=2000&&local.tm_year+1900<=2099){
        manual_time_year=(uint16_t)(local.tm_year+1900);
        manual_time_month=(uint8_t)(local.tm_mon+1);
        manual_time_day=(uint8_t)local.tm_mday;
        manual_time_hour=(uint8_t)local.tm_hour;
        manual_time_minute=(uint8_t)local.tm_min;
    }else{
        manual_time_year=2026;manual_time_month=1;manual_time_day=1;
        manual_time_hour=12;manual_time_minute=0;
    }
    clamp_manual_time_day();
}
static void time_mode_label(char* out,size_t len){
    if(!out||!len)return;
    if(mesh_protocol_time_mode()==MeshInkTimeMode::Manual){snprintf(out,len,"MANUAL");return;}
    const char* source="VALID RTC";
    switch(mesh_protocol_time_source()){
        case MeshInkTimeSource::Gps:source="GPS FIX";break;
        case MeshInkTimeSource::Companion:source="COMPANION";break;
        case MeshInkTimeSource::Protocol:source=mesh_protocol_name();break;
        case MeshInkTimeSource::HardwareRtc:source="VALID RTC";break;
        case MeshInkTimeSource::Manual:source="VALID RTC";break;
        default:source=mesh_protocol_time_valid()?"VALID RTC":"WAITING";break;
    }
    snprintf(out,len,"AUTO (%s)",source);
}
static void current_datetime_label(char* out,size_t len){
    if(!out||!len)return;
    if(!mesh_protocol_time_valid()){strncpy(out,"NOT SET",len-1);out[len-1]=0;return;}
    const time_t now=(time_t)mesh_protocol_current_time();struct tm local{};
    if(!localtime_r(&now,&local)){strncpy(out,"NOT SET",len-1);out[len-1]=0;return;}
    snprintf(out,len,"%02d %s %04d  %02d:%02d",local.tm_mday,MONTH_NAMES[min(11,max(0,local.tm_mon))],local.tm_year+1900,local.tm_hour,local.tm_min);
}
static void draw_date_time(){
    draw_app_header("DATE & TIME",true);
    char current[32]{},mode[40]{},zone[48]{};
    current_datetime_label(current,sizeof(current));time_mode_label(mode,sizeof(mode));timezone_display_label(zone,sizeof(zone));
    settings_info_row("CURRENT TIME",current,118);settings_row("TIME MODE",mode,238);
    settings_row("SET DATE & TIME","MANUAL ENTRY",358);settings_row("TIMEZONE",zone,478);
    const MeshInkUiLayout& layout=portrait_layout();
    ui_draw_wrapped("AUTO accepts trusted clock updates. MANUAL locks the RTC against GPS, companion and protocol time changes.",
                    layout.section_margin,ui_y(650),layout.section_width,2,0,false,5);
}
static MeshInkUiRect manual_time_group_rect(bool time_group){
    const MeshInkUiLayout& layout=portrait_layout();
    return {layout.section_margin,ui_y(time_group?438:116),layout.section_width,ui_h(time_group?262:292)};
}
static MeshInkUiRect manual_time_adjust_button_rect(uint8_t field,bool plus){
    const bool time_field=field>=3;const MeshInkUiRect group=manual_time_group_rect(time_field);
    const uint8_t index=time_field?(uint8_t)(field-3):field,count=time_field?2:3;
    const int gap=ui_w(time_field?30:10),side=ui_w(time_field?76:70);
    const int content_width=side*count+gap*(count-1),x0=group.x+(group.width-content_width)/2;
    return {x0+index*(side+gap),group.y+ui_h(72+(plus?0:130)),side,ui_h(54)};
}
static int manual_time_field_center(uint8_t field){
    const MeshInkUiRect plus=manual_time_adjust_button_rect(field,true);return plus.x+plus.width/2;
}
static void draw_manual_time_field(uint8_t field,const char* label,const char* value){
    const bool time_field=field>=3;const MeshInkUiRect group=manual_time_group_rect(time_field);const int cx=manual_time_field_center(field);
    ui_text(label,cx-ui_text_width(label,2)/2,group.y+ui_h(43),2,0,true);
    ui_action_button("+",manual_time_adjust_button_rect(field,true),false);
    ui_text(value,cx-ui_text_width(value,3)/2,group.y+ui_h(145),3,0,true);
    ui_action_button("-",manual_time_adjust_button_rect(field,false),false);
}
static MeshInkUiRect manual_time_action_rect(bool save){
    const MeshInkUiLayout& layout=portrait_layout();const int gap=ui_w(16),width=(layout.section_width-gap)/2;
    return {layout.section_margin+(save?width+gap:0),ui_y(746),width,ui_h(86)};
}
static void draw_manual_time(){
    draw_app_header("SET DATE & TIME",true);
    const MeshInkUiRect date=manual_time_group_rect(false),tm=manual_time_group_rect(true);
    ui_section_card(date);ui_section_card(tm);
    ui_text("DATE",date.x+ui_w(16),date.y+ui_h(12),3,0,true);ui_text("TIME",tm.x+ui_w(16),tm.y+ui_h(12),3,0,true);
    char value[16]{};
    snprintf(value,sizeof(value),"%u",(unsigned)manual_time_year);draw_manual_time_field(0,"YEAR",value);
    snprintf(value,sizeof(value),"%s",MONTH_NAMES[manual_time_month-1]);draw_manual_time_field(1,"MONTH",value);
    snprintf(value,sizeof(value),"%02u",(unsigned)manual_time_day);draw_manual_time_field(2,"DAY",value);
    snprintf(value,sizeof(value),"%02u",(unsigned)manual_time_hour);draw_manual_time_field(3,"HOUR",value);
    snprintf(value,sizeof(value),"%02u",(unsigned)manual_time_minute);draw_manual_time_field(4,"MINUTE",value);
    ui_action_button("CANCEL",manual_time_action_rect(false),false);ui_action_button("SAVE",manual_time_action_rect(true),true);
}

static void adjust_manual_time_field(uint8_t field,int delta){
    switch(field){
        case 0:
            if(delta<0)manual_time_year=manual_time_year<=2000?2099:(uint16_t)(manual_time_year-1);
            else manual_time_year=manual_time_year>=2099?2000:(uint16_t)(manual_time_year+1);
            clamp_manual_time_day();
            break;
        case 1:
            if(delta<0)manual_time_month=manual_time_month<=1?12:(uint8_t)(manual_time_month-1);
            else manual_time_month=manual_time_month>=12?1:(uint8_t)(manual_time_month+1);
            clamp_manual_time_day();
            break;
        case 2:{
            const uint8_t last=manual_time_days_in_month(manual_time_year,manual_time_month);
            if(delta<0)manual_time_day=manual_time_day<=1?last:(uint8_t)(manual_time_day-1);
            else manual_time_day=manual_time_day>=last?1:(uint8_t)(manual_time_day+1);
            break;
        }
        case 3:
            if(delta<0)manual_time_hour=manual_time_hour==0?23:(uint8_t)(manual_time_hour-1);
            else manual_time_hour=manual_time_hour>=23?0:(uint8_t)(manual_time_hour+1);
            break;
        default:
            if(delta<0)manual_time_minute=manual_time_minute==0?59:(uint8_t)(manual_time_minute-1);
            else manual_time_minute=manual_time_minute>=59?0:(uint8_t)(manual_time_minute+1);
            break;
    }
}
static bool save_manual_time_draft(){
    struct tm requested{};
    requested.tm_year=(int)manual_time_year-1900;
    requested.tm_mon=(int)manual_time_month-1;
    requested.tm_mday=manual_time_day;
    requested.tm_hour=manual_time_hour;
    requested.tm_min=manual_time_minute;
    requested.tm_sec=0;
    requested.tm_isdst=-1;
    const time_t utc=mktime(&requested);
    if(utc<=0)return false;

    // Reject nonexistent/normalized local times (for example a skipped DST
    // wall-clock time) rather than silently saving a different value.
    struct tm verify{};
    if(!localtime_r(&utc,&verify))return false;
    if(verify.tm_year!=(int)manual_time_year-1900||
       verify.tm_mon!=(int)manual_time_month-1||
       verify.tm_mday!=manual_time_day||
       verify.tm_hour!=manual_time_hour||
       verify.tm_min!=manual_time_minute)return false;

    return mesh_protocol_set_manual_time((uint32_t)utc);
}

static MeshInkUiRect timezone_row_rect(uint8_t index){return meshink_outer_row_rect(portrait_layout(),108+index*82,74);}
static void timezone_row_detail(uint8_t index,char* out,size_t len){
    if(index==TIMEZONE_AUTO)snprintf(out,len,"CURRENT: %s",auto_timezone_label);
    else if(index==TIMEZONE_CUSTOM){char label[24]{},rule[32]{};fixed_timezone_text(custom_timezone_minutes,label,sizeof(label),rule,sizeof(rule));snprintf(out,len,"%s - FIXED, NO DST",label);}
    else snprintf(out,len,"%s",TIMEZONES[index].detail);
}
static void draw_timezone(){
    draw_app_header("TIMEZONE",true);const MeshInkUiLayout& layout=portrait_layout();
    for(uint8_t i=0;i<TIMEZONE_COUNT;++i){
        const MeshInkUiRect row=timezone_row_rect(i);rounded_box(row,max(ui_w(10),ui_h(10)),i==timezone_index);
        const uint8_t c=i==timezone_index?0xFF:0;char detail[48]{};timezone_row_detail(i,detail,sizeof(detail));
        ui_text_fit(TIMEZONES[i].label,layout.content_text_x,row.y+ui_h(8),row.width-ui_w(28),2,c,true);
        ui_text_fit(detail,layout.content_text_x,row.y+ui_h(40),row.width-ui_w(28),2,c,false);
    }
}
static MeshInkUiRect custom_timezone_step_rect(bool plus){
    const MeshInkUiLayout& layout=portrait_layout();const int gap=ui_w(18),width=(layout.section_width-gap)/2;
    return {layout.section_margin+(plus?width+gap:0),ui_y(418),width,ui_h(88)};
}
static MeshInkUiRect custom_timezone_action_rect(bool save){
    const MeshInkUiLayout& layout=portrait_layout();const int gap=ui_w(16),width=(layout.section_width-gap)/2;
    return {layout.section_margin+(save?width+gap:0),ui_y(690),width,ui_h(88)};
}
static void draw_custom_timezone(){
    draw_app_header("CUSTOM UTC OFFSET",true);const MeshInkUiLayout& layout=portrait_layout();
    char label[24]{},rule[32]{};fixed_timezone_text(custom_timezone_minutes,label,sizeof(label),rule,sizeof(rule));
    ui_text("FIXED OFFSET",layout.section_margin,ui_y(150),3,0,true);ui_centred(label,ui_y(230),5,0,true);
    ui_draw_wrapped("Custom offsets do not apply daylight-saving changes.",layout.section_margin,ui_y(320),layout.section_width,2,0,false,3);
    ui_action_button("-15 MIN",custom_timezone_step_rect(false),false);ui_action_button("+15 MIN",custom_timezone_step_rect(true),false);
    ui_action_button("CANCEL",custom_timezone_action_rect(false),false);ui_action_button("SAVE",custom_timezone_action_rect(true),true);
}

static void draw_display_settings() {
    draw_app_header("DISPLAY & POWER",true);
    const MeshInkUiLayout& layout=portrait_layout();
    settings_row("MODE",frontlight_mode_name(),118);
    if(frontlight_mode==FrontlightMode::NightTimer){
        const MeshInkUiRect action=meshink_settings_inline_action_rect(layout,118);
        ui_action_button("EDIT TIMES",action,true);
    }
    settings_row("LIGHT TIMEOUT",frontlight_timeout_name(),238);
    const MeshInkUiRect brightness=meshink_display_brightness_rect(layout);
    ui_section_card(brightness);
    ui_text("BRIGHTNESS",layout.content_text_x,brightness.y+ui_h(15),3,0,true);
    char level[8];snprintf(level,sizeof(level),"%u%%",frontlight_brightness);
    ui_text(level,layout.width-ui_w(28)-ui_text_width(level,3),
            brightness.y+ui_h(15),3,0,true);
    const MeshInkUiRect slider=meshink_display_slider_track_rect(layout);
    meshink_display_fill_rect({slider.x,slider.y,slider.width,slider.height},0,fb);
    const int knob=slider.x+(frontlight_brightness*slider.width)/100;
    meshink_display_fill_rect({knob-ui_w(12),slider.y-ui_h(15),ui_w(24),ui_h(35)},0,fb);
    ui_text("-",layout.content_text_x,ui_y(452),3,0,true);
    ui_text("+",layout.width-ui_w(48),ui_y(452),3,0,true);
    settings_row("STANDBY",standby_timeout_name(),538);
    ui_action_button(deep_sleep_standby?"DEEP SLEEP":"NORMAL",
                     meshink_settings_inline_action_rect(layout,538),true);
    settings_row("MAP SCALE",map_imperial?"IMPERIAL":"METRIC",656);
    const MeshInkUiRect shutdown=meshink_shutdown_rect(layout);
    ui_action_button("SHUT DOWN",shutdown,false);
}

static void draw_help() {
    draw_app_header("USING MESHINK",true);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect quick=meshink_outer_row_rect(layout,118,126);
    const MeshInkUiRect button=meshink_outer_row_rect(layout,254,166);
    const MeshInkUiRect keyboard=meshink_outer_row_rect(layout,430,126);
    const MeshInkUiRect maps=meshink_outer_row_rect(layout,566,126);
    const MeshInkUiRect companion=meshink_outer_row_rect(layout,702,170);
    ui_section_card(quick);ui_section_card(button);ui_section_card(keyboard);
    ui_section_card(maps);ui_section_card(companion);

    ui_text("QUICK SETTINGS",layout.content_text_x,quick.y+ui_h(12),3,0,true);
    ui_draw_wrapped("Swipe down from the top edge for front light, advert flood and power off.",
                    layout.content_text_x,quick.y+ui_h(48),quick.width-ui_w(32),2,0,false,3);

    char button_title[32];
    snprintf(button_title,sizeof(button_title),"%s BUTTON",meshink_primary_button_name());
    ui_text(button_title,layout.content_text_x,button.y+ui_h(12),3,0,true);
    char button_help[180];
    snprintf(button_help,sizeof(button_help),
        "Short press refreshes. Hold %s for 2 seconds for standby; hold again for 2 seconds to wake.",
        meshink_primary_button_name());
    ui_draw_wrapped(button_help,layout.content_text_x,button.y+ui_h(48),
                    button.width-ui_w(32),2,0,false,5);

    ui_text("KEYBOARD",layout.content_text_x,keyboard.y+ui_h(12),3,0,true);
    ui_draw_wrapped("Use LAND for the larger landscape message keyboard.",
                    layout.content_text_x,keyboard.y+ui_h(48),keyboard.width-ui_w(32),2,0,false,3);

    ui_text("MAPS",layout.content_text_x,maps.y+ui_h(12),3,0,true);
    ui_draw_wrapped("Pan or pinch, double tap to zoom in, triple tap to zoom out.",
                    layout.content_text_x,maps.y+ui_h(48),maps.width-ui_w(32),2,0,false,3);

    if(mesh_protocol_has(MESHINK_PROTOCOL_CAP_COMPANION)){
        ui_text("BLUETOOTH COMPANION",layout.content_text_x,companion.y+ui_h(12),3,0,true);
        ui_draw_wrapped("Reboots into the active protocol companion mode. Reboot again to return to the local UI.",
                        layout.content_text_x,companion.y+ui_h(48),companion.width-ui_w(32),2,0,false,5);
    }else{
        ui_text("ACTIVE PROTOCOL",layout.content_text_x,companion.y+ui_h(12),3,0,true);
        char protocol_help[160]{};
        snprintf(protocol_help,sizeof(protocol_help),"%s is active. Change protocols or protocol-specific options under More > Settings.",
                 mesh_protocol_name());
        ui_draw_wrapped(protocol_help,layout.content_text_x,companion.y+ui_h(48),
                        companion.width-ui_w(32),2,0,false,5);
    }
}

static void draw_meshink_logo(int top,bool compact=false);

static void draw_standby(){
    meshink_display_set_all_white(&display);
    if(!deep_sleep_standby)draw_status_bar();

    const bool has_direct=status_unread>0;
    const bool has_channel=status_channel_unread>0;
    const bool any_unread=has_direct||has_channel;

    // With nothing waiting, lower the logo into the otherwise-empty centre of
    // the screen. When unread cards are present retain the proven compact top
    // placement so the logo, cards and lower standby treatment all fit.
    const int logo_top=any_unread?ui_y(70):ui_y(165);
    draw_meshink_logo(logo_top,false);

    MeshInkUiRect direct_rect=ui_rect(20,445,244,310);
    MeshInkUiRect channel_rect=ui_rect(276,445,244,310);
    if(has_direct!=has_channel){
        const int centred_x=(portrait_layout().width-ui_w(244))/2;
        if(has_direct)direct_rect.x=centred_x;
        else channel_rect.x=centred_x;
    }

    if(has_direct){
        draw_standby_card(direct_rect);
        draw_standby_envelope_icon(
            direct_rect.x+(direct_rect.width-ui_w(120))/2,
            direct_rect.y+ui_h(28));
        char direct[12];
        if(status_unread>99)strcpy(direct,"99+");
        else snprintf(direct,sizeof(direct),"%u",status_unread);
        standby_centred(direct,direct_rect,direct_rect.y+ui_h(125),11);
        standby_centred("PRIVATE",direct_rect,direct_rect.y+ui_h(224),3);
        standby_centred("MESSAGES",direct_rect,direct_rect.y+ui_h(260),3);
    }

    if(has_channel){
        draw_standby_card(channel_rect);
        draw_standby_channel_icon(
            channel_rect.x+(channel_rect.width-ui_w(126))/2,
            channel_rect.y+ui_h(22));
        char channel[12];
        if(status_channel_unread>99)strcpy(channel,"99+");
        else snprintf(channel,sizeof(channel),"%u",status_channel_unread);
        standby_centred(channel,channel_rect,channel_rect.y+ui_h(125),11);
        standby_centred("CHANNEL",channel_rect,channel_rect.y+ui_h(224),3);
        standby_centred("MESSAGES",channel_rect,channel_rect.y+ui_h(260),3);
    }

    // Deep-sleep standby has its own at-rest identity and deliberately omits
    // the normal status bar. Keep the state label in the same movable region
    // as ordinary STANDBY so future unread-card layout changes can shift the
    // whole treatment rather than relying on fixed status-bar coordinates.
    const int standby_state_y=any_unread?ui_y(775):ui_y(620);
    if(deep_sleep_standby)
        ui_centred_fit("DEEP SLEEP STANDBY",standby_state_y,
                       portrait_layout().width-ui_w(32),4,0,true);
    else
        ui_centred("STANDBY",standby_state_y,5,0,true);

    // Keep the wake instruction low on the panel and make it readable at a
    // glance. Scale 3 uses the smooth Inter renderer; two lines avoid squeezing.
    meshink_display_fill_rect({ui_x(24),ui_y(830),ui_w(492),ui_h(3)},0,fb);
    char wake_line[40];
    snprintf(wake_line,sizeof(wake_line),"HOLD %s FOR TWO SECONDS",meshink_primary_button_name());
    ui_centred_fit(wake_line,ui_y(852),portrait_layout().width-ui_w(32),3,0,true);
    ui_centred("TO WAKE",ui_y(892),3,0,true);
}

static void format_minutes(uint16_t minutes,char out[8]){snprintf(out,8,"%02u:%02u",minutes/60,minutes%60);}
static void draw_night_schedule(){
    draw_app_header("NIGHT SCHEDULE",true);
    char start[8],end[8];format_minutes(night_start_minutes,start);format_minutes(night_end_minutes,end);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect start_rect=meshink_night_start_rect(layout);
    const MeshInkUiRect end_rect=meshink_night_end_rect(layout);
    const MeshInkUiRect minus_rect=meshink_night_minus_rect(layout);
    const MeshInkUiRect plus_rect=meshink_night_plus_rect(layout);
    const MeshInkUiRect save_rect=meshink_night_save_rect(layout);
    rounded_box(start_rect,max(ui_w(14),ui_h(14)),night_edit_field==0);
    ui_text("START",start_rect.x+ui_w(18),start_rect.y+ui_h(16),3,night_edit_field==0?0xFF:0,true);
    ui_text(start,start_rect.x+start_rect.width-ui_w(18)-ui_text_width(start,3),
            start_rect.y+ui_h(16),3,night_edit_field==0?0xFF:0,true);
    rounded_box(end_rect,max(ui_w(14),ui_h(14)),night_edit_field==1);
    ui_text("END",end_rect.x+ui_w(18),end_rect.y+ui_h(16),3,night_edit_field==1?0xFF:0,true);
    ui_text(end,end_rect.x+end_rect.width-ui_w(18)-ui_text_width(end,3),
            end_rect.y+ui_h(16),3,night_edit_field==1?0xFF:0,true);
    ui_action_button("-30 MIN",minus_rect,false);
    ui_action_button("+30 MIN",plus_rect,false);
    ui_action_button("SAVE SCHEDULE",save_rect,true);
    ui_draw_wrapped("The selected timezone from GPS settings is used automatically.",
                    layout.section_margin,ui_y(740),layout.section_width,2,0,false,3);
}

static void draw_meshink_logo(int top,bool compact) {
    (void)compact;
    constexpr uint8_t shades[4]={0x00,0x55,0xAA,0xFF};
    const int target_width=ui_w(MESHINK_LOGO_WIDTH);
    const int target_height=ui_h(MESHINK_LOGO_HEIGHT);
    const int left=(meshink_display_logical_width()-target_width)/2;
    for(int y=0;y<target_height;++y){
        const int source_y=(y*MESHINK_LOGO_HEIGHT)/target_height;
        int x=0;
        while(x<target_width){
            const int source_x=(x*MESHINK_LOGO_WIDTH)/target_width;
            const int pixel=source_y*MESHINK_LOGO_WIDTH+source_x;
            const uint8_t shade=(MESHINK_LOGO_PIXELS[pixel>>2]>>(6-2*(pixel&3)))&3;
            if(shade==3){++x;continue;}
            const int run=x++;
            while(x<target_width){
                const int next_source_x=(x*MESHINK_LOGO_WIDTH)/target_width;
                const int next=source_y*MESHINK_LOGO_WIDTH+next_source_x;
                if(((MESHINK_LOGO_PIXELS[next>>2]>>(6-2*(next&3)))&3)!=shade)break;
                ++x;
            }
            meshink_display_fill_rect({left+run,top+y,x-run,1},shades[shade],fb);
        }
    }
}

static void draw_about() {
    draw_app_header("ABOUT",true);
    const MeshInkUiLayout& layout=portrait_layout();
    draw_meshink_logo(ui_y(118),true);
    ui_centred("Made by Samo",ui_y(506),3,0,true);
    ui_centred("github.com/samo-nz/mesh-ink",ui_y(540),2,0,false);
    if(node_name[0])ui_centred_fit(node_name,ui_y(600),layout.width-ui_w(32),3,0,true);
    ui_centred(UI_VERSION,ui_y(642),3,0,true);
    const MeshInkUiRect info=meshink_outer_row_rect(layout,698,150);
    ui_section_card(info);
    ui_text("HARDWARE",layout.content_text_x,info.y+ui_h(18),2,0,true);
    ui_text("LILYGO T5 PRO",ui_x(170),info.y+ui_h(16),3,0,false);
    ui_text("CORE",layout.content_text_x,info.y+ui_h(78),2,0,true);
    char core_info[80]{};
    snprintf(core_info,sizeof(core_info),"%s %s",mesh_protocol_core_name(),mesh_protocol_core_version());
    ui_text_fit(core_info,ui_x(170),
                info.y+ui_h(74),info.x+info.width-ui_x(170)-ui_w(16),2,0,false);
}

static void draw_screen();
static void refresh(MeshInkRefreshMode mode,bool wake_light);
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh);
static bool hit(int16_t x,int16_t y,const MeshInkUiRect& rect);

static void draw_underlying_screen() {
    // Render the current page normally, but without allowing its controls to
    // receive events while the quick sheet is open.
    const bool panel=quick_panel_active;
    quick_panel_active=false;
    draw_screen();
    quick_panel_active=panel;
}

static void draw_quick_panel() {
    MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-quick-panel-draw");
    draw_underlying_screen();

    const MeshInkUiLayout& layout=portrait_layout();
    const int panel_bottom=meshink_quick_panel_bottom(layout);
    const MeshInkUiRect slider=meshink_quick_slider_track_rect(layout);
    const MeshInkUiRect minus_button=meshink_quick_minus_rect(layout);
    const MeshInkUiRect plus_button=meshink_quick_plus_rect(layout);
    const MeshInkUiRect advert_button=meshink_quick_advert_rect(layout);
    const MeshInkUiRect power_button=meshink_quick_power_rect(layout);

    const int display_width=meshink_display_logical_width();
    const int display_height=meshink_display_logical_height();
    for(int y=panel_bottom;y<display_height;++y)
        for(int x=((y&1)?1:0);x<display_width;x+=2)
            if((y&1)==0) meshink_display_draw_pixel(x,y,0x00,fb);

    meshink_display_fill_rect({0,0,display_width,panel_bottom},0xFF,fb);
    meshink_display_fill_rect({0,panel_bottom-ui_h(4),display_width,ui_h(4)},0x00,fb);

    ui_centred("QUICK SETTINGS",ui_y(34),4,0,true);
    ui_centred("Front light",ui_y(92),3,0,true);

    rounded_box(slider,max(ui_w(7),ui_h(7)),false);
    const int thumb_x=slider.x+((int)frontlight_brightness*slider.width)/100;
    const int track_centre_y=slider.y+slider.height/2;
    meshink_display_fill_rect({slider.x,track_centre_y-ui_h(5),
        max(1,thumb_x-slider.x),ui_h(10)},0x00,fb);
    rounded_fill(max(slider.x,thumb_x-ui_w(9)),track_centre_y-ui_h(19),
                 ui_w(18),ui_h(38),max(ui_w(8),ui_h(8)),0);

    ui_centred("Tap or drag to select",ui_y(221),2,0,false);

    rounded_box(minus_button,max(ui_w(12),ui_h(12)),false);
    meshink_display_fill_rect({
        minus_button.x+ui_w(37),minus_button.y+ui_h(33),ui_w(38),ui_h(4)},0x00,fb);
    rounded_box(plus_button,max(ui_w(12),ui_h(12)),false);
    meshink_display_fill_rect({
        plus_button.x+ui_w(37),plus_button.y+ui_h(33),ui_w(38),ui_h(4)},0x00,fb);
    meshink_display_fill_rect({
        plus_button.x+ui_w(54),plus_button.y+ui_h(16),ui_w(4),ui_h(38)},0x00,fb);
    char level[16];
    if(frontlight_brightness==0) snprintf(level,sizeof(level),"OFF");
    else snprintf(level,sizeof(level),"%u%%",(unsigned)frontlight_brightness);
    ui_centred(level,ui_y(273),4,0,true);

    ui_action_button("ADVERT FLOOD",advert_button,true);
    ui_action_button("POWER OFF",power_button,false);

    ui_centred("Tap below or swipe up to close",ui_y(560),2,0,false);
    draw_toast();
}

static void close_quick_panel() {
    if(!quick_panel_active)return;
    quick_panel_active=false;
    if(quick_panel_restore_landscape) {
        quick_panel_restore_landscape=false;
        keyboard_landscape=true;
        set_ui_orientation(MeshInkOrientation::Landscape);
    }
    draw_screen();refresh(MeshInkRefreshMode::FastGray16,true);
}

static void open_quick_panel() {
    if(quick_panel_active||standby_active)return;
    map_taps={};
    quick_panel_restore_landscape=keyboard_landscape;
    if(keyboard_landscape) {
        keyboard_landscape=false;
        set_ui_orientation(MeshInkOrientation::Portrait);
    }
    quick_panel_active=true;
    draw_quick_panel();
    refresh(MeshInkRefreshMode::FastGray16,true);
}

static void commit_brightness(int value) {
    frontlight_brightness=(uint8_t)max(0,min(100,value));
    frontlight_mode=frontlight_brightness?FrontlightMode::On:FrontlightMode::Off;
    save_frontlight_settings();
    if(frontlight_brightness)frontlight_event();
    else {frontlight_drive(false);frontlight_deadline=0;}
}
static void quick_set_brightness(int value) {
    commit_brightness(value);
    draw_quick_panel();
    refresh(MeshInkRefreshMode::Direct,false);
}
static void display_set_brightness(int value) {
    commit_brightness(value);
    T5_DEBUGF(T5_LOG_UI,"[T5-LIGHT] brightness=%u%%\n",frontlight_brightness);
    draw_screen();
    refresh(MeshInkRefreshMode::Direct,false);
}

static bool handle_quick_panel_tap(int16_t x,int16_t y,int16_t start_x=-1,int16_t start_y=-1) {
    if(!quick_panel_active)return false;
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect slider=meshink_quick_slider_track_rect(layout);
    const MeshInkUiRect slider_touch=meshink_quick_slider_touch_rect(layout);
    const int slider_right=slider.x+slider.width;

    const bool slider_drag_release=
        start_x>=slider_touch.x&&start_x<slider_touch.x+slider_touch.width&&
        start_y>=slider_touch.y&&start_y<slider_touch.y+slider_touch.height;
    const bool slider_tap_release=start_y<0&&hit(x,y,slider_touch);
    if(slider_drag_release||slider_tap_release) {
        const int clamped=max(slider.x,min(slider_right,(int)x));
        const int value=((clamped-slider.x)*100+slider.width/2)/slider.width;
        quick_set_brightness(value);
        return true;
    }

    if(y>=meshink_quick_panel_bottom(layout)) { close_quick_panel();return true; }

    if(hit(x,y,meshink_quick_minus_rect(layout))) { quick_set_brightness((int)frontlight_brightness-1);return true; }
    if(hit(x,y,meshink_quick_plus_rect(layout))) { quick_set_brightness((int)frontlight_brightness+1);return true; }
    if(hit(x,y,meshink_quick_advert_rect(layout))) {
        show_toast(mesh_protocol_send_advert(true)?"SENDING FLOOD ADVERT":"ADVERT BUSY");
        draw_quick_panel();refresh(MeshInkRefreshMode::Direct,true);return true;
    }
    if(hit(x,y,meshink_quick_power_rect(layout))) {
        quick_panel_active=false;quick_panel_restore_landscape=false;
        keyboard_landscape=false;keyboard_visible=false;keyboard_message_mode=false;
        set_ui_orientation(MeshInkOrientation::Portrait);
        screen=Screen::ShutdownConfirm;draw_screen();refresh(MeshInkRefreshMode::FastGray16,true);return true;
    }
    return true;
}

static void draw_screen() {
    // Local UI cruises at 80 MHz. Full framebuffer composition bursts to
    // 240 MHz only while drawing, then restores the previous clock before the
    // caller decides whether to refresh the panel. Small standalone draw paths
    // (keyboard/status/quick panel/toasts) use the same short boost.
    MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-draw");
    // Standby must take precedence over every transient/landscape UI layer.
    if(standby_active){
        draw_standby();
        return;
    }
    if(quick_panel_active){
        draw_quick_panel();
        return;
    }
    if(keyboard_landscape){
        draw_landscape_keyboard();
        return;
    }
    switch(screen){
        case Screen::Welcome:draw_setup_protocol();break;
        case Screen::SetupName:draw_setup_name();break;
        case Screen::SetupRegion:draw_setup_region();break;
        case Screen::SetupPreset:draw_setup_preset();break;
        case Screen::SetupRadio:draw_setup_radio();break;
        case Screen::SetupReview:draw_setup_review();break;
        case Screen::SetupCancel:draw_setup_cancel();break;
        case Screen::Presets:draw_presets();break;case Screen::CompanionConfirm:draw_companion_confirm();break;case Screen::ShutdownConfirm:draw_shutdown_confirm();break;
        case Screen::Contacts:draw_contacts();break;case Screen::ContactChat:draw_chat(false);break;case Screen::ContactDetails:draw_contact_details();break;
        case Screen::Channels:draw_channels();break;case Screen::ChannelChat:draw_chat(true);break;
        case Screen::ChannelManage:draw_channel_manage();break;case Screen::ChannelCreate:draw_channel_create();break;case Screen::ChannelDelete:draw_channel_delete();break;
        case Screen::Maps:draw_maps();break;case Screen::Discovery:draw_discovery();break;case Screen::More:draw_more();break;case Screen::AdvertMenu:draw_advert_menu();break;case Screen::Diagnostics:draw_diagnostics();break;
        case Screen::Settings:draw_settings();break;case Screen::ProtocolSelect:draw_protocol_select();break;case Screen::ProtocolSettings:draw_protocol_settings();break;
        case Screen::BackupOptions:draw_backup_options();break;
        case Screen::BackupFiles:draw_backup_files();break;
        case Screen::BackupConfirm:draw_backup_confirm();break;
        case Screen::BackupResult:draw_backup_result();break;case Screen::GpsSettings:draw_gps_settings();break;case Screen::GpsTuning:draw_gps_tuning();break;
        case Screen::DateTime:draw_date_time();break;case Screen::ManualTime:draw_manual_time();break;case Screen::Timezone:draw_timezone();break;case Screen::CustomTimezone:draw_custom_timezone();break;
        case Screen::DisplaySettings:draw_display_settings();break;case Screen::NightSchedule:draw_night_schedule();break;case Screen::Help:draw_help();break;case Screen::About:draw_about();break;
    }
    const bool settings_page=screen==Screen::Settings||screen==Screen::BackupOptions||
        screen==Screen::BackupFiles||screen==Screen::BackupConfirm||screen==Screen::BackupResult||screen==Screen::ProtocolSelect||screen==Screen::ProtocolSettings||screen==Screen::GpsSettings||screen==Screen::GpsTuning||screen==Screen::DateTime||screen==Screen::ManualTime||screen==Screen::Timezone||screen==Screen::CustomTimezone||screen==Screen::DisplaySettings||screen==Screen::NightSchedule||screen==Screen::Help||screen==Screen::About;
    if(screen==Screen::ContactDetails&&!(keyboard_visible&&keyboard_password_mode))draw_bottom_nav(details_from_discovery?3:0);
    else if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&
            !keyboard_visible&&chat_page==0)
        draw_bottom_nav(screen==Screen::ContactChat?0:1);
    else if(screen==Screen::ChannelManage)draw_bottom_nav(1);
    else if(screen==Screen::Discovery||screen==Screen::AdvertMenu||screen==Screen::Diagnostics||
            (settings_page&&!(screen==Screen::ProtocolSettings&&keyboard_visible)))
        draw_bottom_nav(3);
    draw_toast();
}

static void refresh(MeshInkRefreshMode mode,bool wake_light=true) {
    if(wake_light&&!standby_active)frontlight_event();
    // Maps contains only black and white pixels. Use the direct DU waveform
    // for normal updates; the 1.3.24 device test confirmed it prevents the
    // terrain fading seen after GC16. BOOT on Maps uses DU as well.
    const MeshInkRefreshMode requested_mode=mode;
    (void)requested_mode;
    const bool active_map=screen==Screen::Maps&&!standby_active&&!keyboard_landscape;
    if(active_map&&mode==MeshInkRefreshMode::FastGray16)mode=MeshInkRefreshMode::Direct;
    set_cpu_target(UI_RENDER_CPU_MHZ,"display-refresh");
    meshink_display_poweron();
    const MeshInkDisplayResult err = meshink_display_update_screen(&display,mode,(int)meshink_display_ambient_temperature());
    // The map stays clear when the panel is powered down as soon as EPDiy's
    // synchronous DU waveform completes. Do not reintroduce a powered hold.
    meshink_display_poweroff();
    set_cpu_target(ui_post_render_cpu_target(),"display-complete");
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] refresh=%d waveform=%d requested=%d screen=%d name='%s' preset=%s cpu=%luMHz\n",
        err,(int)mode,(int)requested_mode,(int)screen,node_name,PRESETS[selected_preset].title,(unsigned long)meshink_performance_cpu_mhz());
}

static void refresh_area(MeshInkRefreshMode mode,MeshInkRect area,bool wake_light=true) {
    const uint32_t started=millis();
    if(wake_light&&!standby_active)frontlight_event();
    const MeshInkRefreshMode requested_mode=mode;
    (void)requested_mode;
    const bool active_map=screen==Screen::Maps&&!standby_active&&!keyboard_landscape;
    if(active_map&&mode==MeshInkRefreshMode::FastGray16)mode=MeshInkRefreshMode::Direct;
    set_cpu_target(UI_RENDER_CPU_MHZ,"display-area-refresh");
    meshink_display_poweron();
    const MeshInkDisplayResult err=meshink_display_update_area(
        &display,mode,(int)meshink_display_ambient_temperature(),area);
    meshink_display_poweroff();
    set_cpu_target(ui_post_render_cpu_target(),"display-area-complete");
    const uint32_t elapsed=millis()-started;
    T5_DEBUGF(T5_LOG_MAP,"[T5-MAP-LOAD] area-refresh=%lux%lu@%ld,%ld elapsed=%lums err=%d\n",
        (unsigned long)area.width,(unsigned long)area.height,
        (long)area.x,(long)area.y,(unsigned long)elapsed,(int)err);
}

static void invalidate_display_back_buffer() {
    meshink_display_invalidate_previous(&display);
}

static void force_redraw(MeshInkRefreshMode mode,const char* reason,bool wake_light=false) {
    invalidate_display_back_buffer();
    T5_DEBUGF(T5_LOG_UI,"[T5-EPD] forced full redraw reason=%s mode=%d\n",reason,(int)mode);
    refresh(mode,wake_light);
}

static void fast_full_redraw(const char* reason,bool wake_light=false) {
    // Maps: force an entire DU frame instead of using the GC16 waveform
    // which causes the fine black map detail to fade on this panel. The
    // inverted back framebuffer above ensures DU is not skipped as a no-op.
    // Do not alter standby or other screens' GL16 full redraw behaviour.
    if(screen==Screen::Maps&&!standby_active&&!keyboard_landscape) {
        T5_DEBUGF(T5_LOG_UI,"[T5-EPD] full DU map redraw reason=%s\n",reason);
        force_redraw(MeshInkRefreshMode::Direct,reason,wake_light);
        return;
    }
    T5_DEBUGF(T5_LOG_UI,"[T5-EPD] fast-gray fallback uses GL16-compatible refresh reason=%s\n",reason);
    force_redraw(MeshInkRefreshMode::FastGray16,reason,wake_light);
}

static void reveal_map_after_black_prep(const char* reason,bool wake_light=false) {
    // Match the proven Maps tab-entry transition: update the whole panel with
    // the map content region black, then redraw the completed map with a
    // forced full DU refresh. This is also required when Maps returns from a
    // full-screen owner such as standby, otherwise the previous screen can
    // remain visible through fine map detail.
    meshink_display_fill_rect(
        {0,map_top(),portrait_layout().width,map_bottom()-map_top()},0x00,fb);
    refresh(MeshInkRefreshMode::Direct,false); // intentional transient black prep
    draw_screen();
    fast_full_redraw(reason,wake_light);
}

// Display the saved previous map underneath the same toast used for saved
// settings. Never decode the requested tiles before the progress notification
// has appeared on the physical e-paper screen.
static void load_map_with_feedback(bool already_on_map,bool force_complete_first_frame=false) {
    if(!already_on_map) {
        // Opening Maps from another tab: restore the previously rendered
        // terrain if available. On first-ever entry show the Maps shell
        // rather than leaving Contacts/Settings underneath the notification.
        if(map_base_valid&&map_base_cache&&map_base_bytes)
            memcpy(fb,map_base_cache,map_base_bytes);
        else
            meshink_display_set_all_white(&display);
        draw_status_bar();
        draw_bottom_nav(2);
    }
    // When panning or zooming, fb still holds the visible previous map.
    // Keep that map visible under the loading toast; this avoids a jarring
    // black flash during interactive pan/zoom even though it requires the
    // established three-refresh contrast-preserving sequence.
    draw_toast_message("Loading..");
    if(already_on_map)
        refresh_area(MeshInkRefreshMode::Direct,toast_message_rect("Loading.."));
    else if(force_complete_first_frame)
        // Deep-sleep wake may reuse a framebuffer whose EPDiy back-buffer does
        // not describe the physically retained standby image. Prepare the full
        // Loading frame first, then force that completed frame directly.
        fast_full_redraw("MAP_LOADING_AFTER_DEEP_WAKE",false);
    else
        refresh(MeshInkRefreshMode::Direct);

    // The previous map and toast stay on the panel while all tile I/O and
    // PNG decoding run synchronously. Refreshing the loading toast returns
    // the CPU to the 80 MHz UI cruise clock; temporarily burst to 240 MHz for
    // the CPU-heavy raster render.
    set_cpu_target(UI_RENDER_CPU_MHZ,"map-render");
    draw_screen();

    // Prepare the completed terrain black, then reveal the finished map.
    // This preserves the stable black->map DU transition that prevents
    // progressive darkening of unchanged terrain on repeated map updates.
    reveal_map_after_black_prep("MAP_BLACK_PREP_COMPLETE",false);
}

static void set_touch_power(bool enabled);

static void request_hardware_shutdown() {
    T5_DEBUGLN(T5_LOG_UI,"[T5-SHUTDOWN] user confirmed; preparing peripherals and persistent display");
    keyboard_visible=false;keyboard_message_mode=false;toast_visible=false;text_refresh_pending=false;
    meshink_display_set_all_white(&display);
    const MeshInkUiLayout& layout=portrait_layout();
    ui_centred("POWERED OFF",ui_y(250),6,0,true);
    const MeshInkPowerWakeInfo& wake=meshink_power_wake_info();
    ui_centred_fit(wake.off_battery_line1,ui_y(370),layout.width-ui_w(32),4,0,true);
    ui_centred_fit(wake.off_battery_line2,ui_y(425),layout.width-ui_w(32),4,0,true);
    ui_centred_fit(wake.off_external_line1,ui_y(560),layout.width-ui_w(32),3,0,true);
    ui_centred_fit(wake.off_external_line2,ui_y(610),layout.width-ui_w(32),3,0,true);
    ui_centred(UI_VERSION,ui_y(900),2,0,true);
    refresh(MeshInkRefreshMode::FastGray16,false);
    frontlight_deadline=0;frontlight_drive(false);
    set_touch_power(false);
    mesh_protocol_prepare_shutdown();
    SPIFFS.end();
    T5_DEBUGLN(T5_LOG_UI,"[T5-SHUTDOWN] message store closed; radio, GPS, touch and frontlight stopped");
    meshink_power_enter_ship_mode(MeshInkPowerOffReason::User);
}

[[noreturn]] void ui_minimal_low_battery_shutdown(
        const MeshInkPowerCriticalState& critical,const char* source) {
    if(critical.battery_mv_valid)
        Serial.printf("[T5-ERROR] CRITICAL battery=%umV source=%s; minimal low-battery shutdown\n",
                      (unsigned)critical.battery_mv,source?source:"unknown");
    else
        Serial.printf("[T5-ERROR] CRITICAL battery source=%s; minimal low-battery shutdown\n",
                      source?source:"unknown");

    // No touch, storage, GPS or protocol startup here. Bring up only the
    // frontlight pin (kept at zero) and EPD long enough to leave persistent
    // user guidance before board-level ship mode removes battery power.
    meshink_power_frontlight_begin();
    meshink_power_frontlight_set(0);
    meshink_display_init();
    meshink_display_set_orientation(MeshInkOrientation::Portrait);
    display=meshink_display_state_init();
    fb=meshink_display_framebuffer(&display);

    if(fb){
        meshink_display_set_all_white(&display);
        ui_centred("LOW BATTERY",ui_y(230),6,0,true);
        ui_centred("POWERED DOWN",ui_y(340),5,0,true);
        ui_centred("CONNECT USB TO CHARGE",ui_y(475),3,0,true);
        if(critical.battery_mv_valid){
            char voltage[20];
            snprintf(voltage,sizeof(voltage),"BATTERY %u.%02uV",
                     (unsigned)(critical.battery_mv/1000U),
                     (unsigned)((critical.battery_mv%1000U)/10U));
            ui_centred(voltage,ui_y(650),2,0,true);
        }else{
            ui_centred("BATTERY CRITICAL",ui_y(650),2,0,true);
        }
        ui_centred(UI_VERSION,ui_y(900),2,0,true);
        meshink_display_poweron();
        const MeshInkDisplayResult result=meshink_display_update_screen(
            &display,MeshInkRefreshMode::FastGray16,
            (int)meshink_display_ambient_temperature());
        meshink_display_poweroff();
        Serial.printf("[T5-DEEPSLEEP] minimal low-battery EPD result=%d\n",(int)result);
    }else{
        meshink_display_poweroff();
        Serial.println("[T5-DEEPSLEEP] minimal low-battery framebuffer unavailable");
    }

    meshink_display_release_state(&display);
    fb=nullptr;
    meshink_display_deinit();

    // EPDiy v7 teardown deletes the shared I2C driver. Re-establish only the
    // minimal bus before commanding board-level battery ship mode; otherwise the
    // charger write would fail and low-battery shutdown would degrade to the
    // ESP32-only deep-sleep fallback.
    if(!meshink_power_begin_minimal_bus())
        Serial.println("[T5-ERROR] low-battery ship-mode I2C restart failed");
    meshink_power_enter_ship_mode(MeshInkPowerOffReason::LowBattery);
}

static void critical_battery_shutdown(const MeshInkPowerCriticalState& critical,const char* source) {
    if(critical.battery_mv_valid)
        Serial.printf("[T5-ERROR] CRITICAL battery=%umV source=%s; entering ship mode\n",
                      (unsigned)critical.battery_mv,source?source:"unknown");
    else
        Serial.printf("[T5-ERROR] CRITICAL battery source=%s; entering ship mode\n",
                      source?source:"unknown");
    keyboard_visible=false;keyboard_message_mode=false;toast_visible=false;
    text_refresh_pending=false;message_alert_active=false;
    frontlight_deadline=0;frontlight_drive(false);

    meshink_display_set_all_white(&display);
    ui_centred("LOW BATTERY",ui_y(230),6,0,true);
    ui_centred("POWERED DOWN",ui_y(340),5,0,true);
    ui_centred("CONNECT USB TO CHARGE",ui_y(475),3,0,true);
    if(critical.battery_mv_valid) {
        char voltage[20];
        snprintf(voltage,sizeof(voltage),"BATTERY %u.%02uV",
                 (unsigned)(critical.battery_mv/1000U),
                 (unsigned)((critical.battery_mv%1000U)/10U));
        ui_centred(voltage,ui_y(650),2,0,true);
    } else {
        ui_centred("BATTERY CRITICAL",ui_y(650),2,0,true);
    }
    ui_centred(UI_VERSION,ui_y(900),2,0,true);
    refresh(MeshInkRefreshMode::FastGray16,false);

    set_touch_power(false);
    if(mesh_is_ready) {
        mesh_protocol_prepare_shutdown();
        SPIFFS.end();
        T5_DEBUGLN(T5_LOG_UI,"[T5-SHUTDOWN] low-battery mesh/storage stopped");
    }
    meshink_power_enter_ship_mode(MeshInkPowerOffReason::LowBattery);
}

static void service_critical_battery() {
    MeshInkPowerCriticalState critical{};
    if(meshink_power_poll_critical(critical))
        critical_battery_shutdown(critical,"runtime");
}

static bool update_charge_state(bool* icon_changed=nullptr) {
    MeshInkChargeState next=MeshInkChargeState::Unknown;
    if(!meshink_power_read_charge_state(next)) {
        if(icon_changed)*icon_changed=false;
        return false;
    }
    const MeshInkChargeState previous=status_charge_state;
    status_charge_state=next;
    const bool was_charging=meshink_power_is_charging(previous);
    const bool now_charging=meshink_power_is_charging(next);
    if(icon_changed)*icon_changed=was_charging!=now_charging;
    return previous!=next;
}

static bool update_status_hardware() {
    const int8_t old_hour=status_hour,old_minute=status_minute;
    const int16_t old_battery=status_battery;const MeshInkChargeState old_charge=status_charge_state;
    if(mesh_is_ready&&mesh_protocol_time_valid()){time_t now=(time_t)mesh_protocol_current_time();struct tm local{};localtime_r(&now,&local);if(local.tm_hour>=0&&local.tm_hour<24){status_hour=local.tm_hour;status_minute=local.tm_min;}}
    else{status_hour=-1;status_minute=-1;}
    uint8_t battery_percent=0;
    if(meshink_power_read_battery_percent(battery_percent))
        status_battery=(int16_t)battery_percent;
    update_charge_state();
    const bool clock_changed=old_hour!=status_hour||old_minute!=status_minute;
    const bool battery_changed=old_battery!=status_battery;
    return clock_changed||battery_changed||old_charge!=status_charge_state;
}
static void touch_sampler_task(void*){
    bool held=false,home_held=false,map_previous=false;
    bool map_multi=false,map_pinch_allowed=false;
    bool keyboard_delete_hold=false,keyboard_delete_repeated=false;
    uint32_t keyboard_delete_repeat_at=0;
    int16_t start_x=0,start_y=0,last_x=0,last_y=0;
    int16_t pinch_x=0,pinch_y=0;
    int32_t initial_distance=0,final_distance=0;
    uint32_t pressed_at=0;
    for(;;){
        if(!touch_enabled){
            held=false;home_held=false;map_multi=false;map_previous=false;
            quick_slider_dragging=false;display_slider_dragging=false;
            keyboard_delete_hold=false;keyboard_delete_repeated=false;
            meshink_touch_reset_tracking();
            ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
            continue;
        }
        // Maps owns the gesture-oriented two-point parser only while the
        // map itself is interactive. Quick Settings must fall back to the
        // ordinary single-touch parser so slider movement can preview PWM
        // continuously instead of being delivered only on finger release.
        const bool on_map=screen==Screen::Maps&&!standby_active&&
            !keyboard_landscape&&!quick_panel_active;
        if(on_map!=map_previous) {
            held=false;home_held=false;map_multi=false;
            quick_slider_dragging=false;display_slider_dragging=false;
            keyboard_delete_hold=false;keyboard_delete_repeated=false;
            meshink_touch_reset_tracking();
            map_previous=on_map;
        }
        if(on_map) {
            MeshInkTouchContacts contacts{};
            if(!meshink_touch_read_contacts(contacts)) {
                vTaskDelay(pdMS_TO_TICKS(8));continue;
            }
            const uint8_t count=contacts.count;
            const int16_t x0=contacts.points[0].x,y0=contacts.points[0].y;
            const int16_t x1=contacts.points[1].x,y1=contacts.points[1].y;
            const bool home=contacts.home;
            if(home) {
                if(!home_held){QueuedTap tap{0,0,0,0,true};xQueueSend(touch_queue,&tap,0);}
                home_held=true;held=false;map_multi=false;
            } else if(home_held) {
                if(count==0)home_held=false; // no phantom tap on Home release
            } else if(count>=2) {
                if(!held)frontlight_event();
                held=true;
                if(!map_multi) {
                    map_multi=true;
                    map_pinch_allowed=count==2 &&
                        meshink_map_gestures::terrain_point(x0,y0,portrait_layout()) &&
                        meshink_map_gestures::terrain_point(x1,y1,portrait_layout());
                    pinch_x=(int16_t)((x0+x1)/2);
                    pinch_y=(int16_t)((y0+y1)/2);
                    initial_distance=count==2 ?
                        meshink_map_gestures::distance_squared(x0,y0,x1,y1):0;
                    final_distance=initial_distance;
                } else if(count==2) {
                    final_distance=meshink_map_gestures::distance_squared(x0,y0,x1,y1);
                } else map_pinch_allowed=false;
            } else if(count==1) {
                // If one finger of a previous pinch lifts first, suppress
                // a false single tap or one-finger pan until all are released.
                if(!map_multi) {
                    last_x=x0;last_y=y0;
                    if(!held) {
                        held=true;start_x=x0;start_y=y0;
                        pressed_at=millis();frontlight_event();
                    }
                }
            } else if(map_multi) {
                map_multi=false;held=false;
                QueuedTap tap{pinch_x,pinch_y,0,0,false};
                tap.map_pinch=1;
                tap.map_sampled=1;
                tap.zoom_steps=map_pinch_allowed ?
                    (int8_t)meshink_map_gestures::pinch_zoom_steps(
                        initial_distance,final_distance):0;
                // Even a stationary two-finger gesture must cancel a pending
                // single/double tap, without triggering a phantom pan.
                xQueueSend(touch_queue,&tap,0);
            } else if(held) {
                held=false;
                QueuedTap tap{last_x,last_y,
                    (int16_t)(last_x-start_x),(int16_t)(last_y-start_y),false};
                tap.hold_ms=(uint16_t)min((uint32_t)65535,(uint32_t)(millis()-pressed_at));
                tap.map_sampled=1;
                xQueueSend(touch_queue,&tap,0);
            }
        } else {
            // Non-Maps keeps the legacy single-contact semantics supplied by the touch
            // backend. Keyboard releases get a small thumb-roll stabilization below;
            // other UI releases remain unchanged.
            const MeshInkTouchPrimarySample sample=meshink_touch_read_primary();
            const int16_t x=sample.x,y=sample.y;
            const bool keyboard_active=keyboard_visible||keyboard_landscape;
            const bool suppressed_home=sample.home&&keyboard_active;
            const bool home=sample.home&&!keyboard_active;
            const bool pressed=sample.pressed&&!suppressed_home;
            if(suppressed_home){
                // The capacitive HOME frame arrives as both home=true and
                // pressed=true with no meaningful coordinate. Consume the
                // entire frame while a keyboard is active so it cannot fall
                // through as a (0,0) tap and dismiss the portrait keyboard.
                held=false;home_held=false;quick_slider_dragging=false;display_slider_dragging=false;
                keyboard_delete_hold=false;keyboard_delete_repeated=false;
            }else if(home){
                if(!home_held){QueuedTap tap{0,0,0,0,true};xQueueSend(touch_queue,&tap,0);}
                home_held=true;
                held=false;quick_slider_dragging=false;display_slider_dragging=false;
                keyboard_delete_hold=false;keyboard_delete_repeated=false;
            }else if(home_held){
                if(!pressed)home_held=false;
            }else if(pressed){
                last_x=x;last_y=y;
                if(!held){
                    held=true;start_x=x;start_y=y;pressed_at=millis();frontlight_event();
                    keyboard_delete_hold=false;keyboard_delete_repeated=false;
                    if(keyboard_message_mode&&(keyboard_visible||keyboard_landscape)){
                        const auto delete_metrics=keyboard_metrics(keyboard_landscape);
                        keyboard_delete_hold=
                            meshink_keyboard::in_row(y,delete_metrics.mode_key.y,delete_metrics)&&
                            x>=meshink_keyboard::delete_split(delete_metrics);
                        if(keyboard_delete_hold)
                            keyboard_delete_repeat_at=pressed_at+350;
                    }
                    const MeshInkUiLayout& layout=portrait_layout();
                    const MeshInkUiRect quick_touch=meshink_quick_slider_touch_rect(layout);
                    quick_slider_dragging=quick_panel_active&&
                        x>=quick_touch.x&&x<quick_touch.x+quick_touch.width&&
                        y>=quick_touch.y&&y<quick_touch.y+quick_touch.height;
                    const MeshInkUiRect display_touch=meshink_display_slider_touch_rect(layout);
                    display_slider_dragging=!quick_panel_active&&screen==Screen::DisplaySettings&&
                        x>=display_touch.x&&x<display_touch.x+display_touch.width&&
                        y>=display_touch.y&&y<display_touch.y+display_touch.height;
                }
                if(quick_slider_dragging||display_slider_dragging) {
                    const MeshInkUiLayout& layout=portrait_layout();
                    const MeshInkUiRect slider=quick_slider_dragging?
                        meshink_quick_slider_track_rect(layout):
                        meshink_display_slider_track_rect(layout);
                    const int slider_right=slider.x+slider.width;
                    const int clamped=max(slider.x,min(slider_right,(int)x));
                    const int value=((clamped-slider.x)*100+slider.width/2)/
                        slider.width;
                    quick_slider_preview=(uint8_t)max(0,min(100,value));
                    frontlight_preview(quick_slider_preview);
                }
                if(keyboard_delete_hold&&!quick_panel_active&&
                   (int32_t)(millis()-keyboard_delete_repeat_at)>=0){
                    QueuedTap repeat{start_x,start_y,0,0,false};
                    if(xQueueSend(touch_queue,&repeat,0)==pdTRUE)
                        keyboard_delete_repeated=true;
                    keyboard_delete_repeat_at=millis()+45;
                }
            }else if(held){
                held=false;
                quick_slider_dragging=false;display_slider_dragging=false;
                const int16_t dx=(int16_t)(last_x-start_x);
                const int16_t dy=(int16_t)(last_y-start_y);
                int16_t event_x=last_x,event_y=last_y;
                // Keyboard taps are discrete selections, not drag gestures.
                // Always resolve them from the original touch-down position so
                // thumb roll on release, or one transient bad final sample,
                // cannot move the event into a neighbour. Other UI and map
                // gestures continue to use their release position unchanged.
                const bool keyboard_touch=!quick_panel_active&&
                    (keyboard_landscape||keyboard_visible);
                if(keyboard_touch){
                    event_x=start_x;
                    event_y=start_y;
                }
                const bool suppress_release=
                    keyboard_delete_hold&&keyboard_delete_repeated;
                keyboard_delete_hold=false;keyboard_delete_repeated=false;
                if(!suppress_release){
                    QueuedTap tap{event_x,event_y,dx,dy,false};
                    xQueueSend(touch_queue,&tap,0);
                }
            }
        }
        // Repeated letters can be typed faster than the ordinary 8 ms polling
        // cadence observes the brief release between two taps on the same key.
        // Poll more aggressively only while a keyboard is active; every other
        // screen keeps the lower-overhead 8 ms cadence.
        const uint32_t sample_ms=(keyboard_visible||keyboard_landscape)?2:8;
        vTaskDelay(pdMS_TO_TICKS(sample_ms));
    }
}

static void cycle_keyboard_mode(){
    if(keyboard_symbols){keyboard_symbols=false;keyboard_upper=true;}
    else if(keyboard_upper)keyboard_upper=false;
    else keyboard_symbols=true;
}
static void append(char c) {
    if(screen==Screen::SetupRadio){
        char* value=setup_edit_field?setup_power:setup_freq;
        const size_t limit=setup_edit_field?sizeof(setup_power)-1:sizeof(setup_freq)-1;
        if(!((c>='0'&&c<='9')||(!setup_edit_field&&c=='.')))return;
        const size_t length=strlen(value);
        if(c=='.'&&strchr(value,'.'))return;
        if(length<limit){value[length]=c;value[length+1]=0;}
        return;
    }
    if(screen==Screen::ChannelCreate){
        char* value=channel_form_key_field?channel_form_key_hex:channel_form_name;
        const size_t limit=channel_form_key_field?32:
            min(sizeof(channel_form_name)-1,ui_data?ui_data->channel_name_limit():(size_t)12);
        if(channel_form_key_field){
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return;
            if(c>='a'&&c<='f')c=(char)(c-'a'+'A');
        }else if((unsigned char)c<33||(unsigned char)c>126)return;
        const size_t n=strlen(value);
        if(n<limit){value[n]=c;value[n+1]=0;}
        return;
    }
    if(keyboard_password_mode){size_t n=strlen(remote_password);if(n<15){remote_password[n]=c;remote_password[n+1]=0;}return;}
    if(keyboard_message_mode){
        const size_t n=strlen(compose_text);
        if(n<MESHINK_MESSAGE_TEXT_MAX){
            compose_text[n]=c;compose_text[n+1]=0;
            // Fresh messages start with sentence-style uppercase. Once the
            // first alphabetic character is entered, fall back to lowercase
            // unless the user already selected another keyboard mode.
            if(n==0&&!keyboard_symbols&&keyboard_upper&&
               ((c>='A'&&c<='Z')||(c>='a'&&c<='z'))){
                keyboard_upper=false;
                message_keyboard_case_dirty=true;
            }
        }
        return;
    }
    if(c==' '||!mesh_protocol_name_character_allowed(c)){T5_DEBUGF(T5_LOG_UI,"[T5-UI] discarded protocol-invalid name character 0x%02X\n",(unsigned char)c);return;}
    if (replace_name_on_type) { node_name[0]=0; replace_name_on_type=false; }
    const size_t limit=min(sizeof(node_name)-1,mesh_protocol_node_name_max_length());
    size_t n=strlen(node_name); if (n<limit) { node_name[n]=c; node_name[n+1]=0; saved=false; }
}
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh) { return x>=bx&&x<bx+bw&&y>=by&&y<by+bh; }
static bool hit(int16_t x,int16_t y,const MeshInkUiRect& rect) {
    return hit(x,y,rect.x,rect.y,rect.width,rect.height);
}
static bool hit_header_back(int16_t x,int16_t y) {
    return hit(x,y,meshink_header_back_touch_rect(portrait_layout()));
}
static bool hit_header_action(int16_t x,int16_t y) {
    return hit(x,y,meshink_header_action_touch_rect(portrait_layout()));
}
static bool hit_outer_row(int16_t x,int16_t y,int reference_top,int reference_height=112) {
    return hit(x,y,meshink_outer_row_rect(portrait_layout(),reference_top,reference_height));
}
static void open_screen(Screen next,bool preserve_map_centre=false) {
    // Opening Maps normally goes to our current or last known GPS location.
    // A node's explicit "OPEN POSITION ON MAP" uses preserve_map_centre=true
    // and therefore does not get unexpectedly re-centred on the device.
    if(next==Screen::Maps&&screen!=Screen::Maps&&!preserve_map_centre)
        centre_map_on_device();
    const bool already_on_map=screen==Screen::Maps;
    map_taps={}; // prevent a pending map double tap from firing on another UI
    text_refresh_pending=false;
    if(screen==Screen::ChannelCreate&&next!=Screen::ChannelCreate){
        memset(channel_form_key_hex,0,sizeof(channel_form_key_hex));
        memset(channel_form_name,0,sizeof(channel_form_name));
    }
    keyboard_visible=next==Screen::ChannelCreate;
    keyboard_message_mode=false;keyboard_password_mode=false;
    save_remote_password=false;remote_password[0]=0;screen=next;
    if(next==Screen::Maps) {
        load_map_with_feedback(already_on_map);
        return;
    }
    draw_screen();refresh(MeshInkRefreshMode::FastGray16);
    // The e-paper transition above blocks the UI task while the touch sampler
    // keeps running on the other core. Ignore releases that completed before
    // this new page became visible; their coordinates belong to the previous
    // page and replaying them can trigger several unintended full refreshes.
    // A press held until after the transition remains eligible because its
    // release timestamp is newer than this cutoff.
    navigation_touch_cutoff_ms=millis();
}
// A zoom changes the map centre so the geographic location under the FIRST
// tap / starting pinch midpoint stays under the same screen pixel. Display
// update remains exclusively in the established one-shot Maps loading path.
static void zoom_map_around(int steps,int anchor_x,int anchor_y) {
    if(screen!=Screen::Maps||standby_active||!steps||
       !meshink_map_gestures::terrain_point(anchor_x,anchor_y))return;
    const int next_zoom=max(meshink_map_gestures::MIN_ZOOM,
                            min(meshink_map_gestures::MAX_ZOOM,
                                (int)map_zoom+steps));
    if(next_zoom==(int)map_zoom)return;
    const auto centre=meshink_map_gestures::zoom_about(
        map_latitude,map_longitude,map_zoom,next_zoom,anchor_x,anchor_y,
        portrait_layout());
    map_latitude=centre.latitude;
    map_longitude=centre.longitude;
    map_zoom=(uint8_t)next_zoom;
    map_taps={};
    open_screen(Screen::Maps);
}

static void queue_text_refresh(){
    // Throttle/coalesce rather than debounce. The first character schedules
    // the next paint; later characters join it instead of pushing it farther
    // into the future. This caps visible typing latency during continuous input.
    if(text_refresh_pending)return;
    text_refresh_pending=true;
    text_refresh_queued_at=millis();
    text_refresh_after=text_refresh_queued_at+110;
}
static void set_keyboard_orientation(bool landscape){
    keyboard_landscape=landscape;
    set_ui_orientation(landscape?MeshInkOrientation::Landscape:MeshInkOrientation::Portrait);
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] keyboard orientation=%s\n",landscape?"landscape":"portrait");
    draw_screen();refresh(MeshInkRefreshMode::FastGray16);
}
static void save_node_name(){
    prefs.begin("t5-ui",false);prefs.putString("name",node_name);prefs.putUChar("preset_v2",selected_preset);
    prefs.putBool("name_migrated",true);prefs.end();
    if(mesh_is_ready)mesh_protocol_apply_name(node_name);
    saved=true;
}

static bool handle_landscape_keyboard(int16_t x,int16_t y){
    if(!keyboard_landscape)return false;
    const auto metrics=keyboard_metrics(true);
    // Give the mode and Delete buttons the full third-row edge areas.
    if(meshink_keyboard::in_row(y,metrics.mode_key.y,metrics)){
        if(x<meshink_keyboard::mode_split(metrics)){cycle_keyboard_mode();draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
        if(x>=meshink_keyboard::delete_split(metrics)){
            char* value=keyboard_password_mode?remote_password:(keyboard_message_mode?compose_text:node_name);
            const size_t n=strlen(value);
            if(n)value[n-1]=0;
            if(!keyboard_message_mode&&!keyboard_password_mode)saved=false;
            queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,true,character)){
        append(character);queue_text_refresh();return true;
    }
    if(meshink_keyboard::in_row(y,metrics.bottom_top,metrics)){
        if(x<meshink_keyboard::orientation_split(metrics)){set_keyboard_orientation(false);return true;}
        if(x<meshink_keyboard::action_split(metrics)){
            if(keyboard_message_mode||keyboard_password_mode||mesh_protocol_name_character_allowed(' ')){
                append(' ');queue_text_refresh();
            }
            return true;
        }
        if(keyboard_password_mode){
            const bool ok=ui_data&&ui_data->login_active_node(remote_password,save_remote_password);
            memset(remote_password,0,sizeof(remote_password));keyboard_password_mode=false;keyboard_visible=false;save_remote_password=false;
            keyboard_landscape=false;set_ui_orientation(MeshInkOrientation::Portrait);
            show_toast(ok?"LOGIN REQUESTED":"LOGIN FAILED");draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;
        }
        if(keyboard_message_mode){
            if(!compose_text[0])return true;
            if(!mesh_protocol_send_active(compose_text))return true;
            compose_text[0]=0;text_refresh_pending=false;
            keyboard_symbols=false;keyboard_upper=true;message_keyboard_case_dirty=false;
            keyboard_visible=true;set_keyboard_orientation(false);
            return true;
        }
        // Wizard ENTER dismisses editing; only NEXT advances the setup.
        if(screen==Screen::SetupName||screen==Screen::SetupRadio){
            keyboard_visible=false;
            set_keyboard_orientation(false);
            return true;
        }
        keyboard_visible=true;set_keyboard_orientation(false);
        return true;
    }
    return true;
}

static bool handle_password_keyboard(int16_t x,int16_t y) {
    if(!keyboard_visible||!keyboard_password_mode)return false;
    const auto metrics=keyboard_metrics(false);
    if(hit(x,y,meshink_password_save_rect(portrait_layout()))){save_remote_password=!save_remote_password;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
    if(meshink_keyboard::in_row(y,metrics.mode_key.y,metrics)){
        if(x<meshink_keyboard::mode_split(metrics)){cycle_keyboard_mode();draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
        if(x>=meshink_keyboard::delete_split(metrics)){const size_t n=strlen(remote_password);if(n)remote_password[n-1]=0;queue_text_refresh();return true;}
    }
    char character=0;
    if(keyboard_character_at(x,y,false,character)){append(character);queue_text_refresh();return true;}
    if(meshink_keyboard::in_row(y,metrics.bottom_top,metrics)){
        if(x<meshink_keyboard::orientation_split(metrics)){set_keyboard_orientation(true);return true;}
        if(x<meshink_keyboard::action_split(metrics)){append(' ');queue_text_refresh();return true;}
        const bool ok=ui_data&&ui_data->login_active_node(remote_password,save_remote_password);
        memset(remote_password,0,sizeof(remote_password));keyboard_password_mode=false;keyboard_visible=false;save_remote_password=false;
        show_toast(ok?"LOGIN REQUESTED":"LOGIN FAILED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
    }
    return true;
}

static bool handle_message_keyboard(int16_t x,int16_t y) {
    if(!keyboard_visible||!keyboard_message_mode)return false;
    const auto metrics=keyboard_metrics(false);
    // There is no HIDE key in message composition. Tapping anywhere above
    // the keyboard dismisses it, matching common mobile keyboard behaviour
    // and eliminating the easy-to-hit button beside SEND.
    if(y<metrics.dismiss_above){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
    if(meshink_keyboard::in_row(y,metrics.mode_key.y,metrics)){
        if(x<meshink_keyboard::mode_split(metrics)){cycle_keyboard_mode();draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
        if(x>=meshink_keyboard::delete_split(metrics)){
            const size_t n=strlen(compose_text);
            if(n)compose_text[n-1]=0;
            queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,false,character)){
        append(character);queue_text_refresh();return true;
    }
    if(meshink_keyboard::in_row(y,metrics.bottom_top,metrics)){
        if(x<meshink_keyboard::orientation_split(metrics)){set_keyboard_orientation(true);return true;}
        if(x<meshink_keyboard::action_split(metrics)){append(' ');queue_text_refresh();return true;}
        if(compose_text[0]){
            const bool ok=mesh_protocol_send_active(compose_text);
            if(ok){
                compose_text[0]=0;keyboard_visible=false;text_refresh_pending=false;
                keyboard_symbols=false;keyboard_upper=true;message_keyboard_case_dirty=false;
            }
            draw_screen();refresh(MeshInkRefreshMode::Direct);
        }
        return true;
    }
    return true;
}

static bool handle_name_keyboard(int16_t x,int16_t y){
    if(!keyboard_visible||keyboard_message_mode)return false;
    const auto metrics=keyboard_metrics(false);
    const bool wizard_entry=screen==Screen::SetupName||screen==Screen::SetupRadio;
    if(wizard_entry&&y<metrics.dismiss_above){
        text_refresh_pending=false;keyboard_visible=false;
        draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
    }
    // In Protocol Settings, tapping above the keyboard dismisses name editing.
    // First-time setup keeps its explicit setup controls and save flow.
    if(screen==Screen::ProtocolSettings&&y<metrics.dismiss_above){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
    if(meshink_keyboard::in_row(y,metrics.mode_key.y,metrics)){
        if(x<meshink_keyboard::mode_split(metrics)){cycle_keyboard_mode();draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
        if(x>=meshink_keyboard::delete_split(metrics)){
            char* value=screen==Screen::SetupRadio
                ?(setup_edit_field?setup_power:setup_freq):node_name;
            const size_t n=strlen(value);
            if(n)value[n-1]=0;
            saved=false;queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,false,character)){
        append(character);queue_text_refresh();return true;
    }
    if(meshink_keyboard::in_row(y,metrics.bottom_top,metrics)){
        if(x<meshink_keyboard::orientation_split(metrics)){set_keyboard_orientation(true);return true;}
        if(!wizard_entry&&mesh_protocol_name_character_allowed(' ')&&x<meshink_keyboard::action_split(metrics)){
            append(' ');queue_text_refresh();return true;
        }
        if(wizard_entry){
            keyboard_visible=false;
            text_refresh_pending=false;
            draw_screen();refresh(MeshInkRefreshMode::Direct);
            return true;
        }
        if(!node_name[0]){show_toast("NAME REQUIRED");return true;}
        save_node_name();
        keyboard_visible=false;
        show_toast("IDENTITY SAVED");
        draw_screen();refresh(MeshInkRefreshMode::Direct);
        return true;
    }
    return true;
}


static void setup_enter(Screen next){
    keyboard_visible=false;
    keyboard_landscape=false;
    open_screen(next);
}

static void backup_show_result(const char* value){
    snprintf(backup_result_message,sizeof(backup_result_message),"%s",
             value&&value[0]?value:"BACKUP OR RESTORE FAILED");
    open_screen(Screen::BackupResult);
}
static void backup_open_files(Screen source){
    backup_return_screen=source;
    backup_from_setup=!setup_complete;
    backup_restore_mode=true;
    backup_page=0;
    backup_count=meshink_backup_list(mesh_protocol_descriptor().id,backup_entries,
                    sizeof(backup_entries)/sizeof(backup_entries[0]));
    open_screen(Screen::BackupFiles);
}
static bool handle_backup_tap(int16_t x,int16_t y){
    if(screen==Screen::BackupFiles){
        if(hit_header_back(x,y)){open_screen(backup_return_screen);return true;}
        const size_t first=backup_page*5;
        for(size_t i=first;i<backup_count&&i<first+5;++i){
            if(!hit_outer_row(x,y,170+(int)(i-first)*119))continue;
            snprintf(backup_filename,sizeof(backup_filename),"%s",backup_entries[i].filename);
            backup_available=meshink_backup_categories(mesh_protocol_descriptor().id,backup_filename);
            if(!backup_available){backup_show_result(meshink_backup_error());return true;}
            backup_flags=backup_available;
            backup_restore_mode=true;
            open_screen(Screen::BackupOptions);return true;
        }
        if(hit(x,y,ui_rect(24,800,232,74))&&backup_page>0){
            --backup_page;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(hit(x,y,ui_rect(284,800,232,74))&&(backup_page+1)*5<backup_count){
            ++backup_page;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        return true;
    }
    if(screen==Screen::BackupOptions){
        if(hit_header_back(x,y)){
            if(backup_restore_mode)backup_open_files(backup_return_screen);
            else open_screen(Screen::ProtocolSettings);
            return true;
        }
        constexpr uint8_t flags[]={1,2,4};
        for(int i=0;i<3;++i)if(hit(x,y,ui_rect(24,245+i*124,492,100))){
            if(backup_available&flags[i])backup_flags^=flags[i];
            draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(hit(x,y,ui_rect(24,811,232,80))){
            backup_open_files(backup_return_screen);return true;
        }
        if(hit(x,y,ui_rect(284,811,232,80))){
            if(!backup_flags){show_toast("SELECT A CATEGORY");return true;}
            if(backup_restore_mode){open_screen(Screen::BackupConfirm);return true;}
            char saved[32]{};
            if(meshink_backup_create(mesh_protocol_descriptor().id,backup_flags,
                                     saved,sizeof(saved))){
                char result[84]{};
                snprintf(result,sizeof(result),"BACKUP VERIFIED: %s",saved);
                backup_show_result(result);
            }else backup_show_result(meshink_backup_error());
            return true;
        }
        return true;
    }
    if(screen==Screen::BackupConfirm){
        if(hit_header_back(x,y)||hit(x,y,ui_rect(24,735,232,85))){
            open_screen(Screen::BackupOptions);return true;
        }
        if(hit(x,y,ui_rect(284,735,232,85))){
            if(meshink_backup_restore(mesh_protocol_descriptor().id,
                                       backup_filename,backup_flags))
                mesh_protocol_restart_into(mesh_protocol_descriptor().id);
            else backup_show_result(meshink_backup_error());
        }
        return true;
    }
    if(screen==Screen::BackupResult){
        if(hit_header_back(x,y)||hit(x,y,ui_rect(85,780,370,80))){
            open_screen(backup_from_setup?backup_return_screen:Screen::ProtocolSettings);
        }
        return true;
    }
    return false;
}

static bool handle_setup_tap(int16_t x,int16_t y){
    if(!setup_is_screen(screen))return false;
    if((screen==Screen::SetupName||screen==Screen::SetupRadio)&&keyboard_visible){
        // Keep all editable values above the existing portrait keyboard.
        if(screen==Screen::SetupName||screen==Screen::SetupRadio){
            return handle_name_keyboard(x,y);
        }
    }
    if(screen!=Screen::Welcome&&screen!=Screen::SetupCancel&&
       setup_any_done&&hit_header_action(x,y)){
        setup_cancel_from=screen;
        setup_enter(Screen::SetupCancel);
        return true;
    }
    if(screen==Screen::SetupCancel){
        if(hit(x,y,ui_rect(24,660,232,75))){
            setup_enter(setup_cancel_from);return true;
        }
        if(hit(x,y,ui_rect(284,660,232,75))){
            if(setup_return_protocol&&setup_protocol_done(setup_return_protocol))
                mesh_protocol_restart_into(setup_return_protocol);
            else show_toast("RETURN NOT AVAILABLE");
            return true;
        }
        return true;
    }
    if(screen==Screen::Welcome){
        if(hit(x,y,ui_rect(60,760,420,62))){
            if(!setup_protocol_choice){show_toast("CHOOSE A PROTOCOL");return true;}
            if(setup_protocol_choice!=mesh_protocol_descriptor().id){
                Preferences p;
                if(p.begin("t5-ui",false)){
                    p.putUChar("setup_choice",setup_protocol_choice);
                    p.putBool("restore_pending",true);
                    p.end();
                }
                mesh_protocol_restart_into(setup_protocol_choice);
            }else backup_open_files(Screen::Welcome);
            return true;
        }
        for(size_t i=0;i<mesh_protocol_available_count()&&i<4;++i){
            const auto* choice=mesh_protocol_available(i);
            if(choice&&hit_outer_row(x,y,280+(int)i*145)){
                setup_protocol_choice=choice->id;
                draw_screen();refresh(MeshInkRefreshMode::Direct);
                return true;
            }
        }
        if(hit(x,y,ui_rect(284,850,232,74))){
            if(!setup_protocol_choice){show_toast("CHOOSE A PROTOCOL");return true;}
            if(setup_protocol_choice!=mesh_protocol_descriptor().id){
                Preferences p;
                if(p.begin("t5-ui",false)){
                    p.putUChar("setup_return",0);
                    p.putUChar("setup_choice",setup_protocol_choice);
                    p.end();
                }
                mesh_protocol_restart_into(setup_protocol_choice);
            }else{
                Preferences choice;
                if(choice.begin("t5-ui",false)){
                    choice.putUChar("setup_choice",setup_protocol_choice);
                    choice.end();
                }
                setup_initialize_draft();
                setup_enter(Screen::SetupName);
            }
        }
        return true;
    }
    if(screen==Screen::SetupName){
        if(!keyboard_visible&&hit(x,y,ui_rect(60,720,420,70))){
            backup_open_files(Screen::SetupName);return true;
        }
        if(hit(x,y,ui_rect(24,185,492,96))||
           hit(x,y,ui_rect(60,450,420,74))){
            keyboard_visible=true;
            replace_name_on_type=false;
            draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(hit(x,y,ui_rect(24,850,232,74))){
            setup_enter(setup_any_done?Screen::SetupCancel:Screen::Welcome);return true;
        }
        if(hit(x,y,ui_rect(284,850,232,74))){
            if(!node_name[0]||strlen(node_name)>20||strchr(node_name,' ')){
                show_toast("INVALID NODE NAME");return true;
            }
            setup_enter(Screen::SetupRegion);return true;
        }
        return true;
    }
    if(screen==Screen::SetupRegion||screen==Screen::SetupPreset){
        const bool is_region=screen==Screen::SetupRegion;
        const size_t count=is_region?setup_region_count():setup_preset_count();
        uint8_t& page=is_region?setup_region_page:setup_preset_page;
        const size_t first=(size_t)page*5;
        for(size_t i=first;i<count&&i<first+5;++i){
            if(!hit_outer_row(x,y,142+(int)(i-first)*116))continue;
            if(is_region)setup_region=(uint8_t)i;
            else if(setup_is_meshcore())setup_load_core_preset(setup_core_preset_at(i));
            else setup_radio_preset=(int)i;
            draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(count>5&&hit(x,y,ui_rect(24,744,232,65))){
            if(page)page--;
            draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(count>5&&hit(x,y,ui_rect(284,744,232,65))){
            if(((size_t)page+1)*5<count)page++;
            draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(hit(x,y,ui_rect(24,850,232,74))){
            setup_enter(is_region?Screen::SetupName:Screen::SetupRegion);return true;
        }
        if(hit(x,y,ui_rect(284,850,232,74))){
            if(is_region){
                setup_preset_page=0;
                if(setup_is_meshcore()){
                    if(setup_region==0)setup_load_core_preset(17); // NZ NARROW default
                    else setup_load_core_preset(-1);
                }else setup_radio_preset=0; // LongFast
                setup_enter(Screen::SetupPreset);
            }else{
                if(setup_radio_preset<-1){show_toast("CHOOSE A PRESET");return true;}
                setup_enter(Screen::SetupRadio);
            }
            return true;
        }
        return true;
    }
    if(screen==Screen::SetupRadio){
        if(setup_is_meshcore()){
            if(hit(x,y,ui_rect(24,174,492,88))||
               hit(x,y,ui_rect(24,317,492,88))){
                setup_edit_field=hit(x,y,ui_rect(24,317,492,88))?1:0;
                keyboard_visible=true;
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            if(hit(x,y,ui_rect(24,443,492,70))){
                constexpr float values[]={62.5f,125.0f,250.0f,500.0f};
                size_t current=0;
                for(size_t i=0;i<4;++i)if(setup_bw==values[i])current=i+1;
                setup_bw=values[current%4];
            }else if(hit(x,y,ui_rect(24,524,492,70))){
                setup_sf=setup_sf>=12?5:setup_sf?setup_sf+1:7;
            }else if(hit(x,y,ui_rect(24,605,492,70))){
                setup_cr=setup_cr>=8?5:setup_cr?setup_cr+1:5;
            }else if(hit(x,y,ui_rect(24,686,492,70))){
                setup_hash=setup_hash>=3?1:setup_hash+1;
            }else if(hit(x,y,ui_rect(24,850,232,74))){
                setup_enter(Screen::SetupPreset);return true;
            }else if(hit(x,y,ui_rect(284,850,232,74))){
                if(!setup_radio_valid()){show_toast("CHECK RADIO VALUES");return true;}
                setup_enter(Screen::SetupReview);return true;
            }else return true;
        }else{
            if(hit_outer_row(x,y,436))setup_hops=(uint8_t)(setup_hops>=7?1:setup_hops+1);
            else if(hit(x,y,ui_rect(24,850,232,74))){setup_enter(Screen::SetupPreset);return true;}
            else if(hit(x,y,ui_rect(284,850,232,74))){setup_enter(Screen::SetupReview);return true;}
            else return true;
        }
        draw_screen();refresh(MeshInkRefreshMode::Direct);
        return true;
    }
    if(screen==Screen::SetupReview){
        if(hit(x,y,ui_rect(24,850,232,74))){
            setup_enter(Screen::SetupRadio);return true;
        }
        if(hit(x,y,ui_rect(284,850,232,74))){
            if(!setup_finish()){
                show_toast("COULD NOT SAVE SETUP");
                draw_screen();refresh(MeshInkRefreshMode::Direct);
            }
        }
        return true;
    }
    return true;
}

static bool handle_channel_form_keyboard(int16_t x,int16_t y){
    if(screen!=Screen::ChannelCreate||!keyboard_visible)return false;
    const auto metrics=keyboard_metrics(false);
    if(y<metrics.dismiss_above)return false;
    if(meshink_keyboard::in_row(y,metrics.mode_key.y,metrics)){
        if(x<meshink_keyboard::mode_split(metrics)){
            cycle_keyboard_mode();draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
        }
        if(x>=meshink_keyboard::delete_split(metrics)){
            char* value=channel_form_key_field?channel_form_key_hex:channel_form_name;
            const size_t n=strlen(value);if(n)value[n-1]=0;
            queue_text_refresh();return true;
        }
    }
    char c=0;
    if(keyboard_character_at(x,y,false,c)){
        append(c);queue_text_refresh();return true;
    }
    if(meshink_keyboard::in_row(y,metrics.bottom_top,metrics)){
        // HIDE and DONE both dismiss the keyboard; the ADD action in the
        // header is the only operation that writes a new channel.
        keyboard_visible=false;draw_screen();refresh(MeshInkRefreshMode::Direct);
        return true;
    }
    return true;
}

static bool handle_app_tap(int16_t x,int16_t y) {
    if(screen==Screen::Welcome||screen==Screen::Presets||screen==Screen::CompanionConfirm||screen==Screen::ShutdownConfirm)return false;
    if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&hit_header_back(x,y)){keyboard_visible=false;keyboard_message_mode=false;reset_chat_paging();open_screen(screen==Screen::ChannelChat?Screen::Channels:Screen::Contacts);return true;}
    if(screen==Screen::ContactChat&&hit_header_action(x,y)){keyboard_visible=false;keyboard_message_mode=false;details_from_discovery=false;details_page=0;open_screen(Screen::ContactDetails);return true;}
    if(screen==Screen::ContactDetails&&handle_password_keyboard(x,y))return true;
    if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&handle_message_keyboard(x,y))return true;
    if(screen==Screen::ProtocolSettings&&keyboard_visible&&handle_name_keyboard(x,y))return true;
    if(screen==Screen::ChannelCreate&&handle_channel_form_keyboard(x,y))return true;
    const bool chat_main_page=(screen==Screen::ContactChat||screen==Screen::ChannelChat)&&
                              !keyboard_visible&&chat_page==0;
    if(screen!=Screen::ChannelCreate&&screen!=Screen::ChannelDelete&&
       ((screen!=Screen::ContactChat&&screen!=Screen::ChannelChat)||chat_main_page)&&
       y>=portrait_layout().bottom_nav_top){
        const int tab=min(3,max(0,(int)x/portrait_layout().tab_width));open_screen(tab==0?Screen::Contacts:tab==1?Screen::Channels:tab==2?Screen::Maps:Screen::More);return true;}
    switch(screen){
        case Screen::Contacts:
            if(ui_data){
                const size_t count=ui_data->contact_count();
                clamp_list_page(contacts_page,count);
                const size_t first=contacts_page*LIST_ITEMS_PER_PAGE;
                for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                    const size_t index=first+row;
                    if(hit_outer_row(x,y,portrait_layout().list_top+
                        row*portrait_layout().list_row_stride,
                        portrait_layout().list_row_height)){selected_contact=index;if(ui_data->open_contact(index)){reset_chat_paging();open_screen(Screen::ContactChat);}return true;}
                }
            }
            break;
        case Screen::Channels:
            if(hit_header_action(x,y)){channel_manage_page=0;open_screen(Screen::ChannelManage);return true;}
            if(ui_data){
                const size_t count=ui_data->channel_count();
                clamp_list_page(channels_page,count);
                const size_t first=channels_page*LIST_ITEMS_PER_PAGE;
                for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                    const size_t index=first+row;
                    if(hit_outer_row(x,y,portrait_layout().list_top+
                        row*portrait_layout().list_row_stride,
                        portrait_layout().list_row_height)){selected_channel=index;if(ui_data->open_channel(index)){reset_chat_paging();open_screen(Screen::ChannelChat);}return true;}
                }
            }
            break;
        case Screen::ChannelManage:
            if(hit_header_back(x,y)){open_screen(Screen::Channels);return true;}
            if(hit_header_action(x,y)){
                memset(channel_form_name,0,sizeof(channel_form_name));
                memset(channel_form_key_hex,0,sizeof(channel_form_key_hex));
                channel_form_key_field=false;keyboard_symbols=false;keyboard_upper=true;
                open_screen(Screen::ChannelCreate);return true;
            }
            if(ui_data){
                const size_t count=ui_data->channel_count();
                clamp_list_page(channel_manage_page,count);
                const size_t first=channel_manage_page*LIST_ITEMS_PER_PAGE;
                for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                    const size_t index=first+row;
                    if(hit_outer_row(x,y,portrait_layout().list_top+
                        row*portrait_layout().list_row_stride,portrait_layout().list_row_height)){
                        channel_delete_index=index;
                        strncpy(channel_delete_title,ui_data->channel(index).title,
                                sizeof(channel_delete_title)-1);
                        channel_delete_title[sizeof(channel_delete_title)-1]=0;
                        open_screen(Screen::ChannelDelete);return true;
                    }
                }
            }
            break;
        case Screen::ChannelCreate:
            if(hit_header_back(x,y)){open_screen(Screen::ChannelManage);return true;}
            if(hit_header_action(x,y)){
                if(!ui_data||!ui_data->channel_management_available()){
                    show_toast("COMING SOON");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(!meshink_channel_key::valid_name(channel_form_name,ui_data->channel_name_limit())){
                    show_toast("INVALID CHANNEL NAME");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(channel_form_key_hex[0]&&strlen(channel_form_key_hex)!=32){
                    show_toast("KEY MUST BE 32 HEX");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(!ui_data->create_channel(channel_form_name,channel_form_key_hex)){
                    show_toast("CHANNEL SAVE FAILED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                channel_manage_page=0;
                show_toast("CHANNEL ADDED");open_screen(Screen::ChannelManage);return true;
            }
            if(hit(x,y,ui_rect(22,162,496,82))){
                channel_form_key_field=false;keyboard_visible=true;
                keyboard_symbols=false;keyboard_upper=true;
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            if(hit(x,y,ui_rect(22,303,496,82))){
                channel_form_key_field=true;keyboard_visible=true;
                keyboard_symbols=false;keyboard_upper=true;
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            if(!keyboard_visible&&hit(x,y,ui_rect(55,520,430,70))){
                keyboard_visible=true;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            break;
        case Screen::ChannelDelete:
            if(hit_header_back(x,y)||hit(x,y,meshink_confirm_left_rect(portrait_layout(),650))){
                open_screen(Screen::ChannelManage);return true;
            }
            if(hit(x,y,meshink_confirm_right_rect(portrait_layout(),650))){
                if(!ui_data||!ui_data->channel_removable(channel_delete_index)){
                    show_toast("CHANNEL CANNOT BE DELETED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(channel_delete_index>=ui_data->channel_count()||
                   strcmp(ui_data->channel(channel_delete_index).title,channel_delete_title)!=0){
                    show_toast("CHANNEL LIST CHANGED");open_screen(Screen::ChannelManage);return true;
                }
                if(!ui_data->delete_channel(channel_delete_index)){
                    show_toast("CHANNEL DELETE FAILED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                show_toast("CHANNEL REMOVED");open_screen(Screen::ChannelManage);return true;
            }
            break;
        case Screen::ContactChat:
        case Screen::ChannelChat:
            {const auto metrics=keyboard_metrics(false);const MeshInkUiLayout& layout=portrait_layout();const int compose_y=chat_compose_top();
            if(chat_page==0&&hit(x,y,layout.outer_margin,compose_y,layout.outer_width,metrics.key_height)){
                keyboard_message_mode=true;keyboard_visible=true;reset_chat_paging();
                if(!compose_text[0]){keyboard_symbols=false;keyboard_upper=true;message_keyboard_case_dirty=false;}
                text_refresh_pending=false;
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }}break;
        case Screen::ContactDetails:
            if(hit_header_back(x,y)){open_screen(details_from_discovery?Screen::Discovery:Screen::ContactChat);return true;}
            {UiNodeDetails node{};if(ui_data&&ui_data->active_node_details(node)){
                const NodeInfoPage page=node_info_page(node.capabilities,details_page);
                const bool login_required=node_has_capability(node.capabilities,UI_NODE_CAP_LOGIN);
                if(node.saved_contact&&page==NodeInfoPage::Status&&hit(x,y,meshink_node_action_rect(portrait_layout()))){
                    if(login_required&&!node.authenticated){if(!node.login_active){remote_password[0]=0;save_remote_password=ui_data->active_node_saved_password(remote_password,sizeof(remote_password));keyboard_password_mode=true;keyboard_message_mode=false;keyboard_visible=true;draw_screen();refresh(MeshInkRefreshMode::FastGray16);}return true;}
                    show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Status)?"REQUESTING STATUS":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(node.saved_contact&&page==NodeInfoPage::Telemetry&&hit(x,y,meshink_node_action_rect(portrait_layout()))){
                    if(login_required&&!node.authenticated){if(!node.login_active){remote_password[0]=0;save_remote_password=ui_data->active_node_saved_password(remote_password,sizeof(remote_password));keyboard_password_mode=true;keyboard_message_mode=false;keyboard_visible=true;draw_screen();refresh(MeshInkRefreshMode::FastGray16);}return true;}
                    show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Telemetry)?"REQUESTING TELEMETRY":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(node.saved_contact&&page==NodeInfoPage::Path){
                    const bool can_path=node_has_capability(node.capabilities,UI_NODE_CAP_PATH);
                    const bool can_trace=node_has_capability(node.capabilities,UI_NODE_CAP_TRACE);
                    if(can_path&&can_trace&&hit(x,y,meshink_node_left_action_rect(portrait_layout()))){show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Path)?"DISCOVERING PATH":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                    if(can_path&&can_trace&&hit(x,y,meshink_node_right_action_rect(portrait_layout()))){show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Trace)?"TRACE REQUESTED":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                    if(can_path&&!can_trace&&hit(x,y,meshink_node_action_rect(portrait_layout()))){show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Path)?"DISCOVERING PATH":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                    if(can_trace&&!can_path&&hit(x,y,meshink_node_action_rect(portrait_layout()))){show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Trace)?"TRACE REQUESTED":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                }
                if(page==NodeInfoPage::Overview&&(node.latitude||node.longitude)&&hit(x,y,meshink_node_map_rect(portrait_layout()))){map_latitude=node.latitude/1000000.0;map_longitude=node.longitude/1000000.0;open_screen(Screen::Maps,true);return true;}
                if(page==NodeInfoPage::Telemetry&&(node.latitude||node.longitude)&&hit(x,y,meshink_node_map_rect(portrait_layout()))){map_latitude=node.latitude/1000000.0;map_longitude=node.longitude/1000000.0;open_screen(Screen::Maps,true);return true;}
                if(page==NodeInfoPage::Overview&&node.saved_contact&&hit(x,y,meshink_node_left_action_rect(portrait_layout()))){open_screen(Screen::ContactChat);return true;}
                if(page==NodeInfoPage::Overview&&node.saved_contact&&hit(x,y,meshink_node_right_action_rect(portrait_layout()))){show_toast(ui_data->remove_active_contact()?"CONTACT REMOVED":"REMOVE FAILED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                if(page==NodeInfoPage::Overview&&!node.saved_contact&&hit(x,y,meshink_node_action_rect(portrait_layout()))){show_toast(ui_data->add_active_node()?"CONTACT ADDED":"ADD FAILED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            }}break;
        case Screen::Maps:
            // Controls take priority over map markers near the right edge.
            if(hit(x,y,meshink_map_control_rect(portrait_layout(),0))){if(map_zoom<meshink_map_gestures::MAX_ZOOM){map_zoom++;open_screen(Screen::Maps);}return true;}
            if(hit(x,y,meshink_map_control_rect(portrait_layout(),1))){if(map_zoom>meshink_map_gestures::MIN_ZOOM){map_zoom--;open_screen(Screen::Maps);}return true;}
            if(hit(x,y,meshink_map_control_rect(portrait_layout(),2))){
                long latitude=0,longitude=0;bool current_fix=false;
                if(map_device_position(latitude,longitude,current_fix)){
                    centre_map_on_device();
                    show_toast(meshink_board_has_gps()?
                        (current_fix?"CENTRED ON DEVICE":"CENTRED ON LAST FIX"):
                        "CENTRED ON MY LOCATION");
                    open_screen(Screen::Maps);
                    return true;
                }
                // When no own location exists the control is not drawn; allow a
                // node marker beneath this area to receive the tap instead.
            }
            for(size_t i=0;i<map_marker_hit_count;++i) {
                const auto& marker=map_marker_hits[i];
                const int hit_radius=marker.role==UiNodeRole::Relay?14:10;
                if(abs(x-marker.x)<=hit_radius&&abs(y-marker.y)<=hit_radius&&
                   ui_data&&ui_data->open_map_node(marker.index)) {
                    details_from_discovery=false;details_page=0;open_screen(Screen::ContactDetails);return true;
                }
            }
            break;
        case Screen::Discovery:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            if(ui_data){
                const size_t count=ui_data->advert_count();
                clamp_list_page(discovery_page,count);
                const size_t first=discovery_page*LIST_ITEMS_PER_PAGE;
                for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                    const size_t index=first+row;
                    if(hit_outer_row(x,y,portrait_layout().list_top+
                            row*portrait_layout().list_row_stride,
                            portrait_layout().list_row_height)){
                        if(ui_data->open_advert(index)){
                            details_from_discovery=true;details_page=0;
                            open_screen(Screen::ContactDetails);
                        }
                        return true;
                    }
                }
            }
            break;
        case Screen::More:{
            MoreMenuItem items[6]{};
            const size_t count=more_menu_items(items);
            for(size_t i=0;i<count;++i){
                if(!hit_outer_row(x,y,more_row_y(i)))continue;
                switch(items[i].action){
                    case MoreAction::Discovery:open_screen(Screen::Discovery);break;
                    case MoreAction::Advertise:open_screen(Screen::AdvertMenu);break;
                    case MoreAction::Settings:open_screen(Screen::Settings);break;
                    case MoreAction::Companion:open_screen(Screen::CompanionConfirm);break;
                    case MoreAction::Diagnostics:mesh_protocol_request_diagnostics();open_screen(Screen::Diagnostics);break;
                    case MoreAction::Help:open_screen(Screen::Help);break;
                }
                return true;
            }
            break;
        }
        case Screen::AdvertMenu:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            if(hit_outer_row(x,y,180)){show_toast(mesh_protocol_send_advert(false)?"SENDING ZERO HOP ADVERT":"ADVERT BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,320)){show_toast(mesh_protocol_send_advert(true)?"SENDING FLOOD ADVERT":"ADVERT BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}break;
        case Screen::Diagnostics:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            if(hit(x,y,meshink_node_action_rect(portrait_layout()))){
                show_toast(mesh_protocol_request_diagnostics()?"REFRESHING STATS":"DIAGNOSTICS BUSY");
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }break;
        case Screen::Settings:{
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            SettingsMenuItem items[6]{};
            const size_t count=settings_menu_items(items);
            for(size_t i=0;i<count;++i){
                if(!hit_outer_row(x,y,settings_menu_y(i)))continue;
                switch(items[i].action){
                    case SettingsAction::Protocol:open_screen(Screen::ProtocolSelect);break;
                    case SettingsAction::ProtocolSettings:protocol_settings_page=0;open_screen(Screen::ProtocolSettings);break;
                    case SettingsAction::Gps:open_screen(Screen::GpsSettings);break;
                    case SettingsAction::DateTime:open_screen(Screen::DateTime);break;
                    case SettingsAction::DisplayPower:open_screen(Screen::DisplaySettings);break;
                    case SettingsAction::About:open_screen(Screen::About);break;
                }
                return true;
            }
            break;
        }
        case Screen::ProtocolSelect:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            for(size_t i=0;i<mesh_protocol_available_count()&&i<4;++i){
                if(!hit_outer_row(x,y,protocol_select_row_y(i)))continue;
                const auto* protocol=mesh_protocol_available(i);
                if(!protocol)return true;
                if(protocol->id==mesh_protocol_descriptor().id){
                    show_toast("PROTOCOL ALREADY ACTIVE");
                    draw_screen();refresh(MeshInkRefreshMode::Direct);
                    return true;
                }
                // A first-time switch must remain cancellable after reboot.
                Preferences setup_switch;
                if(setup_switch.begin("t5-ui",false)){
                    setup_switch.putUChar("setup_return",
                        setup_protocol_done(protocol->id)?0:mesh_protocol_descriptor().id);
                    setup_switch.end();
                }
                if(!mesh_protocol_select_for_next_boot(protocol->id)){
                    show_toast("PROTOCOL SWITCH FAILED");
                    draw_screen();refresh(MeshInkRefreshMode::Direct);
                    return true;
                }
                char switching[48]{};
                snprintf(switching,sizeof(switching),"RESTARTING INTO %s",protocol->name);
                show_toast(switching);
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
                mesh_protocol_restart_into(protocol->id);
                return true;
            }
            return true;
        case Screen::ProtocolSettings:{
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            const size_t count=protocol_settings_total_count();
            clamp_list_page(protocol_settings_page,count);
            const size_t first=protocol_settings_page*LIST_ITEMS_PER_PAGE;
            for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                if(!hit_outer_row(x,y,protocol_settings_row_y(row)))continue;
                ProtocolSettingsRowKind kind{};
                const char* title="";const char* value="";
                uint16_t backend_id=0;bool editable=true;
                if(!protocol_settings_row(first+row,kind,title,value,backend_id,editable))return true;
                if(!editable){
                    show_toast(value&&value[0]?value:"READ ONLY");
                    draw_screen();refresh(MeshInkRefreshMode::Direct);
                    return true;
                }
                if(kind==ProtocolSettingsRowKind::Backup){
                    backup_return_screen=Screen::ProtocolSettings;
                    backup_restore_mode=false;backup_from_setup=false;
                    backup_available=7;backup_flags=7;
                    open_screen(Screen::BackupOptions);return true;
                }
                if(kind==ProtocolSettingsRowKind::NodeName){
                    replace_name_on_type=false;keyboard_message_mode=false;keyboard_visible=true;
                    text_refresh_pending=false;draw_screen();refresh(MeshInkRefreshMode::Direct);
                    return true;
                }
                if(kind==ProtocolSettingsRowKind::RadioPreset){
                    preset_return_screen=Screen::ProtocolSettings;
                    screen=Screen::Presets;
                    preset_page=selected_preset/PRESETS_PER_PAGE;
                    draw_screen();refresh(MeshInkRefreshMode::FastGray16);
                    return true;
                }
                const auto result=mesh_protocol_activate_setting(backend_id);
                switch(result){
                    case MeshInkProtocolSettingResult::Saved:
                        show_toast("SETTING SAVED");break;
                    case MeshInkProtocolSettingResult::RestartRequired:
                        show_toast("SAVED - RESTART TO APPLY");break;
                    case MeshInkProtocolSettingResult::RestartNow:
                        show_toast("RESTARTING...");
                        draw_screen();refresh(MeshInkRefreshMode::FastGray16);
                        mesh_protocol_restart_into(mesh_protocol_descriptor().id);
                        return true;
                    case MeshInkProtocolSettingResult::Unchanged:
                        show_toast("NO CHANGE");break;
                    default:
                        show_toast("SETTING FAILED");break;
                }
                draw_screen();refresh(MeshInkRefreshMode::Direct);
                return true;
            }
            return true;
        }
        case Screen::GpsSettings:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            if(hit_outer_row(x,y,120)){open_screen(Screen::GpsTuning);return true;}
            if(hit_outer_row(x,y,356)){
                if(centre_map_on_device())open_screen(Screen::Maps,true);
                else{show_toast("NO KNOWN GPS LOCATION");draw_screen();refresh(MeshInkRefreshMode::Direct);}
                return true;
            }
            if(hit_outer_row(x,y,474)){
                const bool enabled=!mesh_protocol_gps_deep_sleep_power_save();
                if(mesh_protocol_gps_set_deep_sleep_power_save(enabled))
                    show_toast(enabled?"DEEP SLEEP POWER SAVE ON":"DEEP SLEEP POWER SAVE OFF");
                else show_toast("SAVE FAILED");
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }break;
        case Screen::GpsTuning:{
            if(hit_header_back(x,y)){open_screen(Screen::GpsSettings);return true;}
            const MeshInkGpsConstellation constellations[]={
                MeshInkGpsConstellation::Gps,
                MeshInkGpsConstellation::BeiDou,
                MeshInkGpsConstellation::Glonass
            };
            const int constellation_rows[]={118,238,358};
            for(uint8_t i=0;i<3;++i){
                if(!hit_outer_row(x,y,constellation_rows[i]))continue;
                const auto current=mesh_protocol_gps_constellation_mode();
                const bool enabled=meshink_gps_constellation_enabled(
                    current,constellations[i]);
                MeshInkGpsConstellationMode next=current;
                if(!meshink_gps_constellation_mode_set(
                        current,constellations[i],!enabled,next)){
                    show_toast("SAVE FAILED");
                }else if(!mesh_protocol_gps_set_constellation_mode(next)){
                    show_toast("SAVE FAILED");
                }else if(current==MeshInkGpsConstellationMode::None&&
                         next!=MeshInkGpsConstellationMode::None){
                    show_toast("GPS ENABLED");
                }else if(current!=MeshInkGpsConstellationMode::None&&
                         next==MeshInkGpsConstellationMode::None){
                    show_toast("GPS DISABLED");
                }else{
                    show_toast("GPS MODE SAVED");
                }
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            break;
        }
        case Screen::DateTime:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            if(hit_outer_row(x,y,238)){
                const MeshInkTimeMode next=mesh_protocol_time_mode()==MeshInkTimeMode::Manual?MeshInkTimeMode::Auto:MeshInkTimeMode::Manual;
                if(mesh_protocol_set_time_mode(next)){status_dirty=true;status_bar_dirty=true;show_toast(next==MeshInkTimeMode::Manual?"MANUAL TIME LOCKED":"AUTO TIME ENABLED");}
                else show_toast("TIME MODE SAVE FAILED");
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            if(hit_outer_row(x,y,358)){load_manual_time_draft();open_screen(Screen::ManualTime);return true;}
            if(hit_outer_row(x,y,478)){open_screen(Screen::Timezone);return true;}
            break;
        case Screen::ManualTime:{
            if(hit_header_back(x,y)||hit(x,y,manual_time_action_rect(false))){open_screen(Screen::DateTime);return true;}
            for(uint8_t i=0;i<5;++i){
                if(hit(x,y,manual_time_adjust_button_rect(i,false))){adjust_manual_time_field(i,-1);draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                if(hit(x,y,manual_time_adjust_button_rect(i,true))){adjust_manual_time_field(i,1);draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            }
            if(hit(x,y,manual_time_action_rect(true))){
                if(save_manual_time_draft()){status_dirty=true;status_bar_dirty=true;screen=Screen::DateTime;show_toast("MANUAL DATE & TIME SAVED");}
                else show_toast("INVALID DATE OR TIME");
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;
            }
            break;
        }
        case Screen::Timezone:
            if(hit_header_back(x,y)){open_screen(Screen::DateTime);return true;}
            for(uint8_t i=0;i<TIMEZONE_COUNT;++i){
                if(!hit(x,y,timezone_row_rect(i)))continue;
                if(i==TIMEZONE_CUSTOM){open_screen(Screen::CustomTimezone);return true;}
                const uint8_t previous=timezone_index;if(i==TIMEZONE_AUTO)seed_auto_timezone_from_selection(previous);
                timezone_index=i;apply_timezone();persist_timezone_selection();status_dirty=true;status_bar_dirty=true;
                show_toast(i==TIMEZONE_AUTO?"GPS TIMEZONE AUTO":"TIMEZONE SAVED");draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;
            }
            break;
        case Screen::CustomTimezone:
            if(hit_header_back(x,y)||hit(x,y,custom_timezone_action_rect(false))){open_screen(Screen::Timezone);return true;}
            if(hit(x,y,custom_timezone_step_rect(false))){custom_timezone_minutes=(int16_t)(custom_timezone_minutes<=-12*60?14*60:custom_timezone_minutes-15);draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,custom_timezone_step_rect(true))){custom_timezone_minutes=(int16_t)(custom_timezone_minutes>=14*60?-12*60:custom_timezone_minutes+15);draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,custom_timezone_action_rect(true))){
                timezone_index=TIMEZONE_CUSTOM;apply_timezone();persist_timezone_selection();status_dirty=true;status_bar_dirty=true;
                screen=Screen::DateTime;show_toast("CUSTOM TIMEZONE SAVED");draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;
            }
            break;
        case Screen::DisplaySettings:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            if(frontlight_mode==FrontlightMode::NightTimer&&
               hit(x,y,meshink_settings_inline_action_rect(portrait_layout(),118))){
                open_screen(Screen::NightSchedule);return true;
            }
            if(hit_outer_row(x,y,118)){frontlight_mode=(FrontlightMode)(((uint8_t)frontlight_mode+1)%3);save_frontlight_settings();if(frontlight_mode==FrontlightMode::Off)frontlight_drive(false);else frontlight_event();show_toast(frontlight_mode_name());draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,238)){frontlight_timeout_index=(frontlight_timeout_index+1)%5;save_frontlight_settings();frontlight_event();show_toast(frontlight_timeout_name());draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_settings_inline_action_rect(portrait_layout(),538))){deep_sleep_standby=!deep_sleep_standby;save_frontlight_settings();show_toast(deep_sleep_standby?"DEEP SLEEP ON":"NORMAL STANDBY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,538)){standby_timeout_index=(standby_timeout_index+1)%4;save_frontlight_settings();last_user_activity=millis();show_toast(standby_timeout_name());draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,656)){map_imperial=!map_imperial;prefs.begin("t5-ui",false);prefs.putBool("map_imperial",map_imperial);prefs.end();show_toast(map_imperial?"IMPERIAL SCALE":"METRIC SCALE");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_shutdown_rect(portrait_layout()))){
                open_screen(Screen::ShutdownConfirm);return true;
            }
            break;
        case Screen::NightSchedule:
            if(hit_header_back(x,y)){open_screen(Screen::DisplaySettings);return true;}
            if(hit(x,y,meshink_night_start_rect(portrait_layout()))){night_edit_field=0;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_night_end_rect(portrait_layout()))){night_edit_field=1;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_night_minus_rect(portrait_layout()))){uint16_t& value=night_edit_field ? night_end_minutes : night_start_minutes;value=(value+1410)%1440;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_night_plus_rect(portrait_layout()))){uint16_t& value=night_edit_field ? night_end_minutes : night_start_minutes;value=(value+30)%1440;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_night_save_rect(portrait_layout()))){save_frontlight_settings();frontlight_event();show_toast("SCHEDULE SAVED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}break;
        case Screen::Help:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}break;
        case Screen::About:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}break;
        default:break;
    }
    return true;
}

static void handle_tap(int16_t x,int16_t y) {
    last_user_activity=millis();
    if(handle_quick_panel_tap(x,y))return;
    if(handle_landscape_keyboard(x,y))return;
    if(handle_backup_tap(x,y))return;
    if(handle_setup_tap(x,y))return;
    if(handle_app_tap(x,y))return;
    if(screen==Screen::Presets) {
        const MeshInkUiLayout& layout=portrait_layout();
        if(hit(x,y,meshink_preset_back_rect(layout))){screen=preset_return_screen;draw_screen();refresh(MeshInkRefreshMode::FastGray16);return;}
        for(int row=0;row<PRESETS_PER_PAGE;++row) if(hit(x,y,meshink_preset_row_rect(layout,row))){
            const int index=preset_page*PRESETS_PER_PAGE+row;
            if(index<PRESET_COUNT){
                const uint8_t previous=selected_preset;
                selected_preset=index;
                const bool applied=!mesh_is_ready||apply_selected_preset();
                if(applied){
                    saved=false;
                    if(prefs.begin("t5-ui",false)){
                        prefs.putUChar("preset_v2",selected_preset);
                        prefs.end();
                    }
                }else{
                    selected_preset=previous;
                }
                screen=preset_return_screen;
                show_toast(applied?(selected_preset==0?"RADIO KEPT UNCHANGED":"RADIO PRESET APPLIED"):"PRESET FAILED");
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
            return;
        }
        const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
        if(hit(x,y,meshink_preset_prev_rect(layout))&&preset_page>0){preset_page--;draw_screen();refresh(MeshInkRefreshMode::FastGray16);return;}
        if(hit(x,y,meshink_preset_next_rect(layout))&&preset_page+1<page_count){preset_page++;draw_screen();refresh(MeshInkRefreshMode::FastGray16);return;}
        return;
    }
    if(screen==Screen::CompanionConfirm){
        const MeshInkUiLayout& layout=portrait_layout();
        if(hit(x,y,meshink_confirm_left_rect(layout,500))){screen=setup_complete?Screen::More:Screen::Welcome;draw_screen();refresh(MeshInkRefreshMode::Direct);return;}
        if(hit(x,y,meshink_confirm_right_rect(layout,500))){T5_DEBUGLN(T5_LOG_UI,"[T5-UI] companion mode confirmed");mesh_protocol_request_companion_mode();return;}
        return;
    }
    if(screen==Screen::ShutdownConfirm){
        const MeshInkUiLayout& layout=portrait_layout();
        if(hit(x,y,meshink_confirm_left_rect(layout,650))){open_screen(Screen::DisplaySettings);return;}
        if(hit(x,y,meshink_confirm_right_rect(layout,650))){request_hardware_shutdown();return;}
        return;
    }
    const MeshInkUiLayout& layout=portrait_layout();
    if(hit(x,y,meshink_welcome_name_rect(layout))){replace_name_on_type=true;keyboard_visible=true;T5_DEBUGLN(T5_LOG_UI,"[T5-UI] name selected; keyboard shown; next character replaces current name");draw_screen();refresh(MeshInkRefreshMode::Direct);return;}
    if(hit(x,y,meshink_welcome_preset_rect(layout))){preset_return_screen=Screen::Welcome;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MeshInkRefreshMode::FastGray16);return;}
    if(hit(x,y,meshink_welcome_companion_rect(layout))){screen=Screen::CompanionConfirm;draw_screen();refresh(MeshInkRefreshMode::FastGray16);return;}
    if(!keyboard_visible){if(hit(x,y,meshink_welcome_show_keyboard_rect(layout))){keyboard_visible=true;draw_screen();refresh(MeshInkRefreshMode::FastGray16);}return;}
    handle_name_keyboard(x,y);
}

// Only map-surface taps are delayed briefly to distinguish one, two and
// three taps. Every typing/keypad/settings/contact tap bypasses this logic.
static void finish_map_tap_sequence() {
    if(!map_taps.count)return;
    const MapTapSequence completed=map_taps;
    map_taps={};
    if(screen!=Screen::Maps||standby_active)return;
    if(completed.count==2) {
        zoom_map_around(+1,completed.first_x,completed.first_y);
    } else if(completed.count==1) {
        handle_tap(completed.first_x,completed.first_y);
    }
}

static void set_touch_power(bool enabled){
    touch_enabled=false;
    delay(20); // let the sampler observe the disabled state before rail/reset changes
    meshink_touch_set_power(enabled);
    if(enabled){
        touch_enabled=true;
        if(touch_task_handle)xTaskNotifyGive(touch_task_handle);
    }
}

static void enter_standby(const char* reason){
    (void)reason;
    // Onboarding is an interactive, mandatory activity; no standby during setup.
    if(!setup_complete||setup_is_screen(screen)||standby_active)return;
    // Standby owns the whole display. Dismiss transient quick settings first
    // so it cannot remain layered over, or reappear immediately after, standby.
    // Preserve the view underneath Quick Settings before dismissing it.
    // A panel opened over the landscape keyboard has already switched the
    // physical display to portrait, so keyboard_landscape alone is not enough.
    const bool restore_landscape=keyboard_landscape||(quick_panel_active&&quick_panel_restore_landscape);
    quick_panel_active=false;quick_panel_restore_landscape=false;quick_slider_dragging=false;display_slider_dragging=false;
    // Standby is always portrait, but remember a landscape keyboard so wake
    // returns to the exact editing view that was active before standby.
    standby_restore_landscape=restore_landscape;
    if(restore_landscape){
        keyboard_landscape=false;
        set_ui_orientation(MeshInkOrientation::Portrait);
    }
    standby_active=true;text_refresh_pending=false;toast_visible=false;frontlight_deadline=0;frontlight_drive(false);
    // Enter standby with an exact clock/battery sample. Periodic status updates
    // remain anchored to wall-clock :00/:05/:10... boundaries.
    update_status_hardware();
    draw_screen();fast_full_redraw("ENTER_STANDBY",false);set_touch_power(false);if(touch_queue)xQueueReset(touch_queue);set_cpu_target(UI_IDLE_CPU_MHZ,"standby");
    deep_sleep_pending=deep_sleep_standby;
    if(deep_sleep_pending&&setup_complete){
        const uint8_t retained_tab=retained_tab_for_screen(screen);
        meshink_power_retain_ui_tab(retained_tab);
        T5_DEBUGF(T5_LOG_UI,"[T5-DEEPSLEEP] retained top tab=%u\n",(unsigned)retained_tab);
    }
    deep_sleep_retry_at=millis();
    if(deep_sleep_pending)Serial.println("[T5-DEEPSLEEP] standby screen committed; deep-sleep handoff armed after BOOT release");
}

static void leave_standby(){
    if(!standby_active)return;
    deep_sleep_pending=false;
    set_touch_power(true);
    standby_active=false;
    last_user_activity=millis();
    message_alert_active=false;
    meshink_power_frontlight_set(0);
    frontlight_lit=false;
    if(standby_restore_landscape){
        standby_restore_landscape=false;
        keyboard_landscape=true;
        set_ui_orientation(MeshInkOrientation::Landscape);
    } else {
        set_ui_orientation(MeshInkOrientation::Portrait);
    }
    set_cpu_target(UI_IDLE_CPU_MHZ,"wake");
    draw_screen();
    if(screen==Screen::Maps&&!keyboard_landscape) {
        T5_DEBUGLN(T5_LOG_UI,"[T5-EPD] Maps wake uses black-prep reveal");
        reveal_map_after_black_prep("LEAVE_STANDBY_BLACK_PREP_COMPLETE",true);
    } else {
        fast_full_redraw("LEAVE_STANDBY",true);
    }
}

static void start_message_alert(){
    const uint32_t now=millis();
    if(message_alert_active)return;
    if((int32_t)(now-message_alert_cooldown_until)<0)return;
    message_alert_active=true;message_alert_phase=0;message_alert_deadline=now;
}

static void service_message_alert(){
    if(!message_alert_active||(int32_t)(millis()-message_alert_deadline)<0)return;
    switch(message_alert_phase++){
        case 0:
        case 2:
            meshink_power_frontlight_set(0);frontlight_lit=false;
            meshink_display_fill_framebuffer(&display,0x00);
            force_redraw(MeshInkRefreshMode::Direct,"MESSAGE_ALERT_BLACK",false);
            message_alert_deadline=millis()+100;
            break;
        case 1:
        case 3:
            meshink_power_frontlight_set(100);frontlight_lit=true;
            meshink_display_set_all_white(&display);
            force_redraw(MeshInkRefreshMode::Direct,"MESSAGE_ALERT_WHITE",false);
            message_alert_deadline=millis()+100;
            break;
        default:
            meshink_power_frontlight_set(0);frontlight_lit=false;frontlight_deadline=0;
            update_status_hardware();
            // Every headless message processed before this point is represented
            // by the standby redraw below. Clear the pending bit immediately
            // before the synchronous GC16 update; a packet processed after the
            // redraw will set it again and request another alert session.
            if(headless_ui_state)headless_alert_requested=false;
            draw_screen();force_redraw(MeshInkRefreshMode::Gray16,"MESSAGE_ALERT_RESTORE",false);
            status_dirty=false;status_bar_dirty=false;message_alert_active=false;message_alert_cooldown_until=millis()+3000;
            break;
    }
}

static void service_primary_button(){
    static uint32_t pressed_at=0;static bool handled=false;
    if(!setup_complete||setup_is_screen(screen)){
        pressed_at=0;handled=false;
        return; // Short and long BOOT presses are both inert during onboarding.
    }
    const bool pressed=meshink_primary_button_pressed();
    if(pressed&&!pressed_at)pressed_at=millis();
    if(pressed&&!handled&&pressed_at&&millis()-pressed_at>=2000){
        handled=true;
        if(standby_active)leave_standby();
        else enter_standby(meshink_primary_button_name());
    }
    if(!pressed&&pressed_at){const uint32_t duration=millis()-pressed_at;if(!handled&&duration>=40){
        // Short primary-button refresh is deliberately disabled in standby.
        // Waking requires the existing two-second hold, avoiding needless EPD
        // refreshes from accidental short presses.
        if(!standby_active){
            last_user_activity=millis();
            if(screen==Screen::Maps){
                // The visible terrain is already decoded in map_base_cache.
                // Refresh only live node data, flash the physical screen black,
                // then rebuild the same viewport from the cached terrain so
                // moved node/device markers land at their latest positions.
                // Stay on the cache-only refresh path: no toast and no tile I/O.
                mesh_protocol_refresh_ui_data();
                meshink_display_fill_framebuffer(&display,0x00);
                force_redraw(MeshInkRefreshMode::Direct,"SHORT_BUTTON_MAP_BLACK",false);
                draw_screen();
                fast_full_redraw("SHORT_BUTTON_MAP_REFRESH",true);
            }else{
                draw_screen();fast_full_redraw("SHORT_BUTTON_REFRESH",true);
            }
        }
    }pressed_at=0;handled=false;}
}

static void ui_close_headless_display_session() {
    if(!headless_display_session)return;
    message_alert_active=false;
    meshink_power_frontlight_set(0);
    frontlight_lit=false;
    frontlight_deadline=0;
    if(fb){
        meshink_display_release_state(&display);
        fb=nullptr;
    }
    meshink_display_deinit();
    display_session_active=false;
    headless_display_session=false;
    ui_boot_cpu_active=false;
    set_cpu_target(UI_IDLE_CPU_MHZ,"headless-alert-idle");
    Serial.println("[T5-DEEPSLEEP] headless display session fully closed");
}

void ui_quiesce_display_for_deep_sleep() {
    // Keep the high-level EPDiy session/framebuffer initialized through the
    // final race window so an arriving message can still redraw immediately.
    // The physical panel HV and frontlight are nevertheless forced off before
    // ESP deep sleep. Deep sleep resets the ESP-side session state anyway.
    meshink_power_frontlight_set(0);
    frontlight_lit=false;
    frontlight_deadline=0;
    if(display_session_active)meshink_display_poweroff();
    ui_boot_cpu_active=false;
    set_cpu_target(UI_IDLE_CPU_MHZ,"deep-sleep-ready");
    Serial.println("[T5-DEEPSLEEP] display quiesced: panel HV/frontlight off; session retained until reset");
}

bool ui_headless_message_alert_pending() {
    // A later message while EPDiy is already initialized still needs an
    // alert/redraw. Only an alert already in progress coalesces the request.
    return headless_ui_state&&headless_alert_requested&&!message_alert_active;
}

bool ui_headless_display_busy() {
    // An initialized but idle display deliberately stays live during the
    // 40-second radio cooldown and does not block the eventual sleep attempt.
    return headless_alert_requested||message_alert_active;
}

bool ui_headless_display_session_active() {
    return headless_display_session;
}

bool ui_display_session_active() {
    return display_session_active;
}

bool ui_service_headless_message_alert() {
    if(!headless_ui_state)return false;

    bool started_session=false;
    if(headless_alert_requested&&!headless_display_session){
        Serial.println("[T5-DEEPSLEEP] headless display starting with retained radio GPIO ISR service");
        MeshInkUiStartupPlan plan{};
        plan.touch=false;
        plan.radio_settle=false;
        plan.recover_power_path=false;
        plan.splash=false;
        plan.sample_status=false;
        plan.battery_guard=false;
        plan.service_mesh_between_steps=true;
        ui_startup(plan);
        headless_display_session=true;
        started_session=true;
        if(!fb){
            Serial.println("[T5-DEEPSLEEP] headless message display unavailable");
            headless_alert_requested=false;
            ui_close_headless_display_session();
            return false;
        }
        standby_active=true;
    }

    if(headless_alert_requested&&headless_display_session&&!message_alert_active){
        headless_alert_requested=false;
        // Headless notifications are driven by actual message arrival rather
        // than the interactive UI cooldown. Reuse the already-live EPDiy
        // session for every subsequent DM/channel unread update.
        message_alert_cooldown_until=0;
        ui_boot_cpu_active=true;
        set_cpu_target(UI_RENDER_CPU_MHZ,"headless-alert");
        start_message_alert();
        Serial.println(started_session
            ?"[T5-DEEPSLEEP] headless message alert display session started"
            :"[T5-DEEPSLEEP] reusing initialized headless display for unread update");
    }

    if(!headless_display_session)return false;

    const bool was_active=message_alert_active;
    service_message_alert();
    if(was_active&&!message_alert_active){
        // The final GC16 refresh is synchronous. Service the active protocol immediately
        // while keeping EPDiy initialized. A packet landing during that redraw
        // can therefore update unread counts and reuse the same session.
        mesh_protocol_service_startup();
        if(headless_alert_requested){
            headless_alert_requested=false;
            message_alert_cooldown_until=0;
            ui_boot_cpu_active=true;
            set_cpu_target(UI_RENDER_CPU_MHZ,"headless-alert");
            start_message_alert();
            Serial.println("[T5-DEEPSLEEP] message arrived during final standby redraw; restarting headless alert");
            return true;
        }

        ui_boot_cpu_active=false;
        set_cpu_target(UI_IDLE_CPU_MHZ,"headless-alert-idle");
        Serial.println("[T5-DEEPSLEEP] headless display session retained through 40s cooldown");
    }
    return message_alert_active;
}

bool ui_promote_headless_to_interactive() {
    if(!headless_ui_state)return false;

    const bool reuse_display=headless_display_session&&display_session_active&&fb;
    headless_alert_requested=false;
    message_alert_active=false;
    // mesh_protocol_promote_to_ui() has already acknowledged the accepted BOOT
    // request at full brightness. Keep that acknowledgement lit through the
    // retained-to-interactive startup instead of extinguishing it here.
    meshink_power_frontlight_set(100);
    frontlight_lit=true;
    frontlight_deadline=0;

    MeshInkUiStartupPlan plan{};
    // EPDiy high-level state is process-singleton state: epd_deinit() does not
    // make epd_hl_init() legal a second time in the same boot. When a message
    // alert already initialized EPDiy, transfer that exact framebuffer/session
    // into interactive UI rather than tearing it down and reinitializing it.
    plan.display=!reuse_display;
    plan.radio_settle=false;
    plan.splash=false;
    plan.battery_guard=false;
    // ui_mesh_ready() takes the first useful status sample after retained RTC
    // startup; do not deliberately paint --:-- before the protocol helper is attached.
    plan.sample_status=false;
    plan.service_mesh_between_steps=true;
    ui_startup(plan);
    if(reuse_display){
        headless_display_session=false;
        set_ui_orientation(MeshInkOrientation::Portrait);
        Serial.println("[T5-DEEPSLEEP] interactive promotion reusing initialized EPDiy session");
    }
    if(!fb){
        Serial.println("[T5-DEEPSLEEP] interactive promotion failed: display framebuffer unavailable");
        return false;
    }

    mesh_protocol_prepare_interactive_services();
    ui_use_data_provider(mesh_protocol_provider());
    ui_mesh_ready();
    mesh_protocol_refresh_ui_data();

    // Cold boot hides this shallow SD/PMTiles inventory behind the splash.
    // Do the same before revealing retained BOOT promotion so first Maps open
    // does not pay the one-time mount/archive discovery cost.
    map_tiles_warm_storage();
    mesh_protocol_service_startup();

    standby_active=false;
    deep_sleep_pending=false;
    headless_ui_state=false;
    message_alert_active=false;
    // ui_startup() already restored the retained top-level tab. Do not
    // overwrite it when promoting a deep-sleep/headless runtime to interactive.
    if(!setup_complete)screen=(setup_any_done||
        setup_protocol_choice==mesh_protocol_descriptor().id)?Screen::SetupName:Screen::Welcome;
    keyboard_visible=false;
    ui_finish_startup();
    Serial.println("[T5-DEEPSLEEP] interactive UI attached to existing protocol runtime");
    return true;
}

void ui_prepare_headless_rx_wake() {
    // Journal replay runs immediately after storage opens and replaces these
    // zeroes before any message alert is rendered. NVS unread keys from older
    // builds are intentionally ignored so they cannot disagree with per-node
    // journal truth.
    status_unread=0;
    status_channel_unread=0;
    // Existing receive hooks can now update journal-derived unread counts
    // without starting framebuffer, display, touch or frontlight resources.
    standby_active=true;
    touch_enabled=false;
    message_alert_active=false;
    headless_ui_state=true;
    headless_alert_requested=false;
    headless_display_session=false;
    deep_sleep_pending=false;
    Serial.printf("[T5-DEEPSLEEP] headless UI state only: unread_dm=%u unread_ch=%u; display/touch not initialized\n",
                  (unsigned)status_unread,(unsigned)status_channel_unread);
}

static void ui_load_persistent_state() {
    prefs.begin("t5-ui",true);
    String saved_name=prefs.getString("name","");
    selected_preset=prefs.getUChar("preset_v2",17);
    const bool legacy_setup_complete=prefs.getBool("complete",false);
    setup_meshcore_done=prefs.getBool("setup_mc",legacy_setup_complete);
    setup_meshtastic_done=prefs.getBool("setup_mst",false);
    setup_any_done=setup_meshcore_done||setup_meshtastic_done;
    setup_complete=setup_protocol_done(mesh_protocol_descriptor().id);
    setup_return_protocol=prefs.getUChar("setup_return",0);
    setup_protocol_choice=prefs.getUChar("setup_choice",0);
    backup_restore_pending=prefs.getBool("restore_pending",false);
    const bool timezone_v2=prefs.getBool("tz_v2",false);
    timezone_index=prefs.getUChar("timezone",0);
    if(!timezone_v2)timezone_index=(uint8_t)min((int)7,(int)timezone_index+1);
    custom_timezone_minutes=(int16_t)constrain((int)prefs.getInt("custom_tz_min",0),-12*60,14*60);
    const String saved_auto_timezone_label=prefs.getString("auto_tz_label","UTC");
    const String saved_auto_timezone_rule=prefs.getString("auto_tz_rule","UTC0");
    snprintf(auto_timezone_label,sizeof(auto_timezone_label),"%s",saved_auto_timezone_label.c_str());
    snprintf(auto_timezone_rule,sizeof(auto_timezone_rule),"%s",saved_auto_timezone_rule.c_str());
    // Unread truth is reconstructed from the message journal. Do not touch
    // it while loading ordinary UI preferences: on the first retained-RX
    // alert this function runs after journal replay and must preserve the
    // already-restored direct/channel totals for the standby redraw.
    map_has_last_gps_position=prefs.getBool("map_fix_saved",false);
    map_last_gps_latitude=prefs.getLong("map_fix_lat",0);
    map_last_gps_longitude=prefs.getLong("map_fix_lon",0);
    map_has_last_gps_position=map_has_last_gps_position &&
        map_last_gps_latitude>=-85051100L&&map_last_gps_latitude<=85051100L &&
        map_last_gps_longitude>=-180000000L&&map_last_gps_longitude<=180000000L;
    map_last_gps_saved=map_has_last_gps_position;
    map_saved_gps_latitude=map_last_gps_latitude;
    map_saved_gps_longitude=map_last_gps_longitude;
    frontlight_mode=(FrontlightMode)prefs.getUChar("light_mode",(uint8_t)FrontlightMode::On);
    frontlight_timeout_index=prefs.getUChar("light_timeout",2);
    frontlight_brightness=prefs.getUChar("light_level",30);
    standby_timeout_index=prefs.getUChar("standby_timeout",1);
    deep_sleep_standby=prefs.getBool("deep_standby",false);
    night_start_minutes=prefs.getUShort("night_start",20*60);
    night_end_minutes=prefs.getUShort("night_end",7*60);
    map_imperial=prefs.getBool("map_imperial",false);
    prefs.end();

    if((uint8_t)frontlight_mode>(uint8_t)FrontlightMode::Off)frontlight_mode=FrontlightMode::On;
    if(frontlight_timeout_index>4)frontlight_timeout_index=2;
    if(frontlight_brightness>100)frontlight_brightness=30;
    if(standby_timeout_index>3)standby_timeout_index=1;
    if(night_start_minutes>=1440)night_start_minutes=20*60;
    if(night_end_minutes>=1440)night_end_minutes=7*60;
    if(selected_preset>=PRESET_COUNT)selected_preset=17;
    if(timezone_index>=TIMEZONE_COUNT)timezone_index=1;
    if(!auto_timezone_rule[0])snprintf(auto_timezone_rule,sizeof(auto_timezone_rule),"UTC0");
    if(!auto_timezone_label[0])snprintf(auto_timezone_label,sizeof(auto_timezone_label),"UTC");
    apply_timezone();
    if(!timezone_v2){
        Preferences zone_migration;if(zone_migration.begin("t5-ui",false)){
            zone_migration.putUChar("timezone",timezone_index);zone_migration.putBool("tz_v2",true);zone_migration.end();
        }
    }

    if(saved_name.length()){
        size_t out=0;
        for(size_t i=0;i<saved_name.length()&&out<20;++i){
            const char ch=saved_name[i];
            if(mesh_protocol_name_character_allowed(ch))node_name[out++]=ch;
            else T5_DEBUGF(T5_LOG_UI,"[T5-UI] discarded stored illegal name character 0x%02X\n",(unsigned char)ch);
        }
        node_name[out]=0;
    }else if(!setup_complete){
        static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        memcpy(node_name,"MeshInk-",8);
        for(int i=0;i<4;++i)node_name[8+i]=alphabet[esp_random()%36];
        node_name[12]=0;
        Preferences initial_name;
        if(initial_name.begin("t5-ui",false)){
            initial_name.putString("name",node_name);
            initial_name.end();
        }
        T5_DEBUGF(T5_LOG_UI,"[T5-BOOT] generated first-setup device name: %s\n",node_name);
    }

    uint8_t retained_tab=0;
    if(meshink_power_get_retained_ui_tab(retained_tab)){
        retained_wake_tab=retained_tab;
        retained_wake_tab_valid=true;
    }
    if(!setup_complete&&setup_any_done&&
       (!setup_return_protocol||!setup_protocol_done(setup_return_protocol)))
        setup_return_protocol=setup_meshcore_done?1:2;
    setup_initialize_draft();
    screen=setup_complete
        ?(retained_wake_tab_valid?screen_for_retained_tab(retained_wake_tab):Screen::Contacts)
        :((setup_any_done||setup_protocol_choice==mesh_protocol_descriptor().id)
            ?Screen::SetupName:Screen::Welcome);
    if(setup_complete&&retained_wake_tab_valid)
        T5_DEBUGF(T5_LOG_UI,"[T5-DEEPSLEEP] restored top tab=%u screen=%u\n",
                  (unsigned)retained_wake_tab,(unsigned)screen);
    keyboard_visible=false;
}

void ui_startup(const MeshInkUiStartupPlan& plan) {
    ui_boot_cpu_active=true;
    set_cpu_target(UI_RENDER_CPU_MHZ,"ui-startup");
    Serial.begin(115200);
    meshink_buttons_begin();

    if(plan.display){
        meshink_power_frontlight_begin();
        meshink_power_frontlight_set(0);
    }

    if(plan.touch)meshink_touch_prepare_boot();

    if(plan.display){
        meshink_display_init();
        display_session_active=true;
        if(plan.radio_settle)meshink_board_start_local_radio_settle();
        if(plan.touch)set_ui_orientation(MeshInkOrientation::Portrait);
        else meshink_display_set_orientation(MeshInkOrientation::Portrait);
        Serial.println("[T5-INIT] display=initialized");
    }

    if(plan.service_mesh_between_steps)mesh_protocol_service_startup();

    if(plan.recover_power_path)meshink_power_recover_boot_path();

    if(plan.touch){
        meshink_touch_finish_boot();
        touch_enabled=true;
        Serial.println("[T5-INIT] touch=initialized");
    }else{
        touch_enabled=false;
    }

    if(plan.service_mesh_between_steps)mesh_protocol_service_startup();

    if(plan.display){
        display=meshink_display_state_init();
        fb=meshink_display_framebuffer(&display);
    }

    if(plan.load_state)ui_load_persistent_state();
    if(plan.sample_status)update_status_hardware();

    if(plan.battery_guard){
        MeshInkPowerCriticalState boot_power{};
        if(meshink_power_boot_critical(boot_power))
            critical_battery_shutdown(boot_power,"ui-startup");
    }

    if(plan.service_mesh_between_steps)mesh_protocol_service_startup();

    if(plan.splash&&plan.display){
        meshink_display_set_all_white(&display);
        draw_meshink_logo(ui_y(160),false);
        ui_centred("STARTING UP...",ui_y(716),3,0,true);
        if(node_name[0])ui_centred_fit(node_name,ui_y(830),portrait_layout().width-ui_w(32),3,0,true);
        ui_centred(UI_VERSION,ui_y(885),2,0,true);
        meshink_display_poweron();
        meshink_display_clear();
        meshink_display_poweroff();
        refresh(MeshInkRefreshMode::FastGray16);
        T5_DEBUGLN(T5_LOG_UI,"[T5-BOOT] splash visible; starting storage and mesh initialization");
    }
}

void ui_setup() {
    MeshInkUiStartupPlan plan{};
    ui_startup(plan);
}

void ui_show_storage_initializing() {
    // Called only after a non-formatting mount fails. Update the existing
    // splash before SPIFFS.begin(true) may block while preparing storage.
    meshink_display_fill_rect({0,ui_y(704),portrait_layout().width,ui_h(65)},0xFF,fb);
    ui_centred("INITIALISING STORAGE...",ui_y(716),3,0,true);
    refresh(MeshInkRefreshMode::FastGray16);
    T5_DEBUGLN(T5_LOG_UI,"[T5-BOOT] splash: initialising storage after SPIFFS mount failed");
}

void ui_finish_startup() {
    if(hardware_failure)return;
    if(!setup_complete&&backup_restore_pending){
        Preferences p;
        if(p.begin("t5-ui",false)){p.putBool("restore_pending",false);p.end();}
        backup_restore_pending=false;
        backup_open_files(Screen::SetupName);
    }
    // Drop any touch points that accumulated during the non-interactive
    // splash, then show the correct initial setup or existing-user screen.
    meshink_touch_clear();
    const bool retained_deep_wake=setup_complete&&retained_wake_tab_valid;
    if(screen==Screen::Maps){
        // On retained wake the physical panel still contains the standby image.
        // Build the complete Loading frame in memory first, then force that
        // frame directly so the user never sees an intermediate blank screen.
        centre_map_on_device();
        load_map_with_feedback(false,retained_deep_wake);
        frontlight_event();
    }else{
        draw_screen();
        if(setup_is_screen(screen))
            fast_full_redraw("FIRST_SETUP_SCREEN",false);
        else if(screen==Screen::Contacts||retained_deep_wake) {
            // Contacts already used the correct transition: prepare the entire
            // destination screen, invalidate EPDiy's stale previous-frame view,
            // then commit it as one complete update. Apply that same one-shot
            // treatment to every retained top-level tab after deep sleep.
            fast_full_redraw(retained_deep_wake
                ?"RETAINED_TAB_AFTER_DEEP_WAKE":"CONTACTS_AFTER_BOOT",false);
            // Startup can take longer than the saved light timeout. Start a fresh
            // timeout only after the restored screen is actually visible.
            if(setup_complete)frontlight_event();
        } else {
            refresh(MeshInkRefreshMode::FastGray16);
            if(setup_complete)frontlight_event();
        }
    }
    // The first interactive frame already includes the protocol status
    // populated during startup; don't immediately refresh it a second time.
    status_dirty=false;status_bar_dirty=false;
    touch_queue=xQueueCreate(32,sizeof(QueuedTap));
    if(!touch_queue||
       xTaskCreatePinnedToCore(touch_sampler_task,"t5-touch",4096,nullptr,1,
                              &touch_task_handle,0)!=pdPASS)
        Serial.println("[T5-TOUCH] ERROR: sampler could not start");
    T5_DEBUGF(T5_LOG_UI,"[T5-LIGHT] mode=%s timeout=%s brightness=%u%% night=%02u:%02u-%02u:%02u\n",frontlight_mode_name(),frontlight_timeout_name(),frontlight_brightness,night_start_minutes/60,night_start_minutes%60,night_end_minutes/60,night_end_minutes%60);
    // Once the interactive screen is visible, the RTC handoff has served its
    // purpose. A later in-process UI reinitialization must not replay it.
    meshink_power_clear_retained_ui_tab();
    retained_wake_tab=0;
    retained_wake_tab_valid=false;
    ui_boot_cpu_active=false;
    set_cpu_target(UI_IDLE_CPU_MHZ,"ui-ready");last_user_activity=millis();T5_DEBUGLN(T5_LOG_UI,"[T5-UI] touch ready; waiting for input");
}

void ui_loop() {
    if(hardware_failure){

        delay(100);return;
    }
    service_critical_battery();
    service_primary_button();
    if(standby_active&&deep_sleep_pending&&deep_sleep_standby&&!message_alert_active&&
       !meshink_primary_button_pressed()&&(int32_t)(millis()-deep_sleep_retry_at)>=0){
        deep_sleep_retry_at=millis()+250;
        if(mesh_protocol_enter_deep_sleep_standby())return;
    }
    if(map_taps.count&&
       (screen!=Screen::Maps||standby_active||
        millis()-map_taps.last_at>=meshink_map_gestures::TAP_WINDOW_MS))
        finish_map_tap_sequence();
    // Hot card removal/insertion is checked even when the user is not
    // touching Maps. The generation changes on a failed read/remount, so
    // discard the old viewport and show the unavailable or new map promptly.
    static uint32_t last_map_media_poll=0;
    if(screen==Screen::Maps&&!standby_active&&!message_alert_active&&
       millis()-last_map_media_poll>=3000) {
        last_map_media_poll=millis();
        const uint32_t previous_epoch=map_tiles_media_epoch();
        map_tiles_media_ready();
        if(map_tiles_media_epoch()!=previous_epoch) {
            map_base_valid=false;
            load_map_with_feedback(true);
        }
    }
    const uint32_t standby_timeout=STANDBY_TIMEOUTS[min((uint8_t)3,standby_timeout_index)];
    if(setup_complete&&!setup_is_screen(screen)&&!standby_active&&standby_timeout&&
       millis()-last_user_activity>=standby_timeout)enter_standby("TIMEOUT");
    QueuedTap tap{};
    while(!standby_active&&touch_queue&&xQueueReceive(touch_queue,&tap,0)==pdTRUE){
        // Only ordinary portrait page navigation uses this stale-event fence.
        // Keyboard input, Quick Settings and Maps keep their existing queue /
        // gesture semantics and are never discarded by this rule.
        const bool stale_navigation_tap=
            navigation_touch_cutoff_ms&&tap.queued_at_ms&&
            (int32_t)(tap.queued_at_ms-navigation_touch_cutoff_ms)<=0&&
            !keyboard_visible&&!keyboard_landscape&&!quick_panel_active&&
            screen!=Screen::Maps;
        if(stale_navigation_tap)continue;
        last_user_activity=millis();
        if(tap.home){
            if(!setup_complete||setup_is_screen(screen))continue; // Home is inert during setup.
            if(keyboard_visible||keyboard_landscape)continue;
            map_taps={};
            // Home changes the logical page, not just the e-paper frame.
            // Clear pending taps/refreshes so the previous page cannot be
            // redrawn immediately afterward.
            if(touch_queue)xQueueReset(touch_queue);
            text_refresh_pending=false;toast_visible=false;toast_opens_main=false;
            keyboard_visible=false;keyboard_message_mode=false;
            if(keyboard_landscape){
                keyboard_landscape=false;
                set_ui_orientation(MeshInkOrientation::Portrait);
            }
            details_page=0;details_from_discovery=false;reset_chat_paging();
            open_screen(setup_complete?Screen::Contacts:(setup_any_done?Screen::SetupName:Screen::Welcome));
            continue;
        }
        // A deliberate downward pull beginning at the top edge opens the
        // CrossPoint-style quick panel before page-specific swipe handling.
        const int first_y=tap.y-tap.dy;
        if(setup_complete&&!quick_panel_active&&first_y<=80&&tap.dy>=90&&abs(tap.dy)>abs(tap.dx)) {
            open_quick_panel();
            continue;
        }
        if(quick_panel_active) {
            const int quick_start_x=tap.x-tap.dx;
            const int quick_start_y=tap.y-tap.dy;
            if(tap.dy<=-90&&abs(tap.dy)>abs(tap.dx)) close_quick_panel();
            else handle_quick_panel_tap(tap.x,tap.y,quick_start_x,quick_start_y);
            continue;
        }
        // An event sampled while Maps was visible must never become a
        // keyboard/contact tap if navigation changed before it was handled.
        if(tap.map_sampled&&screen!=Screen::Maps)continue;
        if(screen==Screen::Maps&&tap.map_pinch) {
            // A completed multi-contact gesture NEVER also sends a single
            // tap or swipe, even when fingers moved too little to zoom.
            map_taps={};
            zoom_map_around(tap.zoom_steps,tap.x,tap.y);
            continue;
        }
        if(screen==Screen::Maps&&tap.map_sampled) {
            const int first_x=tap.x-tap.dx,first_y=tap.y-tap.dy;
            const bool candidate=
                meshink_map_gestures::terrain_point(first_x,first_y,portrait_layout())&&
                meshink_map_gestures::terrain_point(tap.x,tap.y,portrait_layout())&&
                meshink_map_gestures::tap_candidate(
                    tap.dx,tap.dy,tap.hold_ms);
            if(candidate) {
                const uint32_t now=millis();
                if(map_taps.count &&
                   (now-map_taps.last_at>meshink_map_gestures::TAP_WINDOW_MS||
                    !meshink_map_gestures::same_tap_area(
                        map_taps.first_x,map_taps.first_y,first_x,first_y))) {
                    finish_map_tap_sequence(); // unrelated/new tap
                }
                if(screen!=Screen::Maps)continue;
                if(!map_taps.count) {
                    map_taps.count=1;
                    map_taps.first_x=first_x;map_taps.first_y=first_y;
                } else if(++map_taps.count==3) {
                    const int anchor_x=map_taps.first_x;
                    const int anchor_y=map_taps.first_y;
                    map_taps={};
                    zoom_map_around(-1,anchor_x,anchor_y);
                    continue;
                }
                map_taps.last_x=tap.x;map_taps.last_y=tap.y;
                map_taps.last_at=now;
                continue;
            }
            // Non-gesture movement and map-control taps are not delayed.
            map_taps={};
        }
        if(screen==Screen::DisplaySettings) {
            const MeshInkUiLayout& layout=portrait_layout();
            const MeshInkUiRect slider_touch=meshink_display_slider_touch_rect(layout);
            const int start_x=tap.x-tap.dx,start_y=tap.y-tap.dy;
            if(start_x>=slider_touch.x&&start_x<slider_touch.x+slider_touch.width&&
               start_y>=slider_touch.y&&start_y<slider_touch.y+slider_touch.height) {
                const MeshInkUiRect slider=meshink_display_slider_track_rect(layout);
                const int clamped=max(slider.x,min(slider.x+slider.width,(int)tap.x));
                const int value=((clamped-slider.x)*100+slider.width/2)/slider.width;
                display_set_brightness(value);
                continue;
            }
        }
        if(screen==Screen::Maps&&(abs(tap.dx)>22||abs(tap.dy)>22)&&
           meshink_map_gestures::terrain_point(tap.x,tap.y,portrait_layout())) {
            pan_map_by_pixels(tap.dx,tap.dy);
            open_screen(Screen::Maps);
            continue;
        }
        if(screen==Screen::ProtocolSettings&&!keyboard_visible&&
           abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            const size_t count=protocol_settings_total_count();
            clamp_list_page(protocol_settings_page,count);
            const size_t pages=list_page_count(count);
            int next=(int)protocol_settings_page+(tap.dy<0?1:-1);
            if(next<0)next=0;
            if(next>=(int)pages)next=(int)pages-1;
            if((size_t)next!=protocol_settings_page){
                protocol_settings_page=(size_t)next;
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] protocol-settings page=%u/%u\n",
                          (unsigned)(protocol_settings_page+1),(unsigned)pages);
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
        }else if((screen==Screen::Contacts||screen==Screen::Channels||screen==Screen::ChannelManage||screen==Screen::Discovery)&&
           abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            const size_t count=!ui_data?0:
                (screen==Screen::Contacts?ui_data->contact_count():
                 (screen==Screen::Channels||screen==Screen::ChannelManage)?ui_data->channel_count():ui_data->advert_count());
            size_t& page=screen==Screen::Contacts?contacts_page:
                         screen==Screen::Channels?channels_page:
                          screen==Screen::ChannelManage?channel_manage_page:discovery_page;
            clamp_list_page(page,count);
            const size_t pages=list_page_count(count);
            int next=(int)page+(tap.dy<0?1:-1);
            if(next<0)next=0;
            if(next>=(int)pages)next=(int)pages-1;
            if((size_t)next!=page){
                page=(size_t)next;
                const char* name=screen==Screen::Contacts?"contacts":
                                 (screen==Screen::Channels||screen==Screen::ChannelManage)?"channels":"discovery";
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] %s page=%u/%u\n",name,
                          (unsigned)(page+1),(unsigned)pages);
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
        }else if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&!keyboard_visible&&abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            const size_t count=ui_data?ui_data->active_message_count():0;
            chat_geometry_sync(count);
            size_t first=count,end=count;bool has_older=false;
            if(count)
                chat_page_bounds_lazy(count,chat_history_available_current(),
                                      chat_history_available_paged(),
                                      chat_page,first,end,has_older);
            uint8_t next=chat_page;
            if(tap.dy>0){
                if(has_older&&chat_page+1<CHAT_PAGE_ANCHORS)next=(uint8_t)(chat_page+1);
            }else if(chat_page>0)next=(uint8_t)(chat_page-1);
            if(next!=chat_page){
                chat_page=next;
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] conversation page=%u%s\n",
                          (unsigned)(chat_page+1),has_older?"":" (oldest)");
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
        }else if(screen==Screen::ContactDetails&&!keyboard_visible&&abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            UiNodeDetails node{};const uint8_t pages=(ui_data&&ui_data->active_node_details(node))?node_info_page_count(node.capabilities):1;
            int next=(int)details_page+(tap.dy<0?1:-1);
            if(next<0)next=0;
            if(next>=pages)next=pages-1;
            if((uint8_t)next!=details_page){
                details_page=(uint8_t)next;
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] node-info page=%u/%u\n",details_page+1,pages);
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
        }else if(screen==Screen::Presets&&abs(tap.dy)>60){
            const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
            int next=(int)preset_page+(tap.dy<0?1:-1);if(next<0)next=0;if(next>=page_count)next=page_count-1;
            preset_page=(uint8_t)next;T5_DEBUGF(T5_LOG_UI,"[T5-UI] preset page=%u\n",preset_page+1);draw_screen();refresh(MeshInkRefreshMode::FastGray16);
        }else handle_tap(tap.x,tap.y);
    }
    // Charger plug/unplug is user-visible state and should not wait for the
    // deliberately slow 60 s standby status poll. A one-byte PMIC read once
    // per second is cheap; refresh only the 48 px status bar and only when the
    // lightning-bolt visibility changes.
    static uint32_t last_standby_charge_poll=0;
    if(standby_active&&millis()-last_standby_charge_poll>=1000){
        last_standby_charge_poll=millis();
        bool icon_changed=false;
        update_charge_state(&icon_changed);
        if(icon_changed&&!message_alert_active){
            status_bar_dirty=true;
        }
    }

    static uint32_t last_status_poll=0;
    const uint32_t status_poll_interval=standby_active?60000:15000;
    if(millis()-last_status_poll>=status_poll_interval){
        last_status_poll=millis();
        update_status_hardware();
        // Normal UI keeps the visible clock current minute-by-minute.
        // Standby uses the lower-power five-minute wall-clock cadence.
        // Event-driven redraws still adopt the exact current clock/battery.
        const int16_t status_wall_minute=(status_hour>=0&&status_minute>=0)
            ?(int16_t)(status_hour*60+status_minute):-1;
        const int16_t status_slot=status_wall_minute>=0
            ?(int16_t)(status_wall_minute/5):-1;
        const bool aligned_status_due=standby_active
            ?(status_slot>=0&&status_slot!=status_bar_painted_slot)
            :(status_wall_minute>=0&&status_wall_minute!=status_bar_painted_minute);
        if(aligned_status_due)status_bar_dirty=true;
    }
    const bool text_refresh_due=text_refresh_pending&&(int32_t)(millis()-text_refresh_after)>=0;
    if(status_dirty&&!message_alert_active){
        // Content changes retain the ordinary screen redraw. Refresh the
        // hardware snapshot first so clock and battery come along for free.
        update_status_hardware();
        if(text_refresh_due){
            text_refresh_pending=false;
        }
        const bool wake=status_wake_light&&!standby_active;
        status_dirty=false;status_bar_dirty=false;status_wake_light=false;
        draw_screen();refresh(MeshInkRefreshMode::Direct,wake);
    }else if(text_refresh_due){
        text_refresh_pending=false;
        if(status_bar_dirty&&!quick_panel_active&&!keyboard_landscape){
            // Coalesce a pending bar change into an update that is already
            // required for text rather than performing two panel operations.
            update_status_hardware();
            status_bar_dirty=false;status_wake_light=false;
            draw_screen();
        }else if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&
           keyboard_visible&&keyboard_message_mode&&!keyboard_landscape)
            draw_message_entry_fast();
        else if(screen==Screen::ProtocolSettings&&keyboard_visible&&!keyboard_landscape&&!keyboard_message_mode)
            draw_protocol_name_fast();
        else
            draw_screen();
        refresh(MeshInkRefreshMode::Direct);
    }else if(status_bar_dirty&&!message_alert_active&&!quick_panel_active&&!keyboard_landscape){
        // Event-driven updates still sample and display the exact clock and
        // battery, but they never move the next :00/:05/:10... periodic boundary.
        update_status_hardware();
        const bool wake=status_wake_light&&!standby_active;
        status_bar_dirty=false;status_wake_light=false;
        draw_status_bar();
        refresh_area(MeshInkRefreshMode::Direct,
            {0,0,portrait_layout().width,portrait_layout().status_height},wake);
    }
    if(toast_visible&&(int32_t)(millis()-toast_until)>=0){
        toast_visible=false;
        if(toast_opens_main){toast_opens_main=false;screen=Screen::Contacts;keyboard_visible=false;keyboard_message_mode=false;status_unread=0;status_channel_unread=0;}
        draw_screen();refresh(MeshInkRefreshMode::Direct,true);
    }
    frontlight_service();
    service_message_alert();
    delay(12);
}

bool ui_is_standby(){return standby_active;}

void ui_show_radio_failure(MeshInkRadioFailureClass failure){
    hardware_failure=true;keyboard_visible=false;keyboard_message_mode=false;toast_visible=false;text_refresh_pending=false;
    meshink_display_set_all_white(&display);
    if(failure==MeshInkRadioFailureClass::MissingHardwareVariant){
        ui_centred_fit("MESHINK CANNOT START",ui_y(120),portrait_layout().width-ui_w(24),4,0,true);
        ui_centred_fit("LORA AND GPS NOT FOUND",ui_y(190),portrait_layout().width-ui_w(24),4,0,true);
        ui_centred_fit("BOARD VARIANT MAY OMIT RADIO",ui_y(290),portrait_layout().width-ui_w(24),3,0,true);
        ui_centred_fit("LORA RADIO IS REQUIRED",ui_y(360),portrait_layout().width-ui_w(24),3,0,true);
        ui_centred_fit("IF YOUR BOARD HAS A RADIO",ui_y(520),portrait_layout().width-ui_w(24),2,0,true);
        ui_centred_fit("PLEASE REPORT THIS ERROR",ui_y(555),portrait_layout().width-ui_w(24),2,0,true);
        ui_centred_fit("PRESS RST TO RETRY",ui_y(720),portrait_layout().width-ui_w(24),3,0,true);
    }else{
        ui_centred_fit("RADIO STARTUP",ui_y(190),portrait_layout().width-ui_w(24),5,0,true);
        ui_centred_fit("FAILED",ui_y(255),portrait_layout().width-ui_w(24),6,0,true);
        ui_centred_fit("LORA RADIO NOT DETECTED",ui_y(390),portrait_layout().width-ui_w(24),4,0,true);
        ui_centred_fit("CHECK BOARD RADIO HARDWARE",ui_y(475),portrait_layout().width-ui_w(24),2,0,true);
        ui_centred_fit("PLEASE REPORT THIS ERROR",ui_y(510),portrait_layout().width-ui_w(24),2,0,true);
        ui_centred_fit("PRESS RST TO RETRY",ui_y(600),portrait_layout().width-ui_w(24),3,0,true);
    }
    ui_centred(UI_VERSION,ui_y(900),2,0,true);
    refresh(MeshInkRefreshMode::FastGray16,false);
    ui_boot_cpu_active=false;
    frontlight_deadline=0;frontlight_drive(false);set_touch_power(false);set_cpu_target(UI_IDLE_CPU_MHZ,"hardware-failure");
    Serial.printf("[T5-ERROR] persistent radio failure screen displayed; class=%u; UI and touch stopped\n",(unsigned)failure);
}
void ui_status_set_unread(uint16_t count) {
    if(status_unread!=count){status_unread=count;status_bar_dirty=true;}
}

void ui_status_set_channel_unread(uint16_t count) {
    if(status_channel_unread!=count){status_channel_unread=count;status_bar_dirty=true;}
}

void ui_status_set_gps(bool enabled,bool has_fix,int satellites,long latitude,long longitude,uint32_t timestamp,MeshInkGpsError error) {
    if(!meshink_board_has_gps()){
        (void)enabled;(void)has_fix;(void)satellites;(void)latitude;(void)longitude;(void)timestamp;(void)error;
        return;
    }
    if(error!=MeshInkGpsError::None)has_fix=false;
    const bool state_changed=status_gps_enabled!=enabled||status_gps_fix!=has_fix||status_gps_error!=error;
    const bool detail_changed=status_gps_satellites!=satellites||status_gps_latitude!=latitude||status_gps_longitude!=longitude;
    if(state_changed)T5_DEBUGF(T5_LOG_GPS,"[T5-GPS] state %s error=%u sats=%d lat=%ld lon=%ld\n",enabled?(error!=MeshInkGpsError::None?"error":(has_fix?"fixed":"searching")):"disabled",(unsigned)error,satellites,latitude,longitude);
    const long previous_latitude=status_gps_latitude;
    const long previous_longitude=status_gps_longitude;
    status_gps_enabled=enabled;status_gps_fix=has_fix;status_gps_error=error;status_gps_satellites=satellites;status_gps_latitude=latitude;status_gps_longitude=longitude;status_gps_timestamp=timestamp;
    // Only verified live fixes update the retained map location. GPS disabled
    // or searching may report (0,0), which must not erase the last fix.
    if(enabled&&has_fix&&latitude>=-85051100L&&latitude<=85051100L&&
       longitude>=-180000000L&&longitude<=180000000L){
        map_has_last_gps_position=true;
        map_last_gps_latitude=latitude;map_last_gps_longitude=longitude;
        update_auto_timezone_from_gps(latitude,longitude);
        const uint32_t gps_now=millis();
        // Limit NVS writes: store the first fix, then only changed positions
        // at most once every 30 minutes. Existing saved positions are kept
        // until a fresh fix differs by roughly 0.005 degrees or more.
        const bool differs=!map_last_gps_saved ||
            labs(latitude-map_saved_gps_latitude)>=5000L ||
            labs(longitude-map_saved_gps_longitude)>=5000L;
        if(differs&&(!map_last_gps_save_ms||
            gps_now-map_last_gps_save_ms>=1800000UL)){
            Preferences location_store;
            if(location_store.begin("t5-ui",false)){
                const bool ok=location_store.putLong("map_fix_lat",latitude)>0 &&
                    location_store.putLong("map_fix_lon",longitude)>0 &&
                    location_store.putBool("map_fix_saved",true)>0;
                location_store.end();
                if(ok){
                    map_last_gps_saved=true;
                    map_saved_gps_latitude=latitude;
                    map_saved_gps_longitude=longitude;
                    map_last_gps_save_ms=gps_now?gps_now:1;
                }
            }
        }
    }
    static uint32_t last_detail_refresh=0;
    static uint32_t last_satellite_bar_refresh=0;
    const uint32_t now=millis();
    // Keep live GPS data current, but rate-limit the visible satellite count.
    // State transitions (off/searching/fix) remain immediate. Satellite count
    // is hidden in standby, so it never causes a standby-only panel update.
    bool satellites_refresh=false;
    if(state_changed){
        status_gps_satellites_bar=satellites;
        last_satellite_bar_refresh=now?now:1;
    }else if(!standby_active&&enabled&&has_fix&&
             status_gps_satellites_bar!=satellites&&
             (!last_satellite_bar_refresh||
              now-last_satellite_bar_refresh>=5000UL)){
        status_gps_satellites_bar=satellites;
        last_satellite_bar_refresh=now?now:1;
        satellites_refresh=true;
    }
    bool marker_moved=false;
    // Keep the own-position marker reasonably current while travelling,
    // but avoid expensive e-paper updates for every 1 Hz GPS sample.
    static uint32_t last_marker_refresh=0;
    if(screen==Screen::Maps&&!standby_active&&enabled&&has_fix&&
       (previous_latitude!=latitude||previous_longitude!=longitude)&&
       now-last_marker_refresh>=15000) {
        int sx=0,sy=0;
        if(project_device_on_map(latitude,longitude,sx,sy)) {
            const bool now_visible=sx>=20&&sx<=520&&sy>=map_top()+20&&sy<=map_bottom()-21;
            marker_moved=map_device_marker_visible?
                (abs(sx-map_device_marker_x)>=3||abs(sy-map_device_marker_y)>=3):
                now_visible;
        }
        if(marker_moved)last_marker_refresh=now;
    }
    const bool detail_refresh=detail_changed&&screen==Screen::GpsSettings&&now-last_detail_refresh>=10000;
    if(state_changed&&!standby_active){
        status_bar_dirty=true;
        T5_DEBUGLN(T5_LOG_UI,"[T5-UI] status-bar refresh queued reason=gps-state");
    }else if(satellites_refresh){
        status_bar_dirty=true;
        T5_DEBUGLN(T5_LOG_UI,"[T5-UI] status-bar refresh queued reason=gps-satellites-5s");
    }
    // GPS Settings exposes receiver details in the page body, and Maps owns
    // the moving position marker. Those are genuine content changes and keep
    // the existing screen redraw path.
    if(!standby_active&&((screen==Screen::GpsSettings&&(state_changed||detail_refresh))||marker_moved)){
        if(screen==Screen::GpsSettings)last_detail_refresh=now;
        status_dirty=true;
        T5_DEBUGLN(T5_LOG_UI,marker_moved?"[T5-UI] refresh queued reason=map-own-position":
                               "[T5-UI] refresh queued reason=gps-detail");
    }
}

bool ui_chat_is_visible(bool channel){
    return !standby_active&&(channel?screen==Screen::ChannelChat:screen==Screen::ContactChat);
}

void ui_notify_message_received(bool channel){
    const bool visible=ui_chat_is_visible(channel);
    // The provider has already committed the RX record and synchronized these
    // totals from its journal-derived counters. Notification code must never
    // maintain a second unread truth.
    status_dirty=true;
    if(standby_active){
        if(headless_ui_state){
            // Keep the request latched even while a display-only alert is in
            // progress. The final redraw clears messages already represented;
            // anything processed after that redraw starts a fresh alert.
            headless_alert_requested=true;
        }else{
            start_message_alert();
        }
    }else{
        status_wake_light=true;
    }
    T5_DEBUGF(T5_LOG_MESH,"[T5-UI] %s message event unread=%u refresh queued standby=%d headless=%d visible=%d\n",
              channel?"channel":"direct",channel?status_channel_unread:status_unread,
              standby_active,headless_ui_state,visible);
}

bool ui_restore_failed_compose(const char* text){
    if(!text||!text[0]||compose_text[0])return false;
    strncpy(compose_text,text,sizeof(compose_text)-1);
    compose_text[sizeof(compose_text)-1]=0;
    status_dirty=true;
    return true;
}

void ui_notify_advert_result(bool flood,bool ok){
    show_toast(ok?(flood?"FLOOD ADVERT SENT":"ZERO HOP ADVERT SENT"):"ADVERT FAILED");
    status_dirty=true;T5_DEBUGF(T5_LOG_MESH,"[T5-UI] advert result flood=%d ok=%d\n",flood,ok);
}

void ui_notify_node_position_unavailable(){
    if(screen!=Screen::ContactDetails||standby_active)return;
    show_toast("NO POSITION RECEIVED");
    status_dirty=true;
    T5_DEBUGLN(T5_LOG_MESH,"[T5-UI] no GPS returned by latest node info request; retaining last known position");
}

void ui_request_data_refresh(const char* reason){
    status_dirty=true;T5_DEBUGF(T5_LOG_UI,"[T5-UI] refresh queued reason=%s\n",reason?reason:"data");
}

void ui_apply_initial_radio_preset(){
    // First-use radio changes are staged in the wizard and applied only when
    // the user confirms. Never apply the former NZ preset implicitly at boot.
}

void ui_mesh_ready(){
    mesh_is_ready=true;
    // MeshInk owns the user-visible identity. Every active protocol receives
    // the same stored name through its adapter instead of becoming the source
    // of truth for shared product state.
    Preferences state;
    if(state.begin("t5-ui",false)){
        const String stored_name=state.getString("name",node_name);
        if(stored_name.length()){
            strncpy(node_name,stored_name.c_str(),sizeof(node_name)-1);
            node_name[sizeof(node_name)-1]=0;
        }
        state.putBool("name_migrated",true);
        state.putString("name",node_name);
        state.end();
    }
    if(node_name[0]){
        mesh_protocol_apply_name(node_name);
        T5_DEBUGF(T5_LOG_UI,"[T5-UI] MeshInk node name applied to %s '%s'\n",
                  mesh_protocol_name(),node_name);
    }
    update_status_hardware();status_bar_dirty=true;
}

void ui_use_data_provider(UiDataProvider* provider) {
    if (!provider) return;
    ui_data=provider;
    T5_DEBUGLN(T5_LOG_UI,"[T5-UI] live protocol data provider attached");
    // ui_finish_startup() presents the first interactive screen only after
    // the blocking storage / protocol boot sequence has completed.
}

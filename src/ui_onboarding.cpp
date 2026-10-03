#include <Arduino.h>
#include <esp_random.h>
#include <Preferences.h>
#include "hardware/display.h"
#include "hardware/storage.h"
#include "hardware/touch.h"
#include "hardware/power.h"
#include "hardware/buttons.h"
#include "hardware/board.h"
#include <esp_heap_caps.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp32-hal-cpu.h>
#include <SPIFFS.h>
#include "ui_onboarding.h"
#include "ui_data.h"
#include "local_mesh_runtime.h"
#include "map_tiles.h"
#include "map_gestures.h"
#include "ui_layout.h"
#include "t5_logging.h"
#include "t5_timing.h"
#include "meshcore_version.h"
#include "keyboard_geometry.h"
#include "message_limits.h"
#include "message_store.h"
#include "fonts/inter_15_regular.h"
#include "fonts/inter_20_regular.h"
#include "fonts/inter_25_regular.h"
#include "fonts/inter_30_regular.h"
#include "fonts/inter_50_digits.h"
#include "meshink_logo_bitmap.h"  // generated from original PNG at build time

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.3.0"
#endif

#ifndef MESHINK_GEOMETRY_DIAGNOSTICS
#define MESHINK_GEOMETRY_DIAGNOSTICS 0
#endif

void request_companion_mode() __attribute__((weak));
void request_companion_mode() {}

// UI milestone 0.1.0: standalone onboarding. Bluetooth, radio and GPS are not
// started in this target. Saved values are device-owned and will be handed to
// the MeshCore application adapter in the next milestone.
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
static size_t discovery_page = 0;
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
static uint8_t timezone_index = 0;
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
static uint32_t last_user_activity=0;
static bool status_wake_light=false;
static bool message_alert_active=false;
static bool quick_panel_active=false;
static bool quick_panel_restore_landscape=false;
static volatile bool quick_slider_dragging=false;
static volatile uint8_t quick_slider_preview=30;
static uint8_t message_alert_phase=0;
static uint32_t message_alert_deadline=0;
static uint32_t message_alert_cooldown_until=0;
enum class Screen : uint8_t {
    Welcome, Presets, CompanionConfirm, ShutdownConfirm,
    Contacts, ContactChat, ContactDetails,
    Channels, ChannelChat, Maps, Discovery, More, AdvertMenu, Diagnostics,
    Settings, RadioSettings, GpsSettings, GpsTuning, Timezone, PrivacySettings, DisplaySettings, NightSchedule, Help, About
};
static Screen screen = Screen::Welcome;
static const char* timing_screen_name(){
    switch(screen){
        case Screen::Welcome:return "Welcome";
        case Screen::Presets:return "Presets";
        case Screen::CompanionConfirm:return "Companion";
        case Screen::ShutdownConfirm:return "Shutdown";
        case Screen::Contacts:return "Contacts";
        case Screen::ContactChat:return "ContactChat";
        case Screen::ContactDetails:return "NodeInfo";
        case Screen::Channels:return "Channels";
        case Screen::ChannelChat:return "ChannelChat";
        case Screen::Maps:return "Maps";
        case Screen::Discovery:return "Discovery";
        case Screen::More:return "More";
        case Screen::AdvertMenu:return "Advert";
        case Screen::Diagnostics:return "Diagnostics";
        case Screen::Settings:return "Settings";
        case Screen::RadioSettings:return "RadioSettings";
        case Screen::GpsSettings:return "GpsSettings";
        case Screen::GpsTuning:return "GpsTuning";
        case Screen::Timezone:return "Timezone";
        case Screen::PrivacySettings:return "Privacy";
        case Screen::DisplaySettings:return "DisplaySettings";
        case Screen::NightSchedule:return "NightSchedule";
        case Screen::Help:return "Help";
        case Screen::About:return "About";
        default:return "?";
    }
}
static Screen preset_return_screen = Screen::Welcome;
static uint8_t preset_page = 3;
static bool details_from_discovery=false;
static uint8_t details_page=0;
enum class NodeInfoPage:uint8_t{Overview=0,Status,Telemetry,Path};
static bool node_has_status(uint8_t type){return type==(uint8_t)UiNodeRole::Repeater||type==(uint8_t)UiNodeRole::Room;}
static uint8_t node_info_page_count(uint8_t type){return node_has_status(type)?4:3;}
static NodeInfoPage node_info_page(uint8_t type,uint8_t page){
    if(page==0)return NodeInfoPage::Overview;
    if(node_has_status(type)){if(page==1)return NodeInfoPage::Status;return page==2?NodeInfoPage::Telemetry:NodeInfoPage::Path;}
    return page==1?NodeInfoPage::Telemetry:NodeInfoPage::Path;
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
static MapRenderResult map_base_result{false,0,0,0,0,0,0};
static uint32_t map_base_media_epoch=0;
// The own-position bullseye is drawn over the map, never stored in the
// raster base cache. The last drawn screen position supports low-rate GPS
// marker updates without refreshing the e-paper for every GPS sample.
static bool map_device_marker_visible=false;
static int map_device_marker_x=0,map_device_marker_y=0;
struct MapMarkerHit {int16_t x,y;size_t index;};
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
// GPS use MeshCore's configured static "My Location" coordinates.
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
    return local_mesh_my_location(latitude,longitude);
}
static bool centre_map_on_device(){
    long latitude=0,longitude=0;bool current_fix=false;
    if(!map_device_position(latitude,longitude,current_fix))return false;
    map_latitude=latitude/1000000.0;map_longitude=longitude/1000000.0;
    return true;
}

static constexpr uint8_t PRESETS_PER_PAGE = 5;

struct TimezoneChoice { const char* label; const char* detail; const char* rule; };
static constexpr TimezoneChoice TIMEZONES[] = {
 {"NEW ZEALAND","NZST / NZDT AUTOMATIC","NZST-12NZDT,M9.5.0,M4.1.0/3"},
 {"UTC","COORDINATED UNIVERSAL TIME","UTC0"},
 {"AUSTRALIA EAST","AEST / AEDT AUTOMATIC","AEST-10AEDT,M10.1.0,M4.1.0/3"},
 {"UNITED KINGDOM","GMT / BST AUTOMATIC","GMT0BST,M3.5.0/1,M10.5.0"},
 {"CENTRAL EUROPE","CET / CEST AUTOMATIC","CET-1CEST,M3.5.0,M10.5.0/3"},
 {"US PACIFIC","PST / PDT AUTOMATIC","PST8PDT,M3.2.0,M11.1.0"},
 {"US EASTERN","EST / EDT AUTOMATIC","EST5EDT,M3.2.0,M11.1.0"},
};
static constexpr uint8_t TIMEZONE_COUNT=sizeof(TIMEZONES)/sizeof(TIMEZONES[0]);

static void apply_timezone(){setenv("TZ",TIMEZONES[timezone_index].rule,1);tzset();T5_DEBUGF(T5_LOG_UI,"[T5-TIME] display timezone=%s\n",TIMEZONES[timezone_index].label);}

static constexpr uint32_t FRONTLIGHT_TIMEOUTS[]={5000,10000,15000,30000,0};
static constexpr uint32_t STANDBY_TIMEOUTS[]={300000,600000,900000,0};
static const char* frontlight_mode_name(){return frontlight_mode==FrontlightMode::On?"ON":frontlight_mode==FrontlightMode::NightTimer?"NIGHT TIMER":"OFF";}
static const char* frontlight_timeout_name(){static const char* names[]={"5 SECONDS","10 SECONDS","15 SECONDS","30 SECONDS","ALWAYS ON"};return names[min((uint8_t)4,frontlight_timeout_index)];}
static const char* standby_timeout_name(){static const char* names[]={"5 MINUTES","10 MINUTES","15 MINUTES","NEVER"};return names[min((uint8_t)3,standby_timeout_index)];}
static bool night_window_active(){const uint16_t now=status_hour<0?0:(uint16_t)(status_hour*60+status_minute);return night_start_minutes<=night_end_minutes?(now>=night_start_minutes&&now<night_end_minutes):(now>=night_start_minutes||now<night_end_minutes);}
static bool frontlight_allowed(){return frontlight_mode==FrontlightMode::On||(frontlight_mode==FrontlightMode::NightTimer&&night_window_active());}
static void frontlight_drive(bool on){frontlight_lit=on&&frontlight_allowed();meshink_power_frontlight_set(frontlight_lit?frontlight_brightness:0);}
static void frontlight_preview(uint8_t level){
    // Live PWM feedback for the quick slider only. Do not alter the persisted
    // brightness or redraw the e-paper until the release event is handled.
    frontlight_lit=level>0;
    meshink_power_frontlight_set(level);
    const uint32_t timeout=FRONTLIGHT_TIMEOUTS[min((uint8_t)4,frontlight_timeout_index)];
    frontlight_deadline=timeout?millis()+timeout:0;
}
static void frontlight_event(){if(!frontlight_allowed()){frontlight_drive(false);frontlight_deadline=0;return;}frontlight_drive(true);const uint32_t timeout=FRONTLIGHT_TIMEOUTS[min((uint8_t)4,frontlight_timeout_index)];frontlight_deadline=timeout?millis()+timeout:0;}
static void frontlight_service(){if(message_alert_active||quick_slider_dragging)return;if(frontlight_mode==FrontlightMode::Off||(frontlight_mode==FrontlightMode::NightTimer&&!night_window_active())){if(frontlight_lit)frontlight_drive(false);return;}if(frontlight_lit&&frontlight_deadline&&(int32_t)(millis()-frontlight_deadline)>=0){frontlight_deadline=0;frontlight_drive(false);T5_DEBUGLN(T5_LOG_UI,"[T5-LIGHT] timeout; frontlight off");}}
static void save_frontlight_settings(){Preferences light;if(light.begin("t5-ui",false)){light.putUChar("light_mode",(uint8_t)frontlight_mode);light.putUChar("light_timeout",frontlight_timeout_index);light.putUChar("light_level",frontlight_brightness);light.putUChar("standby_timeout",standby_timeout_index);light.putUShort("night_start",night_start_minutes);light.putUShort("night_end",night_end_minutes);light.end();}}

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

struct T5InputTimingScope{
#if T5_TIMING_DIAGNOSTICS
    uint32_t started_us;
    uint32_t age_ms;
    uint32_t queue_depth;
    T5InputTimingScope(uint32_t queued_at,uint32_t depth):
        started_us(micros()),age_ms(queued_at?(uint32_t)(millis()-queued_at):0),queue_depth(depth){
        t5_timing_set_ui_action(T5UiAction::Touch);
    }
    ~T5InputTimingScope(){
        t5_timing_note_ui_input((uint32_t)(micros()-started_us),age_ms,queue_depth);
        t5_timing_set_ui_action(T5UiAction::None);
    }
#else
    T5InputTimingScope(uint32_t,uint32_t){}
#endif
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
    const bool accepted=setCpuFrequencyMhz(mhz);const uint32_t actual=getCpuFrequencyMhz();
    if(!accepted||actual!=mhz)Serial.printf("[T5-ERROR] CPU target=%lu actual=%lu MHz reason=%s\n",(unsigned long)mhz,(unsigned long)actual,reason);
    return accepted&&actual==mhz;
}

struct T5CpuBoostScope {
    uint32_t previous_mhz;
    bool restore;
    explicit T5CpuBoostScope(bool enabled,const char* reason):
        previous_mhz(getCpuFrequencyMhz()),restore(false){
        if(enabled&&previous_mhz<UI_RENDER_CPU_MHZ)
            restore=set_cpu_target(UI_RENDER_CPU_MHZ,reason);
    }
    ~T5CpuBoostScope(){
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

static bool apply_selected_preset() {
    if(selected_preset>=PRESET_COUNT)return false;
    const Preset& preset=PRESETS[selected_preset];
    if(preset.path_hash_bytes==0)return true; // KEEP CURRENT never alters the radio
    // Apply the *same typed values* displayed by the preset selector.
    // The MeshCore property uses 0/1/2 for a 1/2/3-byte path hash.
    const bool applied=local_mesh_apply_radio(preset.frequency_khz/1000.0f,
        preset.bandwidth_khz,preset.spreading_factor,preset.coding_rate,
        preset.path_hash_bytes-1);
    if(!applied)Serial.printf("[T5-ERROR] radio preset '%s' could not be applied\n",preset.title);
    else T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] radio preset '%s' applied\n",preset.title);
    return applied;
}
static const char* path_hash_label(){static const char* labels[]={"1 BYTE","2 BYTES","3 BYTES"};return labels[min((uint8_t)2,local_mesh_path_hash_mode())];}

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
#if T5_TIMING_DIAGNOSTICS
struct UiMessagePerfCounters {
    uint32_t geometry_us=0;
    uint32_t fill_us=0;
    uint32_t smooth_us=0;
    uint32_t wrap_calls=0;
    uint32_t wrap_chars=0;
    uint32_t advances=0;
    uint32_t smooth_calls=0;
    uint32_t smooth_chars=0;
    uint32_t glyph_pixels=0;
    uint32_t draw_ops=0;
    uint16_t geometry_calls=0;
    uint16_t fill_calls=0;
};
static UiMessagePerfCounters ui_message_perf{};
static bool ui_message_perf_collect=false;
static void ui_message_perf_reset(){ui_message_perf=UiMessagePerfCounters{};}

static T5UiRenderPerf ui_render_perf{};
static bool ui_render_perf_collect=false;
static uint8_t ui_render_perf_depth=0;
static void ui_render_perf_begin(){
    if(ui_render_perf_depth++==0){
        ui_render_perf=T5UiRenderPerf{};
        ui_render_perf_collect=true;
    }
}
static void ui_render_perf_end(uint32_t started_us){
    const uint32_t elapsed=(uint32_t)(micros()-started_us);
    if(ui_render_perf_depth) --ui_render_perf_depth;
    if(!ui_render_perf_depth){
        ui_render_perf.total_us=elapsed;
        ui_render_perf_collect=false;
        t5_timing_note_ui_render(ui_render_perf);
    }
    t5_timing_note_ui_draw(elapsed);
}
#endif

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
#if T5_TIMING_DIAGNOSTICS
    if(ui_message_perf_collect)++ui_message_perf.advances;
#endif
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
    if(!s||!n)return 0;int width=0;
    for(size_t i=0;i<n&&s[i]&&s[i]!='\n';++i)width+=ui_char_advance(s[i],scale);
    return width&&scale<3?width-scale:width;
}
static int ui_text_width(const char* s,int scale) {
    return s?ui_text_width_n(s,strlen(s),scale):0;
}
static void ui_smooth_text(const char* s,int x,int y,int scale,uint8_t color,bool bold) {
    if(!s)return;
#if T5_TIMING_DIAGNOSTICS
    const uint32_t perf_started=ui_message_perf_collect?micros():0;
    if(ui_message_perf_collect)++ui_message_perf.smooth_calls;
#endif
    const UiSmoothFont face=ui_smooth_font(scale);
    const int baseline=y+face.baseline_from_top;
    while(*s&&*s!='\n'){
        const char c=*s++;
        const MeshInkFontGlyph* glyph_data=ui_smooth_glyph(face.font,(uint8_t)c);
        if(!glyph_data)continue;
#if T5_TIMING_DIAGNOSTICS
        if(ui_message_perf_collect){
            ++ui_message_perf.smooth_chars;
            ui_message_perf.glyph_pixels+=(uint32_t)glyph_data->width*glyph_data->height;
        }
#endif
        const int gx=x+glyph_data->left;
        const int gy=baseline-glyph_data->top;
        for(int sy=0;sy<glyph_data->height;++sy){
            for(int sx=0;sx<glyph_data->width;++sx){
                const uint32_t bit=(uint32_t)sy*glyph_data->width+(uint32_t)sx;
                const uint8_t packed=face.font->bitmap[glyph_data->dataOffset+(bit>>3)];
                if(!(packed&(uint8_t)(0x80U>>(bit&7))))continue;
#if T5_TIMING_DIAGNOSTICS
                if(ui_message_perf_collect)ui_message_perf.draw_ops+=bold?2U:1U;
#endif
                meshink_display_draw_pixel(gx+sx,gy+sy,color,fb);
                if(bold)meshink_display_draw_pixel(gx+sx+1,gy+sy,color,fb);
            }
        }
        x+=ui_smooth_char_advance(c,scale);
    }
#if T5_TIMING_DIAGNOSTICS
    if(ui_message_perf_collect)ui_message_perf.smooth_us+=(uint32_t)(micros()-perf_started);
#endif
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
#if T5_TIMING_DIAGNOSTICS
    const uint32_t render_started=ui_render_perf_collect?micros():0;
    if(ui_render_perf_collect){
        ++ui_render_perf.text_calls;
        for(const char* p=s;*p&&*p!='\n';++p)++ui_render_perf.text_chars;
    }
#endif
    if(scale>=3){
        ui_smooth_text(s,x,y,scale,color,bold);
#if T5_TIMING_DIAGNOSTICS
        if(ui_render_perf_collect)ui_render_perf.text_us+=(uint32_t)(micros()-render_started);
#endif
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
#if T5_TIMING_DIAGNOSTICS
    if(ui_render_perf_collect)ui_render_perf.text_us+=(uint32_t)(micros()-render_started);
#endif
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
#if T5_TIMING_DIAGNOSTICS
    if(ui_message_perf_collect)++ui_message_perf.wrap_calls;
#endif
    size_t count=0,last_space=0;int width=0;
    while(value[count]&&value[count]!='\n'){
#if T5_TIMING_DIAGNOSTICS
        if(ui_message_perf_collect)++ui_message_perf.wrap_chars;
#endif
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
        while(*cursor==' ')++cursor;if(*cursor=='\n')++cursor;
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

    // One line remains vertically comfortable. Once wrapping starts, pin the
    // newest line to the bottom and let the preceding line peek through the
    // clipped top of the viewport so text never appears to vanish.
    const int current_y=line_count<=1
        ?y+(max_height-glyph_height)/2
        :y+max_height-glyph_height;
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

    // A 1 px, half-height non-blinking caret is enough to expose trailing
    // spaces and the next insertion point without dominating an e-paper field.
    const UiComposeLine& current=lines[line_count-1];
    const int caret_advance=ui_text_width_n(current.start,current.len,scale);
    const int caret_x=max(x,min(x+max_width-1,x+caret_advance));
    const int caret_height=max(8,glyph_height/2);
    const int caret_y=current_y+(glyph_height-caret_height)/2;
    meshink_display_fill_rect({caret_x,caret_y,1,caret_height},0,fb);
}

// Rounded surfaces are composed directly in the selected display backend.
// The T5 backend preserves these exact integer corner pixels while choosing
// packed-framebuffer-friendly spans for the current rotation.
static void rounded_fill(int x,int y,int w,int h,int radius,uint8_t color) {
    if(w<=0||h<=0)return;
#if T5_TIMING_DIAGNOSTICS
    const uint32_t render_started=ui_render_perf_collect?micros():0;
    if(ui_render_perf_collect)++ui_render_perf.rounded_calls;
#endif
    meshink_display_fill_rounded_rect({x,y,w,h},radius,color,fb);
#if T5_TIMING_DIAGNOSTICS
    if(ui_render_perf_collect)ui_render_perf.rounded_us+=(uint32_t)(micros()-render_started);
#endif
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

#if MESHINK_GEOMETRY_DIAGNOSTICS
static bool ui_rect_inside(int width,int height,const MeshInkUiRect& rect) {
    return rect.x>=0&&rect.y>=0&&rect.width>0&&rect.height>0&&
           rect.x+rect.width<=width&&rect.y+rect.height<=height;
}
static bool ui_rect_contains(const MeshInkUiRect& outer,const MeshInkUiRect& inner) {
    return inner.x>=outer.x&&inner.y>=outer.y&&
           inner.x+inner.width<=outer.x+outer.width&&
           inner.y+inner.height<=outer.y+outer.height;
}
static bool keyboard_rect_inside(int width,int height,const meshink_keyboard::Rect& rect) {
    return rect.x>=0&&rect.y>=0&&rect.width>0&&rect.height>0&&
           rect.x+rect.width<=width&&rect.y+rect.height<=height;
}
static void audit_ui_geometry() {
    const MeshInkUiLayout& layout=portrait_layout();
    bool ok=layout.width>0&&layout.height>0&&
            layout.status_height>0&&layout.bottom_nav_height>0&&
            layout.bottom_nav_top+layout.bottom_nav_height==layout.height&&
            layout.map_top==layout.status_height&&
            layout.map_bottom==layout.bottom_nav_top;

    struct NamedUiRect { const char* name; MeshInkUiRect rect; };
    const NamedUiRect rects[]={
        {"header-back",meshink_header_back_rect(layout)},
        {"header-back-touch",meshink_header_back_touch_rect(layout)},
        {"header-action",meshink_header_action_rect(layout)},
        {"header-action-touch",meshink_header_action_touch_rect(layout)},
        {"welcome-name",meshink_welcome_name_rect(layout)},
        {"welcome-preset",meshink_welcome_preset_rect(layout)},
        {"welcome-companion",meshink_welcome_companion_rect(layout)},
        {"welcome-keyboard",meshink_welcome_show_keyboard_rect(layout)},
        {"preset-back",meshink_preset_back_rect(layout)},
        {"preset-prev",meshink_preset_prev_rect(layout)},
        {"preset-next",meshink_preset_next_rect(layout)},
        {"confirm-left",meshink_confirm_left_rect(layout,500)},
        {"confirm-right",meshink_confirm_right_rect(layout,500)},
        {"shutdown-confirm-left",meshink_confirm_left_rect(layout,650)},
        {"shutdown-confirm-right",meshink_confirm_right_rect(layout,650)},
        {"node-map",meshink_node_map_rect(layout)},
        {"node-action",meshink_node_action_rect(layout)},
        {"node-left",meshink_node_left_action_rect(layout)},
        {"node-right",meshink_node_right_action_rect(layout)},
        {"password-save",meshink_password_save_rect(layout)},
        {"map-plus",meshink_map_control_rect(layout,0)},
        {"map-minus",meshink_map_control_rect(layout,1)},
        {"map-locate",meshink_map_control_rect(layout,2)},
        {"quick-slider",meshink_quick_slider_track_rect(layout)},
        {"quick-slider-touch",meshink_quick_slider_touch_rect(layout)},
        {"quick-minus",meshink_quick_minus_rect(layout)},
        {"quick-plus",meshink_quick_plus_rect(layout)},
        {"quick-advert",meshink_quick_advert_rect(layout)},
        {"quick-power",meshink_quick_power_rect(layout)},
        {"display-edit-times",meshink_settings_inline_action_rect(layout,118)},
        {"display-brightness",meshink_display_brightness_rect(layout)},
        {"display-slider",meshink_display_slider_track_rect(layout)},
        {"display-slider-touch",meshink_display_slider_touch_rect(layout)},
        {"shutdown",meshink_shutdown_rect(layout)},
        {"night-start",meshink_night_start_rect(layout)},
        {"night-end",meshink_night_end_rect(layout)},
        {"night-minus",meshink_night_minus_rect(layout)},
        {"night-plus",meshink_night_plus_rect(layout)},
        {"night-save",meshink_night_save_rect(layout)}
    };
    for(const auto& item:rects) {
        if(!ui_rect_inside(layout.width,layout.height,item.rect)) {
            ok=false;
            Serial.printf("[T5-GEOM] ERROR ui %s rect=%d,%d %dx%d outside %dx%d\n",
                          item.name,item.rect.x,item.rect.y,item.rect.width,item.rect.height,
                          layout.width,layout.height);
        }
    }
    for(int row=0;row<PRESETS_PER_PAGE;++row) {
        const MeshInkUiRect preset=meshink_preset_row_rect(layout,row);
        if(!ui_rect_inside(layout.width,layout.height,preset)) {
            ok=false;
            Serial.printf("[T5-GEOM] ERROR preset row=%d rect=%d,%d %dx%d outside %dx%d\n",
                          row,preset.x,preset.y,preset.width,preset.height,
                          layout.width,layout.height);
        }
    }
    if(!ui_rect_contains(meshink_header_back_touch_rect(layout),
                         meshink_header_back_rect(layout))) {
        ok=false;Serial.println("[T5-GEOM] ERROR header-back touch does not contain visual");
    }
    if(!ui_rect_contains(meshink_header_action_touch_rect(layout),
                         meshink_header_action_rect(layout))) {
        ok=false;Serial.println("[T5-GEOM] ERROR header-action touch does not contain visual");
    }
    if(!ui_rect_contains(meshink_quick_slider_touch_rect(layout),
                         meshink_quick_slider_track_rect(layout))) {
        ok=false;Serial.println("[T5-GEOM] ERROR quick slider touch does not contain visual");
    }
    if(!ui_rect_contains(meshink_display_slider_touch_rect(layout),
                         meshink_display_slider_track_rect(layout))) {
        ok=false;Serial.println("[T5-GEOM] ERROR display slider touch does not contain visual");
    }

    for(int index=0;index<3;++index) {
        const MeshInkUiRect control=meshink_map_control_rect(layout,index);
        if(control.y<layout.map_top||control.y+control.height>layout.map_bottom) {
            ok=false;
            Serial.printf("[T5-GEOM] ERROR map control %d y=%d..%d outside viewport=%d..%d\n",
                          index,control.y,control.y+control.height,
                          layout.map_top,layout.map_bottom);
        }
    }

    const auto portrait=keyboard_metrics(false);
    const auto landscape=keyboard_metrics(true);
    struct NamedKeyboardRect { const char* name; meshink_keyboard::Rect rect; };
    const NamedKeyboardRect portrait_rects[]={
        {"entry",portrait.entry},{"mode",portrait.mode_key},{"delete",portrait.delete_key},
        {"orientation",portrait.orientation_key},{"space",portrait.space_key},
        {"action",portrait.action_key},{"wide-action",portrait.wide_action_key}
    };
    const NamedKeyboardRect landscape_rects[]={
        {"entry",landscape.entry},{"mode",landscape.mode_key},{"delete",landscape.delete_key},
        {"orientation",landscape.orientation_key},{"space",landscape.space_key},
        {"action",landscape.action_key}
    };
    for(const auto& item:portrait_rects) {
        if(!keyboard_rect_inside(portrait.width,portrait.height,item.rect)) {
            ok=false;
            Serial.printf("[T5-GEOM] ERROR keyboard portrait %s=%d,%d %dx%d outside %dx%d\n",
                          item.name,item.rect.x,item.rect.y,item.rect.width,item.rect.height,
                          portrait.width,portrait.height);
        }
    }
    for(const auto& item:landscape_rects) {
        if(!keyboard_rect_inside(landscape.width,landscape.height,item.rect)) {
            ok=false;
            Serial.printf("[T5-GEOM] ERROR keyboard landscape %s=%d,%d %dx%d outside %dx%d\n",
                          item.name,item.rect.x,item.rect.y,item.rect.width,item.rect.height,
                          landscape.width,landscape.height);
        }
    }
    if(portrait.number_top<0||
       portrait.bottom_top+portrait.key_height>portrait.height||
       landscape.number_top<0||
       landscape.bottom_top+landscape.key_height>landscape.height) {
        ok=false;
        Serial.printf("[T5-GEOM] ERROR keyboard rows portrait=%d..%d/%d landscape=%d..%d/%d\n",
                      portrait.number_top,portrait.bottom_top+portrait.key_height,portrait.height,
                      landscape.number_top,landscape.bottom_top+landscape.key_height,landscape.height);
    }

    const bool reference=layout.width==540&&layout.height==960;
    if(reference) {
        const MeshInkUiRect quick=meshink_quick_slider_touch_rect(layout);
        const MeshInkUiRect display_touch=meshink_display_slider_touch_rect(layout);
        if(layout.status_height!=48||layout.bottom_nav_top!=900||
           quick.x!=28||quick.y!=146||quick.width!=484||quick.height!=80||
           display_touch.x!=40||display_touch.y!=420||
           display_touch.width!=460||display_touch.height!=100) {
            ok=false;
            Serial.println("[T5-GEOM] ERROR T5 reference geometry no longer pixel-exact");
        }
    }

    if(!fb) {
        ok=false;
        Serial.println("[T5-GEOM] ERROR display framebuffer is null");
    }
    Serial.printf("[T5-GEOM] version=%s board=%s logical=%dx%d physical=%dx%d fb=%p bytes=%u ref=%u "
                  "map=%d..%d kbP=%d..%d kbL=%d..%d result=%s\n",
                  UI_VERSION,meshink_board_name(),layout.width,layout.height,
                  meshink_display_physical_width(),meshink_display_physical_height(),
                  (void*)fb,(unsigned)meshink_display_framebuffer_bytes(),reference?1U:0U,
                  layout.map_top,layout.map_bottom,
                  portrait.number_top,portrait.bottom_top+portrait.key_height,
                  landscape.number_top,landscape.bottom_top+landscape.key_height,
                  ok?"OK":"FAIL");
}
#endif

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
    key("LAND",metrics.orientation_key);
    if(keyboard_message_mode||keyboard_password_mode){
        // Space is valid for message/password entry. Match familiar phone
        // keyboards with a wide space bar and an isolated action at right.
        key("SPACE",metrics.space_key);
        key(keyboard_password_mode?"LOGIN":"SEND",metrics.action_key);
    }else{
        // MeshCore node names do not accept spaces. Use the entire remaining
        // row for SAVE instead of showing dead SPACE/HIDE controls.
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
    landscape_key("SPACE",metrics.space_key);
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
    if(level<0)level=status_battery;if(level>0){const int fill=(level*27)/100;meshink_display_fill_rect({x+2,y+8,fill,14},0,fb);}
    if(meshink_power_is_charging(status_charge_state)){
        meshink_display_fill_rect({x+10,y+6,14,17},0xFF,fb);
        // Wide, bold lightning bolt for the low-resolution status bar.
        for(int d=-2;d<=2;++d){line(x+22+d,y+5,x+12+d,y+16);line(x+12+d,y+16,x+20+d,y+16);line(x+20+d,y+16,x+10+d,y+27);}
    }
}

static void draw_status_bar() {
    T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-status-draw");
#if T5_TIMING_DIAGNOSTICS
    const uint32_t render_started=ui_render_perf_collect?micros():0;
#endif
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
        else if(status_gps_fix)draw_target_icon(ui_x(6),ui_y(9),false);
        else draw_search_icon(ui_x(6),ui_y(9));
        left=ui_x(46);
        if(!standby_active&&status_gps_enabled&&status_gps_fix) {
            char satellites[4];
            snprintf(satellites,sizeof(satellites),"%d",max(0,min(99,(int)status_gps_satellites_bar)));
            // Match the clock and battery percentage: the satellite count
            // is primary status information, not compact secondary metadata.
            text(satellites,ui_x(43),ui_y(13),3,0,true);
            left=ui_x(43)+(int)strlen(satellites)*18+ui_w(8);
        }
    }
    if(status_unread){
        draw_envelope_icon(left,ui_y(9));left+=ui_w(36);
        char count[5];compact_count(status_unread,count);
        text(count,left,ui_y(17),2,0,true);
        left+=(int)strlen(count)*12+ui_w(8);
    }
    if(status_channel_unread){
        draw_channel_status_icon(left,ui_y(9));left+=ui_w(34);
        char count[5];compact_count(status_channel_unread,count);
        text(count,left,ui_y(17),2,0,true);
    }
    char clock_text[8];
    if(status_hour>=0)snprintf(clock_text,sizeof(clock_text),"%02d:%02d",status_hour,status_minute);
    else snprintf(clock_text,sizeof(clock_text),"--:--");
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
        status_gps_enabled?(status_gps_fix?"fix":"searching"):"off");
#if T5_TIMING_DIAGNOSTICS
    if(ui_render_perf_collect)ui_render_perf.status_us+=(uint32_t)(micros()-render_started);
#endif
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
    T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-toast-draw");
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

static void draw_welcome() {
    meshink_display_set_all_white(&display);
    draw_status_bar();
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect name_rect=meshink_welcome_name_rect(layout);
    const MeshInkUiRect preset_rect=meshink_welcome_preset_rect(layout);
    const MeshInkUiRect companion_rect=meshink_welcome_companion_rect(layout);
    ui_centred("MESHCORE",ui_y(62),6,0,true);
    ui_centred("Set up your T5",ui_y(116),3,0,true);
    ui_text("YOUR NAME",layout.form_margin,ui_y(154),2,0,true);
    ui_section_card(name_rect);
    ui_text_fit(node_name,layout.form_text_x,name_rect.y+ui_h(18),
                name_rect.width-ui_w(36),3,0,false);
    ui_text("RADIO PRESET",layout.form_margin,ui_y(268),2,0,true);
    ui_section_card(preset_rect);
    ui_text_fit(PRESETS[selected_preset].title,preset_rect.x+ui_w(14),preset_rect.y+ui_h(10),
                preset_rect.width-ui_w(58),3,0,true);
    ui_text_fit(PRESETS[selected_preset].detail,preset_rect.x+ui_w(14),preset_rect.y+ui_h(51),
                preset_rect.width-ui_w(58),2,0,false);
    ui_text(">",preset_rect.x+preset_rect.width-ui_w(30),preset_rect.y+ui_h(27),3,0,true);
    ui_section_card(companion_rect);
    ui_centred("Bluetooth companion mode",companion_rect.y+ui_h(15),2,0,true);
    if(keyboard_visible){ui_centred("Enter a name",ui_y(586),2,0,true);draw_keyboard();}
    else {
        const MeshInkUiRect show_rect=meshink_welcome_show_keyboard_rect(layout);
        ui_action_button("SHOW KEYBOARD",show_rect,true);
    }
}

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
#if T5_TIMING_DIAGNOSTICS
    const uint32_t render_started=ui_render_perf_collect?micros():0;
#endif
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
#if T5_TIMING_DIAGNOSTICS
    if(ui_render_perf_collect)ui_render_perf.nav_us+=(uint32_t)(micros()-render_started);
#endif
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

static const char* node_role_label(uint8_t type){
    static char unknown[16];
    switch(type){
        case (uint8_t)UiNodeRole::Unknown:return "UNKNOWN";
        case (uint8_t)UiNodeRole::Chat:return "CHAT";
        case (uint8_t)UiNodeRole::Repeater:return "REPEATER";
        case (uint8_t)UiNodeRole::Room:return "ROOM SERVER";
        case (uint8_t)UiNodeRole::Sensor:return "SENSOR";
        default:snprintf(unknown,sizeof(unknown),"TYPE %u",(unsigned)type);return unknown;
    }
}
static void thick_line(int x1,int y1,int x2,int y2){
    for(int d=-1;d<=1;++d){line(x1+d,y1,x2+d,y2);line(x1,y1+d,x2,y2+d);}
}
static void thick_rect(int x,int y,int w,int h){
    for(int d=0;d<3;++d)meshink_display_draw_rect({x+d,y+d,w-2*d,h-2*d},0,fb);
}
static void draw_node_role_icon(uint8_t type,int x,int y){
    if(type==(uint8_t)UiNodeRole::Chat){thick_rect(x,y+3,28,20);thick_line(x+6,y+23,x+3,y+29);thick_line(x+6,y+23,x+12,y+23);}
    else if(type==(uint8_t)UiNodeRole::Repeater){meshink_display_fill_rect({x+12,y+4,5,27},0,fb);thick_line(x+14,y+4,x+7,y+14);thick_line(x+14,y+4,x+21,y+14);thick_line(x+5,y+7,x,y+14);thick_line(x+23,y+7,x+28,y+14);meshink_display_fill_rect({x+7,y+28,15,5},0,fb);}
    else if(type==(uint8_t)UiNodeRole::Room){thick_rect(x+2,y+2,25,29);meshink_display_fill_rect({x+8,y+8,5,5},0,fb);meshink_display_fill_rect({x+17,y+8,5,5},0,fb);thick_rect(x+9,y+18,11,13);}
    else if(type==(uint8_t)UiNodeRole::Sensor){thick_rect(x+2,y+5,25,23);meshink_display_fill_rect({x+12,y+10,6,6},0,fb);thick_line(x+14,y+15,x+7,y+23);thick_line(x+14,y+15,x+22,y+20);}
    else {thick_rect(x+2,y+3,25,27);ui_text("?",x+8,y+8,2,0,true);}
}
static void draw_list_entry(const UiListEntry& item,int y,int subtitle_scale=3) {
    const MeshInkUiLayout& layout=portrait_layout();
    rounded_box(layout.outer_margin,y,layout.outer_width,layout.list_row_height,
                max(ui_w(12),ui_h(12)));
    const bool typed=item.node_type!=0;
    const int title_x=typed?layout.content_text_x+ui_w(42):layout.content_text_x;
    if(typed)draw_node_role_icon(item.node_type,layout.content_text_x,y+ui_h(15));

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
    draw_app_header("CHANNELS");
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

// Node positions are stable; only the labels move to avoid collisions.
static void draw_map_nodes() {
    map_marker_hit_count=0;
    if(!ui_data)return;
    const double world=256.0*(1U<<map_zoom);
    const double centre_x=(map_longitude+180.0)/360.0*world;
    const double rad=map_latitude*PI/180.0;
    const double centre_y=(1.0-log(tan(rad)+1.0/cos(rad))/PI)*world/2.0;
    // Keep only projected marker coordinates in the existing global hit array.
    // The previous implementation copied up to 50 complete UiMapNode records
    // onto loopTask's stack (~several KB) while Maps was already the deepest
    // UI rendering path. Fetch node details again only when drawing labels.
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
        if(sx<7||sx>portrait_layout().width-7||sy<map_top()+7||sy>map_bottom()-7)continue;
        map_marker_hits[count++]={(int16_t)sx,(int16_t)sy,i};
    }
    struct Bounds {int16_t x,y,w,h;};
    Bounds occupied[50]{};size_t occupied_count=0;
    const uint32_t now=(uint32_t)time(nullptr);
    for(size_t i=0;i<count;++i) {
        const auto& n=map_marker_hits[i];
        UiMapNode node{};if(!ui_data->map_node(n.index,node))continue;
        char short_name[19]{};strncpy(short_name,node.name,sizeof(short_name)-1);
        char age[16];
        if(node.gps_from_reply){
            // This is when our T5 RECEIVED GPS telemetry, not the remote fix time.
            const uint32_t seconds=(uint32_t)(millis()-node.gps_received_millis)/1000U;
            if(seconds<3600)snprintf(age,sizeof(age),"GPS %lum",(unsigned long)(seconds/60));
            else if(seconds<86400)snprintf(age,sizeof(age),"GPS %luh",(unsigned long)(seconds/3600));
            else snprintf(age,sizeof(age),"GPS %lud",(unsigned long)(seconds/86400));
        }else if(!node.advertised_at||now<node.advertised_at)strcpy(age,"ADV ?");
        else {const uint32_t seconds=now-node.advertised_at;
            if(seconds<3600)snprintf(age,sizeof(age),"ADV %lum",(unsigned long)(seconds/60));
            else if(seconds<86400)snprintf(age,sizeof(age),"ADV %luh",(unsigned long)(seconds/3600));
            else snprintf(age,sizeof(age),"ADV %lud",(unsigned long)(seconds/86400));}
        const int w=min(230,max(48,max(ui_text_width(short_name,2),ui_text_width(age,2))+8));
        const int offsets[4][2]={{12,-18},{-12-w,-18},{12,12},{-12-w,12}};
        int lx=0,ly=0;bool placed=false;
        const int label_bottom=ui_y(766);
        for(const auto& offset:offsets) {
            const int x=n.x+offset[0],y=n.y+offset[1];
            if(x<3||x+w>portrait_layout().width-3||y<map_top()+3||y+34>label_bottom)continue;
            bool overlap=false;
            for(size_t control=0;control<3&&!overlap;++control){
                const MeshInkUiRect r=meshink_map_control_rect(portrait_layout(),(int)control);
                if(x<r.x+r.width+4&&x+w+4>r.x&&y<r.y+r.height+4&&y+38>r.y)overlap=true;
            }
            for(size_t j=0;j<occupied_count&&!overlap;++j)if(x<occupied[j].x+occupied[j].w+4&&
                x+w+4>occupied[j].x&&y<occupied[j].y+occupied[j].h+3&&y+37>occupied[j].y)
                overlap=true;
            if(!overlap){lx=x;ly=y;placed=true;break;}
        }
        if(!placed)continue; // Keep the true-position dot even if labels collide.
        occupied[occupied_count++]={(int16_t)lx,(int16_t)ly,(int16_t)w,34};
        meshink_display_fill_rect({lx,ly,w,34},0xFF,fb);
        ui_text_fit(short_name,lx+4,ly+2,w-ui_w(8),2,0,true);
        ui_text_fit(age,lx+4,ly+18,w-ui_w(8),2,0,true);
    }
    // Always draw position dots last so a neighbouring label cannot move or
    // obscure a marker. Each circle has a white halo for contrast.
    for(size_t i=0;i<count;++i) {
        const auto& n=map_marker_hits[i];
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
    MapRenderResult result{media_ready,0,0,0,0,map_zoom,map_zoom};
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
            case UiMessageState::Retrying3:state="SENDING";break;
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
#if T5_TIMING_DIAGNOSTICS
    const uint32_t perf_started=ui_message_perf_collect?micros():0;
    if(ui_message_perf_collect&&ui_message_perf.geometry_calls<0xFFFF)
        ++ui_message_perf.geometry_calls;
#endif
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
    const MessageBubbleGeometry geometry={x,width,height,text_width};
#if T5_TIMING_DIAGNOSTICS
    if(ui_message_perf_collect)
        ui_message_perf.geometry_us+=(uint32_t)(micros()-perf_started);
#endif
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

static int message_bubble_height(const UiMessage& message){
    return message_bubble_geometry(message).height;
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
#if T5_TIMING_DIAGNOSTICS
    const uint32_t perf_started=ui_message_perf_collect?micros():0;
    if(ui_message_perf_collect&&ui_message_perf.fill_calls<0xFFFF)
        ++ui_message_perf.fill_calls;
#endif
    size_t candidate=end;int used=0;
    while(candidate>0){
        const int h=chat_message_geometry(candidate-1).height;
        const int needed=h+(used?ui_h(8):0);
        if(used+needed>available)break;
        used+=needed;--candidate;
        if(used>=available)break;
    }
#if T5_TIMING_DIAGNOSTICS
    if(ui_message_perf_collect)
        ui_message_perf.fill_us+=(uint32_t)(micros()-perf_started);
#endif
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
    if(page>0)draw_page_arrow(text_left-ui_w(24),y+ui_h(7),false);
    if(has_older)draw_page_arrow(text_left+text_width+ui_w(24),y+ui_h(7),true);
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
    const uint32_t timing_history_started=micros();
#if T5_TIMING_DIAGNOSTICS
    ui_message_perf_reset();
    ui_message_perf_collect=true;
    MeshInkMessageStorePerf perf_store_before{},perf_store_after{};
    meshink_message_store_perf_snapshot(perf_store_before);
    const uint32_t perf_layout_started=micros();
#endif
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
#if T5_TIMING_DIAGNOSTICS
    const uint32_t perf_layout_us=(uint32_t)(micros()-perf_layout_started);
    const uint32_t perf_render_started=micros();
    uint16_t perf_visible=0;
#endif
    if(!count)ui_centred("NO MESSAGES YET",ui_y(300),3,0,true);
    else{
        int y=ui_y(126);
        for(size_t i=first;i<end;++i){
            const UiMessage& message=ui_data->active_message(i);
            const MessageBubbleGeometry geometry=chat_message_geometry(i);
            draw_message_bubble(message,y,geometry);
            y+=geometry.height+ui_h(8);
#if T5_TIMING_DIAGNOSTICS
            if(perf_visible<0xFFFF)++perf_visible;
#endif
        }
    }
#if T5_TIMING_DIAGNOSTICS
    const uint32_t perf_render_us=(uint32_t)(micros()-perf_render_started);
#endif
    const uint32_t timing_history_us=(uint32_t)(micros()-timing_history_started);
#if T5_TIMING_DIAGNOSTICS
    meshink_message_store_perf_snapshot(perf_store_after);
    ui_message_perf_collect=false;
    T5MessageDrawPerf perf{};
    perf.history_us=timing_history_us;
    perf.layout_us=perf_layout_us;
    perf.render_us=perf_render_us;
    perf.store_read_us=perf_store_after.read_us-perf_store_before.read_us;
    perf.store_read_worst_us=perf_store_after.read_worst_us;
    const uint32_t perf_reads=perf_store_after.reads-perf_store_before.reads;
    perf.store_reads=(uint16_t)(perf_reads>0xFFFFU?0xFFFFU:perf_reads);
    const uint32_t perf_cache_reads=perf_store_after.cache_reads-perf_store_before.cache_reads;
    perf.store_cache_reads=(uint16_t)(perf_cache_reads>0xFFFFU?0xFFFFU:perf_cache_reads);
    const uint32_t perf_writes=perf_store_after.writes-perf_store_before.writes;
    perf.store_writes=(uint16_t)(perf_writes>0xFFFFU?0xFFFFU:perf_writes);
    perf.active_messages=(uint16_t)(count>0xFFFFU?0xFFFFU:count);
    perf.visible_messages=perf_visible;
    perf.geometry_calls=ui_message_perf.geometry_calls;
    perf.fill_calls=ui_message_perf.fill_calls;
    perf.geometry_us=ui_message_perf.geometry_us;
    perf.fill_us=ui_message_perf.fill_us;
    perf.smooth_us=ui_message_perf.smooth_us;
    perf.wrap_calls=ui_message_perf.wrap_calls;
    perf.wrap_chars=ui_message_perf.wrap_chars;
    perf.advances=ui_message_perf.advances;
    perf.smooth_calls=ui_message_perf.smooth_calls;
    perf.smooth_chars=ui_message_perf.smooth_chars;
    perf.glyph_pixels=ui_message_perf.glyph_pixels;
    perf.draw_ops=ui_message_perf.draw_ops;
    perf.page=(uint8_t)(chat_page+1);
    perf.channel=channel;
    perf.keyboard=keyboard;
    t5_timing_note_message_draw(perf);
#endif
    const uint32_t timing_keyboard_started=micros();
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
    t5_timing_note_chat_draw(timing_history_us,(uint32_t)(micros()-timing_keyboard_started));
}

static void draw_message_entry_fast() {
    T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-message-entry-draw");
    // Typing does not change the chat history. Avoid rebuilding the status
    // bar, message bubbles and bottom navigation for every character.
    const uint32_t timing_draw_started=micros();
    const uint32_t timing_keyboard_started=micros();
    const auto metrics=keyboard_metrics(false);
    draw_compose_entry(metrics);
    if(message_keyboard_case_dirty){
        draw_keyboard();
        message_keyboard_case_dirty=false;
    }
    const uint32_t keyboard_us=(uint32_t)(micros()-timing_keyboard_started);
    t5_timing_note_chat_draw(0,keyboard_us);
    t5_timing_note_ui_draw((uint32_t)(micros()-timing_draw_started));
}

static void draw_contact_details() {
    draw_app_header("NODE INFO",true);UiNodeDetails node{};
    if(!ui_data||!ui_data->active_node_details(node)){ui_centred("NODE DETAILS UNAVAILABLE",ui_y(300),3,0,true);return;}
    const uint8_t pages=node_info_page_count(node.node_type);if(details_page>=pages)details_page=pages-1;
    const NodeInfoPage page=node_info_page(node.node_type,details_page);
    ui_centred_fit(node.name,ui_y(126),portrait_layout().section_width,4,0,true);
    ui_centred(node_role_label(node.node_type),ui_y(174),2,0,true);
    auto action_button=[](const char* label,const MeshInkUiRect& rect,bool selected=false) {
        ui_action_button(label,rect,selected);
    };
    auto request_label=[&](UiNodeInfoRequest request,const char* idle) {
        return node.request_active?(node.request_type==request?"REQUESTING...":"REQUEST BUSY"):idle;
    };
    const bool repeater=node.node_type==(uint8_t)UiNodeRole::Repeater;
    const bool room_server=node.node_type==(uint8_t)UiNodeRole::Room;
    const bool login_required=repeater||room_server;
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
        ui_text(room_server?"ROOM SERVER STATUS":"REPEATER STATUS",layout.section_margin,ui_y(220),3,0,true);
        if(!node.authenticated){
            ui_draw_wrapped(room_server?"Login with the room password to request status.":"Login with the repeater guest or admin password to request status.",
                            layout.section_margin,ui_y(282),layout.section_width,2,0,false,5);
            if(!strcmp(node.status,"LOGIN FAILED"))ui_text("LOGIN FAILED",layout.section_margin,ui_y(410),2,0,true);
            action_button(node.login_active?"LOGGING IN...":"ENTER PASSWORD",full_action,true);
        }else{
            char login_text[48];snprintf(login_text,sizeof(login_text),"LOGGED IN - %s",node.access_level?node.access_level:"UNKNOWN");
            ui_text(login_text,layout.section_margin,ui_y(258),2,0,true);
            const int status_scale=ui_text_max_line_width(node.status,3)<=layout.section_width?3:2;
            ui_draw_wrapped(node.status,layout.section_margin,ui_y(294),
                            layout.section_width,status_scale,0,true,14);
            action_button(request_label(UiNodeInfoRequest::Status,"REQUEST STATUS"),full_action,true);
        }
    } else if(page==NodeInfoPage::Telemetry){
        ui_text("TELEMETRY / POSITION",layout.section_margin,ui_y(220),3,0,true);
        if(login_required&&!node.authenticated){
            ui_draw_wrapped(room_server?"Room server telemetry requires login.":"Repeater telemetry requires login.",
                            layout.section_margin,ui_y(282),layout.section_width,2,0,false,3);
            action_button(node.login_active?"LOGGING IN...":"ENTER PASSWORD",full_action,true);
        }else{
            const int telemetry_scale=
                ui_wrapped_line_count(node.telemetry,layout.section_width,3)<=4?3:2;
            ui_draw_wrapped(node.telemetry,layout.section_margin,ui_y(270),
                            layout.section_width,telemetry_scale,0,true,4);
            ui_text("POSITION",layout.section_margin,ui_y(420),2,0,true);
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
        ui_text("DISCOVERED PATH",layout.section_margin,ui_y(220),3,0,true);
        const int path_text_y=ui_y(260);
        const int path_lines=min(3,ui_wrapped_line_count(node.path,layout.section_width,3));
        ui_draw_wrapped(node.path,layout.section_margin,path_text_y,
                        layout.section_width,3,0,true,3);
        const int trace_heading_y=max(
            ui_y(360),path_text_y+path_lines*ui_text_line_step(3)+ui_h(10));
        ui_text("TRACE ROUTE",layout.section_margin,trace_heading_y,3,0,true);
        const int trace_text_y=trace_heading_y+ui_h(40);
        const int trace_lines=min(9,ui_wrapped_line_count(node.trace,layout.section_width,2));
        ui_draw_wrapped(node.trace,layout.section_margin,trace_text_y,
                        layout.section_width,2,0,true,9);
        const int saved_route_y=max(
            ui_y(620),trace_text_y+trace_lines*ui_text_line_step(2)+ui_h(10));
        ui_text("SAVED ROUTE",layout.section_margin,saved_route_y,2,0,true);
        ui_draw_wrapped(node.route,layout.section_margin,saved_route_y+ui_h(34),
                        layout.section_width,3,0,true,2);
        action_button(request_label(UiNodeInfoRequest::Path,"DISCOVER PATH"),left_action,true);
        action_button(request_label(UiNodeInfoRequest::Trace,"TRACE ROUTE"),right_action,true);
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

static void draw_more() {
    draw_app_header("MORE");
    settings_row("DISCOVERED ADVERTS","Recent nodes heard",130);settings_row("ADVERTISE","Zero hop or flood",260);
    settings_row("SETTINGS","Device and radio",390);settings_row("BLUETOOTH COMPANION","Restart in companion mode",520);
    settings_row("DIAGNOSTICS","Live MeshCore radio stats",650);settings_row("HELP","Using MeshInk",780);
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
    ui_draw_wrapped(local_mesh_diagnostics_core(),layout.content_text_x,core.y+ui_h(52),
                    core.width-ui_w(32),2,0,false,5);
    ui_text("RADIO",layout.content_text_x,radio.y+ui_h(13),3,0,true);
    ui_draw_wrapped(local_mesh_diagnostics_radio(),layout.content_text_x,radio.y+ui_h(52),
                    radio.width-ui_w(32),2,0,false,5);
    ui_text("PACKETS",layout.content_text_x,packets.y+ui_h(13),3,0,true);
    ui_draw_wrapped(local_mesh_diagnostics_packets(),layout.content_text_x,packets.y+ui_h(52),
                    packets.width-ui_w(32),2,0,false,5);
    const MeshInkUiRect action=meshink_node_action_rect(layout);
    const char* label=local_mesh_diagnostics_busy()?"REFRESHING...":"REFRESH STATS";
    ui_action_button(label,action,true);
}

static void draw_advert_menu() {
    draw_app_header("ADVERTISE",true);
    settings_row("ZERO HOP ADVERT","NEARBY NODES ONLY",180);settings_row("FLOOD ADVERT","SEND ACROSS THE MESH",320);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect note=meshink_outer_row_rect(layout,490,180);
    ui_section_card(note);
    ui_draw_wrapped("Advertising shares this node identity using MeshCore radio settings.",
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

static void draw_settings() {
    draw_app_header("SETTINGS",true);
    settings_row("ID & RADIO",local_mesh_radio_summary(),118);
    if(meshink_board_has_gps()){
        settings_row("LOCATION & GPS","Position, interval, advert",238);
        settings_row("PRIVACY","Contacts and telemetry",358);
        settings_row("DISPLAY & POWER","Frontlight, refresh, standby",478);
        settings_row("ABOUT","Firmware and device info",598);
    }else{
        settings_row("PRIVACY","Contacts and telemetry",238);
        settings_row("DISPLAY & POWER","Frontlight, refresh, standby",358);
        settings_row("ABOUT","Firmware and device info",478);
    }
}

static void draw_radio_settings() {
    draw_app_header("ID & RADIO",true);settings_row("NODE NAME",node_name,120);
    settings_row("REGION PRESET",PRESETS[selected_preset].title,250);
    settings_row("ACTIVE RADIO",local_mesh_radio_summary(),380);
    if(!keyboard_visible)settings_row("PATH HASH MODE",path_hash_label(),510);
    if(keyboard_visible){draw_keyboard();}
}

static void draw_radio_name_fast() {
    T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-radio-name-draw");
    const uint32_t timing_draw_started=micros();
    settings_row("NODE NAME",node_name,120);
    t5_timing_note_ui_draw((uint32_t)(micros()-timing_draw_started));
}

static void draw_gps_settings() {
    draw_app_header("LOCATION & GPS",true);settings_row("GPS POWER",local_mesh_gps_enabled()?"ENABLED":"DISABLED",120);
    char fix[32];snprintf(fix,sizeof(fix),status_gps_fix?"FIXED  %d SATELLITES":"SEARCHING  %d SATELLITES",status_gps_satellites);settings_row("CURRENT STATUS",local_mesh_gps_enabled()?fix:"DISABLED",238);
    char position[64];if(status_gps_fix){const long alat=abs(status_gps_latitude),alon=abs(status_gps_longitude);snprintf(position,sizeof(position),"%c%ld.%06ld  %c%ld.%06ld",status_gps_latitude<0?'-':'+',alat/1000000,alat%1000000,status_gps_longitude<0?'-':'+',alon/1000000,alon%1000000);}else strcpy(position,"NO VALID POSITION");settings_row("LATITUDE / LONGITUDE",position,356);
    char interval[24];const uint32_t seconds=local_mesh_gps_interval();if(!seconds)strcpy(interval,"CONTINUOUS");else if(seconds<60)snprintf(interval,sizeof(interval),"%lu SECONDS",(unsigned long)seconds);else snprintf(interval,sizeof(interval),"%lu MINUTES",(unsigned long)(seconds/60));settings_row("GPS INTERVAL",interval,474);
    settings_row("POSITION ADVERT",local_mesh_gps_advert_location()?"SHARE GPS POSITION":"LOCATION HIDDEN",592);
    settings_row("GPS POWER SAVING","CONSTELLATIONS, TIMEZONE",710);
}

static const char* gps_constellation_label(){
    switch(local_mesh_gps_constellation_mode()){
        case MeshInkGpsConstellationMode::GpsOnly:return "GPS ONLY (TEST LOWER POWER)";
        case MeshInkGpsConstellationMode::GpsBeiDou:return "GPS + BEIDOU";
        case MeshInkGpsConstellationMode::GpsGlonass:return "GPS + GLONASS";
        case MeshInkGpsConstellationMode::GpsBeiDouGlonass:return "GPS + BEIDOU + GLONASS";
        default:return "UNCHANGED (CURRENT MODE)";
    }
}
static void draw_gps_tuning(){
    draw_app_header("GPS POWER SAVING",true);
    settings_row("CONSTELLATIONS",gps_constellation_label(),120);
    const MeshInkUiLayout& layout=portrait_layout();
    const MeshInkUiRect nmea=meshink_outer_row_rect(layout,238,112);
    ui_section_card(nmea);
    ui_text("NMEA OUTPUT",layout.content_text_x,nmea.y+ui_h(13),3,0,true);
    ui_text("RMC + GGA (AUTOMATIC)",layout.content_text_x,ui_y(292),3,0,false);
    settings_row("TIMEZONE",TIMEZONES[timezone_index].label,356);
    ui_draw_wrapped("GPS only may lower receiver load, but can take longer to fix. Choose more satellite systems if reception is poor.",
                    layout.section_margin,ui_y(515),layout.section_width,2,0,false,5);
    ui_draw_wrapped(local_mesh_gps_tuning_note(),layout.section_margin,ui_y(700),
                    layout.section_width,2,0,false,4);
}

static void draw_timezone(){
    draw_app_header("TIMEZONE",true);
    const MeshInkUiLayout& layout=portrait_layout();
    for(uint8_t i=0;i<TIMEZONE_COUNT;++i){
        const MeshInkUiRect row=meshink_outer_row_rect(layout,118+i*102,92);
        rounded_box(row,max(ui_w(12),ui_h(12)),i==timezone_index);
        const uint8_t c=i==timezone_index?0xFF:0;
        ui_text_fit(TIMEZONES[i].label,layout.content_text_x,row.y+ui_h(9),
                    row.width-ui_w(32),3,c,true);
        ui_text_fit(TIMEZONES[i].detail,layout.content_text_x,row.y+ui_h(50),
                    row.width-ui_w(32),3,c,false);
    }
}

static void draw_privacy_settings() {
    draw_app_header("PRIVACY",true);
    settings_row("AUTO ADD CONTACTS",local_mesh_privacy_value(0),130);settings_row("AUTO ADD MAX HOPS",local_mesh_privacy_value(1),248);
    settings_row("ADVERTISE LOCATION",local_mesh_privacy_value(2),366);settings_row("BASE TELEMETRY",local_mesh_privacy_value(3),484);
    settings_row("LOCATION TELEMETRY",local_mesh_privacy_value(4),602);settings_row("PACKET REPEATING",local_mesh_privacy_value(5),720);
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
    settings_row("STANDBY TIMEOUT",standby_timeout_name(),538);
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

    ui_text("BLUETOOTH COMPANION",layout.content_text_x,companion.y+ui_h(12),3,0,true);
    ui_draw_wrapped("Reboots into companion mode for MeshCore apps. Reboot again to return to the local UI.",
                    layout.content_text_x,companion.y+ui_h(48),companion.width-ui_w(32),2,0,false,5);
}

static void draw_meshink_logo(int top,bool compact=false);

static void draw_standby(){
    meshink_display_set_all_white(&display);
    draw_status_bar();

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

    // STANDBY is part of the at-rest identity rather than small helper copy.
    // Move it beneath unread cards when present; otherwise use the open centre.
    ui_centred("STANDBY",any_unread?ui_y(775):ui_y(620),5,0,true);

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
    ui_text_fit("MESHCORE " MESHCORE_RELEASE " (" MESHCORE_REVISION ")",ui_x(170),
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
    T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-quick-panel-draw");
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

static void quick_set_brightness(int value) {
    frontlight_brightness=(uint8_t)max(0,min(100,value));
    frontlight_mode=frontlight_brightness?FrontlightMode::On:FrontlightMode::Off;
    save_frontlight_settings();
    if(frontlight_brightness) frontlight_event();
    else { frontlight_drive(false);frontlight_deadline=0; }
    draw_quick_panel();
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
        show_toast(local_mesh_send_advert(true)?"SENDING FLOOD ADVERT":"ADVERT BUSY");
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
    T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-draw");
    const uint32_t timing_draw_started=micros();
#if T5_TIMING_DIAGNOSTICS
    ui_render_perf_begin();
#endif
    t5_timing_set_ui_context(timing_screen_name(),keyboard_visible,keyboard_landscape,standby_active);
    // Standby must take precedence over every transient/landscape UI layer.
    if(standby_active){
        draw_standby();
#if T5_TIMING_DIAGNOSTICS
        ui_render_perf_end(timing_draw_started);
#else
        t5_timing_note_ui_draw((uint32_t)(micros()-timing_draw_started));
#endif
        return;
    }
    if(quick_panel_active){
        draw_quick_panel();
#if T5_TIMING_DIAGNOSTICS
        ui_render_perf_end(timing_draw_started);
#else
        t5_timing_note_ui_draw((uint32_t)(micros()-timing_draw_started));
#endif
        return;
    }
    if(keyboard_landscape){
        draw_landscape_keyboard();
#if T5_TIMING_DIAGNOSTICS
        ui_render_perf_end(timing_draw_started);
#else
        t5_timing_note_ui_draw((uint32_t)(micros()-timing_draw_started));
#endif
        return;
    }
    switch(screen){
        case Screen::Welcome:draw_welcome();break;case Screen::Presets:draw_presets();break;case Screen::CompanionConfirm:draw_companion_confirm();break;case Screen::ShutdownConfirm:draw_shutdown_confirm();break;
        case Screen::Contacts:draw_contacts();break;case Screen::ContactChat:draw_chat(false);break;case Screen::ContactDetails:draw_contact_details();break;
        case Screen::Channels:draw_channels();break;case Screen::ChannelChat:draw_chat(true);break;case Screen::Maps:draw_maps();break;case Screen::Discovery:draw_discovery();break;case Screen::More:draw_more();break;case Screen::AdvertMenu:draw_advert_menu();break;case Screen::Diagnostics:draw_diagnostics();break;
        case Screen::Settings:draw_settings();break;case Screen::RadioSettings:draw_radio_settings();break;case Screen::GpsSettings:draw_gps_settings();break;case Screen::GpsTuning:draw_gps_tuning();break;case Screen::Timezone:draw_timezone();break;
        case Screen::PrivacySettings:draw_privacy_settings();break;case Screen::DisplaySettings:draw_display_settings();break;case Screen::NightSchedule:draw_night_schedule();break;case Screen::Help:draw_help();break;case Screen::About:draw_about();break;
    }
    const bool settings_page=screen==Screen::Settings||screen==Screen::RadioSettings||screen==Screen::GpsSettings||screen==Screen::GpsTuning||screen==Screen::Timezone||screen==Screen::PrivacySettings||screen==Screen::DisplaySettings||screen==Screen::NightSchedule||screen==Screen::Help||screen==Screen::About;
    if(screen==Screen::ContactDetails&&!(keyboard_visible&&keyboard_password_mode))draw_bottom_nav(details_from_discovery?3:0);
    else if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&
            !keyboard_visible&&chat_page==0)
        draw_bottom_nav(screen==Screen::ContactChat?0:1);
    else if(screen==Screen::Discovery||screen==Screen::AdvertMenu||screen==Screen::Diagnostics||
            (settings_page&&!(screen==Screen::RadioSettings&&keyboard_visible)))
        draw_bottom_nav(3);
    draw_toast();
#if T5_TIMING_DIAGNOSTICS
    ui_render_perf_end(timing_draw_started);
#else
    t5_timing_note_ui_draw((uint32_t)(micros()-timing_draw_started));
#endif
}

static void refresh(MeshInkRefreshMode mode,bool wake_light=true) {
    const uint32_t timing_display_started=t5_timing_display_begin();
    if(wake_light&&!standby_active)frontlight_event();
    // Maps contains only black and white pixels. Use the direct DU waveform
    // for normal updates; the 1.3.24 device test confirmed it prevents the
    // terrain fading seen after GC16. BOOT on Maps uses DU as well.
    const MeshInkRefreshMode requested_mode=mode;
    const bool active_map=screen==Screen::Maps&&!standby_active&&!keyboard_landscape;
    if(active_map&&mode==MeshInkRefreshMode::FastGray16)mode=MeshInkRefreshMode::Direct;
    t5_timing_note_refresh((uint8_t)requested_mode,(uint8_t)mode);
    set_cpu_target(UI_RENDER_CPU_MHZ,"display-refresh");
    meshink_display_poweron();
    const MeshInkDisplayResult err = meshink_display_update_screen(&display,mode,(int)meshink_display_ambient_temperature());
    // The map stays clear when the panel is powered down as soon as EPDiy's
    // synchronous DU waveform completes. Do not reintroduce a powered hold.
    meshink_display_poweroff();
    set_cpu_target(ui_post_render_cpu_target(),"display-complete");
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] refresh=%d waveform=%d requested=%d screen=%d name='%s' preset=%s cpu=%luMHz\n",
        err,(int)mode,(int)requested_mode,(int)screen,node_name,PRESETS[selected_preset].title,(unsigned long)getCpuFrequencyMhz());
    t5_timing_display_end(timing_display_started);
}

static void refresh_area(MeshInkRefreshMode mode,MeshInkRect area,bool wake_light=true) {
    const uint32_t timing_display_started=t5_timing_display_begin();
    const uint32_t started=millis();
    if(wake_light&&!standby_active)frontlight_event();
    const MeshInkRefreshMode requested_mode=mode;
    const bool active_map=screen==Screen::Maps&&!standby_active&&!keyboard_landscape;
    if(active_map&&mode==MeshInkRefreshMode::FastGray16)mode=MeshInkRefreshMode::Direct;
    t5_timing_note_refresh((uint8_t)requested_mode,(uint8_t)mode);
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
    t5_timing_display_end(timing_display_started);
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
static void load_map_with_feedback(bool already_on_map) {
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

static void full_display_clean(const char* reason) {
    T5_DEBUGF(T5_LOG_UI,"[T5-EPD] full GC16 redraw requested by %s standby=%d\n",reason,standby_active);
    draw_screen();
    force_redraw(MeshInkRefreshMode::Gray16,reason,false);
    T5_DEBUGLN(T5_LOG_UI,"[T5-EPD] full GC16 UI redraw complete; buffers synchronized");
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
    local_mesh_prepare_shutdown();
    SPIFFS.end();
    T5_DEBUGLN(T5_LOG_UI,"[T5-SHUTDOWN] message store closed; radio, GPS, touch and frontlight stopped");
    meshink_power_enter_ship_mode(MeshInkPowerOffReason::User);
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
        local_mesh_prepare_shutdown();
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
    if(mesh_is_ready&&local_mesh_time_valid()){time_t now=(time_t)local_mesh_current_time();struct tm local{};localtime_r(&now,&local);if(local.tm_hour>=0&&local.tm_hour<24){status_hour=local.tm_hour;status_minute=local.tm_min;}}
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
            keyboard_delete_hold=false;keyboard_delete_repeated=false;
            meshink_touch_reset_tracking();
            t5_timing_touch_reset();
            T5_DEBUGLN(T5_LOG_TOUCH,"[T5-POWER] touch sampler suspended");
            ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
            t5_timing_touch_reset();
            T5_DEBUGLN(T5_LOG_TOUCH,"[T5-POWER] touch sampler resumed");
            continue;
        }
        const uint32_t timing_touch_started=t5_timing_touch_begin();
        // Maps owns the gesture-oriented two-point parser only while the
        // map itself is interactive. Quick Settings must fall back to the
        // ordinary single-touch parser so slider movement can preview PWM
        // continuously instead of being delivered only on finger release.
        const bool on_map=screen==Screen::Maps&&!standby_active&&
            !keyboard_landscape&&!quick_panel_active;
        if(on_map!=map_previous) {
            held=false;home_held=false;map_multi=false;
            keyboard_delete_hold=false;keyboard_delete_repeated=false;
            meshink_touch_reset_tracking();
            map_previous=on_map;
        }
        if(on_map) {
            MeshInkTouchContacts contacts{};
            if(!meshink_touch_read_contacts(contacts)) {
                t5_timing_touch_end(timing_touch_started);
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
                if(xQueueSend(touch_queue,&tap,0)!=pdTRUE)
                    t5_timing_note_touch_queue_drop();
            } else if(held) {
                held=false;
                QueuedTap tap{last_x,last_y,
                    (int16_t)(last_x-start_x),(int16_t)(last_y-start_y),false};
                tap.hold_ms=(uint16_t)min((uint32_t)65535,(uint32_t)(millis()-pressed_at));
                tap.map_sampled=1;
                if(xQueueSend(touch_queue,&tap,0)!=pdTRUE)
                    t5_timing_note_touch_queue_drop();
            }
        } else {
            // Non-Maps keeps the legacy single-contact semantics supplied by the touch
            // backend. Keyboard releases get a small thumb-roll stabilization below;
            // other UI releases remain unchanged.
            const MeshInkTouchPrimarySample sample=meshink_touch_read_primary();
            const int16_t x=sample.x,y=sample.y;
            const bool home=sample.home,pressed=sample.pressed;
            if(home){
                if(!home_held){QueuedTap tap{0,0,0,0,true};xQueueSend(touch_queue,&tap,0);}
                home_held=true;
                held=false;keyboard_delete_hold=false;keyboard_delete_repeated=false;
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
                    const MeshInkUiRect slider_touch=
                        meshink_quick_slider_touch_rect(portrait_layout());
                    quick_slider_dragging=quick_panel_active&&
                        x>=slider_touch.x&&x<slider_touch.x+slider_touch.width&&
                        y>=slider_touch.y&&y<slider_touch.y+slider_touch.height;
                }
                if(quick_slider_dragging) {
                    const MeshInkUiRect slider=
                        meshink_quick_slider_track_rect(portrait_layout());
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
                    else
                        t5_timing_note_touch_queue_drop();
                    keyboard_delete_repeat_at=millis()+45;
                }
            }else if(held){
                held=false;
                quick_slider_dragging=false;
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
                    if(xQueueSend(touch_queue,&tap,0)!=pdTRUE)
                        t5_timing_note_touch_queue_drop();
                }
            }
        }
        t5_timing_touch_end(timing_touch_started);
        // Repeated letters can be typed faster than the ordinary 8 ms polling
        // cadence observes the brief release between two taps on the same key.
        // Poll more aggressively only while a keyboard is active; every other
        // screen keeps the lower-overhead 8 ms cadence.
        const uint32_t sample_ms=(keyboard_visible||keyboard_landscape)?2:8;
        vTaskDelay(pdMS_TO_TICKS(sample_ms));
    }
}

static bool legal_name_character(char c) { return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
static void cycle_keyboard_mode(){
    if(keyboard_symbols){keyboard_symbols=false;keyboard_upper=true;}
    else if(keyboard_upper)keyboard_upper=false;
    else keyboard_symbols=true;
}
static void append(char c) {
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
    if(!legal_name_character(c)){T5_DEBUGF(T5_LOG_UI,"[T5-UI] discarded illegal name character 0x%02X\n",(unsigned char)c);return;}
    if (replace_name_on_type) { node_name[0]=0; replace_name_on_type=false; }
    size_t n=strlen(node_name); if (n<20) { node_name[n]=c; node_name[n+1]=0; saved=false; }
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
static bool hit_section_row(int16_t x,int16_t y,int reference_top,int reference_height) {
    return hit(x,y,meshink_section_row_rect(portrait_layout(),reference_top,reference_height));
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
    keyboard_visible=false;keyboard_message_mode=false;keyboard_password_mode=false;save_remote_password=false;remote_password[0]=0;screen=next;
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

static void persist_unread(){Preferences state;if(state.begin("t5-ui",false)){state.putUShort("unread_dm",status_unread);state.putUShort("unread_ch",status_channel_unread);state.end();}}
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
    prefs.putBool("complete",true);prefs.putBool("name_migrated",true);prefs.end();
    if(mesh_is_ready)local_mesh_apply_name(node_name);
    saved=true;setup_complete=true;
}

// A full redraw of the first Contacts screen removes the initial setup
// frame cleanly; do not briefly show a saved toast on the old keyboard.
static void show_contacts_after_setup(){
    if(keyboard_landscape){
        keyboard_landscape=false;
        set_ui_orientation(MeshInkOrientation::Portrait);
    }
    keyboard_visible=false;keyboard_message_mode=false;
    replace_name_on_type=false;
    text_refresh_pending=false;toast_visible=false;toast_opens_main=false;
    status_dirty=false;status_bar_dirty=false;status_wake_light=false;
    if(touch_queue)xQueueReset(touch_queue);
    meshink_touch_clear();
    screen=Screen::Contacts;
    draw_screen();
    fast_full_redraw("FIRST_CONTACTS_AFTER_SETUP",true);
}
static bool handle_landscape_keyboard(int16_t x,int16_t y){
    if(!keyboard_landscape)return false;
    const auto metrics=keyboard_metrics(true);
    T5_DEBUGF(T5_LOG_TOUCH,"[T5-UI] landscape tap logical=%d,%d\n",x,y);
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
        if(x<meshink_keyboard::action_split(metrics)){append(' ');queue_text_refresh();return true;}
        if(keyboard_password_mode){
            const bool ok=ui_data&&ui_data->login_active_node(remote_password,save_remote_password);
            memset(remote_password,0,sizeof(remote_password));keyboard_password_mode=false;keyboard_visible=false;save_remote_password=false;
            keyboard_landscape=false;set_ui_orientation(MeshInkOrientation::Portrait);
            show_toast(ok?"LOGIN REQUESTED":"LOGIN FAILED");draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;
        }
        if(keyboard_message_mode){
            if(!compose_text[0])return true;
            if(!local_mesh_send_active(compose_text))return true;
            compose_text[0]=0;text_refresh_pending=false;
            keyboard_symbols=false;keyboard_upper=true;message_keyboard_case_dirty=false;
            keyboard_visible=true;set_keyboard_orientation(false);
            return true;
        }
        // DONE in landscape is only an orientation switch for name entry.
        // Return to portrait setup with radio preset still available; only
        // the portrait SAVE button can persist the name and complete setup.
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
            const bool ok=local_mesh_send_active(compose_text);
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
    // In Radio Settings, tapping above the keyboard dismisses name editing.
    // First-time setup keeps its explicit setup controls and save flow.
    if(screen==Screen::RadioSettings&&y<metrics.dismiss_above){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
    if(meshink_keyboard::in_row(y,metrics.mode_key.y,metrics)){
        if(x<meshink_keyboard::mode_split(metrics)){cycle_keyboard_mode();draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
        if(x>=meshink_keyboard::delete_split(metrics)){
            const size_t n=strlen(node_name);
            if(n)node_name[n-1]=0;
            saved=false;queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,false,character)){
        append(character);queue_text_refresh();return true;
    }
    if(meshink_keyboard::in_row(y,metrics.bottom_top,metrics)){
        if(x<meshink_keyboard::orientation_split(metrics)){set_keyboard_orientation(true);return true;}
        const bool was_setup=screen==Screen::Welcome;
        save_node_name();
        if(was_setup){
            show_contacts_after_setup();
        }else{
            keyboard_visible=false;
            show_toast("IDENTITY SAVED");
            draw_screen();refresh(MeshInkRefreshMode::Direct);
        }
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
    if(screen==Screen::RadioSettings&&keyboard_visible&&handle_name_keyboard(x,y))return true;
    const bool chat_main_page=(screen==Screen::ContactChat||screen==Screen::ChannelChat)&&
                              !keyboard_visible&&chat_page==0;
    if((screen!=Screen::ContactChat&&screen!=Screen::ChannelChat||chat_main_page)&&
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
                        portrait_layout().list_row_height)){selected_contact=index;if(ui_data->open_contact(index)){status_unread=local_mesh_direct_unread_total();persist_unread();reset_chat_paging();open_screen(Screen::ContactChat);}return true;}
                }
            }
            break;
        case Screen::Channels:
            if(ui_data){
                const size_t count=ui_data->channel_count();
                clamp_list_page(channels_page,count);
                const size_t first=channels_page*LIST_ITEMS_PER_PAGE;
                for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                    const size_t index=first+row;
                    if(hit_outer_row(x,y,portrait_layout().list_top+
                        row*portrait_layout().list_row_stride,
                        portrait_layout().list_row_height)){selected_channel=index;if(ui_data->open_channel(index)){status_channel_unread=local_mesh_channel_unread_total();persist_unread();reset_chat_paging();open_screen(Screen::ChannelChat);}return true;}
                }
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
                const NodeInfoPage page=node_info_page(node.node_type,details_page);
                const bool repeater=node.node_type==(uint8_t)UiNodeRole::Repeater;
                const bool room_server=node.node_type==(uint8_t)UiNodeRole::Room;
                const bool login_required=repeater||room_server;
                if(node.saved_contact&&page==NodeInfoPage::Status&&hit(x,y,meshink_node_action_rect(portrait_layout()))){
                    if(!node.authenticated){if(!node.login_active){remote_password[0]=0;save_remote_password=ui_data->active_node_saved_password(remote_password,sizeof(remote_password));keyboard_password_mode=true;keyboard_message_mode=false;keyboard_visible=true;draw_screen();refresh(MeshInkRefreshMode::FastGray16);}return true;}
                    show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Status)?"REQUESTING STATUS":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(node.saved_contact&&page==NodeInfoPage::Telemetry&&hit(x,y,meshink_node_action_rect(portrait_layout()))){
                    if(login_required&&!node.authenticated){if(!node.login_active){remote_password[0]=0;save_remote_password=ui_data->active_node_saved_password(remote_password,sizeof(remote_password));keyboard_password_mode=true;keyboard_message_mode=false;keyboard_visible=true;draw_screen();refresh(MeshInkRefreshMode::FastGray16);}return true;}
                    show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Telemetry)?"REQUESTING TELEMETRY":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
                }
                if(node.saved_contact&&page==NodeInfoPage::Path&&hit(x,y,meshink_node_left_action_rect(portrait_layout()))){show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Path)?"DISCOVERING PATH":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
                if(node.saved_contact&&page==NodeInfoPage::Path&&hit(x,y,meshink_node_right_action_rect(portrait_layout()))){show_toast(ui_data->request_active_node_info(UiNodeInfoRequest::Trace)?"TRACE REQUESTED":"REQUEST BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
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
                if(abs(x-marker.x)<=10&&abs(y-marker.y)<=10&&ui_data&&ui_data->open_map_node(marker.index)) {
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
        case Screen::More:
            if(hit_outer_row(x,y,130)){open_screen(Screen::Discovery);return true;}
            if(hit_outer_row(x,y,260)){open_screen(Screen::AdvertMenu);return true;}
            if(hit_outer_row(x,y,390)){open_screen(Screen::Settings);return true;}
            if(hit_outer_row(x,y,520)){open_screen(Screen::CompanionConfirm);return true;}
            if(hit_outer_row(x,y,650)){local_mesh_request_diagnostics();open_screen(Screen::Diagnostics);return true;}
            if(hit_outer_row(x,y,780)){open_screen(Screen::Help);return true;}break;
        case Screen::AdvertMenu:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            if(hit_outer_row(x,y,180)){show_toast(local_mesh_send_advert(false)?"SENDING ZERO HOP ADVERT":"ADVERT BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,320)){show_toast(local_mesh_send_advert(true)?"SENDING FLOOD ADVERT":"ADVERT BUSY");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}break;
        case Screen::Diagnostics:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            if(hit(x,y,meshink_node_action_rect(portrait_layout()))){
                show_toast(local_mesh_request_diagnostics()?"REFRESHING STATS":"DIAGNOSTICS BUSY");
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }break;
        case Screen::Settings:
            if(hit_header_back(x,y)){open_screen(Screen::More);return true;}
            if(hit_outer_row(x,y,118)){open_screen(Screen::RadioSettings);return true;}
            if(meshink_board_has_gps()){
                if(hit_outer_row(x,y,238)){open_screen(Screen::GpsSettings);return true;}
                if(hit_outer_row(x,y,358)){open_screen(Screen::PrivacySettings);return true;}
                if(hit_outer_row(x,y,478)){open_screen(Screen::DisplaySettings);return true;}
                if(hit_outer_row(x,y,598)){open_screen(Screen::About);return true;}
            }else{
                if(hit_outer_row(x,y,238)){open_screen(Screen::PrivacySettings);return true;}
                if(hit_outer_row(x,y,358)){open_screen(Screen::DisplaySettings);return true;}
                if(hit_outer_row(x,y,478)){open_screen(Screen::About);return true;}
            }
            break;
        case Screen::RadioSettings:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            if(hit_outer_row(x,y,120)){replace_name_on_type=false;keyboard_message_mode=false;keyboard_visible=true;text_refresh_pending=false;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,250)){preset_return_screen=Screen::RadioSettings;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;}
            if(hit_outer_row(x,y,510)){local_mesh_cycle_path_hash();show_toast("PATH MODE SAVED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            return true;
        case Screen::GpsSettings:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            if(hit_outer_row(x,y,120)){local_mesh_apply_gps(!local_mesh_gps_enabled());show_toast(local_mesh_gps_enabled()?"GPS ENABLED":"GPS DISABLED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,356)){
                if(centre_map_on_device())open_screen(Screen::Maps,true);
                else{show_toast("NO KNOWN GPS LOCATION");draw_screen();refresh(MeshInkRefreshMode::Direct);}
                return true;
            }
            if(hit_outer_row(x,y,474)){local_mesh_cycle_gps_interval();show_toast("GPS INTERVAL SAVED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,592)){local_mesh_toggle_gps_advert_location();show_toast(local_mesh_gps_advert_location()?"POSITION SHARED":"POSITION HIDDEN");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,710)){open_screen(Screen::GpsTuning);return true;}break;
        case Screen::GpsTuning:
            if(hit_header_back(x,y)){open_screen(Screen::GpsSettings);return true;}
            if(hit_outer_row(x,y,120)){
                const auto mode=local_mesh_gps_constellation_mode();
                const auto next=meshink_gps_next_constellation_mode(mode);
                show_toast(local_mesh_gps_set_constellation_mode(next)?"MODE SAVED":"SAVE FAILED");
                draw_screen();refresh(MeshInkRefreshMode::Direct);return true;
            }
            // NMEA output is automatic; this informational row has no action.
            if(hit_outer_row(x,y,356)){open_screen(Screen::Timezone);return true;}break;
        case Screen::Timezone:
            if(hit_header_back(x,y)){open_screen(Screen::GpsTuning);return true;}
            for(uint8_t i=0;i<TIMEZONE_COUNT;++i)if(hit_outer_row(x,y,118+i*102,92)){timezone_index=i;apply_timezone();prefs.begin("t5-ui",false);prefs.putUChar("timezone",timezone_index);prefs.end();show_toast("TIMEZONE SAVED");draw_screen();refresh(MeshInkRefreshMode::FastGray16);return true;}break;
        case Screen::PrivacySettings:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            for(uint8_t i=0;i<6;++i)if(hit_outer_row(x,y,130+i*118)){local_mesh_toggle_privacy(i);show_toast("SETTING SAVED");draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}return true;
        case Screen::DisplaySettings:
            if(hit_header_back(x,y)){open_screen(Screen::Settings);return true;}
            if(frontlight_mode==FrontlightMode::NightTimer&&
               hit(x,y,meshink_settings_inline_action_rect(portrait_layout(),118))){
                open_screen(Screen::NightSchedule);return true;
            }
            if(hit_outer_row(x,y,118)){frontlight_mode=(FrontlightMode)(((uint8_t)frontlight_mode+1)%3);save_frontlight_settings();if(frontlight_mode==FrontlightMode::Off)frontlight_drive(false);else frontlight_event();show_toast(frontlight_mode_name());draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit_outer_row(x,y,238)){frontlight_timeout_index=(frontlight_timeout_index+1)%5;save_frontlight_settings();frontlight_event();show_toast(frontlight_timeout_name());draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
            if(hit(x,y,meshink_display_slider_touch_rect(portrait_layout()))){
                const MeshInkUiRect slider=meshink_display_slider_track_rect(portrait_layout());
                int value=((int)x-slider.x)*100/slider.width;
                frontlight_brightness=(uint8_t)min(100,max(1,value));save_frontlight_settings();frontlight_event();T5_DEBUGF(T5_LOG_UI,"[T5-LIGHT] brightness=%u%%\n",frontlight_brightness);draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}
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
#if MESHINK_GEOMETRY_DIAGNOSTICS
    // test.8 changed non-keyboard touch geometry. Log those consumer-side
    // releases without slowing the 4 ms keyboard sampling/typing path.
    if(!keyboard_visible&&!keyboard_landscape)
        Serial.printf("[T5-TOUCH] tap screen=%s x=%d y=%d quick=%u\n",
                      timing_screen_name(),x,y,quick_panel_active?1U:0U);
#endif
    T5_DEBUGF(T5_LOG_TOUCH,
        "[T5-TOUCH] tap screen=%s x=%d y=%d quick=%u keyboard=%u landscape=%u\n",
        timing_screen_name(),x,y,quick_panel_active?1U:0U,
        keyboard_visible?1U:0U,keyboard_landscape?1U:0U);
    if(handle_quick_panel_tap(x,y))return;
    if(handle_landscape_keyboard(x,y))return;
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
        if(hit(x,y,meshink_confirm_right_rect(layout,500))){T5_DEBUGLN(T5_LOG_UI,"[T5-UI] companion mode confirmed");request_companion_mode();return;}
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
    if(standby_active)return;
    // Standby owns the whole display. Dismiss transient quick settings first
    // so it cannot remain layered over, or reappear immediately after, standby.
    // Preserve the view underneath Quick Settings before dismissing it.
    // A panel opened over the landscape keyboard has already switched the
    // physical display to portrait, so keyboard_landscape alone is not enough.
    const bool restore_landscape=keyboard_landscape||(quick_panel_active&&quick_panel_restore_landscape);
    quick_panel_active=false;quick_panel_restore_landscape=false;quick_slider_dragging=false;
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
}

static void leave_standby(){
    if(!standby_active)return;set_touch_power(true);standby_active=false;last_user_activity=millis();message_alert_active=false;meshink_power_frontlight_set(0);frontlight_lit=false;
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
            draw_screen();force_redraw(MeshInkRefreshMode::Gray16,"MESSAGE_ALERT_RESTORE",false);
            status_dirty=false;status_bar_dirty=false;message_alert_active=false;message_alert_cooldown_until=millis()+3000;
            break;
    }
}

static void service_primary_button(){
    static uint32_t pressed_at=0;static bool handled=false;
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
                local_mesh_refresh_ui_data();
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

void ui_setup() {
    ui_boot_cpu_active=true;
    set_cpu_target(UI_RENDER_CPU_MHZ,"boot-ui-start");
    const uint32_t bootperf_total_started=millis();
    uint32_t bootperf_stage_started=millis();
    // unified_main has already started USB CDC before entering the local UI
    // path. Keep begin() here for the standalone UI target, but do not burn a
    // fixed 200 ms delay before useful boot work.
    Serial.begin(115200);
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] onboarding %s boot heap=%u psram=%u; Bluetooth disabled\n",UI_VERSION,ESP.getFreeHeap(),ESP.getFreePsram());
    meshink_buttons_begin();
    // Stay off until preferences have been loaded. The splash refresh then
    // uses the saved brightness or the new 30% first-install default.
    meshink_power_frontlight_begin();
    meshink_touch_prepare_boot();
    Serial.printf("[T5-BOOTPERF] ui-pre-display=%lums\n",
                  (unsigned long)(millis()-bootperf_stage_started));
    bootperf_stage_started=millis();
    meshink_display_init();
    Serial.printf("[T5-BOOTPERF] display-init=%lums\n",
                  (unsigned long)(millis()-bootperf_stage_started));
    // EPDiy has now established the shared board/I2C environment. Start the
    // LoRa/GPS rail before framebuffer, preferences and splash rendering so
    // those operations overlap its required settling time.
    meshink_board_start_local_radio_settle();
    set_ui_orientation(MeshInkOrientation::Portrait);
    Serial.println("[T5-INIT] display=initialized");
    bootperf_stage_started=millis();
    meshink_power_recover_boot_path();
    meshink_touch_finish_boot();
    Serial.println("[T5-INIT] touch=initialized");
    display=meshink_display_state_init();fb=meshink_display_framebuffer(&display);
    Serial.printf("[T5-BOOTPERF] touch-display-state=%lums\n",
                  (unsigned long)(millis()-bootperf_stage_started));
#if MESHINK_GEOMETRY_DIAGNOSTICS
    audit_ui_geometry();
#endif
    bootperf_stage_started=millis();
    prefs.begin("t5-ui",true);String saved_name=prefs.getString("name","");selected_preset=prefs.getUChar("preset_v2",17);setup_complete=prefs.getBool("complete",false);timezone_index=prefs.getUChar("timezone",0);status_unread=prefs.getUShort("unread_dm",0);status_channel_unread=prefs.getUShort("unread_ch",0);
    map_has_last_gps_position=prefs.getBool("map_fix_saved",false);
    map_last_gps_latitude=prefs.getLong("map_fix_lat",0);
    map_last_gps_longitude=prefs.getLong("map_fix_lon",0);
    map_has_last_gps_position=map_has_last_gps_position &&
        map_last_gps_latitude>=-85051100L&&map_last_gps_latitude<=85051100L &&
        map_last_gps_longitude>=-180000000L&&map_last_gps_longitude<=180000000L;
    map_last_gps_saved=map_has_last_gps_position;
    map_saved_gps_latitude=map_last_gps_latitude;
    map_saved_gps_longitude=map_last_gps_longitude;
    frontlight_mode=(FrontlightMode)prefs.getUChar("light_mode",(uint8_t)FrontlightMode::On);frontlight_timeout_index=prefs.getUChar("light_timeout",2);frontlight_brightness=prefs.getUChar("light_level",30);standby_timeout_index=prefs.getUChar("standby_timeout",1);night_start_minutes=prefs.getUShort("night_start",20*60);night_end_minutes=prefs.getUShort("night_end",7*60);map_imperial=prefs.getBool("map_imperial",false);prefs.end();
    if((uint8_t)frontlight_mode>(uint8_t)FrontlightMode::Off)frontlight_mode=FrontlightMode::On;
    if(frontlight_timeout_index>4)frontlight_timeout_index=2;if(frontlight_brightness>100)frontlight_brightness=30;
    if(standby_timeout_index>3)standby_timeout_index=1;
    if(night_start_minutes>=1440)night_start_minutes=20*60;if(night_end_minutes>=1440)night_end_minutes=7*60;
    if(selected_preset>=PRESET_COUNT)selected_preset=17;
    if(timezone_index>=TIMEZONE_COUNT)timezone_index=0;apply_timezone();
    if(saved_name.length()){
        size_t out=0;
        for(size_t i=0;i<saved_name.length()&&out<20;++i){const char c=saved_name[i];if(legal_name_character(c))node_name[out++]=c;else T5_DEBUGF(T5_LOG_UI,"[T5-UI] discarded stored illegal name character 0x%02X\n",(unsigned char)c);}
        node_name[out]=0;
    }else if(!setup_complete){
        // Generate once per new device and persist immediately: rebooting
        // before pressing SAVE must not change the displayed MeshInk ID.
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
    if(setup_complete){screen=Screen::Contacts;keyboard_visible=false;}
    update_status_hardware();
    MeshInkPowerCriticalState boot_power{};
    if(meshink_power_boot_critical(boot_power))
        critical_battery_shutdown(boot_power,"boot");
    Serial.printf("[T5-BOOTPERF] ui-prefs-status=%lums\n",
                  (unsigned long)(millis()-bootperf_stage_started));
    bootperf_stage_started=millis();
    meshink_display_set_all_white(&display);
    draw_meshink_logo(ui_y(160),false);
    // Keep the original logo visible throughout MeshCore startup. Storage
    // is normally already mounted, so use the generic boot status by default.
    // local_mesh_setup() changes it only if SPIFFS fails to mount and must
    // attempt first-time initialization/recovery.
    ui_centred("STARTING UP...",ui_y(716),3,0,true);
    if(node_name[0])ui_centred_fit(node_name,ui_y(830),portrait_layout().width-ui_w(32),3,0,true);
    ui_centred(UI_VERSION,ui_y(885),2,0,true);
    Serial.printf("[T5-BOOTPERF] splash-compose=%lums\n",
                  (unsigned long)(millis()-bootperf_stage_started));
    bootperf_stage_started=millis();
    meshink_display_poweron();meshink_display_clear();meshink_display_poweroff();refresh(MeshInkRefreshMode::FastGray16);
    Serial.printf("[T5-BOOTPERF] splash-refresh=%lums\n",
                  (unsigned long)(millis()-bootperf_stage_started));
    Serial.printf("[T5-BOOTPERF] ui-setup-total=%lums\n",
                  (unsigned long)(millis()-bootperf_total_started));
    T5_DEBUGLN(T5_LOG_UI,"[T5-BOOT] splash visible; starting storage and mesh initialization");
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
    const uint32_t bootperf_started=millis();
    // Drop any touch points that accumulated during the non-interactive
    // splash, then show the correct initial setup or existing-user screen.
    meshink_touch_clear();
    draw_screen();
    if(screen==Screen::Welcome)
        fast_full_redraw("FIRST_SETUP_SCREEN",false);
    else if(screen==Screen::Contacts) {
        // Existing-user boot transitions directly from the dark startup logo
        // to Contacts. Force the same complete refresh as a short BOOT press
        // so the splash cannot remain faintly visible in the panel history.
        fast_full_redraw("CONTACTS_AFTER_BOOT",false);
        // Startup can take longer than the saved light timeout. Start a fresh
        // timeout only after Contacts is actually visible.
        frontlight_event();
    } else
        refresh(MeshInkRefreshMode::FastGray16);
    // The first interactive frame already includes the MeshCore status
    // populated during startup; don't immediately refresh it a second time.
    status_dirty=false;status_bar_dirty=false;
    t5_timing_begin();
    touch_queue=xQueueCreate(32,sizeof(QueuedTap));
    if(touch_queue&&xTaskCreatePinnedToCore(touch_sampler_task,"t5-touch",4096,nullptr,1,&touch_task_handle,0)==pdPASS)T5_DEBUGLN(T5_LOG_TOUCH,"[T5-TOUCH] sampler running; interval=8ms queue depth=32");
    else Serial.println("[T5-TOUCH] ERROR: sampler could not start");
    T5_DEBUGF(T5_LOG_UI,"[T5-LIGHT] mode=%s timeout=%s brightness=%u%% night=%02u:%02u-%02u:%02u\n",frontlight_mode_name(),frontlight_timeout_name(),frontlight_brightness,night_start_minutes/60,night_start_minutes%60,night_end_minutes/60,night_end_minutes%60);
    ui_boot_cpu_active=false;
    set_cpu_target(UI_IDLE_CPU_MHZ,"ui-ready");last_user_activity=millis();T5_DEBUGLN(T5_LOG_UI,"[T5-UI] touch ready; waiting for input");
    Serial.printf("[T5-BOOTPERF] ui-finish=%lums\n",
                  (unsigned long)(millis()-bootperf_started));
}

void ui_loop() {
    t5_timing_set_ui_context(timing_screen_name(),keyboard_visible,keyboard_landscape,standby_active);
    if(hardware_failure){

        delay(100);return;
    }
    service_critical_battery();
    service_primary_button();
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
    if(!standby_active&&standby_timeout&&millis()-last_user_activity>=standby_timeout)enter_standby("TIMEOUT");
    QueuedTap tap{};
    while(!standby_active&&touch_queue&&xQueueReceive(touch_queue,&tap,0)==pdTRUE){
        T5InputTimingScope timing_input(tap.queued_at_ms,(uint32_t)uxQueueMessagesWaiting(touch_queue));
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
            open_screen(setup_complete?Screen::Contacts:Screen::Welcome);
            continue;
        }
        // A deliberate downward pull beginning at the top edge opens the
        // CrossPoint-style quick panel before page-specific swipe handling.
        const int first_y=tap.y-tap.dy;
        if(!quick_panel_active&&first_y<=80&&tap.dy>=90&&abs(tap.dy)>abs(tap.dx)) {
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
        if(screen==Screen::Maps&&(abs(tap.dx)>22||abs(tap.dy)>22)&&
           meshink_map_gestures::terrain_point(tap.x,tap.y,portrait_layout())) {
            pan_map_by_pixels(tap.dx,tap.dy);
            open_screen(Screen::Maps);
            continue;
        }
        if((screen==Screen::Contacts||screen==Screen::Channels||screen==Screen::Discovery)&&
           abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            const size_t count=!ui_data?0:
                (screen==Screen::Contacts?ui_data->contact_count():
                 screen==Screen::Channels?ui_data->channel_count():ui_data->advert_count());
            size_t& page=screen==Screen::Contacts?contacts_page:
                         screen==Screen::Channels?channels_page:discovery_page;
            clamp_list_page(page,count);
            const size_t pages=list_page_count(count);
            int next=(int)page+(tap.dy<0?1:-1);
            if(next<0)next=0;if(next>=(int)pages)next=(int)pages-1;
            if((size_t)next!=page){
                page=(size_t)next;
                const char* name=screen==Screen::Contacts?"contacts":
                                 screen==Screen::Channels?"channels":"discovery";
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] %s page=%u/%u\n",name,
                          (unsigned)(page+1),(unsigned)pages);
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
        }else if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&!keyboard_visible&&abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
#if T5_TIMING_DIAGNOSTICS
            ui_message_perf_reset();
            ui_message_perf_collect=true;
            MeshInkMessageStorePerf perf_store_before{},perf_store_after{};
            meshink_message_store_perf_snapshot(perf_store_before);
            const uint32_t perf_nav_started=micros();
#endif
            const size_t count=ui_data?ui_data->active_message_count():0;
            chat_geometry_sync(count);
            size_t first=count,end=count;bool has_older=false;
            if(count)
                chat_page_bounds_lazy(count,chat_history_available_current(),
                                      chat_history_available_paged(),
                                      chat_page,first,end,has_older);
            uint8_t next=chat_page;
            if(tap.dy<0){
                if(has_older&&chat_page+1<CHAT_PAGE_ANCHORS)next=(uint8_t)(chat_page+1);
            }else if(chat_page>0)next=(uint8_t)(chat_page-1);
#if T5_TIMING_DIAGNOSTICS
            const uint32_t perf_nav_elapsed=(uint32_t)(micros()-perf_nav_started);
            meshink_message_store_perf_snapshot(perf_store_after);
            ui_message_perf_collect=false;
            T5MessageNavPerf perf{};
            perf.elapsed_us=perf_nav_elapsed;
            perf.store_read_us=perf_store_after.read_us-perf_store_before.read_us;
            perf.store_read_worst_us=perf_store_after.read_worst_us;
            const uint32_t perf_reads=perf_store_after.reads-perf_store_before.reads;
            perf.store_reads=(uint16_t)(perf_reads>0xFFFFU?0xFFFFU:perf_reads);
            const uint32_t perf_cache_reads=perf_store_after.cache_reads-perf_store_before.cache_reads;
            perf.store_cache_reads=(uint16_t)(perf_cache_reads>0xFFFFU?0xFFFFU:perf_cache_reads);
            const uint32_t perf_writes=perf_store_after.writes-perf_store_before.writes;
            perf.store_writes=(uint16_t)(perf_writes>0xFFFFU?0xFFFFU:perf_writes);
            perf.geometry_calls=ui_message_perf.geometry_calls;
            perf.fill_calls=ui_message_perf.fill_calls;
            perf.geometry_us=ui_message_perf.geometry_us;
            perf.fill_us=ui_message_perf.fill_us;
            perf.wrap_calls=ui_message_perf.wrap_calls;
            perf.wrap_chars=ui_message_perf.wrap_chars;
            perf.advances=ui_message_perf.advances;
            t5_timing_note_message_nav(perf);
#endif
            if(next!=chat_page){
                chat_page=next;
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] conversation page=%u%s\n",
                          (unsigned)(chat_page+1),has_older?"":" (oldest)");
                draw_screen();refresh(MeshInkRefreshMode::FastGray16);
            }
        }else if(screen==Screen::ContactDetails&&!keyboard_visible&&abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            UiNodeDetails node{};const uint8_t pages=(ui_data&&ui_data->active_node_details(node))?node_info_page_count(node.node_type):1;
            int next=(int)details_page+(tap.dy<0?1:-1);
            if(next<0)next=0;if(next>=pages)next=pages-1;
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
        t5_timing_set_ui_action(T5UiAction::StatusPoll);
        const uint32_t timing_status_started=micros();
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
        t5_timing_note_ui_status((uint32_t)(micros()-timing_status_started));
        t5_timing_set_ui_action(T5UiAction::None);
    }
    const bool text_refresh_due=text_refresh_pending&&(int32_t)(millis()-text_refresh_after)>=0;
    if(status_dirty&&!message_alert_active){
        // Content changes retain the ordinary screen redraw. Refresh the
        // hardware snapshot first so clock and battery come along for free.
        t5_timing_set_ui_action(T5UiAction::StatusRefresh);
        update_status_hardware();
        if(text_refresh_due){
            const uint32_t timing_text_now=millis();
            t5_timing_note_text_wait(text_refresh_queued_at?(uint32_t)(timing_text_now-text_refresh_queued_at):0);
            text_refresh_pending=false;
        }
        const bool wake=status_wake_light&&!standby_active;
        status_dirty=false;status_bar_dirty=false;status_wake_light=false;
        draw_screen();refresh(MeshInkRefreshMode::Direct,wake);
        t5_timing_set_ui_action(T5UiAction::None);
    }else if(text_refresh_due){
        t5_timing_set_ui_action(T5UiAction::TextRefresh);
        const uint32_t timing_text_now=millis();
        t5_timing_note_text_wait(text_refresh_queued_at?(uint32_t)(timing_text_now-text_refresh_queued_at):0);
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
        else if(screen==Screen::RadioSettings&&keyboard_visible&&!keyboard_landscape&&!keyboard_message_mode)
            draw_radio_name_fast();
        else
            draw_screen();
        refresh(MeshInkRefreshMode::Direct);
        t5_timing_set_ui_action(T5UiAction::None);
    }else if(status_bar_dirty&&!message_alert_active&&!quick_panel_active&&!keyboard_landscape){
        t5_timing_set_ui_action(T5UiAction::StatusRefresh);
        // Event-driven updates still sample and display the exact clock and
        // battery, but they never move the next :00/:05/:10... periodic boundary.
        update_status_hardware();
        const bool wake=status_wake_light&&!standby_active;
        status_bar_dirty=false;status_wake_light=false;
        draw_status_bar();
        refresh_area(MeshInkRefreshMode::Direct,
            {0,0,portrait_layout().width,portrait_layout().status_height},wake);
        t5_timing_set_ui_action(T5UiAction::None);
    }
    if(toast_visible&&(int32_t)(millis()-toast_until)>=0){
        t5_timing_set_ui_action(T5UiAction::ToastRefresh);
        toast_visible=false;
        if(toast_opens_main){toast_opens_main=false;screen=Screen::Contacts;keyboard_visible=false;keyboard_message_mode=false;status_unread=0;status_channel_unread=0;}
        draw_screen();refresh(MeshInkRefreshMode::Direct,true);
        t5_timing_set_ui_action(T5UiAction::None);
    }
    frontlight_service();
    if(message_alert_active)t5_timing_set_ui_action(T5UiAction::MessageAlert);
    service_message_alert();
    t5_timing_set_ui_action(T5UiAction::None);
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

void ui_status_set_gps(bool enabled,bool has_fix,int satellites,long latitude,long longitude,uint32_t timestamp) {
    if(!meshink_board_has_gps()){
        (void)enabled;(void)has_fix;(void)satellites;(void)latitude;(void)longitude;(void)timestamp;
        return;
    }
    const bool state_changed=status_gps_enabled!=enabled||status_gps_fix!=has_fix;
    const bool detail_changed=status_gps_satellites!=satellites||status_gps_latitude!=latitude||status_gps_longitude!=longitude;
    if(state_changed)T5_DEBUGF(T5_LOG_GPS,"[T5-GPS] state %s sats=%d lat=%ld lon=%ld\n",enabled?(has_fix?"fixed":"searching"):"disabled",satellites,latitude,longitude);
    const long previous_latitude=status_gps_latitude;
    const long previous_longitude=status_gps_longitude;
    status_gps_enabled=enabled;status_gps_fix=has_fix;status_gps_satellites=satellites;status_gps_latitude=latitude;status_gps_longitude=longitude;status_gps_timestamp=timestamp;
    // Only verified live fixes update the retained map location. GPS disabled
    // or searching may report (0,0), which must not erase the last fix.
    if(enabled&&has_fix&&latitude>=-85051100L&&latitude<=85051100L&&
       longitude>=-180000000L&&longitude<=180000000L){
        map_has_last_gps_position=true;
        map_last_gps_latitude=latitude;map_last_gps_longitude=longitude;
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
    if(!visible){if(channel){if(status_channel_unread<65535)status_channel_unread++;}else if(status_unread<65535)status_unread++;persist_unread();}
    status_dirty=true;if(standby_active)start_message_alert();else status_wake_light=true;T5_DEBUGF(T5_LOG_MESH,"[T5-UI] %s message event unread=%u refresh queued standby=%d visible=%d\n",channel?"channel":"direct",channel?status_channel_unread:status_unread,standby_active,visible);
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
    // On a clean install the UI defaults to NZ NARROW, while MeshCore's
    // compiled defaults use SF8. Apply the displayed selection immediately
    // after MeshCore has loaded its prefs, before the UI becomes interactive.
    // Do not override a completed installation's stored radio configuration,
    // and honour KEEP CURRENT as a deliberate no-change selection.
    if(setup_complete||selected_preset==0)return;
    if(apply_selected_preset()){
        const Preset& preset=PRESETS[selected_preset];
        T5_DEBUGF(T5_LOG_UI,"[T5-BOOT] initial radio preset %s: %lu.%03lu MHz SF%u BW%.1f CR%u %uB\n",
            preset.title,(unsigned long)(preset.frequency_khz/1000),
            (unsigned long)(preset.frequency_khz%1000),preset.spreading_factor,
            (double)preset.bandwidth_khz,preset.coding_rate,preset.path_hash_bytes);
    }else{
        Serial.println("[T5-ERROR] first-time radio preset failed; select a radio preset in setup");
    }
}

void ui_mesh_ready(){
    mesh_is_ready=true;Preferences state;bool migrated=false;if(state.begin("t5-ui",false)){migrated=state.getBool("name_migrated",false);if(!migrated&&node_name[0]){local_mesh_apply_name(node_name);state.putBool("name_migrated",true);T5_DEBUGF(T5_LOG_UI,"[T5-UI] migrated node name to MeshCore '%s'\n",node_name);}else{strncpy(node_name,local_mesh_node_name(),sizeof(node_name)-1);node_name[sizeof(node_name)-1]=0;T5_DEBUGF(T5_LOG_UI,"[T5-UI] node name loaded from MeshCore '%s'\n",node_name);}state.putString("name",node_name);state.end();}
    update_status_hardware();status_bar_dirty=true;
}

void ui_use_data_provider(UiDataProvider* provider) {
    if (!provider) return;
    ui_data=provider;
    T5_DEBUGLN(T5_LOG_UI,"[T5-UI] live MeshCore data provider attached");
    // ui_finish_startup() presents the first interactive screen only after
    // the blocking storage / MeshCore boot sequence has completed.
}

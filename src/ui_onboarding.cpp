#include <Arduino.h>
#include <esp_random.h>
#include <Preferences.h>
#include <epdiy.h>
#include <driver/i2c.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp32-hal-cpu.h>
#include <esp_sleep.h>
#include <SPIFFS.h>
#include "ui_onboarding.h"
#include "ui_data.h"
#include "local_mesh_runtime.h"
#include "map_tiles.h"
#include "map_gestures.h"
#include "t5_logging.h"
#include "meshcore_version.h"
#include "keyboard_geometry.h"
#include "meshink_logo_bitmap.h"  // generated from original PNG at build time

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.3.0"
#endif

void request_companion_mode() __attribute__((weak));
void request_companion_mode() { Serial.println("[T5-UI] companion mode requires unified build"); }

// UI milestone 0.1.0: standalone onboarding. Bluetooth, radio and GPS are not
// started in this target. Saved values are device-owned and will be handed to
// the MeshCore application adapter in the next milestone.
static constexpr char UI_VERSION[] = T5_FIRMWARE_VERSION;
static constexpr uint8_t GT911_ADDR = 0x5D;
static constexpr gpio_num_t TOUCH_RST = GPIO_NUM_9;
static constexpr gpio_num_t TOUCH_INT = GPIO_NUM_3;
static constexpr gpio_num_t FRONTLIGHT = GPIO_NUM_11;
static constexpr gpio_num_t BOOT_BUTTON = GPIO_NUM_0;
static constexpr uint8_t FRONTLIGHT_PWM_CHANNEL=6;

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

static EpdiyHighlevelState display;
static uint8_t* fb = nullptr;
static Preferences prefs;
static char node_name[21] = "MeshInk-";
static uint8_t selected_preset = 17;
static bool saved = false;
static bool was_pressed = false;
static bool replace_name_on_type = false;
static bool keyboard_visible = true;
static bool keyboard_upper = true;
static bool keyboard_symbols = false;
static bool keyboard_landscape = false;
static bool standby_restore_landscape = false;
static uint16_t status_unread = 0;
static uint16_t status_channel_unread = 0;
static bool status_gps_enabled = false;
static bool status_gps_fix = false;
static int16_t status_gps_satellites = 0;
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
static uint8_t status_charge_state=0;
static int8_t status_hour = -1;
static int8_t status_minute = -1;
static bool status_dirty = false;
static bool toast_visible = false;
static uint32_t toast_until = 0;
static char toast_message[32] = {};
static bool setup_complete = false;
static bool toast_opens_main = false;
static bool keyboard_message_mode = false;
static char compose_text[49] = {};
static UiDataProvider* ui_data = nullptr;
static size_t selected_contact = 0;
static size_t selected_channel = 0;
static constexpr size_t LIST_ITEMS_PER_PAGE = 5;
static size_t contacts_page = 0;
static size_t channels_page = 0;
static int16_t cached_touch_x = 0, cached_touch_y = 0;
static uint8_t chat_page = 0;
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
static bool touch_enabled=true;
static bool standby_active=false;
static bool hardware_failure=false;
static uint8_t standby_timeout_index=1;
static uint32_t last_user_activity=0;
static bool status_wake_light=false;
static bool message_alert_active=false;
static bool quick_panel_active=false;
static bool quick_panel_restore_landscape=false;
static constexpr int QUICK_PANEL_BOTTOM=620;
static constexpr int QUICK_SLIDER_LEFT=44;
static constexpr int QUICK_SLIDER_RIGHT=496;
static constexpr int QUICK_SLIDER_Y=190;
static volatile bool quick_slider_dragging=false;
static volatile uint8_t quick_slider_preview=30;
static uint8_t message_alert_phase=0;
static uint32_t message_alert_deadline=0;
static uint32_t message_alert_cooldown_until=0;
enum class Screen : uint8_t {
    Welcome, Presets, CompanionConfirm, ShutdownConfirm,
    Contacts, ContactChat, ContactDetails,
    Channels, ChannelChat, Maps, Discovery, More, AdvertMenu,
    Settings, RadioSettings, GpsSettings, GpsTuning, Timezone, PrivacySettings, DisplaySettings, NightSchedule, Help, About
};
static Screen screen = Screen::Welcome;
static Screen preset_return_screen = Screen::Welcome;
static uint8_t preset_page = 3;
static bool details_from_discovery=false;
static uint8_t details_page=0;
// Maps spans the screen between the compact 48-pixel status bar and
// the 60-pixel bottom navigation; there is no extra title/header strip.
static constexpr int MAP_TOP=48;
static constexpr int MAP_BOTTOM=900;
static constexpr int MAP_CENTRE_Y=(MAP_TOP+MAP_BOTTOM)/2;
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
// A current fix takes priority; otherwise use the last verified position.
// The stored fallback is for map navigation, never advertised as a GPS fix.
static bool map_device_position(long& latitude,long& longitude,bool& current_fix){
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
static void frontlight_drive(bool on){frontlight_lit=on&&frontlight_allowed();const uint8_t duty=frontlight_lit?(uint8_t)max(1,(frontlight_brightness*255)/100):0;ledcWrite(FRONTLIGHT_PWM_CHANNEL,duty);}
static void frontlight_preview(uint8_t level){
    // Live PWM feedback for the quick slider only. Do not alter the persisted
    // brightness or redraw the e-paper until the release event is handled.
    frontlight_lit=level>0;
    const uint8_t duty=level?(uint8_t)max(1,((int)level*255)/100):0;
    ledcWrite(FRONTLIGHT_PWM_CHANNEL,duty);
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
    QueuedTap()=default;
    // Arduino's C++11 toolchain requires an explicit constructor here once
    // the map-only fields have default initializers. Preserve all existing
    // five-argument single-touch event construction unchanged.
    QueuedTap(int16_t px,int16_t py,int16_t pdx,int16_t pdy,bool is_home):
        x(px),y(py),dx(pdx),dy(pdy),home(is_home) {}
};
struct MapTapSequence{
    uint8_t count=0;
    int16_t first_x=0,first_y=0;
    int16_t last_x=0,last_y=0;
    uint32_t last_at=0;
};
static MapTapSequence map_taps{};

static bool set_cpu_target(uint32_t mhz,const char* reason,bool verbose=true){
    const bool accepted=setCpuFrequencyMhz(mhz);const uint32_t actual=getCpuFrequencyMhz();
    if(!accepted||actual!=mhz)Serial.printf("[T5-ERROR] CPU target=%lu actual=%lu MHz reason=%s\n",(unsigned long)mhz,(unsigned long)actual,reason);
    else T5_DEBUGF(T5_LOG_POWER && verbose,"[T5-POWER] cpu target=%lu actual=%lu MHz apb=%lu MHz reason=%s result=OK\n",(unsigned long)mhz,(unsigned long)(getApbFrequency()/1000000),reason);
    return accepted&&actual==mhz;
}

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
    return FONT[0].r;
}

static void text(const char* s, int x, int y, int scale, uint8_t color = 0, bool bold = false) {
    while (*s) {
        const uint8_t* rows = glyph(*s++);
        for (int ry=0; ry<7; ++ry) for (int rx=0; rx<5; ++rx)
            if (rows[ry] & (1 << (4-rx)))
                for (int dy=0; dy<scale; ++dy) for (int dx=0; dx<scale; ++dx)
                    { epd_draw_pixel(x+rx*scale+dx, y+ry*scale+dy, color, fb);
                      if (bold) epd_draw_pixel(x+rx*scale+dx+1, y+ry*scale+dy, color, fb); }
        x += 6*scale;
    }
}

static void centred(const char* s, int y, int scale, uint8_t color = 0, bool bold = false) {
    text(s, (540 - (int)strlen(s)*6*scale)/2, y, scale, color, bold);
}

static void box(int x, int y, int w, int h, bool selected=false) {
    EpdRect r = {x,y,w,h};
    if (selected) epd_fill_rect(r, 0, fb);
    else { epd_fill_rect(r, 0xFF, fb); epd_draw_rect(r, 0, fb); }
}

static void key(const char* label, int x, int y, int w) {
    box(x,y,w,62);
    int scale=((int)strlen(label)*18+8<=w)?3:2;
    text(label,x+(w-(int)strlen(label)*6*scale)/2,y+(62-7*scale)/2,scale,0,true);
}

static void draw_wrapped(const char* value,int x,int y,int chars_per_line,int scale,uint8_t color,bool bold,int max_lines);

static void draw_keyboard() {
    const char* numbers="1234567890";
    const auto digits=meshink_keyboard::numbers(false);
    for(int i=0;numbers[i];++i){
        char label[2]={numbers[i],0};
        key(label,digits.start+i*digits.pitch,618,digits.width);
    }
    const char* letter_rows_upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    const char* letter_rows_lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbol_rows[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    const char** rows=keyboard_symbols?symbol_rows:(keyboard_upper?letter_rows_upper:letter_rows_lower);
    const int ys[]={688,758,828};
    for(int r=0;r<3;++r){
        const auto layout=meshink_keyboard::letters(false,r,(int)strlen(rows[r]));
        for(int i=0;rows[r][i];++i){
            char label[2]={rows[r][i],0};
            key(label,layout.start+i*layout.pitch,ys[r],layout.width);
        }
    }
    key(keyboard_symbols?"ABC":(keyboard_upper?"abc":"#+="),12,828,76);
    key("DEL",460,828,68);
    key("LAND",12,898,100);key("SPACE",120,898,190);key("HIDE",318,898,100);key(keyboard_message_mode?"SEND":"SAVE",426,898,102);
}

static void landscape_key(const char* label,int x,int y,int w){
    EpdRect r={x,y,w,62};epd_fill_rect(r,0xFF,fb);epd_draw_rect(r,0,fb);
    const int scale=strlen(label)<=5?3:2;text(label,x+(w-(int)strlen(label)*6*scale)/2,y+(62-7*scale)/2,scale,0,true);
}
static const char** active_keyboard_rows(){
    static const char* upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    static const char* lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    static const char* symbols[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    return keyboard_symbols?symbols:(keyboard_upper?upper:lower);
}
static void draw_landscape_keyboard(){
    epd_hl_set_all_white(&display);
    const char* value=keyboard_message_mode?compose_text:node_name;
    EpdRect entry={16,14,928,112};epd_draw_rect(entry,0,fb);
    draw_wrapped(value[0]?value:"ENTER TEXT",32,30,48,4,0,true,2);
    const char* numbers="1234567890";
    const auto digits=meshink_keyboard::numbers(true);
    for(int i=0;i<10;++i){
        char s[2]={numbers[i],0};
        landscape_key(s,digits.start+i*digits.pitch,145,digits.width);
    }
    const char** rows=active_keyboard_rows();const int ys[]={215,285,355};
    for(int r=0;r<3;++r){
        const auto layout=meshink_keyboard::letters(true,r,(int)strlen(rows[r]));
        for(int i=0;rows[r][i];++i){
            char s[2]={rows[r][i],0};
            landscape_key(s,layout.start+i*layout.pitch,ys[r],layout.width);
        }
    }
    landscape_key(keyboard_symbols?"ABC":(keyboard_upper?"abc":"#+="),15,355,130);
    landscape_key("DEL",812,355,133);
    landscape_key("PORTRAIT",15,425,180);landscape_key("SPACE",203,425,500);
    landscape_key(keyboard_message_mode?"SEND":"DONE",711,425,234);
}

// The number and letter hitboxes are calculated from the EXACT geometry used
// when drawing them. This also divides gaps at the midpoint and prevents
// symbols in the 8-key bottom row overlapping the mode / Delete buttons.
static bool keyboard_character_at(int x,int y,bool landscape,char& character){
    const int number_top=landscape?145:618;
    if(meshink_keyboard::in_row(y,number_top)){
        const int i=meshink_keyboard::key_index(meshink_keyboard::numbers(landscape),x);
        if(i<0)return false;
        character="1234567890"[i];
        return true;
    }
    const char** rows=active_keyboard_rows();
    const int top=landscape?215:688;
    for(int r=0;r<3;++r){
        if(!meshink_keyboard::in_row(y,top+r*70))continue;
        const int i=meshink_keyboard::key_index(
            meshink_keyboard::letters(landscape,r,(int)strlen(rows[r])),x);
        if(i<0)return false;
        character=rows[r][i];
        return true;
    }
    return false;
}

static void line(int x0,int y0,int x1,int y1,uint8_t color=0) {
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    while(true){epd_draw_pixel(x0,y0,color,fb);if(x0==x1&&y0==y1)break;const int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}

static void draw_target_icon(int x,int y,bool disabled) {
    // Three-pixel strokes for a clearly visible status-bar GPS icon.
    epd_fill_rect({x+4,y+4,22,3},0,fb);
    epd_fill_rect({x+4,y+23,22,3},0,fb);
    epd_fill_rect({x+4,y+4,3,22},0,fb);
    epd_fill_rect({x+23,y+4,3,22},0,fb);
    epd_fill_rect({x+12,y+12,6,6},0,fb);
    epd_fill_rect({x,y+14,30,3},0,fb);
    epd_fill_rect({x+14,y,3,30},0,fb);
    if(disabled) {
        for(int d=-3;d<=3;++d)line(x+2,y+2+d,x+27,y+27+d);
    }
}

static void draw_search_icon(int x,int y) {
    epd_fill_rect({x+3,y+3,20,3},0,fb);
    epd_fill_rect({x+3,y+20,20,3},0,fb);
    epd_fill_rect({x+3,y+3,3,20},0,fb);
    epd_fill_rect({x+20,y+3,3,20},0,fb);
    for(int d=-1;d<=1;++d)line(x+20,y+20+d,x+29,y+29+d);
}

static void draw_envelope_icon(int x,int y) {
    // Bold three-pixel outline and flap, legible at status-bar size.
    epd_fill_rect({x,y+5,30,3},0,fb);
    epd_fill_rect({x,y+23,30,3},0,fb);
    epd_fill_rect({x,y+5,3,21},0,fb);
    epd_fill_rect({x+27,y+5,3,21},0,fb);
    for(int d=-1;d<=1;++d) {
        line(x+3,y+8+d,x+15,y+18+d);
        line(x+26,y+8+d,x+15,y+18+d);
    }
}

static void draw_battery_icon(int x,int y,int level=-1) {
    epd_draw_rect({x,y+6,31,18},0,fb);epd_fill_rect({x+31,y+11,4,8},0,fb);
    if(level<0)level=status_battery;if(level>0){const int fill=(level*27)/100;epd_fill_rect({x+2,y+8,fill,14},0,fb);}
    if(status_charge_state==1||status_charge_state==2){
        epd_fill_rect({x+10,y+6,14,17},0xFF,fb);
        // Wide, bold lightning bolt for the low-resolution status bar.
        for(int d=-2;d<=2;++d){line(x+22+d,y+5,x+12+d,y+16);line(x+12+d,y+16,x+20+d,y+16);line(x+20+d,y+16,x+10+d,y+27);}
    }
}

static void draw_status_bar(bool standby_quantized=false) {
    epd_fill_rect({0,0,540,48},0xFF,fb);
    epd_draw_rect({0,0,540,48},0,fb);
    if(!status_gps_enabled)draw_target_icon(6,9,true);
    else if(status_gps_fix)draw_target_icon(6,9,false);
    else draw_search_icon(6,9);
    int left=46;
    // Show receiver-reported satellite count only while GPS is enabled.
    if(!standby_active&&!standby_quantized&&status_gps_enabled&&status_gps_fix) {
        char satellites[4];
        snprintf(satellites,sizeof(satellites),"%d",max(0,min(99,(int)status_gps_satellites)));
        text(satellites,43,13,3,0,true);
        left=43+(int)strlen(satellites)*18+12;
    }
    if(status_unread){draw_envelope_icon(left,9);left+=36;char count[7];snprintf(count,sizeof(count),"%u",status_unread);text(count,left,13,3,0,true);left+=(int)strlen(count)*18+12;}
    if(status_channel_unread){text("#",left,13,3,0,true);left+=22;char count[7];snprintf(count,sizeof(count),"%u",status_channel_unread);text(count,left,13,3,0,true);}
    char clock_text[8];
    const int shown_minute=standby_quantized&&status_minute>=0?(status_minute/10)*10:status_minute;
    if(status_hour>=0)snprintf(clock_text,sizeof(clock_text),"%02d:%02d",status_hour,shown_minute);
    else snprintf(clock_text,sizeof(clock_text),"--:--");
    centred(clock_text,13,3,0,true);
    char battery[8];
    const int shown_battery=standby_quantized&&status_battery>=0?(status_battery/5)*5:status_battery;
    if(shown_battery>=0)snprintf(battery,sizeof(battery),"%d%%",shown_battery);
    else snprintf(battery,sizeof(battery),"--%%");
    const int battery_x=530-(int)strlen(battery)*18;
    draw_battery_icon(battery_x-43,8,shown_battery);
    text(battery,battery_x,13,3,0,true);
}

// Share the same small black notification style between ordinary settings
// toasts and the synchronous Maps loading message (which has no timeout).
static void draw_toast_message(const char* message) {
    const int scale=3,w=max(300,(int)strlen(message)*6*scale+48),h=72,x=(540-w)/2,y=640,r=12;
    epd_fill_rect({x+r,y,w-2*r,h},0,fb);epd_fill_rect({x,y+r,w,h-2*r},0,fb);
    epd_fill_rect({x+5,y+5,w-10,h-10},0,fb);
    text(message,x+(w-(int)strlen(message)*6*scale)/2,y+25,scale,0xFF,true);
}
static void draw_toast() {
    if(toast_visible)draw_toast_message(toast_message);
}

static void show_toast(const char* message) {
    strncpy(toast_message,message,sizeof(toast_message)-1);toast_message[sizeof(toast_message)-1]=0;
    toast_visible=true;toast_until=millis()+1500;
}

static void draw_welcome() {
    epd_hl_set_all_white(&display);
    draw_status_bar();
    centred("MESHCORE", 62, 6, 0, true);
    centred("SET UP YOUR T5", 116, 3, 0, true);
    text("YOUR NAME", 30, 154, 2, 0, true);
    box(30,180,480,64);
    text(node_name, 48,199,3);
    text("RADIO PRESET", 30, 268, 2, 0, true);
    box(24,292,492,88);
    text(PRESETS[selected_preset].title,38,303,3,0,true);
    text(PRESETS[selected_preset].detail,38,344,2);
    text(">",486,319,3,0,true);
    box(30,402,480,52);
    centred("BLUETOOTH COMPANION MODE",418,2,0,true);
    if(keyboard_visible){centred("ENTER A NAME",586,2,0,true);draw_keyboard();}
    else {box(30,840,480,64);centred("SHOW KEYBOARD",861,3,0,true);}
}

static void draw_presets() {
    epd_hl_set_all_white(&display);
    draw_status_bar();
    text("< BACK",24,62,2,0,true);
    centred("RADIO PRESETS",92,4,0,true);
    const int first=preset_page*PRESETS_PER_PAGE;
    for (int row=0; row<PRESETS_PER_PAGE; ++row) {
        const int index=first+row; if(index>=PRESET_COUNT) break;
        const int y=132+row*128; box(12,y,516,112,index==selected_preset);
        const uint8_t color=index==selected_preset?0xFF:0;
        text(PRESETS[index].title,28,y+12,3,color,true);
        text(PRESETS[index].detail,28,y+60,2,color,true);
    }
    box(24,800,180,62,preset_page==0);text("PREV",75,821,2,preset_page==0?0xFF:0,true);
    const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
    box(336,800,180,62,preset_page+1>=page_count);text("NEXT",385,821,2,preset_page+1>=page_count?0xFF:0,true);
    char page_text[20];snprintf(page_text,sizeof(page_text),"PAGE %u OF %u",preset_page+1,page_count);
    centred(page_text,890,2,0,true);
}

static void draw_companion_confirm() {
    epd_hl_set_all_white(&display);
    draw_status_bar();
    centred("BLUETOOTH",120,5,0,true);centred("COMPANION MODE",180,4,0,true);
    centred("THE LOCAL UI WILL CLOSE",300,2);centred("UNTIL THE DEVICE RESTARTS",335,2);
    box(30,500,220,72);text("CANCEL",74,524,3,0,true);
    box(290,500,220,72,true);text("START",338,524,3,0xFF,true);
}

static void draw_shutdown_confirm() {
    epd_hl_set_all_white(&display);
    draw_status_bar();
    centred("SHUT DOWN",120,5,0,true);
    centred("FULL BATTERY POWER CUT",245,3,0,true);
    centred("THE DEVICE WILL STOP",305,3,0,true);
    centred("RECEIVING MESSAGES",350,3,0,true);
    centred("PRESS PWR TO START AGAIN",445,3,0,true);
    centred("ON USB: HOLD BOOT TO WAKE",500,3,0,true);
    box(30,650,220,72);text("CANCEL",74,674,3,0,true);
    box(290,650,220,72,true);text("SHUT DOWN",311,674,3,0xFF,true);
}

static void draw_wrapped(const char* value,int x,int y,int chars_per_line,int scale,uint8_t color,bool bold,int max_lines) {
    const char* cursor=value;
    for(int row=0;row<max_lines&&*cursor;++row){
        while(*cursor==' ')++cursor;
        int remaining=strlen(cursor),take=min(chars_per_line,remaining);
        if(remaining>chars_per_line){int split=take;while(split>0&&cursor[split]!=' ')--split;if(split>0)take=split;}
        char line_text[64]={};memcpy(line_text,cursor,min(take,63));
        text(line_text,x,y+row*(7*scale+8),scale,color,bold);cursor+=take;
    }
}

static void draw_bottom_nav(int selected) {
    static const char* labels[]={"CONTACTS","CHANNELS","MAPS","MORE"};
    for(int i=0;i<4;++i){
        box(i*135,900,135,60,i==selected);const uint8_t color=i==selected?0xFF:0;
        text(labels[i],i*135+(135-(int)strlen(labels[i])*12)/2,920,2,color,true);
        const bool unread=(i==0&&status_unread)||(i==1&&status_channel_unread);
        if(unread)epd_fill_rect({i*135+118,908,11,11},color,fb);
    }
}

static void draw_app_header(const char* title,bool back=false,const char* action=nullptr) {
    epd_hl_set_all_white(&display);draw_status_bar();
    if(back){box(12,58,58,48,true);centred("",0,1);text("<",31,70,3,0xFF,true);}
    centred(title,64,4,0,true);
    if(action){box(470,58,58,48,true);text(action,470+(58-(int)strlen(action)*18)/2,70,3,0xFF,true);}
}

static void draw_list_entry(const UiListEntry& item,int y) {
    box(12,y,516,142);
    text(item.title,28,y+16,3,0,true);
    text(item.time,528-(int)strlen(item.time)*12-16,y+20,2,0,true);
    draw_wrapped(item.subtitle,28,y+60,36,2,0,false,2);
    if(item.unread){epd_fill_rect({482,y+94,20,20},0,fb);}
}

static size_t list_page_count(size_t count) {
    return count ? (count+LIST_ITEMS_PER_PAGE-1)/LIST_ITEMS_PER_PAGE : 1;
}

static void clamp_list_page(size_t& page,size_t count) {
    const size_t pages=list_page_count(count);
    if(page>=pages)page=pages-1;
}

static void draw_list_page_footer(size_t page,size_t count) {
    const size_t pages=list_page_count(count);
    if(pages<=1)return;
    char page_text[24];
    snprintf(page_text,sizeof(page_text),"PAGE %u OF %u",(unsigned)(page+1),(unsigned)pages);
    centred(page_text,875,2,0,true);
}

static void draw_contacts() {
    draw_app_header("CONTACTS");
    // The model is fully populated before ui_use_data_provider() attaches it.
    // A missing provider means STARTUP, not a completed empty contact list.
    if(!ui_data)centred("LOADING CONTACT INFO..",300,3,0,true);
    else {
        const size_t count=ui_data->contact_count();
        clamp_list_page(contacts_page,count);
        if(!count)centred("NO SAVED CONTACTS",300,3,0,true);
        else {
            const size_t first=contacts_page*LIST_ITEMS_PER_PAGE;
            for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row)
                draw_list_entry(ui_data->contact(first+row),120+row*150);
            draw_list_page_footer(contacts_page,count);
        }
    }
    draw_bottom_nav(0);
}

static void draw_channels() {
    draw_app_header("CHANNELS");
    if(!ui_data)centred("NO CONFIGURED CHANNELS",300,3,0,true);
    else {
        const size_t count=ui_data->channel_count();
        clamp_list_page(channels_page,count);
        if(!count)centred("NO CONFIGURED CHANNELS",300,3,0,true);
        else {
            const size_t first=channels_page*LIST_ITEMS_PER_PAGE;
            for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row)
                draw_list_entry(ui_data->channel(first+row),120+row*150);
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
    struct Visible {int16_t x,y;size_t index;UiMapNode node;};
    Visible visible[50]{};size_t count=0;
    for(size_t i=0;i<ui_data->map_node_count()&&count<50;++i) {
        UiMapNode node{};if(!ui_data->map_node(i,node))continue;
        const double x=(node.longitude/1000000.0+180.0)/360.0*world;
        double delta_x=x-centre_x;
        if(delta_x>world/2)delta_x-=world;
        if(delta_x<-world/2)delta_x+=world;
        const double lat=node.latitude/1000000.0,r=lat*PI/180.0;
        const double y=(1.0-log(tan(r)+1.0/cos(r))/PI)*world/2.0;
        const int sx=(int)lround(270+delta_x),sy=(int)lround(MAP_CENTRE_Y+y-centre_y);
        if(sx<7||sx>533||sy<MAP_TOP+7||sy>MAP_BOTTOM-7)continue;
        visible[count++]={(int16_t)sx,(int16_t)sy,i,node};
    }
    struct Bounds {int x,y,w,h;};Bounds occupied[50]{};size_t occupied_count=0;
    const uint32_t now=(uint32_t)time(nullptr);
    for(size_t i=0;i<count;++i) {
        const auto& n=visible[i];
        char short_name[19]{};strncpy(short_name,n.node.name,sizeof(short_name)-1);
        const int w=min(230,max(48,(int)strlen(short_name)*12+8));
        char age[16];
        if(n.node.gps_from_reply){
            // This is when our T5 RECEIVED GPS telemetry, not the remote fix time.
            const uint32_t seconds=(uint32_t)(millis()-n.node.gps_received_millis)/1000U;
            if(seconds<3600)snprintf(age,sizeof(age),"GPS %lum",(unsigned long)(seconds/60));
            else if(seconds<86400)snprintf(age,sizeof(age),"GPS %luh",(unsigned long)(seconds/3600));
            else snprintf(age,sizeof(age),"GPS %lud",(unsigned long)(seconds/86400));
        }else if(!n.node.advertised_at||now<n.node.advertised_at)strcpy(age,"ADV ?");
        else {const uint32_t seconds=now-n.node.advertised_at;
            if(seconds<3600)snprintf(age,sizeof(age),"ADV %lum",(unsigned long)(seconds/60));
            else if(seconds<86400)snprintf(age,sizeof(age),"ADV %luh",(unsigned long)(seconds/3600));
            else snprintf(age,sizeof(age),"ADV %lud",(unsigned long)(seconds/86400));}
        const int offsets[4][2]={{12,-18},{-12-w,-18},{12,12},{-12-w,12}};
        int lx=0,ly=0;bool placed=false;
        for(const auto& offset:offsets) {
            const int x=n.x+offset[0],y=n.y+offset[1];
            if(x<3||x+w>537||y<MAP_TOP+3||y+34>MAP_BOTTOM-3)continue;
            bool overlap=false;
            for(size_t j=0;j<occupied_count;++j)if(x<occupied[j].x+occupied[j].w+4&&
                x+w+4>occupied[j].x&&y<occupied[j].y+occupied[j].h+3&&y+37>occupied[j].y)
                {overlap=true;break;}
            if(!overlap){lx=x;ly=y;placed=true;break;}
        }
        if(!placed)continue; // Keep the true-position dot even if labels collide.
        occupied[occupied_count++]={lx,ly,w,34};
        epd_fill_rect({lx,ly,w,34},0xFF,fb);
        text(short_name,lx+4,ly+2,2,0,true);
        text(age,lx+4,ly+18,2,0,true);
    }
    // Always draw position dots last so a neighbouring label cannot move or
    // obscure a marker. Each circle has a white halo for contrast.
    for(size_t i=0;i<count;++i) {
        const auto& n=visible[i];
        epd_fill_rect({n.x-6,n.y-6,13,13},0xFF,fb);
        for(int dy=-4;dy<=4;++dy) {
            const int half=abs(dy)==4?1:abs(dy)==3?3:4;
            epd_fill_rect({n.x-half,n.y+dy,half*2+1,1},0,fb);
        }
        map_marker_hits[map_marker_hit_count++]={n.x,n.y,n.index};
    }
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
    sx=(int)lround(270+delta_x);
    sy=(int)lround(MAP_CENTRE_Y+y-centre_y);
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
       sx<20||sx>520||sy<MAP_TOP+20||sy>MAP_BOTTOM-21)return;
    // Same bold 30x30 crosshair as the GPS-fix status icon, at real
    // coordinates (not an always-centred marker). White backing stays
    // legible on dark map tiles; the icon works for last-known fixes too.
    epd_fill_rect({sx-17,sy-17,35,35},0xFF,fb);
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
    // Even a perfect framebuffer cache is stale after SD removal/remount.
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
        draw_status_bar(); // clock, battery and unread counts are live.
    } else {
        epd_hl_set_all_white(&display);
        // Only the compact status bar remains above the terrain. The map
        // starts directly below it and fills the view down to bottom nav.
        draw_status_bar();
        result=map_tiles_render(fb,0,MAP_TOP,540,MAP_BOTTOM-MAP_TOP,
                                map_latitude,map_longitude,map_zoom);
        if(!result.sd_ready||map_base_media_epoch!=map_tiles_media_epoch()) {
            map_base_valid=false;
            map_base_media_epoch=map_tiles_media_epoch();
            if(!result.sd_ready)
                epd_fill_rect({0,MAP_TOP,540,MAP_BOTTOM-MAP_TOP},0xFF,fb);
        }
        // 4 bits per pixel in the high-level EPD framebuffer. A full base
        // snapshot also preserves exact panel row ordering and rotation.
        if(result.sd_ready&&result.tiles) {
            const size_t bytes=(size_t)epd_width()*epd_height()/2;
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
    // Show missing-map coverage when needed, but keep source zoom and tile
    // statistics in serial diagnostics instead of overlaying them on the map.
    if(result.sd_ready&&!result.tiles) {
        epd_fill_rect({18,774,232,30},0xFF,fb);
        text("NO MAP TILES HERE",22,778,2,0,true);
    }
    char zoom[12];snprintf(zoom,sizeof(zoom),"ZOOM %u",map_zoom);epd_fill_rect({18,812,100,30},0xFF,fb);text(zoom,22,816,2,0,true);
    // The three map controls share their 66x66 size, black background and
    // white glyphs. Keep their touch rectangles in sync below.
    constexpr int control_x=462;
    box(control_x,58,66,66,true);
    epd_fill_rect({control_x+20,88,26,5},0xFF,fb);
    epd_fill_rect({control_x+30,78,5,26},0xFF,fb);
    box(control_x,133,66,66,true);
    epd_fill_rect({control_x+20,163,26,5},0xFF,fb);
    box(control_x,208,66,66,true);
    // A large, entirely white crosshair, rather than black strokes on a
    // black button. Its design matches the status-bar GPS target at 1.5x.
    const int target_x=control_x+12,target_y=220;
    epd_fill_rect({target_x+6,target_y+6,33,5},0xFF,fb);
    epd_fill_rect({target_x+6,target_y+34,33,5},0xFF,fb);
    epd_fill_rect({target_x+6,target_y+6,5,33},0xFF,fb);
    epd_fill_rect({target_x+34,target_y+6,5,33},0xFF,fb);
    epd_fill_rect({target_x+18,target_y+18,9,9},0xFF,fb);
    epd_fill_rect({target_x,target_y+21,45,5},0xFF,fb);
    epd_fill_rect({target_x+21,target_y,5,45},0xFF,fb);
    const double metres_per_pixel=cos(map_latitude*PI/180.0)*2.0*PI*6378137.0/(256.0*(1<<map_zoom));double target=metres_per_pixel*120.0,nice=1.0;while(nice*10.0<=target)nice*=10.0;if(target/nice>=5)nice*=5;else if(target/nice>=2)nice*=2;int pixels=(int)(nice/metres_per_pixel);
    char scale[24];if(map_imperial){const double feet=nice*3.28084;if(feet>=5280)snprintf(scale,sizeof(scale),"%.1f MI",feet/5280.0);else snprintf(scale,sizeof(scale),"%.0f FT",feet);}else if(nice>=1000)snprintf(scale,sizeof(scale),"%.0f KM",nice/1000.0);else snprintf(scale,sizeof(scale),"%.0f M",nice);
    epd_fill_rect({20,850,pixels+12,34},0xFF,fb);line(26,872,26+pixels,872);line(26,866,26,878);line(26+pixels,866,26+pixels,878);text(scale,28,850,2,0,true);
    if(!result.sd_ready){epd_fill_rect({80,300,380,80},0xFF,fb);centred("SD CARD / MAPS UNAVAILABLE",328,2,0,true);}
    draw_bottom_nav(2);
}

static int wrapped_line_count(const char* value,int chars_per_line) {
    int lines=0;const char* cursor=value;
    while(*cursor){while(*cursor==' ')++cursor;if(!*cursor)break;int remaining=strlen(cursor),take=min(chars_per_line,remaining);
        if(remaining>chars_per_line){int split=take;while(split>0&&cursor[split]!=' ')--split;if(split>0)take=split;}cursor+=max(1,take);++lines;}
    return max(1,lines);
}

static int message_bubble_height(const UiMessage& message){return max(104,wrapped_line_count(message.text,23)*29+48);}

static void draw_message_bubble(const UiMessage& message,int y,int h) {
    const int x=message.outgoing?82:12,w=446;box(x,y,w,h,message.outgoing);
    draw_wrapped(message.text,x+16,y+12,23,3,message.outgoing?0xFF:0,true,8);
    char footer[26];const char* state="";
    if(message.outgoing){switch(message.state){case UiMessageState::Sending:state="SENDING";break;case UiMessageState::Sent:state="SENT";break;case UiMessageState::Delivered:state="DELIVERED";break;case UiMessageState::Failed:state="FAILED";break;case UiMessageState::Retrying1:state="RETRYING 1/5";break;case UiMessageState::Retrying2:state="RETRYING 2/5";break;case UiMessageState::Retrying3:state="RETRYING 3/5";break;case UiMessageState::Retrying4:state="RETRYING 4/5";break;case UiMessageState::Retrying5:state="RETRYING 5/5";break;default:break;}}
    snprintf(footer,sizeof(footer),"%s%s%s",message.time,state[0]?"  ":"",state);
    text(footer,x+w-(int)strlen(footer)*12-12,y+h-28,2,message.outgoing?0xFF:0,true);
}

static void chat_page_bounds(size_t count,int available,uint8_t requested,size_t& first,size_t& end,uint8_t& pages){
    size_t cursor=count;first=count;end=count;pages=0;uint8_t page=0;
    while(cursor>0){size_t candidate=cursor;int used=0;while(candidate>0){const int h=message_bubble_height(ui_data->active_message(candidate-1));const int needed=h+(used?8:0);if(used&&used+needed>available)break;used+=needed;--candidate;if(used>=available)break;}
        if(page==requested){first=candidate;end=cursor;}++pages;if(candidate==0)break;cursor=candidate;++page;
    }
    if(!count){first=end=0;pages=1;}else if(requested>=pages){chat_page=pages-1;chat_page_bounds(count,available,chat_page,first,end,pages);}
}

static void draw_chat(bool channel) {
    draw_app_header(ui_data?ui_data->active_title():(channel?"CHANNEL":"CONTACT"),true,channel?nullptr:"INFO");
    const size_t count=ui_data?ui_data->active_message_count():0;
    const bool keyboard=keyboard_visible&&keyboard_message_mode;const int history_bottom=keyboard?526:800;const int available=history_bottom-126;
    size_t first=0,end=0;uint8_t pages=1;const uint8_t requested=keyboard?0:chat_page;chat_page_bounds(count,available,requested,first,end,pages);
    if(!count)centred("NO MESSAGES YET",300,3,0,true);else{int y=126;for(size_t i=first;i<end;++i){const int h=message_bubble_height(ui_data->active_message(i));draw_message_bubble(ui_data->active_message(i),y,h);y+=h+8;}}
    if(keyboard){box(12,544,516,70);if(compose_text[0])draw_wrapped(compose_text,28,552,25,3,0,true,2);else text("Enter text",28,568,2,0,false);draw_keyboard();}
    else{
        if(pages>1){box(12,818,160,62,chat_page+1>=pages);text("OLDER",50,839,2,chat_page+1>=pages?0xFF:0,true);box(368,818,160,62,chat_page==0);text("NEWER",406,839,2,chat_page==0?0xFF:0,true);char p[18];snprintf(p,sizeof(p),"PAGE %u OF %u",chat_page+1,pages);centred(p,840,2,0,true);}
        box(12,888,516,62);text(compose_text[0]?compose_text:"TAP TO WRITE A MESSAGE",28,908,2,0,true);
    }
}

static void draw_contact_details() {
    draw_app_header("NODE INFO",true);UiNodeDetails node{};
    if(!ui_data||!ui_data->active_node_details(node)){centred("NODE DETAILS UNAVAILABLE",300,3,0,true);return;}
    centred(node.name,126,4,0,true);char page[20];snprintf(page,sizeof(page),"PAGE %u OF 2",details_page+1);centred(page,174,2,0,true);
    if(details_page==0){
        text("LAST ADVERT",24,230,2,0,true);
        text(node.advert_age,230,230,2);
        text("ROUTE",24,310,2,0,true);text(node.route,230,310,2);
        text("POSITION",24,380,2,0,true);
        draw_wrapped(node.position,230,380,25,2,0,false,2);
        // Source age and last-heard time differ: a successful info reply
        // does not make an older saved coordinate a fresh GPS fix.
        draw_wrapped(node.position_source,230,424,25,2,0,false,1);
        text("LAST HEARD",24,450,2,0,true);
        draw_wrapped(node.last_seen,230,450,25,2,0,false,1);
        text("IDENTITY",24,480,2,0,true);text(node.identity,230,480,2);
        if(node.latitude||node.longitude){
            box(24,540,492,62);centred("OPEN POSITION ON MAP",561,2,0,true);
        }
    }
    else{text("STATUS",24,230,2,0,true);draw_wrapped(node.status,24,264,39,2,0,false,2);text("TELEMETRY / POSITION",24,350,2,0,true);draw_wrapped(node.telemetry,24,384,39,2,0,false,4);text("DISCOVERED PATH",24,530,2,0,true);draw_wrapped(node.path,24,564,39,2,0,false,2);box(24,650,492,70,true);centred(node.request_active?"REQUESTING...":"REQUEST ALL INFO",674,3,0xFF,true);}
    // Action labels must be centered within THEIR OWN rectangles. centred()
    // centers over the entire 540px display and previously put both CHAT and
    // DELETE on top of each other, between two otherwise blank buttons.
    auto action_button=[](const char* label,int x,int y,int w,int h,bool selected=false) {
        box(x,y,w,h,selected);
        const int scale=2;
        const int label_width=(int)strlen(label)*6*scale;
        text(label,x+(w-label_width)/2,y+(h-7*scale)/2,scale,selected?0xFF:0,true);
    };
    action_button("PREV",24,738,220,58,details_page==0);
    action_button("NEXT",296,738,220,58,details_page==1);
    if(details_page==0){
        if(node.saved_contact){
            action_button("CHAT",24,808,240,70);
            action_button("DELETE",276,808,240,70);
        } else action_button("ADD CONTACT",24,808,492,70,true);
    }
}

static void settings_row(const char* title,const char* subtitle,int y);

static void draw_discovery() {
    draw_app_header("DISCOVERED",true);
    if(!ui_data||!ui_data->advert_count()){centred("NO ADVERTS HEARD",300,3,0,true);centred("SEND AN ADVERT OR WAIT",350,2);}
    else for(size_t i=0;i<ui_data->advert_count()&&i<5;++i)draw_list_entry(ui_data->advert(i),120+i*150);
}

static void draw_more() {
    draw_app_header("MORE");
    settings_row("DISCOVERED ADVERTS","RECENT NODES HEARD",130);settings_row("ADVERTISE","ZERO HOP OR FLOOD",260);
    settings_row("SETTINGS","DEVICE AND RADIO",390);settings_row("BLUETOOTH COMPANION","RESTART IN COMPANION MODE",520);
    settings_row("HELP","USING MESHINK",650);
    draw_bottom_nav(3);
}

static void draw_advert_menu() {
    draw_app_header("ADVERTISE",true);
    settings_row("ZERO HOP ADVERT","NEARBY NODES ONLY",180);settings_row("FLOOD ADVERT","SEND ACROSS THE MESH",320);
    draw_wrapped("Advertising shares this node identity using MeshCore radio settings.",24,500,39,2,0,true,4);
}

static void settings_row(const char* title,const char* subtitle,int y) {
    box(12,y,516,112);text(title,28,y+14,3,0,true);text(subtitle,28,y+58,2);text(">",494,y+42,3,0,true);
}

static void draw_settings() {
    draw_app_header("SETTINGS",true);
    settings_row("ID & RADIO",local_mesh_radio_summary(),118);
    settings_row("LOCATION & GPS","POSITION, INTERVAL, ADVERT",238);settings_row("PRIVACY","CONTACTS AND TELEMETRY",358);
    settings_row("DISPLAY & POWER","FRONTLIGHT, REFRESH, STANDBY",478);
    settings_row("ABOUT","FIRMWARE AND DEVICE INFO",598);
}

static void draw_radio_settings() {
    draw_app_header("ID & RADIO",true);settings_row("NODE NAME",node_name,120);
    settings_row("REGION PRESET",PRESETS[selected_preset].title,250);
    settings_row("ACTIVE RADIO",local_mesh_radio_summary(),380);settings_row("PATH HASH MODE",path_hash_label(),510);
    if(keyboard_visible){draw_keyboard();}
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
        case 1:return "GPS ONLY (TEST LOWER POWER)";
        case 3:return "GPS + BEIDOU";
        case 5:return "GPS + GLONASS";
        case 7:return "GPS + BEIDOU + GLONASS";
        default:return "UNCHANGED (CURRENT MODE)";
    }
}
static void draw_gps_tuning(){
    draw_app_header("GPS POWER SAVING",true);
    settings_row("CONSTELLATIONS",gps_constellation_label(),120);
    // Informational only: output is configured automatically, not selectable.
    box(12,238,516,112);text("NMEA OUTPUT",28,252,3,0,true);
    text("RMC + GGA (AUTOMATIC)",28,296,2,0,true);
    settings_row("TIMEZONE",TIMEZONES[timezone_index].label,356);
    draw_wrapped("GPS ONLY MAY LOWER RECEIVER LOAD, BUT MAY TAKE LONGER TO FIX. CHOOSE MORE SATELLITE SYSTEMS IF RECEPTION IS POOR.",24,515,45,2,0,true,5);
    draw_wrapped("COMPACT NMEA IS AUTOMATIC FOR L76K. CONSTELLATION POWER SAVINGS ARE UNMEASURED. GPS STAYS POWERED WHILE LORA IS ON.",24,700,45,2,0,true,4);
}

static void draw_timezone(){draw_app_header("TIMEZONE",true);for(uint8_t i=0;i<TIMEZONE_COUNT;++i){const int y=118+i*102;box(12,y,516,92,i==timezone_index);const uint8_t c=i==timezone_index?0xFF:0;text(TIMEZONES[i].label,28,y+10,3,c,true);text(TIMEZONES[i].detail,28,y+54,2,c,true);}}

static void draw_privacy_settings() {
    draw_app_header("PRIVACY",true);
    settings_row("AUTO ADD CONTACTS",local_mesh_privacy_value(0),130);settings_row("AUTO ADD MAX HOPS",local_mesh_privacy_value(1),248);
    settings_row("ADVERTISE LOCATION",local_mesh_privacy_value(2),366);settings_row("BASE TELEMETRY",local_mesh_privacy_value(3),484);
    settings_row("LOCATION TELEMETRY",local_mesh_privacy_value(4),602);settings_row("PACKET REPEATING",local_mesh_privacy_value(5),720);
}

static void draw_display_settings() {
    draw_app_header("DISPLAY & POWER",true);
    settings_row("MODE",frontlight_mode_name(),118);settings_row("LIGHT TIMEOUT",frontlight_timeout_name(),238);
    box(12,358,516,160);text("BRIGHTNESS",28,374,3,0,true);char level[8];snprintf(level,sizeof(level),"%u%%",frontlight_brightness);text(level,528-(int)strlen(level)*18-20,374,3,0,true);
    epd_fill_rect({62,464,416,5},0,fb);const int knob=62+(frontlight_brightness*416)/100;epd_fill_rect({knob-12,449,24,35},0,fb);text("-",28,452,3,0,true);text("+",492,452,3,0,true);
    settings_row("STANDBY TIMEOUT",standby_timeout_name(),538);
    settings_row("MAP SCALE",map_imperial?"IMPERIAL":"METRIC",656);
    const int shutdown_y=frontlight_mode==FrontlightMode::NightTimer?806:790;
    if(frontlight_mode==FrontlightMode::NightTimer){box(24,674,492,70,true);centred("NIGHT SCHEDULE",697,3,0xFF,true);}
    box(24,shutdown_y,492,70);centred("SHUT DOWN",shutdown_y+23,3,0,true);
}

static void draw_help() {
    draw_app_header("USING MESHINK",true);
    // Keep each heading close to its paragraph; 2x body text is the largest
    // size that fits the complete guide, including the final "UI.".
    text("QUICK SETTINGS",24,142,3,0,true);
    draw_wrapped("Swipe down from the top edge for front light brightness, advert flood and power off.",24,176,39,2,0,false,3);
    text("BOOT BUTTON",24,266,3,0,true);
    draw_wrapped("Short press refreshes the current screen. Hold for 2 seconds to lock screen and enter standby - hold boot button for 2 seconds to unlock",24,300,39,2,0,false,5);
    text("KEYBOARD",24,444,3,0,true);
    draw_wrapped("Message entry can be made easier using the landscape keyboard. Toggle it via LAND/portrait button.",24,478,39,2,0,false,4);
    text("MAPS",24,586,3,0,true);
    draw_wrapped("Pan and pinch zooming is supported, the screen will refresh on release. double tap to zoom in, triple tap to zoom out.",24,620,39,2,0,false,4);
    text("BLUETOOTH COMPANION MODE",24,728,3,0,true);
    draw_wrapped("Reboots to a special mode where you can connect any meshcore app to it and have full control. Reboot to return to the UI.",24,762,39,2,0,false,5);
}

static void draw_standby(){
    epd_hl_set_all_white(&display);draw_status_bar(true);centred("STANDBY",126,5,0,true);
    box(24,240,492,150);draw_envelope_icon(48,282);text("PRIVATE MESSAGES",100,266,3,0,true);char direct[12];snprintf(direct,sizeof(direct),"%u",status_unread);text(direct,100,318,4,0,true);
    box(24,420,492,150);text("#",48,464,4,0,true);text("CHANNEL MESSAGES",100,446,3,0,true);char channel[12];snprintf(channel,sizeof(channel),"%u",status_channel_unread);text(channel,100,498,4,0,true);
    centred("HOLD BOOT 2 SECONDS TO WAKE",820,2,0,true);
}

static void format_minutes(uint16_t minutes,char out[8]){snprintf(out,8,"%02u:%02u",minutes/60,minutes%60);}
static void draw_night_schedule(){
    draw_app_header("NIGHT SCHEDULE",true);char start[8],end[8];format_minutes(night_start_minutes,start);format_minutes(night_end_minutes,end);
    box(24,150,492,112,night_edit_field==0);text("START",42,166,3,night_edit_field==0?0xFF:0,true);text(start,360,166,3,night_edit_field==0?0xFF:0,true);
    box(24,286,492,112,night_edit_field==1);text("END",42,302,3,night_edit_field==1?0xFF:0,true);text(end,360,302,3,night_edit_field==1?0xFF:0,true);
    box(24,460,220,76);text("-30 MIN",62,486,3,0,true);box(296,460,220,76);text("+30 MIN",334,486,3,0,true);
    box(24,600,492,76,true);centred("SAVE SCHEDULE",626,3,0xFF,true);
    draw_wrapped("The selected timezone from GPS settings is used automatically.",24,740,39,2,0,true,3);
}

static void draw_meshink_logo(int top,bool compact=false) {
    // Use the original, build-time downscaled PNG rather than reconstructing
    // its mountain, forest, wordmark and quill with approximate geometry.
    // Both splash and About share the same 520x347 grayscale source image.
    (void)compact;
    constexpr uint8_t shades[4]={0x00,0x55,0xAA,0xFF};
    const int left=(540-MESHINK_LOGO_WIDTH)/2;
    for(int y=0;y<MESHINK_LOGO_HEIGHT;++y){
        const int row=y*MESHINK_LOGO_WIDTH;
        int x=0;
        while(x<MESHINK_LOGO_WIDTH){
            const int pixel=row+x;
            const uint8_t shade=(MESHINK_LOGO_PIXELS[pixel>>2]>>(6-2*(pixel&3)))&3;
            if(shade==3){++x;continue;} // already white
            const int run=x++;
            while(x<MESHINK_LOGO_WIDTH){
                const int next=row+x;
                if(((MESHINK_LOGO_PIXELS[next>>2]>>(6-2*(next&3)))&3)!=shade)break;
                ++x;
            }
            epd_fill_rect({left+run,top+y,x-run,1},shades[shade],fb);
        }
    }
}
static void draw_about() {
    draw_app_header("ABOUT",true);
    draw_meshink_logo(118,true);
    centred("Made by Samo",506,3,0,true);
    centred("github.com/samo-nz/mesh-ink",540,2,0,true);
    if(node_name[0])centred(node_name,600,3,0,true);
    centred(UI_VERSION,642,3,0,true);
    text("HARDWARE",24,720,2,0,true);text("LILYGO T5 PRO",170,720,2);
    text("CORE",24,770,2,0,true);text("MESHCORE " MESHCORE_RELEASE " (" MESHCORE_REVISION ")",170,770,2);
}

static void draw_screen();
static void refresh(EpdDrawMode mode,bool wake_light);
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh);

static void draw_underlying_screen() {
    // Render the current page normally, but without allowing its controls to
    // receive events while the quick sheet is open.
    const bool panel=quick_panel_active;
    quick_panel_active=false;
    draw_screen();
    quick_panel_active=panel;
}

static void draw_quick_panel() {
    // Keep the previous page visible below the sheet. E-paper has no alpha,
    // so this is a normal redraw followed by an opaque top overlay.
    draw_underlying_screen();

    // Visually disable the exposed page with a sparse 25% black dither.
    // One pixel in each 2x2 cell is darkened, preserving the page beneath
    // while making it clear that tapping it only dismisses quick settings.
    for(int y=QUICK_PANEL_BOTTOM;y<960;++y)
        for(int x=((y&1)?1:0);x<540;x+=2)
            if((y&1)==0) epd_draw_pixel(x,y,0x00,fb);

    epd_fill_rect({0,0,540,QUICK_PANEL_BOTTOM},0xFF,fb);
    epd_fill_rect({0,QUICK_PANEL_BOTTOM-4,540,4},0x00,fb);

    centred("QUICK SETTINGS",34,4,0,true);
    centred("FRONT LIGHT",92,3,0,true);

    // Full-width release-driven slider. The filled track and thumb show the
    // persisted brightness; dragging does not redraw until the finger lifts.
    const int track_y=QUICK_SLIDER_Y;
    const int track_w=QUICK_SLIDER_RIGHT-QUICK_SLIDER_LEFT;
    box(QUICK_SLIDER_LEFT,track_y-8,track_w,16);
    const int thumb_x=QUICK_SLIDER_LEFT+
        ((int)frontlight_brightness*track_w)/100;
    epd_fill_rect({QUICK_SLIDER_LEFT,track_y-5,
        max(1,thumb_x-QUICK_SLIDER_LEFT),10},0x00,fb);
    epd_fill_rect({max(QUICK_SLIDER_LEFT,thumb_x-7),track_y-18,14,36},0x00,fb);

    centred("TAP OR DRAG TO SELECT",221,2,0,true);

    // End controls and the current value share one row beneath the slider.
    box(24,256,112,70);
    // Draw the symbols as primitives so their visual centres are exact and
    // independent of font glyph metrics.
    epd_fill_rect({61,289,38,4},0x00,fb);
    box(404,256,112,70);
    epd_fill_rect({441,289,38,4},0x00,fb);
    epd_fill_rect({458,272,4,38},0x00,fb);
    char level[16];
    if(frontlight_brightness==0) snprintf(level,sizeof(level),"OFF");
    else snprintf(level,sizeof(level),"%u%%",(unsigned)frontlight_brightness);
    centred(level,273,4,0,true);

    box(24,410,238,100,true);
    text("ADVERT FLOOD",24+(238-12*12)/2,449,2,0xFF,true);
    box(278,410,238,100);
    text("POWER OFF",278+(238-9*12)/2,449,2,0,true);

    centred("TAP BELOW OR SWIPE UP TO CLOSE",560,2,0,true);
    draw_toast();
}

static void close_quick_panel() {
    if(!quick_panel_active)return;
    quick_panel_active=false;
    if(quick_panel_restore_landscape) {
        quick_panel_restore_landscape=false;
        keyboard_landscape=true;
        epd_set_rotation(EPD_ROT_LANDSCAPE);
    }
    draw_screen();refresh(MODE_GL16,true);
}

static void open_quick_panel() {
    if(quick_panel_active||standby_active)return;
    map_taps={};
    quick_panel_restore_landscape=keyboard_landscape;
    if(keyboard_landscape) {
        keyboard_landscape=false;
        epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    }
    quick_panel_active=true;
    draw_quick_panel();
    refresh(MODE_GL16,true);
}

static void quick_set_brightness(int value) {
    frontlight_brightness=(uint8_t)max(0,min(100,value));
    frontlight_mode=frontlight_brightness?FrontlightMode::On:FrontlightMode::Off;
    save_frontlight_settings();
    if(frontlight_brightness) frontlight_event();
    else { frontlight_drive(false);frontlight_deadline=0; }
    draw_quick_panel();
    refresh(MODE_DU,false);
}

static bool handle_quick_panel_tap(int16_t x,int16_t y,int16_t start_x=-1,int16_t start_y=-1) {
    if(!quick_panel_active)return false;
    // Once a gesture starts on the slider, always commit its final clamped X
    // on release. This includes releasing beyond either end or outside the
    // slider vertically, matching the live PWM preview seen during the drag.
    const bool slider_drag_release=
        start_x>=QUICK_SLIDER_LEFT-16&&start_x<=QUICK_SLIDER_RIGHT+16&&
        start_y>=146&&start_y<=226;
    const bool slider_tap_release=
        start_y<0&&y>=146&&y<=226&&
        x>=QUICK_SLIDER_LEFT-16&&x<=QUICK_SLIDER_RIGHT+16;
    if(slider_drag_release||slider_tap_release) {
        const int clamped=max(QUICK_SLIDER_LEFT,min(QUICK_SLIDER_RIGHT,(int)x));
        const int value=((clamped-QUICK_SLIDER_LEFT)*100+
            (QUICK_SLIDER_RIGHT-QUICK_SLIDER_LEFT)/2)/
            (QUICK_SLIDER_RIGHT-QUICK_SLIDER_LEFT);
        quick_set_brightness(value);
        return true;
    }

    // Everything below the sheet is intentionally inert except dismissal.
    if(y>=QUICK_PANEL_BOTTOM) { close_quick_panel();return true; }

    if(hit(x,y,24,256,112,70)) { quick_set_brightness((int)frontlight_brightness-1);return true; }
    if(hit(x,y,404,256,112,70)) { quick_set_brightness((int)frontlight_brightness+1);return true; }
    if(hit(x,y,24,410,238,100)) {
        show_toast(local_mesh_send_advert(true)?"SENDING FLOOD ADVERT":"ADVERT BUSY");
        draw_quick_panel();refresh(MODE_DU,true);return true;
    }
    if(hit(x,y,278,410,238,100)) {
        quick_panel_active=false;quick_panel_restore_landscape=false;
        keyboard_landscape=false;keyboard_visible=false;keyboard_message_mode=false;
        epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
        screen=Screen::ShutdownConfirm;draw_screen();refresh(MODE_GL16,true);return true;
    }
    return true;
}

static void draw_screen() {
    // Standby must take precedence over every transient/landscape UI layer.
    if(standby_active){draw_standby();return;}
    if(quick_panel_active){draw_quick_panel();return;}
    if(keyboard_landscape){draw_landscape_keyboard();return;}
    switch(screen){
        case Screen::Welcome:draw_welcome();break;case Screen::Presets:draw_presets();break;case Screen::CompanionConfirm:draw_companion_confirm();break;case Screen::ShutdownConfirm:draw_shutdown_confirm();break;
        case Screen::Contacts:draw_contacts();break;case Screen::ContactChat:draw_chat(false);break;case Screen::ContactDetails:draw_contact_details();break;
        case Screen::Channels:draw_channels();break;case Screen::ChannelChat:draw_chat(true);break;case Screen::Maps:draw_maps();break;case Screen::Discovery:draw_discovery();break;case Screen::More:draw_more();break;case Screen::AdvertMenu:draw_advert_menu();break;
        case Screen::Settings:draw_settings();break;case Screen::RadioSettings:draw_radio_settings();break;case Screen::GpsSettings:draw_gps_settings();break;case Screen::GpsTuning:draw_gps_tuning();break;case Screen::Timezone:draw_timezone();break;
        case Screen::PrivacySettings:draw_privacy_settings();break;case Screen::DisplaySettings:draw_display_settings();break;case Screen::NightSchedule:draw_night_schedule();break;case Screen::Help:draw_help();break;case Screen::About:draw_about();break;
    }
    const bool settings_page=screen==Screen::Settings||screen==Screen::RadioSettings||screen==Screen::GpsSettings||screen==Screen::GpsTuning||screen==Screen::Timezone||screen==Screen::PrivacySettings||screen==Screen::DisplaySettings||screen==Screen::NightSchedule||screen==Screen::Help||screen==Screen::About;
    if(screen==Screen::ContactDetails)draw_bottom_nav(details_from_discovery?3:0);
    else if(screen==Screen::Discovery||screen==Screen::AdvertMenu||settings_page)draw_bottom_nav(3);
    draw_toast();
}

static void refresh(EpdDrawMode mode,bool wake_light=true) {
    if(wake_light&&!standby_active)frontlight_event();
    // Maps contains only black and white pixels. Use the direct DU waveform
    // for normal updates; the 1.3.24 device test confirmed it prevents the
    // terrain fading seen after GC16. BOOT on Maps uses DU as well.
    const EpdDrawMode requested_mode=mode;
    const bool active_map=screen==Screen::Maps&&!standby_active&&!keyboard_landscape;
    if(active_map&&mode==MODE_GL16)mode=MODE_DU;
    set_cpu_target(240,"display-refresh",false);
    epd_poweron();
    const EpdDrawError err = epd_hl_update_screen(&display,mode,(int)epd_ambient_temperature());
    // The map stays clear when the panel is powered down as soon as EPDiy's
    // synchronous DU waveform completes. Do not reintroduce a powered hold.
    epd_poweroff();
    set_cpu_target(standby_active?80:160,"display-complete",false);
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] refresh=%d waveform=%d requested=%d screen=%d name='%s' preset=%s cpu=%luMHz\n",
        err,(int)mode,(int)requested_mode,(int)screen,node_name,PRESETS[selected_preset].title,(unsigned long)getCpuFrequencyMhz());
}

static void invalidate_display_back_buffer() {
    const size_t bytes=(size_t)epd_width()*epd_height()/2;
    for(size_t i=0;i<bytes;++i)display.back_fb[i]=(uint8_t)~display.front_fb[i];
}

static void force_redraw(EpdDrawMode mode,const char* reason,bool wake_light=false) {
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
        force_redraw(MODE_DU,reason,wake_light);
        return;
    }
    T5_DEBUGF(T5_LOG_UI,"[T5-EPD] GC16_FAST unavailable in ED047TC1 waveform; using GL16 reason=%s\n",reason);
    force_redraw(MODE_GL16,reason,wake_light);
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
            epd_hl_set_all_white(&display);
        draw_status_bar();
        draw_bottom_nav(2);
    }
    // When panning or zooming, fb still holds the visible previous map.
    // This overlay does not invalidate or replace the cached map background.
    draw_toast_message("Loading..");
    refresh(MODE_DU);

    // The previous map and toast stay on the panel while all tile I/O and
    // PNG decoding run synchronously. Refreshing the loading toast lowered
    // the CPU to 160 MHz; temporarily use the ESP32-S3's existing 240 MHz
    // display-performance setting for the CPU-heavy raster render. The
    // subsequent refresh restores 160 MHz through its normal power path.
    set_cpu_target(240,"map-render",false);
    draw_screen();

    // The old map under the dark toast retained balanced contrast, while
    // unchanged terrain became progressively too dark after repeated forced
    // map-to-map DU passes. Prepare the complete terrain with the SAME
    // black-to-map transition as the toast, then reveal the finished map with
    // one short-BOOT-style forced DU redraw. Keep the loading toast and old
    // map visible through all tile I/O; the black preparation is only after
    // the next frame is completely ready.
    epd_fill_rect({0,MAP_TOP,540,MAP_BOTTOM-MAP_TOP},0x00,fb);
    refresh(MODE_DU,false); // transient black prep; waveform completes, then power off
    // draw_screen() reuses the fully decoded map_base_cache for this same
    // view (including map overlays); no second PNG/PMTiles decode occurs.
    draw_screen();
    fast_full_redraw("MAP_BLACK_PREP_COMPLETE",false);
}

static void full_display_clean(const char* reason) {
    T5_DEBUGF(T5_LOG_UI,"[T5-EPD] full GC16 redraw requested by %s standby=%d\n",reason,standby_active);
    draw_screen();
    force_redraw(MODE_GC16,reason,false);
    T5_DEBUGLN(T5_LOG_UI,"[T5-EPD] full GC16 UI redraw complete; buffers synchronized");
}

static bool i2c_read(uint16_t reg, uint8_t* data, size_t len) {
    uint8_t address[2] = {(uint8_t)(reg>>8),(uint8_t)reg};
    return i2c_master_write_read_device(I2C_NUM_0,GT911_ADDR,address,2,data,len,pdMS_TO_TICKS(20)) == ESP_OK;
}
static bool i2c_read8(uint8_t device,uint8_t reg,uint8_t* data,size_t len) {
    return i2c_master_write_read_device(I2C_NUM_0,device,&reg,1,data,len,pdMS_TO_TICKS(20))==ESP_OK;
}
static bool i2c_write8(uint8_t device,uint8_t reg,uint8_t value) {
    const uint8_t data[2]={reg,value};
    return i2c_master_write_to_device(I2C_NUM_0,device,data,sizeof(data),pdMS_TO_TICKS(50))==ESP_OK;
}

static void recover_pmic_power_path() {
    constexpr uint8_t REG09=0x09,BATFET_DIS=1u<<5,BATFET_RST_EN=1u<<2;
    uint8_t value=0,address=0;
    for(const uint8_t candidate:{(uint8_t)0x6B,(uint8_t)0x6A})if(i2c_read8(candidate,REG09,&value,1)){address=candidate;break;}
    if(!address){Serial.println("[T5-POWER] boot PMIC recovery skipped: charger not detected");return;}
    if(!(value&BATFET_DIS)){T5_DEBUGF(T5_LOG_POWER,"[T5-POWER] boot PMIC address=0x%02X REG09=0x%02X battery path ready\n",address,value);return;}
    const uint8_t restored=(uint8_t)((value&~BATFET_DIS)|BATFET_RST_EN);
    const bool ok=i2c_write8(address,REG09,restored);
    Serial.printf("[T5-POWER] boot PMIC recovery address=0x%02X REG09 0x%02X->0x%02X result=%s\n",address,value,restored,ok?"OK":"FAILED");
    delay(150);
}

static void set_touch_power(bool enabled);

static void deep_sleep_shutdown(const char* reason) {
    Serial.printf("[T5-SHUTDOWN] entering deep-sleep fallback reason=%s wake=BOOT/GPIO0\n",reason);
    Serial.flush();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0,0);
    delay(50);
    esp_deep_sleep_start();
}

static void request_hardware_shutdown() {
    Serial.println("[T5-SHUTDOWN] user confirmed; preparing peripherals and persistent display");
    keyboard_visible=false;keyboard_message_mode=false;toast_visible=false;text_refresh_pending=false;
    epd_hl_set_all_white(&display);
    centred("POWERED OFF",250,6,0,true);
    centred("PRESS PWR BUTTON",370,4,0,true);
    centred("TO POWER ON",425,4,0,true);
    centred("IF STILL POWERED BY USB",560,3,0,true);
    centred("HOLD BOOT TO WAKE",610,3,0,true);
    centred(UI_VERSION,900,2,0,true);
    refresh(MODE_GL16,false);
    frontlight_deadline=0;frontlight_drive(false);
    set_touch_power(false);
    local_mesh_prepare_shutdown();
    SPIFFS.end();
    Serial.println("[T5-SHUTDOWN] message store closed; radio, GPS, touch and frontlight stopped");

    constexpr uint8_t REG09=0x09;
    constexpr uint8_t BATFET_DIS=1u<<5;
    constexpr uint8_t BATFET_DLY=1u<<3;
    constexpr uint8_t BATFET_RST_EN=1u<<2;
    uint8_t address=0,reg09=0;
    for(const uint8_t candidate:{(uint8_t)0x6B,(uint8_t)0x6A}) {
        if(i2c_read8(candidate,REG09,&reg09,1)){address=candidate;break;}
    }
    if(!address){Serial.println("[T5-SHUTDOWN] ERROR: BQ25896 not detected at 0x6B or 0x6A");deep_sleep_shutdown("PMIC_NOT_FOUND");return;}
    T5_DEBUGF(T5_LOG_POWER,"[T5-SHUTDOWN] PMIC detected address=0x%02X REG09 before=0x%02X\n",address,reg09);
    const uint8_t requested=(uint8_t)((reg09|BATFET_DIS|BATFET_RST_EN)&~BATFET_DLY);
    T5_DEBUGF(T5_LOG_POWER,"[T5-SHUTDOWN] preserving wake reset; REG09 request=0x%02X BATFET_DIS=1\n",requested);
    Serial.flush();
    if(!i2c_write8(address,REG09,requested)){Serial.println("[T5-SHUTDOWN] ERROR: REG09 write failed");deep_sleep_shutdown("PMIC_WRITE_FAILED");return;}
    delay(750);
    Serial.println("[T5-SHUTDOWN] PMIC command returned; USB/VBUS is probably present, using deep sleep until power is removed");
    deep_sleep_shutdown("VBUS_STILL_POWERED");
}
static uint8_t from_bcd(uint8_t value) { return (value>>4)*10+(value&0x0F); }
static bool update_status_hardware() {
    const int8_t old_hour=status_hour,old_minute=status_minute;
    const int16_t old_battery=status_battery;const uint8_t old_charge=status_charge_state;
    if(mesh_is_ready&&local_mesh_time_valid()){time_t now=(time_t)local_mesh_current_time();struct tm local{};localtime_r(&now,&local);if(local.tm_hour>=0&&local.tm_hour<24){status_hour=local.tm_hour;status_minute=local.tm_min;}}
    else{status_hour=-1;status_minute=-1;}
    uint8_t gauge[2]={};
    if(i2c_read8(0x55,0x2C,gauge,sizeof(gauge))){
        const uint16_t soc=(uint16_t)(gauge[0]|((uint16_t)gauge[1]<<8));
        if(soc<=100)status_battery=(int16_t)soc;
    }
    uint8_t charger=0;if(i2c_read8(0x6B,0x0B,&charger,1))status_charge_state=(charger>>3)&0x03;
    const bool clock_changed=standby_active?(old_hour!=status_hour||old_minute/10!=status_minute/10):(old_hour!=status_hour||old_minute!=status_minute);
    const bool battery_changed=standby_active?(old_battery/5!=status_battery/5):(old_battery!=status_battery);
    const bool changed=clock_changed||battery_changed||old_charge!=status_charge_state;
    if(changed)T5_DEBUGF(T5_LOG_UI,"[T5-UI] status clock=%02d:%02d battery=%d%% direct=%u channel=%u gps=%s\n",
        status_hour,status_minute,status_battery,status_unread,status_channel_unread,
        status_gps_enabled?(status_gps_fix?"fix":"searching"):"off");
    return changed;
}
static void clear_touch() {
    uint8_t data[3] = {0x81,0x4E,0};
    i2c_master_write_to_device(I2C_NUM_0,GT911_ADDR,data,3,pdMS_TO_TICKS(20));
}
static bool touch_point(int16_t& x, int16_t& y,bool& home) {
    uint8_t status=0;
    home=false;if (!i2c_read(0x814E,&status,1) || !(status&0x80)) { x=cached_touch_x; y=cached_touch_y; return was_pressed; }
    if(status&0x10){home=true;clear_touch();was_pressed=false;return true;}
    const uint8_t count=status&0x0F;
    if (!count || count>5) { clear_touch(); was_pressed=false; x=cached_touch_x; y=cached_touch_y; return false; }
    uint8_t p[8]={};
    if (!i2c_read(0x814F,p,sizeof(p))) return was_pressed;
    x=(int16_t)(p[1]|((uint16_t)p[2]<<8)); y=(int16_t)(p[3]|((uint16_t)p[4]<<8));
    cached_touch_x=x; cached_touch_y=y;
    clear_touch(); was_pressed=true; return true;
}

// Maps-only GT911 reader. Unlike touch_point() above, fetch BOTH of the
// controller's eight-byte coordinate records from 0x814F when count=2.
// No other screen or keyboard calls this function, and touch_point() remains
// byte-for-byte identical to the previously tested single-finger reader.
static uint8_t map_last_count=0;
static int16_t map_last_x0=0,map_last_y0=0,map_last_x1=0,map_last_y1=0;
static bool map_touch_points(uint8_t& count,int16_t& x0,int16_t& y0,
                             int16_t& x1,int16_t& y1,bool& home) {
    uint8_t status=0;
    home=false;
    if(!i2c_read(0x814E,&status,1))return false;
    if(!(status&0x80)) {
        count=map_last_count;x0=map_last_x0;y0=map_last_y0;
        x1=map_last_x1;y1=map_last_y1;
        return true; // no new frame; keep the previous press until release
    }
    if(status&0x10) {
        home=true;map_last_count=0;count=0;clear_touch();return true;
    }
    const uint8_t reported=status&0x0F;
    if(reported>5) {map_last_count=0;count=0;clear_touch();return true;}
    if(reported==0) {map_last_count=0;count=0;clear_touch();return true;}
    if(reported>2) {
        // Multi-contact outside supported two-point gesture: suppress any
        // accidental tap or pan until ALL fingers are lifted.
        map_last_count=3;count=3;clear_touch();return true;
    }
    uint8_t points[16]{};
    if(!i2c_read(0x814F,points,reported*8))return false;
    map_last_x0=(int16_t)(points[1]|((uint16_t)points[2]<<8));
    map_last_y0=(int16_t)(points[3]|((uint16_t)points[4]<<8));
    if(reported==2) {
        map_last_x1=(int16_t)(points[9]|((uint16_t)points[10]<<8));
        map_last_y1=(int16_t)(points[11]|((uint16_t)points[12]<<8));
    }
    map_last_count=reported;count=reported;
    x0=map_last_x0;y0=map_last_y0;x1=map_last_x1;y1=map_last_y1;
    clear_touch();
    return true;
}

static void touch_sampler_task(void*){
    bool held=false,home_held=false,map_previous=false;
    bool map_multi=false,map_pinch_allowed=false;
    int16_t start_x=0,start_y=0,last_x=0,last_y=0;
    int16_t pinch_x=0,pinch_y=0;
    int32_t initial_distance=0,final_distance=0;
    uint32_t pressed_at=0;
    for(;;){
        if(!touch_enabled){
            held=false;home_held=false;map_multi=false;map_previous=false;
            map_last_count=0;
            T5_DEBUGLN(T5_LOG_TOUCH,"[T5-POWER] touch sampler suspended");
            ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
            T5_DEBUGLN(T5_LOG_TOUCH,"[T5-POWER] touch sampler resumed");
            continue;
        }
        // Preserve the EXACT legacy single-touch parser and release-driven
        // typing behaviour on all screens except Maps.
        const bool on_map=screen==Screen::Maps&&!standby_active&&!keyboard_landscape;
        if(on_map!=map_previous) {
            held=false;home_held=false;map_multi=false;
            was_pressed=false;map_last_count=0;
            map_previous=on_map;
        }
        if(on_map) {
            uint8_t count=0;int16_t x0=0,y0=0,x1=0,y1=0;
            bool home=false;
            if(!map_touch_points(count,x0,y0,x1,y1,home)) {
                vTaskDelay(pdMS_TO_TICKS(8));continue;
            }
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
                        meshink_map_gestures::terrain_point(x0,y0) &&
                        meshink_map_gestures::terrain_point(x1,y1);
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
                    Serial.println("[T5-TOUCH] input queue full; pinch discarded");
            } else if(held) {
                held=false;
                QueuedTap tap{last_x,last_y,
                    (int16_t)(last_x-start_x),(int16_t)(last_y-start_y),false};
                tap.hold_ms=(uint16_t)min((uint32_t)65535,(uint32_t)(millis()-pressed_at));
                tap.map_sampled=1;
                if(xQueueSend(touch_queue,&tap,0)!=pdTRUE)
                    Serial.println("[T5-TOUCH] input queue full; tap discarded");
            }
        } else {
            // Original non-Maps sampling and release logic is unchanged.
            int16_t x=0,y=0;bool home=false;
            const bool pressed=touch_point(x,y,home);
            if(home){
                if(!home_held){QueuedTap tap{0,0,0,0,true};xQueueSend(touch_queue,&tap,0);}
                home_held=true;
                held=false;
            }else if(home_held){
                if(!pressed)home_held=false;
            }else if(pressed){
                last_x=x;last_y=y;
                if(!held){
                    held=true;start_x=x;start_y=y;frontlight_event();
                    quick_slider_dragging=quick_panel_active&&
                        y>=146&&y<=226&&x>=QUICK_SLIDER_LEFT-16&&x<=QUICK_SLIDER_RIGHT+16;
                }
                if(quick_slider_dragging) {
                    const int clamped=max(QUICK_SLIDER_LEFT,min(QUICK_SLIDER_RIGHT,(int)x));
                    const int value=((clamped-QUICK_SLIDER_LEFT)*100+
                        (QUICK_SLIDER_RIGHT-QUICK_SLIDER_LEFT)/2)/
                        (QUICK_SLIDER_RIGHT-QUICK_SLIDER_LEFT);
                    quick_slider_preview=(uint8_t)max(0,min(100,value));
                    frontlight_preview(quick_slider_preview);
                }
            }else if(held){
                held=false;
                quick_slider_dragging=false;
                QueuedTap tap{last_x,last_y,(int16_t)(last_x-start_x),(int16_t)(last_y-start_y),false};
                if(xQueueSend(touch_queue,&tap,0)!=pdTRUE)
                    Serial.println("[T5-TOUCH] input queue full; tap discarded");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

static bool legal_name_character(char c) { return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
static void cycle_keyboard_mode(){
    if(keyboard_symbols){keyboard_symbols=false;keyboard_upper=true;}
    else if(keyboard_upper)keyboard_upper=false;
    else keyboard_symbols=true;
}
static void append(char c) {
    if(keyboard_message_mode){size_t n=strlen(compose_text);if(n<48){compose_text[n]=c;compose_text[n+1]=0;}return;}
    if(!legal_name_character(c)){Serial.printf("[T5-UI] discarded illegal name character 0x%02X\n",(unsigned char)c);return;}
    if (replace_name_on_type) { node_name[0]=0; replace_name_on_type=false; }
    size_t n=strlen(node_name); if (n<20) { node_name[n]=c; node_name[n+1]=0; saved=false; }
}
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh) { return x>=bx&&x<bx+bw&&y>=by&&y<by+bh; }

static void open_screen(Screen next,bool preserve_map_centre=false) {
    // Opening Maps normally goes to our current or last known GPS location.
    // A node's explicit "OPEN POSITION ON MAP" uses preserve_map_centre=true
    // and therefore does not get unexpectedly re-centred on the device.
    if(next==Screen::Maps&&screen!=Screen::Maps&&!preserve_map_centre)
        centre_map_on_device();
    const bool already_on_map=screen==Screen::Maps;
    map_taps={}; // prevent a pending map double tap from firing on another UI
    keyboard_visible=false;keyboard_message_mode=false;screen=next;
    if(next==Screen::Maps) {
        load_map_with_feedback(already_on_map);
        return;
    }
    draw_screen();refresh(MODE_GL16);
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
        map_latitude,map_longitude,map_zoom,next_zoom,anchor_x,anchor_y);
    map_latitude=centre.latitude;
    map_longitude=centre.longitude;
    map_zoom=(uint8_t)next_zoom;
    map_taps={};
    open_screen(Screen::Maps);
}

static void persist_unread(){Preferences state;if(state.begin("t5-ui",false)){state.putUShort("unread_dm",status_unread);state.putUShort("unread_ch",status_channel_unread);state.end();}}
static void queue_text_refresh(){text_refresh_pending=true;text_refresh_after=millis()+110;}
static void set_keyboard_orientation(bool landscape){
    keyboard_landscape=landscape;
    epd_set_rotation(landscape?EPD_ROT_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] keyboard orientation=%s\n",landscape?"landscape":"portrait");
    draw_screen();refresh(MODE_GL16);
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
        epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    }
    keyboard_visible=false;keyboard_message_mode=false;
    replace_name_on_type=false;
    text_refresh_pending=false;toast_visible=false;toast_opens_main=false;
    status_dirty=false;status_wake_light=false;
    if(touch_queue)xQueueReset(touch_queue);
    clear_touch();
    screen=Screen::Contacts;
    draw_screen();
    fast_full_redraw("FIRST_CONTACTS_AFTER_SETUP",true);
}
static bool handle_landscape_keyboard(int16_t raw_x,int16_t raw_y){
    if(!keyboard_landscape)return false;
    const int16_t x=raw_y,y=539-raw_x;
    T5_DEBUGF(T5_LOG_TOUCH,"[T5-UI] landscape tap raw=%d,%d mapped=%d,%d\n",raw_x,raw_y,x,y);
    // Give the mode and Delete buttons the full third-row edge areas.
    if(meshink_keyboard::in_row(y,355)){
        if(x<149){cycle_keyboard_mode();draw_screen();refresh(MODE_DU);return true;}
        if(x>=805){
            char* value=keyboard_message_mode?compose_text:node_name;
            const size_t n=strlen(value);
            if(n)value[n-1]=0;
            if(!keyboard_message_mode)saved=false;
            queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,true,character)){
        append(character);queue_text_refresh();return true;
    }
    if(meshink_keyboard::in_row(y,425)){
        if(x<199){set_keyboard_orientation(false);return true;}
        if(x<707){append(' ');queue_text_refresh();return true;}
        if(keyboard_message_mode){
            if(compose_text[0]&&local_mesh_send_active(compose_text))compose_text[0]=0;
        }
        // DONE in landscape is only an orientation switch for name entry.
        // Return to portrait setup with radio preset still available; only
        // the portrait SAVE button can persist the name and complete setup.
        keyboard_visible=true;set_keyboard_orientation(false);
        return true;
    }
    return true;
}

static bool handle_message_keyboard(int16_t x,int16_t y) {
    if(!keyboard_visible||!keyboard_message_mode)return false;
    if(meshink_keyboard::in_row(y,828)){
        if(x<91){cycle_keyboard_mode();draw_screen();refresh(MODE_DU);return true;}
        if(x>=457){
            const size_t n=strlen(compose_text);
            if(n)compose_text[n-1]=0;
            queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,false,character)){
        append(character);queue_text_refresh();return true;
    }
    if(y>=894&&y<960){
        // Extend each action into half of its neighbouring gap.
        if(x<116){set_keyboard_orientation(true);return true;}
        if(x<314){append(' ');queue_text_refresh();return true;}
        if(x<422){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return true;}
        if(compose_text[0]){
            const bool ok=local_mesh_send_active(compose_text);
            if(ok){compose_text[0]=0;keyboard_visible=false;}
            draw_screen();refresh(MODE_DU);
        }
        return true;
    }
    return true;
}

static bool handle_name_keyboard(int16_t x,int16_t y){
    if(!keyboard_visible||keyboard_message_mode)return false;
    if(meshink_keyboard::in_row(y,828)){
        if(x<91){cycle_keyboard_mode();draw_screen();refresh(MODE_DU);return true;}
        if(x>=457){
            const size_t n=strlen(node_name);
            if(n)node_name[n-1]=0;
            saved=false;queue_text_refresh();return true;
        }
    }
    char character=0;
    if(keyboard_character_at(x,y,false,character)){
        append(character);queue_text_refresh();return true;
    }
    if(y>=894&&y<960){
        if(x<116){set_keyboard_orientation(true);return true;}
        if(x<314)return true; // node names cannot contain spaces
        if(x<422){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return true;}
        const bool was_setup=screen==Screen::Welcome;
        save_node_name();
        if(was_setup){
            show_contacts_after_setup();
        }else{
            keyboard_visible=false;
            show_toast("IDENTITY SAVED");
            draw_screen();refresh(MODE_DU);
        }
        return true;
    }
    return true;
}

static bool handle_app_tap(int16_t x,int16_t y) {
    if(screen==Screen::Welcome||screen==Screen::Presets||screen==Screen::CompanionConfirm||screen==Screen::ShutdownConfirm)return false;
    if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&hit(x,y,0,48,110,70)){keyboard_visible=false;keyboard_message_mode=false;chat_page=0;open_screen(screen==Screen::ChannelChat?Screen::Channels:Screen::Contacts);return true;}
    if(screen==Screen::ContactChat&&hit(x,y,450,48,90,70)){keyboard_visible=false;keyboard_message_mode=false;details_from_discovery=false;details_page=0;open_screen(Screen::ContactDetails);return true;}
    if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&handle_message_keyboard(x,y))return true;
    if(screen==Screen::RadioSettings&&keyboard_visible&&handle_name_keyboard(x,y))return true;
    if(screen!=Screen::ContactChat&&screen!=Screen::ChannelChat&&y>=900){const int tab=min(3,max(0,(int)x/135));open_screen(tab==0?Screen::Contacts:tab==1?Screen::Channels:tab==2?Screen::Maps:Screen::More);return true;}
    switch(screen){
        case Screen::Contacts:
            if(ui_data){
                const size_t count=ui_data->contact_count();
                clamp_list_page(contacts_page,count);
                const size_t first=contacts_page*LIST_ITEMS_PER_PAGE;
                for(size_t row=0;row<LIST_ITEMS_PER_PAGE&&first+row<count;++row){
                    const size_t index=first+row;
                    if(hit(x,y,12,120+row*150,516,142)){selected_contact=index;if(ui_data->open_contact(index)){status_unread=local_mesh_direct_unread_total();persist_unread();chat_page=0;open_screen(Screen::ContactChat);}return true;}
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
                    if(hit(x,y,12,120+row*150,516,142)){selected_channel=index;if(ui_data->open_channel(index)){status_channel_unread=local_mesh_channel_unread_total();persist_unread();chat_page=0;open_screen(Screen::ChannelChat);}return true;}
                }
            }
            break;
        case Screen::ContactChat:
            if(!keyboard_visible&&hit(x,y,12,818,160,62)){chat_page++;draw_screen();refresh(MODE_GL16);return true;}
            if(!keyboard_visible&&hit(x,y,368,818,160,62)&&chat_page>0){chat_page--;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,12,888,516,62)){keyboard_message_mode=true;keyboard_visible=true;chat_page=0;draw_screen();refresh(MODE_GL16);return true;}break;
        case Screen::ChannelChat:
            if(!keyboard_visible&&hit(x,y,12,818,160,62)){chat_page++;draw_screen();refresh(MODE_GL16);return true;}
            if(!keyboard_visible&&hit(x,y,368,818,160,62)&&chat_page>0){chat_page--;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,12,888,516,62)){keyboard_message_mode=true;keyboard_visible=true;chat_page=0;draw_screen();refresh(MODE_GL16);return true;}break;
        case Screen::ContactDetails:
            if(hit(x,y,0,48,90,70)){open_screen(details_from_discovery?Screen::Discovery:Screen::ContactChat);return true;}
            {UiNodeDetails node{};if(ui_data&&ui_data->active_node_details(node)){
                if(hit(x,y,24,738,220,58)&&details_page>0){details_page--;draw_screen();refresh(MODE_GL16);return true;}
                if(hit(x,y,296,738,220,58)&&details_page<1){details_page++;draw_screen();refresh(MODE_GL16);return true;}
                if(details_page==1&&hit(x,y,24,650,492,70)){show_toast(ui_data->request_active_node_info()?"REQUESTING ALL INFO":"REQUEST BUSY");draw_screen();refresh(MODE_DU);return true;}
                if(details_page==0&&(node.latitude||node.longitude)&&hit(x,y,24,540,492,62)){map_latitude=node.latitude/1000000.0;map_longitude=node.longitude/1000000.0;open_screen(Screen::Maps,true);return true;}
                if(details_page==0&&node.saved_contact&&hit(x,y,24,808,240,70)){open_screen(Screen::ContactChat);return true;}
                if(details_page==0&&node.saved_contact&&hit(x,y,276,808,240,70)){show_toast(ui_data->remove_active_contact()?"CONTACT REMOVED":"REMOVE FAILED");draw_screen();refresh(MODE_DU);return true;}
                if(details_page==0&&!node.saved_contact&&hit(x,y,24,808,492,70)){show_toast(ui_data->add_active_node()?"CONTACT ADDED":"ADD FAILED");draw_screen();refresh(MODE_DU);return true;}
            }}break;
        case Screen::Maps:
            // Controls take priority over map markers near the right edge.
            if(hit(x,y,462,58,66,66)){if(map_zoom<18){map_zoom++;open_screen(Screen::Maps);}return true;}
            if(hit(x,y,462,133,66,66)){if(map_zoom>8){map_zoom--;open_screen(Screen::Maps);}return true;}
            if(hit(x,y,462,208,66,66)){
                long latitude=0,longitude=0;bool current_fix=false;
                if(map_device_position(latitude,longitude,current_fix)){
                    centre_map_on_device();
                    show_toast(current_fix?"CENTRED ON DEVICE":"CENTRED ON LAST FIX");
                    open_screen(Screen::Maps);
                }else{
                    show_toast("NO KNOWN GPS LOCATION");
                    draw_screen();refresh(MODE_DU);
                }
                return true;
            }
            for(size_t i=0;i<map_marker_hit_count;++i) {
                const auto& marker=map_marker_hits[i];
                if(abs(x-marker.x)<=10&&abs(y-marker.y)<=10&&ui_data&&ui_data->open_map_node(marker.index)) {
                    details_from_discovery=false;details_page=0;open_screen(Screen::ContactDetails);return true;
                }
            }
            break;
        case Screen::Discovery:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::More);return true;}
            if(ui_data)for(size_t i=0;i<ui_data->advert_count()&&i<5;++i)if(hit(x,y,12,120+i*150,516,142)){if(ui_data->open_advert(i)){details_from_discovery=true;details_page=0;open_screen(Screen::ContactDetails);}return true;}break;
        case Screen::More:
            if(hit(x,y,12,130,516,112)){open_screen(Screen::Discovery);return true;}
            if(hit(x,y,12,260,516,112)){open_screen(Screen::AdvertMenu);return true;}
            if(hit(x,y,12,390,516,112)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,520,516,112)){open_screen(Screen::CompanionConfirm);return true;}
            if(hit(x,y,12,650,516,112)){open_screen(Screen::Help);return true;}break;
        case Screen::AdvertMenu:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::More);return true;}
            if(hit(x,y,12,180,516,112)){show_toast(local_mesh_send_advert(false)?"SENDING ZERO HOP ADVERT":"ADVERT BUSY");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,320,516,112)){show_toast(local_mesh_send_advert(true)?"SENDING FLOOD ADVERT":"ADVERT BUSY");draw_screen();refresh(MODE_DU);return true;}break;
        case Screen::Settings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::More);return true;}
            if(hit(x,y,12,118,516,112)){open_screen(Screen::RadioSettings);return true;}
            if(hit(x,y,12,238,516,112)){open_screen(Screen::GpsSettings);return true;}
            if(hit(x,y,12,358,516,112)){open_screen(Screen::PrivacySettings);return true;}
            if(hit(x,y,12,478,516,112)){open_screen(Screen::DisplaySettings);return true;}
            if(hit(x,y,12,598,516,112)){open_screen(Screen::About);return true;}break;
        case Screen::RadioSettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,120,516,112)){replace_name_on_type=true;keyboard_message_mode=false;keyboard_visible=true;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,12,250,516,112)){preset_return_screen=Screen::RadioSettings;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,12,510,516,112)){local_mesh_cycle_path_hash();show_toast("PATH MODE SAVED");draw_screen();refresh(MODE_DU);return true;}
            return true;
        case Screen::GpsSettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,120,516,112)){local_mesh_apply_gps(!local_mesh_gps_enabled());show_toast(local_mesh_gps_enabled()?"GPS ENABLED":"GPS DISABLED");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,356,516,112)){
                if(centre_map_on_device())open_screen(Screen::Maps,true);
                else{show_toast("NO KNOWN GPS LOCATION");draw_screen();refresh(MODE_DU);}
                return true;
            }
            if(hit(x,y,12,474,516,112)){local_mesh_cycle_gps_interval();show_toast("GPS INTERVAL SAVED");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,592,516,112)){local_mesh_toggle_gps_advert_location();show_toast(local_mesh_gps_advert_location()?"POSITION SHARED":"POSITION HIDDEN");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,710,516,112)){open_screen(Screen::GpsTuning);return true;}break;
        case Screen::GpsTuning:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::GpsSettings);return true;}
            if(hit(x,y,12,120,516,112)){
                const uint8_t mode=local_mesh_gps_constellation_mode();
                const uint8_t next=mode==0?1:mode==1?5:mode==5?3:mode==3?7:1;
                show_toast(local_mesh_gps_set_constellation_mode(next)?"MODE SAVED":"SAVE FAILED");
                draw_screen();refresh(MODE_DU);return true;
            }
            // NMEA output is automatic; this informational row has no action.
            if(hit(x,y,12,356,516,112)){open_screen(Screen::Timezone);return true;}break;
        case Screen::Timezone:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::GpsTuning);return true;}
            for(uint8_t i=0;i<TIMEZONE_COUNT;++i)if(hit(x,y,12,118+i*102,516,92)){timezone_index=i;apply_timezone();prefs.begin("t5-ui",false);prefs.putUChar("timezone",timezone_index);prefs.end();show_toast("TIMEZONE SAVED");draw_screen();refresh(MODE_GL16);return true;}break;
        case Screen::PrivacySettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            for(uint8_t i=0;i<6;++i)if(hit(x,y,12,130+i*118,516,112)){local_mesh_toggle_privacy(i);show_toast("SETTING SAVED");draw_screen();refresh(MODE_DU);return true;}return true;
        case Screen::DisplaySettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,118,516,112)){frontlight_mode=(FrontlightMode)(((uint8_t)frontlight_mode+1)%3);save_frontlight_settings();if(frontlight_mode==FrontlightMode::Off)frontlight_drive(false);else frontlight_event();show_toast(frontlight_mode_name());draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,238,516,112)){frontlight_timeout_index=(frontlight_timeout_index+1)%5;save_frontlight_settings();frontlight_event();show_toast(frontlight_timeout_name());draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,40,420,460,100)){int value=((int)x-62)*100/416;frontlight_brightness=(uint8_t)min(100,max(1,value));save_frontlight_settings();frontlight_event();T5_DEBUGF(T5_LOG_UI,"[T5-LIGHT] brightness=%u%%\n",frontlight_brightness);draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,538,516,112)){standby_timeout_index=(standby_timeout_index+1)%4;save_frontlight_settings();last_user_activity=millis();show_toast(standby_timeout_name());draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,656,516,112)){map_imperial=!map_imperial;prefs.begin("t5-ui",false);prefs.putBool("map_imperial",map_imperial);prefs.end();show_toast(map_imperial?"IMPERIAL SCALE":"METRIC SCALE");draw_screen();refresh(MODE_DU);return true;}
            if(frontlight_mode==FrontlightMode::NightTimer&&hit(x,y,24,790,492,70)){open_screen(Screen::NightSchedule);return true;}
            if(hit(x,y,24,frontlight_mode==FrontlightMode::NightTimer?806:790,492,70)){open_screen(Screen::ShutdownConfirm);return true;}break;
        case Screen::NightSchedule:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::DisplaySettings);return true;}
            if(hit(x,y,24,150,492,112)){night_edit_field=0;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,286,492,112)){night_edit_field=1;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,460,220,76)){uint16_t& value=night_edit_field ? night_end_minutes : night_start_minutes;value=(value+1410)%1440;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,296,460,220,76)){uint16_t& value=night_edit_field ? night_end_minutes : night_start_minutes;value=(value+30)%1440;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,600,492,76)){save_frontlight_settings();frontlight_event();show_toast("SCHEDULE SAVED");draw_screen();refresh(MODE_DU);return true;}break;
        case Screen::Help:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::More);return true;}break;
        case Screen::About:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}break;
        default:break;
    }
    return true;
}

static void handle_tap(int16_t x,int16_t y) {
    last_user_activity=millis();
    T5_DEBUGF(T5_LOG_TOUCH,"[T5-UI] tap x=%d y=%d\n",x,y);
    if(handle_quick_panel_tap(x,y))return;
    if(handle_landscape_keyboard(x,y))return;
    if(handle_app_tap(x,y))return;
    if(screen==Screen::Presets) {
        if(y>=48&&y<132){screen=preset_return_screen;draw_screen();refresh(MODE_GL16);return;}
        for(int row=0;row<PRESETS_PER_PAGE;++row) if(hit(x,y,12,132+row*128,516,112)){
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
                    // Do not highlight or persist a radio preset that failed.
                    selected_preset=previous;
                }
                screen=preset_return_screen;
                show_toast(applied?(selected_preset==0?"RADIO KEPT UNCHANGED":"RADIO PRESET APPLIED"):"PRESET FAILED");
                draw_screen();refresh(MODE_GL16);
            }
            return;
        }
        const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
        if(hit(x,y,24,800,180,62)&&preset_page>0){preset_page--;draw_screen();refresh(MODE_GL16);return;}
        if(hit(x,y,336,800,180,62)&&preset_page+1<page_count){preset_page++;draw_screen();refresh(MODE_GL16);return;}
        return;
    }
    if(screen==Screen::CompanionConfirm){
        if(hit(x,y,30,500,220,72)){screen=setup_complete?Screen::More:Screen::Welcome;draw_screen();refresh(MODE_DU);return;}
        if(hit(x,y,290,500,220,72)){T5_DEBUGLN(T5_LOG_UI,"[T5-UI] companion mode confirmed");request_companion_mode();return;}
        return;
    }
    if(screen==Screen::ShutdownConfirm){
        if(hit(x,y,30,650,220,72)){open_screen(Screen::DisplaySettings);return;}
        if(hit(x,y,290,650,220,72)){request_hardware_shutdown();return;}
        return;
    }
    if(hit(x,y,30,180,480,64)){replace_name_on_type=true;keyboard_visible=true;T5_DEBUGLN(T5_LOG_UI,"[T5-UI] name selected; keyboard shown; next character replaces current name");draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,24,292,492,88)){preset_return_screen=Screen::Welcome;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MODE_GL16);return;}
    if(hit(x,y,30,402,480,52)){screen=Screen::CompanionConfirm;draw_screen();refresh(MODE_GL16);return;}
    if(!keyboard_visible){if(hit(x,y,30,840,480,64)){keyboard_visible=true;draw_screen();refresh(MODE_GL16);}return;}
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
    touch_enabled=false;delay(20);was_pressed=false;
    if(enabled){pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);pinMode(TOUCH_INT,OUTPUT);digitalWrite(TOUCH_INT,LOW);delay(10);digitalWrite(TOUCH_RST,HIGH);delay(60);pinMode(TOUCH_INT,INPUT);clear_touch();touch_enabled=true;if(touch_task_handle)xTaskNotifyGive(touch_task_handle);T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] touch controller enabled");}
    else{pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] touch controller disabled");}
}

static void enter_standby(const char* reason){
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
        epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    }
    standby_active=true;text_refresh_pending=false;toast_visible=false;frontlight_deadline=0;frontlight_drive(false);
    T5_DEBUGF(T5_LOG_POWER,"[T5-STANDBY] entering reason=%s timeout=%s\n",reason,standby_timeout_name());draw_screen();fast_full_redraw("ENTER_STANDBY",false);set_touch_power(false);if(touch_queue)xQueueReset(touch_queue);set_cpu_target(80,"standby");
}

static void leave_standby(){
    if(!standby_active)return;set_touch_power(true);standby_active=false;last_user_activity=millis();message_alert_active=false;ledcWrite(FRONTLIGHT_PWM_CHANNEL,0);frontlight_lit=false;
    if(standby_restore_landscape){
        standby_restore_landscape=false;
        keyboard_landscape=true;
        epd_set_rotation(EPD_ROT_LANDSCAPE);
    } else {
        epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    }
    set_cpu_target(160,"wake");T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] leaving; restoring local UI");draw_screen();fast_full_redraw("LEAVE_STANDBY",true);
}

static void start_message_alert(){
    const uint32_t now=millis();
    if(message_alert_active){T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] message alert coalesced into active sequence");return;}
    if((int32_t)(now-message_alert_cooldown_until)<0){T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] message alert suppressed by cooldown");return;}
    message_alert_active=true;message_alert_phase=0;message_alert_deadline=now;
    T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] combined EPD/frontlight message alert started");
}

static void service_message_alert(){
    if(!message_alert_active||(int32_t)(millis()-message_alert_deadline)<0)return;
    switch(message_alert_phase++){
        case 0:
        case 2:
            ledcWrite(FRONTLIGHT_PWM_CHANNEL,0);frontlight_lit=false;
            memset(display.front_fb,0x00,(size_t)epd_width()*epd_height()/2);
            force_redraw(MODE_DU,"MESSAGE_ALERT_BLACK",false);
            message_alert_deadline=millis()+100;
            break;
        case 1:
        case 3:
            ledcWrite(FRONTLIGHT_PWM_CHANNEL,255);frontlight_lit=true;
            epd_hl_set_all_white(&display);
            force_redraw(MODE_DU,"MESSAGE_ALERT_WHITE",false);
            message_alert_deadline=millis()+100;
            break;
        default:
            ledcWrite(FRONTLIGHT_PWM_CHANNEL,0);frontlight_lit=false;frontlight_deadline=0;
            draw_screen();force_redraw(MODE_GC16,"MESSAGE_ALERT_RESTORE",false);
            status_dirty=false;message_alert_active=false;message_alert_cooldown_until=millis()+3000;
            T5_DEBUGLN(T5_LOG_POWER,"[T5-STANDBY] combined message alert complete; standby screen restored with GC16");
            break;
    }
}

static void service_boot_button(){
    static uint32_t pressed_at=0;static bool handled=false;const bool pressed=digitalRead(BOOT_BUTTON)==LOW;
    if(pressed&&!pressed_at)pressed_at=millis();
    if(pressed&&!handled&&pressed_at&&millis()-pressed_at>=2000){handled=true;if(standby_active)leave_standby();else enter_standby("BOOT");}
    if(!pressed&&pressed_at){const uint32_t duration=millis()-pressed_at;if(!handled&&duration>=40){
        // Short BOOT refresh is deliberately disabled in standby. Waking from
        // standby requires the existing two-second hold, avoiding needless EPD
        // refreshes from accidental short presses.
        if(!standby_active){draw_screen();fast_full_redraw("SHORT_BOOT_REFRESH",false);}
    }pressed_at=0;handled=false;}
}

void ui_setup() {
    Serial.begin(115200); delay(200);
    T5_DEBUGF(T5_LOG_UI,"[T5-UI] onboarding %s boot heap=%u psram=%u; Bluetooth disabled\n",UI_VERSION,ESP.getFreeHeap(),ESP.getFreePsram());
    pinMode(BOOT_BUTTON,INPUT_PULLUP);pinMode(FRONTLIGHT,OUTPUT);digitalWrite(FRONTLIGHT,LOW);
    // Stay off until preferences have been loaded. The splash refresh then
    // uses the saved brightness or the new 30% first-install default.
    ledcSetup(FRONTLIGHT_PWM_CHANNEL,5000,8);ledcAttachPin(FRONTLIGHT,FRONTLIGHT_PWM_CHANNEL);ledcWrite(FRONTLIGHT_PWM_CHANNEL,0);
    pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);pinMode(TOUCH_INT,OUTPUT);digitalWrite(TOUCH_INT,LOW);
    epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);epd_set_lcd_pixel_clock_MHz(17);
    recover_pmic_power_path();
    delay(10);digitalWrite(TOUCH_RST,HIGH);delay(60);pinMode(TOUCH_INT,INPUT);
    display=epd_hl_init(EPD_BUILTIN_WAVEFORM);fb=epd_hl_get_framebuffer(&display);
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
        for(size_t i=0;i<saved_name.length()&&out<20;++i){const char c=saved_name[i];if(legal_name_character(c))node_name[out++]=c;else Serial.printf("[T5-UI] discarded stored illegal name character 0x%02X\n",(unsigned char)c);}
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
        Serial.printf("[T5-BOOT] generated first-setup device name: %s\n",node_name);
    }
    if(setup_complete){screen=Screen::Contacts;keyboard_visible=false;}
    update_status_hardware();
    epd_hl_set_all_white(&display);
    draw_meshink_logo(160,false);
    // Keep the original logo visible throughout MeshCore startup. Storage
    // is normally already mounted, so use the generic boot status by default.
    // local_mesh_setup() changes it only if SPIFFS fails to mount and must
    // attempt first-time initialization/recovery.
    centred("STARTING UP...",716,3,0,true);
    if(node_name[0])centred(node_name,830,3,0,true);
    centred(UI_VERSION,885,2,0,true);
    epd_poweron();epd_clear();epd_poweroff();refresh(MODE_GL16);
    T5_DEBUGLN(T5_LOG_UI,"[T5-BOOT] splash visible; starting storage and mesh initialization");
}

void ui_show_storage_initializing() {
    // Called only after a non-formatting mount fails. Update the existing
    // splash before SPIFFS.begin(true) may block while preparing storage.
    epd_fill_rect({0,704,540,65},0xFF,fb);
    centred("INITIALISING STORAGE...",716,3,0,true);
    refresh(MODE_GL16);
    Serial.println("[T5-BOOT] splash: initialising storage after SPIFFS mount failed");
}

void ui_finish_startup() {
    if(hardware_failure)return;
    // Drop any touch points that accumulated during the non-interactive
    // splash, then show the correct initial setup or existing-user screen.
    clear_touch();
    draw_screen();
    if(screen==Screen::Welcome)
        fast_full_redraw("FIRST_SETUP_SCREEN",false);
    else if(screen==Screen::Contacts)
        // Existing-user boot transitions directly from the dark startup logo
        // to Contacts. Force the same complete refresh as a short BOOT press
        // so the splash cannot remain faintly visible in the panel history.
        fast_full_redraw("CONTACTS_AFTER_BOOT",false);
    else
        refresh(MODE_GL16);
    // The first interactive frame already includes the MeshCore status
    // populated during startup; don't immediately refresh it a second time.
    status_dirty=false;
    touch_queue=xQueueCreate(32,sizeof(QueuedTap));
    if(touch_queue&&xTaskCreatePinnedToCore(touch_sampler_task,"t5-touch",4096,nullptr,1,&touch_task_handle,0)==pdPASS)T5_DEBUGLN(T5_LOG_TOUCH,"[T5-TOUCH] sampler running; interval=8ms queue depth=32");
    else Serial.println("[T5-TOUCH] ERROR: sampler could not start");
    T5_DEBUGF(T5_LOG_UI,"[T5-LIGHT] mode=%s timeout=%s brightness=%u%% night=%02u:%02u-%02u:%02u\n",frontlight_mode_name(),frontlight_timeout_name(),frontlight_brightness,night_start_minutes/60,night_start_minutes%60,night_end_minutes/60,night_end_minutes%60);
    set_cpu_target(160,"ui-ready");last_user_activity=millis();T5_DEBUGLN(T5_LOG_UI,"[T5-UI] touch ready; waiting for input");
}

void ui_loop() {
    if(hardware_failure){
        static uint32_t report_at=0;if(millis()-report_at>=60000){report_at=millis();Serial.println("[T5-ERROR] radio unavailable; startup halted; press RST to retry");}
        delay(100);return;
    }
    service_boot_button();
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
                epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
            }
            details_page=0;details_from_discovery=false;chat_page=0;
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
                meshink_map_gestures::terrain_point(first_x,first_y)&&
                meshink_map_gestures::terrain_point(tap.x,tap.y)&&
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
           tap.y>=MAP_TOP&&tap.y<MAP_BOTTOM&&tap.x>=0&&tap.x<540&&
           !(tap.x>=456&&tap.y<281)) {
            pan_map_by_pixels(tap.dx,tap.dy);
            open_screen(Screen::Maps);
            continue;
        }
        if((screen==Screen::Contacts||screen==Screen::Channels)&&abs(tap.dy)>60&&abs(tap.dy)>abs(tap.dx)){
            const size_t count=!ui_data?0:(screen==Screen::Contacts?ui_data->contact_count():ui_data->channel_count());
            size_t& page=screen==Screen::Contacts?contacts_page:channels_page;
            clamp_list_page(page,count);
            const size_t pages=list_page_count(count);
            int next=(int)page+(tap.dy<0?1:-1);
            if(next<0)next=0;if(next>=(int)pages)next=(int)pages-1;
            if((size_t)next!=page){
                page=(size_t)next;
                T5_DEBUGF(T5_LOG_UI,"[T5-UI] %s page=%u/%u\n",screen==Screen::Contacts?"contacts":"channels",(unsigned)(page+1),(unsigned)pages);
                draw_screen();refresh(MODE_GL16);
            }
        }else if(screen==Screen::Presets&&abs(tap.dy)>60){
            const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
            int next=(int)preset_page+(tap.dy<0?1:-1);if(next<0)next=0;if(next>=page_count)next=page_count-1;
            preset_page=(uint8_t)next;T5_DEBUGF(T5_LOG_UI,"[T5-UI] preset page=%u\n",preset_page+1);draw_screen();refresh(MODE_GL16);
        }else handle_tap(tap.x,tap.y);
    }
    if(text_refresh_pending&&(int32_t)(millis()-text_refresh_after)>=0){text_refresh_pending=false;draw_screen();refresh(MODE_DU);}
    static uint32_t last_status_poll=0;
    const uint32_t status_poll_interval=standby_active?60000:15000;
    if(millis()-last_status_poll>=status_poll_interval){
        last_status_poll=millis();
        if(update_status_hardware())status_dirty=true;
    }
    if(status_dirty&&!message_alert_active){const bool wake=status_wake_light&&!standby_active;status_dirty=false;status_wake_light=false;draw_screen();refresh(MODE_DU,wake);}
    if(toast_visible&&(int32_t)(millis()-toast_until)>=0){
        toast_visible=false;
        if(toast_opens_main){toast_opens_main=false;screen=Screen::Contacts;keyboard_visible=false;keyboard_message_mode=false;status_unread=0;status_channel_unread=0;}
        draw_screen();refresh(MODE_DU,true);
    }
    frontlight_service();
    service_message_alert();
#if T5_LOG_POWER
    static uint32_t power_report_at=0,loop_count=0;loop_count++;
    if(millis()-power_report_at>=60000){const uint32_t elapsed=static_cast<uint32_t>(millis()-power_report_at);T5_DEBUGF(T5_LOG_POWER,"[T5-POWER] health cpu=%luMHz apb=%luMHz standby=%d loops=%lu/s heap=%u psram=%u stack=%u touch=%s\n",(unsigned long)getCpuFrequencyMhz(),(unsigned long)(getApbFrequency()/1000000),standby_active,(unsigned long)(loop_count*1000/elapsed),ESP.getFreeHeap(),ESP.getFreePsram(),(unsigned)uxTaskGetStackHighWaterMark(nullptr),touch_enabled?"active":"suspended");power_report_at=millis();loop_count=0;}
#endif
    delay(12);
}

bool ui_is_standby(){return standby_active;}

void ui_show_radio_failure(){
    hardware_failure=true;keyboard_visible=false;keyboard_message_mode=false;toast_visible=false;text_refresh_pending=false;
    epd_hl_set_all_white(&display);
    centred("RADIO STARTUP",190,5,0,true);
    centred("FAILED",255,6,0,true);
    centred("SX1262 NOT DETECTED",390,4,0,true);
    centred("PRESS RST TO RETRY",500,4,0,true);
    centred(UI_VERSION,900,2,0,true);
    refresh(MODE_GL16,false);
    frontlight_deadline=0;frontlight_drive(false);set_touch_power(false);set_cpu_target(80,"hardware-failure");
    Serial.println("[T5-ERROR] persistent radio failure screen displayed; UI and touch stopped");
}

void ui_status_set_unread(uint16_t count) {
    if(status_unread!=count){status_unread=count;status_dirty=true;}
}

void ui_status_set_channel_unread(uint16_t count) {
    if(status_channel_unread!=count){status_channel_unread=count;status_dirty=true;}
}

void ui_status_set_gps(bool enabled,bool has_fix,int satellites,long latitude,long longitude,uint32_t timestamp) {
    const bool state_changed=status_gps_enabled!=enabled||status_gps_fix!=has_fix;
    const bool detail_changed=status_gps_satellites!=satellites||status_gps_latitude!=latitude||status_gps_longitude!=longitude;
    const bool satellites_changed=enabled&&has_fix&&!standby_active&&status_gps_satellites!=satellites;
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
    static uint32_t last_satellite_refresh=0;
    const uint32_t now=millis();
    const bool satellites_refresh=satellites_changed&&
        now-last_satellite_refresh>=(standby_active?60000UL:15000UL);
    if(satellites_refresh)last_satellite_refresh=now;
    bool marker_moved=false;
    // Keep the own-position marker reasonably current while travelling,
    // but avoid expensive e-paper updates for every 1 Hz GPS sample.
    static uint32_t last_marker_refresh=0;
    if(screen==Screen::Maps&&enabled&&has_fix&&
       (previous_latitude!=latitude||previous_longitude!=longitude)&&
       now-last_marker_refresh>=15000) {
        int sx=0,sy=0;
        if(project_device_on_map(latitude,longitude,sx,sy)) {
            const bool now_visible=sx>=20&&sx<=520&&sy>=MAP_TOP+20&&sy<=MAP_BOTTOM-21;
            marker_moved=map_device_marker_visible?
                (abs(sx-map_device_marker_x)>=3||abs(sy-map_device_marker_y)>=3):
                now_visible;
        }
        if(marker_moved)last_marker_refresh=now;
    }
    if(state_changed||satellites_refresh||(detail_changed&&screen==Screen::GpsSettings&&now-last_detail_refresh>=10000)||marker_moved){
        last_detail_refresh=now;
        status_dirty=true;
        T5_DEBUGLN(T5_LOG_UI,marker_moved?"[T5-UI] refresh queued reason=map-own-position":
                               "[T5-UI] refresh queued reason=gps-state");
    }
}

void ui_notify_message_received(bool channel){
    const bool visible=channel?screen==Screen::ChannelChat:screen==Screen::ContactChat;
    if(!visible){if(channel){if(status_channel_unread<65535)status_channel_unread++;}else if(status_unread<65535)status_unread++;persist_unread();}
    status_dirty=true;if(standby_active)start_message_alert();else status_wake_light=true;T5_DEBUGF(T5_LOG_MESH,"[T5-UI] %s message event unread=%u refresh queued standby=%d\n",channel?"channel":"direct",channel?status_channel_unread:status_unread,standby_active);
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
        Serial.printf("[T5-BOOT] initial radio preset %s: %lu.%03lu MHz SF%u BW%.1f CR%u %uB\n",
            preset.title,(unsigned long)(preset.frequency_khz/1000),
            (unsigned long)(preset.frequency_khz%1000),preset.spreading_factor,
            (double)preset.bandwidth_khz,preset.coding_rate,preset.path_hash_bytes);
    }else{
        Serial.println("[T5-ERROR] first-time radio preset failed; select a radio preset in setup");
    }
}

void ui_mesh_ready(){
    mesh_is_ready=true;Preferences state;bool migrated=false;if(state.begin("t5-ui",false)){migrated=state.getBool("name_migrated",false);if(!migrated&&node_name[0]){local_mesh_apply_name(node_name);state.putBool("name_migrated",true);T5_DEBUGF(T5_LOG_UI,"[T5-UI] migrated node name to MeshCore '%s'\n",node_name);}else{strncpy(node_name,local_mesh_node_name(),sizeof(node_name)-1);node_name[sizeof(node_name)-1]=0;T5_DEBUGF(T5_LOG_UI,"[T5-UI] node name loaded from MeshCore '%s'\n",node_name);}state.putString("name",node_name);state.end();}
    update_status_hardware();status_dirty=true;
}

void ui_use_data_provider(UiDataProvider* provider) {
    if (!provider) return;
    ui_data=provider;
    T5_DEBUGLN(T5_LOG_UI,"[T5-UI] live MeshCore data provider attached");
    // ui_finish_startup() presents the first interactive screen only after
    // the blocking storage / MeshCore boot sequence has completed.
}

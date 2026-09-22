#include <Arduino.h>
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
static char node_name[21] = "MY T5";
static uint8_t selected_preset = 17;
static bool saved = false;
static bool was_pressed = false;
static bool replace_name_on_type = false;
static bool keyboard_visible = true;
static bool keyboard_upper = true;
static bool keyboard_symbols = false;
static bool keyboard_landscape = false;
static uint16_t status_unread = 0;
static uint16_t status_channel_unread = 0;
static bool status_gps_enabled = false;
static bool status_gps_fix = false;
static int16_t status_gps_satellites = 0;
static long status_gps_latitude = 0;
static long status_gps_longitude = 0;
static uint32_t status_gps_timestamp = 0;
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
static int16_t cached_touch_x = 0, cached_touch_y = 0;
static uint8_t chat_page = 0;
static uint8_t timezone_index = 0;
static bool mesh_is_ready = false;
enum class FrontlightMode:uint8_t{On=0,NightTimer=1,Off=2};
static FrontlightMode frontlight_mode=FrontlightMode::On;
static uint8_t frontlight_timeout_index=2;
static uint8_t frontlight_brightness=60;
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
static uint8_t message_alert_phase=0;
static uint32_t message_alert_deadline=0;
static uint32_t message_alert_cooldown_until=0;
enum class Screen : uint8_t {
    Welcome, Presets, CompanionConfirm, ShutdownConfirm,
    Contacts, ContactChat, ContactDetails,
    Channels, ChannelChat, Maps, Discovery, More, AdvertMenu,
    Settings, RadioSettings, GpsSettings, Timezone, PrivacySettings, DisplaySettings, NightSchedule, About
};
static Screen screen = Screen::Welcome;
static Screen preset_return_screen = Screen::Welcome;
static uint8_t preset_page = 3;
static bool details_from_discovery=false;
static uint8_t details_page=0;
static double map_latitude=-41.2865,map_longitude=174.7762;
static uint8_t map_zoom=12;
static bool map_imperial=false;
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

static void apply_timezone(){setenv("TZ",TIMEZONES[timezone_index].rule,1);tzset();Serial.printf("[T5-TIME] display timezone=%s\n",TIMEZONES[timezone_index].label);}

static constexpr uint32_t FRONTLIGHT_TIMEOUTS[]={5000,10000,15000,30000,0};
static constexpr uint32_t STANDBY_TIMEOUTS[]={300000,600000,900000,0};
static const char* frontlight_mode_name(){return frontlight_mode==FrontlightMode::On?"ON":frontlight_mode==FrontlightMode::NightTimer?"NIGHT TIMER":"OFF";}
static const char* frontlight_timeout_name(){static const char* names[]={"5 SECONDS","10 SECONDS","15 SECONDS","30 SECONDS","ALWAYS ON"};return names[min((uint8_t)4,frontlight_timeout_index)];}
static const char* standby_timeout_name(){static const char* names[]={"5 MINUTES","10 MINUTES","15 MINUTES","NEVER"};return names[min((uint8_t)3,standby_timeout_index)];}
static bool night_window_active(){const uint16_t now=status_hour<0?0:(uint16_t)(status_hour*60+status_minute);return night_start_minutes<=night_end_minutes?(now>=night_start_minutes&&now<night_end_minutes):(now>=night_start_minutes||now<night_end_minutes);}
static bool frontlight_allowed(){return frontlight_mode==FrontlightMode::On||(frontlight_mode==FrontlightMode::NightTimer&&night_window_active());}
static void frontlight_drive(bool on){frontlight_lit=on&&frontlight_allowed();const uint8_t duty=frontlight_lit?(uint8_t)max(1,(frontlight_brightness*255)/100):0;ledcWrite(FRONTLIGHT_PWM_CHANNEL,duty);}
static void frontlight_event(){if(!frontlight_allowed()){frontlight_drive(false);frontlight_deadline=0;return;}frontlight_drive(true);const uint32_t timeout=FRONTLIGHT_TIMEOUTS[min((uint8_t)4,frontlight_timeout_index)];frontlight_deadline=timeout?millis()+timeout:0;}
static void frontlight_service(){if(message_alert_active)return;if(frontlight_mode==FrontlightMode::Off||(frontlight_mode==FrontlightMode::NightTimer&&!night_window_active())){if(frontlight_lit)frontlight_drive(false);return;}if(frontlight_lit&&frontlight_deadline&&(int32_t)(millis()-frontlight_deadline)>=0){frontlight_deadline=0;frontlight_drive(false);Serial.println("[T5-LIGHT] timeout; frontlight off");}}
static void save_frontlight_settings(){Preferences light;if(light.begin("t5-ui",false)){light.putUChar("light_mode",(uint8_t)frontlight_mode);light.putUChar("light_timeout",frontlight_timeout_index);light.putUChar("light_level",frontlight_brightness);light.putUChar("standby_timeout",standby_timeout_index);light.putUShort("night_start",night_start_minutes);light.putUShort("night_end",night_end_minutes);light.end();}}

struct QueuedTap{int16_t x;int16_t y;int16_t dy;bool home;};

static bool set_cpu_target(uint32_t mhz,const char* reason,bool verbose=true){
    const bool accepted=setCpuFrequencyMhz(mhz);const uint32_t actual=getCpuFrequencyMhz();
    if(verbose||!accepted||actual!=mhz)Serial.printf("[T5-POWER] cpu target=%lu actual=%lu MHz apb=%lu MHz reason=%s result=%s\n",(unsigned long)mhz,(unsigned long)actual,(unsigned long)(getApbFrequency()/1000000),reason,(accepted&&actual==mhz)?"OK":"FAILED");
    return accepted&&actual==mhz;
}

struct Preset { const char* title; const char* detail; };
static constexpr Preset PRESETS[] = {
 {"KEEP CURRENT","NO RADIO CHANGES"},{"AUSTRALIA","915.800 / SF10 / BW250 / CR5"},
 {"AUSTRALIA NARROW","916.575 / SF7 / BW62.5 / CR8"},{"AUSTRALIA MID","915.075 / SF9 / BW125 / CR5"},
 {"AUSTRALIA SA WA","923.125 / SF8 / BW62.5 / CR8"},{"AUSTRALIA QLD","923.125 / SF8 / BW62.5 / CR5"},
 {"BRAZIL","923.125 / SF8 / BW62.5 / CR8"},{"CANADA","910.525 / SF7 / BW62.5 / CR5 / 3B"},
 {"COSTA RICA","910.525 / SF11 / BW125 / CR5"},{"EU UK NARROW","869.618 / SF8 / BW62.5 / CR8"},
 {"EU UK DEPRECATED","869.525 / SF11 / BW250 / CR5"},{"CZECH NARROW","869.432 / SF7 / BW62.5 / CR5"},
 {"EU 433 LONG RANGE","433.650 / SF11 / BW250 / CR5"},{"EU 433 NARROW","433.650 / SF8 / BW62.5 / CR8"},
 {"HUNGARY","869.618 / SF7 / BW62.5 / CR5 / 2B"},{"NETHERLANDS","869.618 / SF7 / BW62.5 / CR5"},
 {"NL LIMBURG","869.618 / SF8 / BW62.5 / CR8 / 2B"},{"NZ NARROW","917.375 / SF7 / BW62.5 / CR5 / 2B"},
 {"NZ GISBORNE","917.375 / SF11 / BW250 / CR5 / 1B"},{"PORTUGAL 433","433.375 / SF9 / BW62.5 / CR6"},
 {"PORTUGAL 868","869.618 / SF7 / BW62.5 / CR6"},{"SLOVAKIA","869.618 / SF7 / BW62.5 / CR5 / 2B"},
 {"SWITZERLAND","869.618 / SF8 / BW62.5 / CR8"},{"USA","910.525 / SF7 / BW62.5 / CR5"},
 {"USA PHILLYMESH","902.250 / SF11 / BW500 / CR5 / 2B"},{"USA SOCAL","927.875 / SF7 / BW62.5 / CR5 / 3B"},
 {"VIETNAM NARROW","920.250 / SF8 / BW62.5 / CR5"},{"VIETNAM DEPRECATED","920.250 / SF11 / BW250 / CR5"}
};
static constexpr uint8_t PRESET_COUNT = sizeof(PRESETS)/sizeof(PRESETS[0]);

static bool apply_selected_preset(){
    if(selected_preset==0)return true;float freq=0,bw=0;unsigned sf=0,cr=0,bytes=1;
    const int fields=sscanf(PRESETS[selected_preset].detail,"%f / SF%u / BW%f / CR%u / %uB",&freq,&sf,&bw,&cr,&bytes);
    if(fields<4)return false;return local_mesh_apply_radio(freq,bw,(uint8_t)sf,(uint8_t)cr,(uint8_t)(max(1u,min(3u,bytes))-1));
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
    for(int i=0;numbers[i];++i){char label[2]={numbers[i],0};key(label,15+i*52,618,49);}
    const char* letter_rows_upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    const char* letter_rows_lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbol_rows[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    const char** rows=keyboard_symbols?symbol_rows:(keyboard_upper?letter_rows_upper:letter_rows_lower);
    const int starts[]={15,41,93};const int ys[]={688,758,828};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i){char label[2]={rows[r][i],0};key(label,starts[r]+i*52,ys[r],49);}
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
    for(int i=0;i<10;++i){char s[2]={numbers[i],0};landscape_key(s,15+i*93,145,88);}
    const char** rows=active_keyboard_rows();const int starts[]={15,60,153};const int ys[]={215,285,355};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i){char s[2]={rows[r][i],0};landscape_key(s,starts[r]+i*93,ys[r],88);}
    landscape_key(keyboard_symbols?"ABC":(keyboard_upper?"abc":"#+="),15,355,130);
    landscape_key("DEL",812,355,133);
    landscape_key("PORTRAIT",15,425,180);landscape_key("SPACE",203,425,500);
    landscape_key(keyboard_message_mode?"SEND":"SAVE",711,425,234);
}

static void line(int x0,int y0,int x1,int y1,uint8_t color=0) {
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    while(true){epd_draw_pixel(x0,y0,color,fb);if(x0==x1&&y0==y1)break;const int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}

static void draw_target_icon(int x,int y,bool disabled) {
    epd_draw_rect({x+5,y+5,20,20},0,fb);epd_draw_rect({x+9,y+9,12,12},0,fb);
    epd_fill_rect({x+13,y+13,4,4},0,fb);line(x,y+15,x+29,y+15);line(x+15,y,x+15,y+29);
    if(disabled){for(int d=-1;d<=1;++d)line(x+2,y+2+d,x+28,y+28+d);}
}

static void draw_search_icon(int x,int y) {
    epd_draw_rect({x+3,y+3,19,19},0,fb);epd_draw_rect({x+7,y+7,11,11},0,fb);
    for(int d=-1;d<=1;++d)line(x+20,y+20+d,x+29,y+29+d);
}

static void draw_envelope_icon(int x,int y) {
    epd_draw_rect({x,y+5,30,21},0,fb);line(x+1,y+6,x+15,y+17);line(x+29,y+6,x+15,y+17);
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

static void draw_toast() {
    if(!toast_visible)return;
    const int scale=3,w=max(300,(int)strlen(toast_message)*6*scale+48),h=72,x=(540-w)/2,y=640,r=12;
    epd_fill_rect({x+r,y,w-2*r,h},0,fb);epd_fill_rect({x,y+r,w,h-2*r},0,fb);
    epd_fill_rect({x+5,y+5,w-10,h-10},0,fb);
    text(toast_message,x+(w-(int)strlen(toast_message)*6*scale)/2,y+25,scale,0xFF,true);
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
    if(saved&&!keyboard_visible)centred("SETTINGS SAVED",760,2,0,true);
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

static void draw_contacts() {
    draw_app_header("CONTACTS");
    if(!ui_data||!ui_data->contact_count())centred("NO SAVED CONTACTS",300,3,0,true);
    else for(size_t i=0;i<ui_data->contact_count()&&i<5;++i)draw_list_entry(ui_data->contact(i),120+i*150);
    draw_bottom_nav(0);
}

static void draw_channels() {
    draw_app_header("CHANNELS");
    if(!ui_data||!ui_data->channel_count())centred("NO CONFIGURED CHANNELS",300,3,0,true);
    else for(size_t i=0;i<ui_data->channel_count()&&i<5;++i)draw_list_entry(ui_data->channel(i),120+i*150);
    draw_bottom_nav(1);
}

static void draw_maps() {
    draw_app_header("MAPS");
    const auto result=map_tiles_render(fb,0,118,540,782,map_latitude,map_longitude,map_zoom);
    epd_fill_rect({258,500,24,24},0xFF,fb);epd_draw_rect({258,500,24,24},0,fb);line(270,494,270,530);line(252,512,288,512);
    char zoom[12];snprintf(zoom,sizeof(zoom),"ZOOM %u",map_zoom);epd_fill_rect({18,812,100,30},0xFF,fb);text(zoom,22,816,2,0,true);
    // Vertical zoom rocker. Draw symbols directly so they don't depend on
    // unsupported font glyphs.
    box(484,130,44,44,true);line(495,152,517,152,0xFF);line(506,141,506,163,0xFF);line(495,151,517,151,0xFF);line(505,141,505,163,0xFF);
    box(484,184,44,44,true);line(495,206,517,206,0xFF);line(495,205,517,205,0xFF);
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
    if(details_page==0){text("LAST SEEN",24,230,2,0,true);draw_wrapped(node.last_seen,230,230,25,2,0,false,2);text("ROUTE",24,310,2,0,true);text(node.route,230,310,2);text("POSITION",24,380,2,0,true);draw_wrapped(node.position,230,380,25,2,0,false,2);text("IDENTITY",24,470,2,0,true);text(node.identity,230,470,2);if(node.latitude||node.longitude){box(24,540,492,62);centred("OPEN POSITION ON MAP",561,2,0,true);}}
    else{text("STATUS",24,230,2,0,true);draw_wrapped(node.status,24,264,39,2,0,false,2);text("TELEMETRY / POSITION",24,350,2,0,true);draw_wrapped(node.telemetry,24,384,39,2,0,false,4);text("DISCOVERED PATH",24,530,2,0,true);draw_wrapped(node.path,24,564,39,2,0,false,2);box(24,650,492,70,true);centred(node.request_active?"REQUESTING...":"REQUEST ALL INFO",674,3,0xFF,true);}
    box(24,748,220,62,details_page==0);text("PREV",82,769,2,details_page==0?0xFF:0,true);box(296,748,220,62,details_page==1);text("NEXT",354,769,2,details_page==1?0xFF:0,true);
    if(details_page==0){if(node.saved_contact){box(24,828,240,62);centred("CHAT",849,2,0,true);box(276,828,240,62);centred("DELETE",849,2,0,true);}else{box(24,828,492,62,true);centred("ADD CONTACT",849,2,0xFF,true);}}
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
    settings_row("DISPLAY & POWER","FRONTLIGHT, REFRESH, STANDBY",478);settings_row("ABOUT","FIRMWARE AND DEVICE INFO",598);
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
    settings_row("TIMEZONE",TIMEZONES[timezone_index].label,710);
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
    text(map_imperial?"MAP SCALE: IMPERIAL":"MAP SCALE: METRIC",28,652,2,0,true);
    const int shutdown_y=frontlight_mode==FrontlightMode::NightTimer?758:674;
    if(frontlight_mode==FrontlightMode::NightTimer){box(24,674,492,70,true);centred("NIGHT SCHEDULE",697,3,0xFF,true);}
    box(24,shutdown_y,492,70);centred("SHUT DOWN",shutdown_y+23,3,0,true);
    centred("SHORT BOOT: CLEAN DISPLAY",856,2,0,true);
    centred("HOLD BOOT: STANDBY",882,2,0,true);
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

static void draw_about() {
    draw_app_header("ABOUT",true);centred("MESHCORE T5 PAPER",150,4,0,true);centred(UI_VERSION,210,3,0,true);
    text("HARDWARE",24,300,2,0,true);text("LILYGO T5 PRO",250,300,2);
    text("MODE",24,354,2,0,true);text("LOCAL UI",250,354,2);
    text("DATA",24,408,2,0,true);text("MESHCORE LIVE",250,408,2);
    text("STORE",24,462,2,0,true);text("96 MESSAGE RING",250,462,2);
    draw_wrapped("Oldest messages are removed automatically when fixed storage reaches capacity.",24,550,38,2,0,true,4);
}

static void draw_screen() {
    if(keyboard_landscape){draw_landscape_keyboard();return;}
    if(standby_active){draw_standby();return;}
    switch(screen){
        case Screen::Welcome:draw_welcome();break;case Screen::Presets:draw_presets();break;case Screen::CompanionConfirm:draw_companion_confirm();break;case Screen::ShutdownConfirm:draw_shutdown_confirm();break;
        case Screen::Contacts:draw_contacts();break;case Screen::ContactChat:draw_chat(false);break;case Screen::ContactDetails:draw_contact_details();break;
        case Screen::Channels:draw_channels();break;case Screen::ChannelChat:draw_chat(true);break;case Screen::Maps:draw_maps();break;case Screen::Discovery:draw_discovery();break;case Screen::More:draw_more();break;case Screen::AdvertMenu:draw_advert_menu();break;
        case Screen::Settings:draw_settings();break;case Screen::RadioSettings:draw_radio_settings();break;case Screen::GpsSettings:draw_gps_settings();break;case Screen::Timezone:draw_timezone();break;
        case Screen::PrivacySettings:draw_privacy_settings();break;case Screen::DisplaySettings:draw_display_settings();break;case Screen::NightSchedule:draw_night_schedule();break;case Screen::About:draw_about();break;
    }
    const bool settings_page=screen==Screen::Settings||screen==Screen::RadioSettings||screen==Screen::GpsSettings||screen==Screen::Timezone||screen==Screen::PrivacySettings||screen==Screen::DisplaySettings||screen==Screen::NightSchedule||screen==Screen::About;
    if(screen==Screen::ContactDetails)draw_bottom_nav(details_from_discovery?3:0);
    else if(screen==Screen::Discovery||screen==Screen::AdvertMenu||settings_page)draw_bottom_nav(3);
    draw_toast();
}

static void refresh(EpdDrawMode mode,bool wake_light=true) {
    if(wake_light&&!standby_active)frontlight_event();
    set_cpu_target(240,"display-refresh",false);
    epd_poweron();
    const EpdDrawError err = epd_hl_update_screen(&display,mode,(int)epd_ambient_temperature());
    epd_poweroff();
    set_cpu_target(standby_active?80:160,"display-complete",false);
    Serial.printf("[T5-UI] refresh=%d name='%s' preset=%s cpu=%luMHz\n",err,node_name,PRESETS[selected_preset].title,(unsigned long)getCpuFrequencyMhz());
}

static void invalidate_display_back_buffer() {
    const size_t bytes=(size_t)epd_width()*epd_height()/2;
    for(size_t i=0;i<bytes;++i)display.back_fb[i]=(uint8_t)~display.front_fb[i];
}

static void force_redraw(EpdDrawMode mode,const char* reason,bool wake_light=false) {
    invalidate_display_back_buffer();
    Serial.printf("[T5-EPD] forced full redraw reason=%s mode=%d\n",reason,(int)mode);
    refresh(mode,wake_light);
}

static void fast_full_redraw(const char* reason,bool wake_light=false) {
    // ED047TC1's bundled waveform provides DU, GC16, GL16 and the known-state
    // transitions, but not mode 3 (GC16_FAST). GL16 is the supported fast,
    // full-screen quality transition for this panel.
    Serial.printf("[T5-EPD] GC16_FAST unavailable in ED047TC1 waveform; using GL16 reason=%s\n",reason);
    force_redraw(MODE_GL16,reason,wake_light);
}

static void full_display_clean(const char* reason) {
    Serial.printf("[T5-EPD] full GC16 redraw requested by %s standby=%d\n",reason,standby_active);
    draw_screen();
    force_redraw(MODE_GC16,reason,false);
    Serial.println("[T5-EPD] full GC16 UI redraw complete; buffers synchronized");
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
    if(!(value&BATFET_DIS)){Serial.printf("[T5-POWER] boot PMIC address=0x%02X REG09=0x%02X battery path ready\n",address,value);return;}
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
    Serial.printf("[T5-SHUTDOWN] PMIC detected address=0x%02X REG09 before=0x%02X\n",address,reg09);
    const uint8_t requested=(uint8_t)((reg09|BATFET_DIS|BATFET_RST_EN)&~BATFET_DLY);
    Serial.printf("[T5-SHUTDOWN] preserving wake reset; REG09 request=0x%02X BATFET_DIS=1\n",requested);
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
    if(changed)Serial.printf("[T5-UI] status clock=%02d:%02d battery=%d%% direct=%u channel=%u gps=%s\n",
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

static void touch_sampler_task(void*){
    bool held=false;int16_t start_y=0,last_x=0,last_y=0;
    for(;;){if(!touch_enabled){held=false;Serial.println("[T5-POWER] touch sampler suspended");ulTaskNotifyTake(pdTRUE,portMAX_DELAY);Serial.println("[T5-POWER] touch sampler resumed");continue;}int16_t x=0,y=0;bool home=false;const bool pressed=touch_point(x,y,home);if(home){if(!held){QueuedTap tap{0,0,0,true};xQueueSend(touch_queue,&tap,0);held=true;}}else if(pressed){last_x=x;last_y=y;if(!held){held=true;start_y=y;frontlight_event();}}
        else if(held){held=false;QueuedTap tap{last_x,last_y,(int16_t)(last_y-start_y),false};if(xQueueSend(touch_queue,&tap,0)!=pdTRUE)Serial.println("[T5-TOUCH] input queue full; tap discarded");}
        vTaskDelay(pdMS_TO_TICKS(8));}
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

static void open_screen(Screen next) { keyboard_visible=false;keyboard_message_mode=false;screen=next;draw_screen();refresh(MODE_GL16); }
static void persist_unread(){Preferences state;if(state.begin("t5-ui",false)){state.putUShort("unread_dm",status_unread);state.putUShort("unread_ch",status_channel_unread);state.end();}}
static void queue_text_refresh(){text_refresh_pending=true;text_refresh_after=millis()+110;}
static void set_keyboard_orientation(bool landscape){
    keyboard_landscape=landscape;
    epd_set_rotation(landscape?EPD_ROT_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);
    Serial.printf("[T5-UI] keyboard orientation=%s\n",landscape?"landscape":"portrait");
    draw_screen();refresh(MODE_GL16);
}
static void save_node_name(){
    prefs.begin("t5-ui",false);prefs.putString("name",node_name);prefs.putUChar("preset_v2",selected_preset);
    prefs.putBool("complete",true);prefs.putBool("name_migrated",true);prefs.end();
    if(mesh_is_ready)local_mesh_apply_name(node_name);
    saved=true;setup_complete=true;
}
static bool handle_landscape_keyboard(int16_t raw_x,int16_t raw_y){
    if(!keyboard_landscape)return false;
    const int16_t x=raw_y,y=539-raw_x;
    Serial.printf("[T5-UI] landscape tap raw=%d,%d mapped=%d,%d\n",raw_x,raw_y,x,y);
    const char* numbers="1234567890";if(y>=141&&y<211){append(numbers[min(9,max(0,(x-1)/96))]);queue_text_refresh();return true;}
    const char** rows=active_keyboard_rows();const int starts[]={15,60,153};const int ys[]={215,285,355};
    for(int r=0;r<3;++r)if(y>=ys[r]-4&&y<ys[r]+66){const int count=strlen(rows[r]);int i=min(count-1,max(0,(x-starts[r]+46)/93));if(r!=2||!(x<148||x>=808)){append(rows[r][i]);queue_text_refresh();return true;}}
    if(hit(x,y,15,355,130,62)){cycle_keyboard_mode();draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,812,355,133,62)){char* value=keyboard_message_mode?compose_text:node_name;size_t n=strlen(value);if(n)value[n-1]=0;queue_text_refresh();return true;}
    if(hit(x,y,15,425,180,62)){set_keyboard_orientation(false);return true;}
    if(hit(x,y,203,425,500,62)){append(' ');queue_text_refresh();return true;}
    if(hit(x,y,711,425,234,62)){
        if(keyboard_message_mode){if(compose_text[0]&&local_mesh_send_active(compose_text))compose_text[0]=0;}
        else save_node_name();
        keyboard_visible=true;set_keyboard_orientation(false);return true;
    }
    return true;
}

static bool handle_message_keyboard(int16_t x,int16_t y) {
    if(!keyboard_visible||!keyboard_message_mode)return false;
    const char* numbers="1234567890";if(y>=614&&y<684){append(numbers[min(9,max(0,(int)x*10/540))]);queue_text_refresh();return true;}
    const char* upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};const char* lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbols[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};const char** rows=keyboard_symbols?symbols:(keyboard_upper?upper:lower);
    const int starts[]={15,41,93};const int ys[]={688,758,828};
    for(int r=0;r<3;++r)if(y>=ys[r]-4&&y<ys[r]+66){const int count=strlen(rows[r]);int i=min(count-1,max(0,(x-starts[r]+26)/52));if(r!=2||!(x<90||x>=455)){append(rows[r][i]);queue_text_refresh();return true;}}
    if(hit(x,y,12,828,76,62)){cycle_keyboard_mode();draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,460,828,68,62)){size_t n=strlen(compose_text);if(n)compose_text[n-1]=0;queue_text_refresh();return true;}
    if(hit(x,y,12,898,100,62)){set_keyboard_orientation(true);return true;}
    if(hit(x,y,110,894,210,66)){append(' ');queue_text_refresh();return true;}
    if(hit(x,y,318,898,100,62)){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return true;}
    if(hit(x,y,426,898,102,62)){if(compose_text[0]){const bool ok=local_mesh_send_active(compose_text);if(ok){compose_text[0]=0;keyboard_visible=false;}draw_screen();refresh(MODE_DU);}return true;}
    return true;
}

static bool handle_name_keyboard(int16_t x,int16_t y){
    if(!keyboard_visible||keyboard_message_mode)return false;
    const char* numbers="1234567890";if(y>=614&&y<684){append(numbers[min(9,max(0,(int)x*10/540))]);queue_text_refresh();return true;}
    const char** rows=active_keyboard_rows();const int starts[]={15,41,93};const int ys[]={688,758,828};
    for(int r=0;r<3;++r)if(y>=ys[r]-4&&y<ys[r]+66){const int count=strlen(rows[r]);int i=min(count-1,max(0,(x-starts[r]+26)/52));if(r!=2||!(x<90||x>=455)){append(rows[r][i]);queue_text_refresh();return true;}}
    if(hit(x,y,12,828,76,62)){cycle_keyboard_mode();draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,460,828,68,62)){size_t n=strlen(node_name);if(n)node_name[n-1]=0;saved=false;queue_text_refresh();return true;}
    if(hit(x,y,12,898,100,62)){set_keyboard_orientation(true);return true;}
    if(hit(x,y,120,898,190,62))return true;
    if(hit(x,y,318,898,100,62)){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return true;}
    if(hit(x,y,426,898,102,62)){save_node_name();keyboard_visible=false;toast_opens_main=screen==Screen::Welcome;show_toast(screen==Screen::Welcome?"SETTINGS SAVED":"IDENTITY SAVED");draw_screen();refresh(MODE_DU);return true;}
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
            if(ui_data)for(size_t i=0;i<ui_data->contact_count()&&i<5;++i)if(hit(x,y,12,120+i*150,516,142)){selected_contact=i;if(ui_data->open_contact(i)){status_unread=local_mesh_direct_unread_total();persist_unread();chat_page=0;open_screen(Screen::ContactChat);}return true;}break;
        case Screen::Channels:
            if(ui_data)for(size_t i=0;i<ui_data->channel_count()&&i<5;++i)if(hit(x,y,12,120+i*150,516,142)){selected_channel=i;if(ui_data->open_channel(i)){status_channel_unread=local_mesh_channel_unread_total();persist_unread();chat_page=0;open_screen(Screen::ChannelChat);}return true;}break;
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
                if(hit(x,y,24,748,220,62)&&details_page>0){details_page--;draw_screen();refresh(MODE_GL16);return true;}
                if(hit(x,y,296,748,220,62)&&details_page<1){details_page++;draw_screen();refresh(MODE_GL16);return true;}
                if(details_page==1&&hit(x,y,24,650,492,70)){show_toast(ui_data->request_active_node_info()?"REQUESTING ALL INFO":"REQUEST BUSY");draw_screen();refresh(MODE_DU);return true;}
                if(details_page==0&&(node.latitude||node.longitude)&&hit(x,y,24,540,492,62)){map_latitude=node.latitude/1000000.0;map_longitude=node.longitude/1000000.0;open_screen(Screen::Maps);return true;}
                if(details_page==0&&node.saved_contact&&hit(x,y,24,828,240,62)){open_screen(Screen::ContactChat);return true;}
                if(details_page==0&&node.saved_contact&&hit(x,y,276,828,240,62)){show_toast(ui_data->remove_active_contact()?"CONTACT REMOVED":"REMOVE FAILED");draw_screen();refresh(MODE_DU);return true;}
                if(details_page==0&&!node.saved_contact&&hit(x,y,24,828,492,62)){show_toast(ui_data->add_active_node()?"CONTACT ADDED":"ADD FAILED");draw_screen();refresh(MODE_DU);return true;}
            }}break;
        case Screen::Maps:
            if(hit(x,y,478,118,62,62)){if(map_zoom<18)map_zoom++;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,478,174,62,70)){if(map_zoom>8)map_zoom--;draw_screen();refresh(MODE_GL16);return true;}
            break;
        case Screen::Discovery:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::More);return true;}
            if(ui_data)for(size_t i=0;i<ui_data->advert_count()&&i<5;++i)if(hit(x,y,12,120+i*150,516,142)){if(ui_data->open_advert(i)){details_from_discovery=true;details_page=0;open_screen(Screen::ContactDetails);}return true;}break;
        case Screen::More:
            if(hit(x,y,12,130,516,112)){open_screen(Screen::Discovery);return true;}
            if(hit(x,y,12,260,516,112)){open_screen(Screen::AdvertMenu);return true;}
            if(hit(x,y,12,390,516,112)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,520,516,112)){open_screen(Screen::CompanionConfirm);return true;}break;
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
            if(status_gps_fix&&hit(x,y,12,356,516,112)){map_latitude=status_gps_latitude/1000000.0;map_longitude=status_gps_longitude/1000000.0;open_screen(Screen::Maps);return true;}
            if(hit(x,y,12,474,516,112)){local_mesh_cycle_gps_interval();show_toast("GPS INTERVAL SAVED");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,592,516,112)){local_mesh_toggle_gps_advert_location();show_toast(local_mesh_gps_advert_location()?"POSITION SHARED":"POSITION HIDDEN");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,710,516,112)){open_screen(Screen::Timezone);return true;}break;
        case Screen::Timezone:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::GpsSettings);return true;}
            for(uint8_t i=0;i<TIMEZONE_COUNT;++i)if(hit(x,y,12,118+i*102,516,92)){timezone_index=i;apply_timezone();prefs.begin("t5-ui",false);prefs.putUChar("timezone",timezone_index);prefs.end();show_toast("TIMEZONE SAVED");draw_screen();refresh(MODE_GL16);return true;}break;
        case Screen::PrivacySettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            for(uint8_t i=0;i<6;++i)if(hit(x,y,12,130+i*118,516,112)){local_mesh_toggle_privacy(i);show_toast("SETTING SAVED");draw_screen();refresh(MODE_DU);return true;}return true;
        case Screen::DisplaySettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,118,516,112)){frontlight_mode=(FrontlightMode)(((uint8_t)frontlight_mode+1)%3);save_frontlight_settings();if(frontlight_mode==FrontlightMode::Off)frontlight_drive(false);else frontlight_event();show_toast(frontlight_mode_name());draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,238,516,112)){frontlight_timeout_index=(frontlight_timeout_index+1)%5;save_frontlight_settings();frontlight_event();show_toast(frontlight_timeout_name());draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,40,420,460,100)){int value=((int)x-62)*100/416;frontlight_brightness=(uint8_t)min(100,max(1,value));save_frontlight_settings();frontlight_event();Serial.printf("[T5-LIGHT] brightness=%u%%\n",frontlight_brightness);draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,538,516,112)){standby_timeout_index=(standby_timeout_index+1)%4;save_frontlight_settings();last_user_activity=millis();show_toast(standby_timeout_name());draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,12,630,516,50)){map_imperial=!map_imperial;prefs.begin("t5-ui",false);prefs.putBool("map_imperial",map_imperial);prefs.end();show_toast(map_imperial?"IMPERIAL SCALE":"METRIC SCALE");draw_screen();refresh(MODE_DU);return true;}
            if(frontlight_mode==FrontlightMode::NightTimer&&hit(x,y,24,674,492,70)){open_screen(Screen::NightSchedule);return true;}
            if(hit(x,y,24,frontlight_mode==FrontlightMode::NightTimer?758:674,492,70)){open_screen(Screen::ShutdownConfirm);return true;}break;
        case Screen::NightSchedule:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::DisplaySettings);return true;}
            if(hit(x,y,24,150,492,112)){night_edit_field=0;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,286,492,112)){night_edit_field=1;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,460,220,76)){uint16_t& value=night_edit_field ? night_end_minutes : night_start_minutes;value=(value+1410)%1440;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,296,460,220,76)){uint16_t& value=night_edit_field ? night_end_minutes : night_start_minutes;value=(value+30)%1440;draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,600,492,76)){save_frontlight_settings();frontlight_event();show_toast("SCHEDULE SAVED");draw_screen();refresh(MODE_DU);return true;}break;
        case Screen::About:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}break;
        default:break;
    }
    return true;
}

static void handle_tap(int16_t x,int16_t y) {
    last_user_activity=millis();
    Serial.printf("[T5-UI] tap x=%d y=%d\n",x,y);
    if(handle_landscape_keyboard(x,y))return;
    if(handle_app_tap(x,y))return;
    if(screen==Screen::Presets) {
        if(y>=48&&y<132){screen=preset_return_screen;draw_screen();refresh(MODE_GL16);return;}
        for(int row=0;row<PRESETS_PER_PAGE;++row) if(hit(x,y,12,132+row*128,516,112)){
            const int index=preset_page*PRESETS_PER_PAGE+row;if(index<PRESET_COUNT){selected_preset=index;saved=false;const bool applied=!mesh_is_ready||apply_selected_preset();prefs.begin("t5-ui",false);prefs.putUChar("preset_v2",selected_preset);prefs.end();screen=preset_return_screen;show_toast(applied?"RADIO PRESET APPLIED":"PRESET FAILED");draw_screen();refresh(MODE_GL16);}return;
        }
        const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
        if(hit(x,y,24,800,180,62)&&preset_page>0){preset_page--;draw_screen();refresh(MODE_GL16);return;}
        if(hit(x,y,336,800,180,62)&&preset_page+1<page_count){preset_page++;draw_screen();refresh(MODE_GL16);return;}
        return;
    }
    if(screen==Screen::CompanionConfirm){
        if(hit(x,y,30,500,220,72)){screen=setup_complete?Screen::More:Screen::Welcome;draw_screen();refresh(MODE_DU);return;}
        if(hit(x,y,290,500,220,72)){Serial.println("[T5-UI] companion mode confirmed");request_companion_mode();return;}
        return;
    }
    if(screen==Screen::ShutdownConfirm){
        if(hit(x,y,30,650,220,72)){open_screen(Screen::DisplaySettings);return;}
        if(hit(x,y,290,650,220,72)){request_hardware_shutdown();return;}
        return;
    }
    if(hit(x,y,30,180,480,64)){replace_name_on_type=true;keyboard_visible=true;Serial.println("[T5-UI] name selected; keyboard shown; next character replaces current name");draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,24,292,492,88)){preset_return_screen=Screen::Welcome;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MODE_GL16);return;}
    if(hit(x,y,30,402,480,52)){screen=Screen::CompanionConfirm;draw_screen();refresh(MODE_GL16);return;}
    if(!keyboard_visible){if(hit(x,y,30,840,480,64)){keyboard_visible=true;draw_screen();refresh(MODE_GL16);}return;}
    handle_name_keyboard(x,y);
}

static void set_touch_power(bool enabled){
    touch_enabled=false;delay(20);was_pressed=false;
    if(enabled){pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);pinMode(TOUCH_INT,OUTPUT);digitalWrite(TOUCH_INT,LOW);delay(10);digitalWrite(TOUCH_RST,HIGH);delay(60);pinMode(TOUCH_INT,INPUT);clear_touch();touch_enabled=true;if(touch_task_handle)xTaskNotifyGive(touch_task_handle);Serial.println("[T5-STANDBY] touch controller enabled");}
    else{pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);Serial.println("[T5-STANDBY] touch controller disabled");}
}

static void enter_standby(const char* reason){
    if(standby_active)return;standby_active=true;text_refresh_pending=false;toast_visible=false;frontlight_deadline=0;frontlight_drive(false);
    Serial.printf("[T5-STANDBY] entering reason=%s timeout=%s\n",reason,standby_timeout_name());draw_screen();fast_full_redraw("ENTER_STANDBY",false);set_touch_power(false);if(touch_queue)xQueueReset(touch_queue);set_cpu_target(80,"standby");
}

static void leave_standby(){
    if(!standby_active)return;set_touch_power(true);standby_active=false;last_user_activity=millis();message_alert_active=false;ledcWrite(FRONTLIGHT_PWM_CHANNEL,0);frontlight_lit=false;
    set_cpu_target(160,"wake");Serial.println("[T5-STANDBY] leaving; restoring local UI");draw_screen();fast_full_redraw("LEAVE_STANDBY",true);
}

static void start_message_alert(){
    const uint32_t now=millis();
    if(message_alert_active){Serial.println("[T5-STANDBY] message alert coalesced into active sequence");return;}
    if((int32_t)(now-message_alert_cooldown_until)<0){Serial.println("[T5-STANDBY] message alert suppressed by cooldown");return;}
    message_alert_active=true;message_alert_phase=0;message_alert_deadline=now;
    Serial.println("[T5-STANDBY] combined EPD/frontlight message alert started");
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
            Serial.println("[T5-STANDBY] combined message alert complete; standby screen restored with GC16");
            break;
    }
}

static void service_boot_button(){
    static uint32_t pressed_at=0;static bool handled=false;const bool pressed=digitalRead(BOOT_BUTTON)==LOW;
    if(pressed&&!pressed_at)pressed_at=millis();
    if(pressed&&!handled&&pressed_at&&millis()-pressed_at>=2000){handled=true;if(standby_active)leave_standby();else enter_standby("BOOT");}
    if(!pressed&&pressed_at){const uint32_t duration=millis()-pressed_at;if(!handled&&duration>=40){draw_screen();fast_full_redraw("SHORT_BOOT",false);}pressed_at=0;handled=false;}
}

void ui_setup() {
    Serial.begin(115200); delay(200);
    Serial.printf("[T5-UI] onboarding %s boot heap=%u psram=%u; Bluetooth disabled\n",UI_VERSION,ESP.getFreeHeap(),ESP.getFreePsram());
    pinMode(BOOT_BUTTON,INPUT_PULLUP);pinMode(FRONTLIGHT,OUTPUT);digitalWrite(FRONTLIGHT,HIGH);
    ledcSetup(FRONTLIGHT_PWM_CHANNEL,5000,8);ledcAttachPin(FRONTLIGHT,FRONTLIGHT_PWM_CHANNEL);ledcWrite(FRONTLIGHT_PWM_CHANNEL,255);
    pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);pinMode(TOUCH_INT,OUTPUT);digitalWrite(TOUCH_INT,LOW);
    epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);epd_set_lcd_pixel_clock_MHz(17);
    recover_pmic_power_path();
    delay(10);digitalWrite(TOUCH_RST,HIGH);delay(60);pinMode(TOUCH_INT,INPUT);
    display=epd_hl_init(EPD_BUILTIN_WAVEFORM);fb=epd_hl_get_framebuffer(&display);
    prefs.begin("t5-ui",true);String saved_name=prefs.getString("name","");selected_preset=prefs.getUChar("preset_v2",17);setup_complete=prefs.getBool("complete",false);timezone_index=prefs.getUChar("timezone",0);status_unread=prefs.getUShort("unread_dm",0);status_channel_unread=prefs.getUShort("unread_ch",0);
    frontlight_mode=(FrontlightMode)prefs.getUChar("light_mode",(uint8_t)FrontlightMode::On);frontlight_timeout_index=prefs.getUChar("light_timeout",2);frontlight_brightness=prefs.getUChar("light_level",60);standby_timeout_index=prefs.getUChar("standby_timeout",1);night_start_minutes=prefs.getUShort("night_start",20*60);night_end_minutes=prefs.getUShort("night_end",7*60);map_imperial=prefs.getBool("map_imperial",false);prefs.end();
    if((uint8_t)frontlight_mode>(uint8_t)FrontlightMode::Off)frontlight_mode=FrontlightMode::On;
    if(frontlight_timeout_index>4)frontlight_timeout_index=2;if(frontlight_brightness<1||frontlight_brightness>100)frontlight_brightness=60;
    if(standby_timeout_index>3)standby_timeout_index=1;
    if(night_start_minutes>=1440)night_start_minutes=20*60;if(night_end_minutes>=1440)night_end_minutes=7*60;
    if(selected_preset>=PRESET_COUNT)selected_preset=17;
    if(timezone_index>=TIMEZONE_COUNT)timezone_index=0;apply_timezone();
    if(saved_name.length()){
        size_t out=0;
        for(size_t i=0;i<saved_name.length()&&out<20;++i){const char c=saved_name[i];if(legal_name_character(c))node_name[out++]=c;else Serial.printf("[T5-UI] discarded stored illegal name character 0x%02X\n",(unsigned char)c);}
        node_name[out]=0;
    }
    if(setup_complete){screen=Screen::Contacts;keyboard_visible=false;}
    update_status_hardware();
    epd_hl_set_all_white(&display);centred("MESHCORE",290,7,0,true);centred(UI_VERSION,900,2);
    epd_poweron();epd_clear();epd_poweroff();refresh(MODE_GL16);delay(700);
    draw_screen();refresh(MODE_GL16);
    touch_queue=xQueueCreate(32,sizeof(QueuedTap));
    if(touch_queue&&xTaskCreatePinnedToCore(touch_sampler_task,"t5-touch",4096,nullptr,1,&touch_task_handle,0)==pdPASS)Serial.println("[T5-TOUCH] sampler running; interval=8ms queue depth=32");
    else Serial.println("[T5-TOUCH] ERROR: sampler could not start");
    Serial.printf("[T5-LIGHT] mode=%s timeout=%s brightness=%u%% night=%02u:%02u-%02u:%02u\n",frontlight_mode_name(),frontlight_timeout_name(),frontlight_brightness,night_start_minutes/60,night_start_minutes%60,night_end_minutes/60,night_end_minutes%60);
    set_cpu_target(160,"ui-ready");last_user_activity=millis();Serial.println("[T5-UI] touch ready; waiting for input");
}

void ui_loop() {
    if(hardware_failure){
        static uint32_t report_at=0;if(millis()-report_at>=60000){report_at=millis();Serial.println("[T5-ERROR] radio unavailable; startup halted; press RST to retry");}
        delay(100);return;
    }
    service_boot_button();
    const uint32_t standby_timeout=STANDBY_TIMEOUTS[min((uint8_t)3,standby_timeout_index)];
    if(!standby_active&&standby_timeout&&millis()-last_user_activity>=standby_timeout)enter_standby("TIMEOUT");
    QueuedTap tap{};
    while(!standby_active&&touch_queue&&xQueueReceive(touch_queue,&tap,0)==pdTRUE){
        last_user_activity=millis();
        if(tap.home){if(!keyboard_visible&&!keyboard_landscape){details_page=0;open_screen(Screen::Contacts);}continue;}
        if(screen==Screen::Presets&&abs(tap.dy)>60){
            const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
            int next=(int)preset_page+(tap.dy<0?1:-1);if(next<0)next=0;if(next>=page_count)next=page_count-1;
            preset_page=(uint8_t)next;Serial.printf("[T5-UI] preset page=%u\n",preset_page+1);draw_screen();refresh(MODE_GL16);
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
    static uint32_t power_report_at=0,loop_count=0;loop_count++;
    if(millis()-power_report_at>=60000){const uint32_t elapsed=static_cast<uint32_t>(millis()-power_report_at);Serial.printf("[T5-POWER] health cpu=%luMHz apb=%luMHz standby=%d loops=%lu/s heap=%u psram=%u stack=%u touch=%s\n",(unsigned long)getCpuFrequencyMhz(),(unsigned long)(getApbFrequency()/1000000),standby_active,(unsigned long)(loop_count*1000/elapsed),ESP.getFreeHeap(),ESP.getFreePsram(),(unsigned)uxTaskGetStackHighWaterMark(nullptr),touch_enabled?"active":"suspended");power_report_at=millis();loop_count=0;}
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
    if(state_changed)Serial.printf("[T5-GPS] state %s sats=%d lat=%ld lon=%ld\n",enabled?(has_fix?"fixed":"searching"):"disabled",satellites,latitude,longitude);
    status_gps_enabled=enabled;status_gps_fix=has_fix;status_gps_satellites=satellites;status_gps_latitude=latitude;status_gps_longitude=longitude;status_gps_timestamp=timestamp;
    static uint32_t last_detail_refresh=0;if(state_changed||(detail_changed&&screen==Screen::GpsSettings&&millis()-last_detail_refresh>=10000)){last_detail_refresh=millis();status_dirty=true;Serial.println("[T5-UI] refresh queued reason=gps-state");}
}

void ui_notify_message_received(bool channel){
    const bool visible=channel?screen==Screen::ChannelChat:screen==Screen::ContactChat;
    if(!visible){if(channel){if(status_channel_unread<65535)status_channel_unread++;}else if(status_unread<65535)status_unread++;persist_unread();}
    status_dirty=true;if(standby_active)start_message_alert();else status_wake_light=true;Serial.printf("[T5-UI] %s message event unread=%u refresh queued standby=%d\n",channel?"channel":"direct",channel?status_channel_unread:status_unread,standby_active);
}

void ui_notify_advert_result(bool flood,bool ok){
    show_toast(ok?(flood?"FLOOD ADVERT SENT":"ZERO HOP ADVERT SENT"):"ADVERT FAILED");
    status_dirty=true;Serial.printf("[T5-UI] advert result flood=%d ok=%d\n",flood,ok);
}

void ui_request_data_refresh(const char* reason){
    status_dirty=true;Serial.printf("[T5-UI] refresh queued reason=%s\n",reason?reason:"data");
}

void ui_mesh_ready(){
    mesh_is_ready=true;Preferences state;bool migrated=false;if(state.begin("t5-ui",false)){migrated=state.getBool("name_migrated",false);if(!migrated&&node_name[0]){local_mesh_apply_name(node_name);state.putBool("name_migrated",true);Serial.printf("[T5-UI] migrated node name to MeshCore '%s'\n",node_name);}else{strncpy(node_name,local_mesh_node_name(),sizeof(node_name)-1);node_name[sizeof(node_name)-1]=0;Serial.printf("[T5-UI] node name loaded from MeshCore '%s'\n",node_name);}state.putString("name",node_name);state.end();}
    update_status_hardware();status_dirty=true;
}

void ui_use_data_provider(UiDataProvider* provider) {
    if (!provider) return;
    ui_data=provider;
    Serial.println("[T5-UI] live MeshCore data provider attached");
    draw_screen();
    refresh(MODE_GL16);
}

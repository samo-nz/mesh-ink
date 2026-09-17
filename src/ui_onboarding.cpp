#include <Arduino.h>
#include <Preferences.h>
#include <epdiy.h>
#include <driver/i2c.h>
#include <esp_heap_caps.h>
#include "ui_onboarding.h"
#include "ui_data.h"
#include "local_mesh_runtime.h"

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "0.4.0"
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
static uint16_t status_unread = 0;
static uint16_t status_channel_unread = 0;
static bool status_gps_enabled = false;
static bool status_gps_fix = false;
static int16_t status_battery = -1;
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
static MockUiDataProvider mock_data;
static UiDataProvider* ui_data = &mock_data;
static size_t selected_contact = 0;
static size_t selected_channel = 0;
static int16_t cached_touch_x = 0, cached_touch_y = 0;
enum class Screen : uint8_t {
    Welcome, Presets, CompanionConfirm,
    Contacts, ContactChat, ContactDetails,
    Channels, ChannelChat, Discovery,
    Settings, RadioSettings, GpsSettings, DisplaySettings, About
};
static Screen screen = Screen::Welcome;
static Screen preset_return_screen = Screen::Welcome;
static uint8_t preset_page = 3;
static constexpr uint8_t PRESETS_PER_PAGE = 5;

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

static void draw_keyboard() {
    const char* numbers="1234567890";
    for(int i=0;numbers[i];++i){char label[2]={numbers[i],0};key(label,15+i*52,618,49);}
    const char* letter_rows_upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    const char* letter_rows_lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbol_rows[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    const char** rows=keyboard_symbols?symbol_rows:(keyboard_upper?letter_rows_upper:letter_rows_lower);
    const int starts[]={15,41,93};const int ys[]={688,758,828};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i){char label[2]={rows[r][i],0};key(label,starts[r]+i*52,ys[r],49);}
    key(keyboard_upper?"Aa":"aA",12,828,76);
    key(keyboard_symbols?"ABC":"SYM",460,828,68);
    key("DEL",12,898,100);key("SPACE",120,898,190);key("HIDE",318,898,100);key(keyboard_message_mode?"SEND":"SAVE",426,898,102);
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

static void draw_battery_icon(int x,int y) {
    epd_draw_rect({x,y+6,31,18},0,fb);epd_fill_rect({x+31,y+11,4,8},0,fb);
    if(status_battery>0){const int fill=(status_battery*27)/100;epd_fill_rect({x+2,y+8,fill,14},0,fb);}
}

static void draw_status_bar() {
    epd_fill_rect({0,0,540,48},0xFF,fb);
    epd_draw_rect({0,0,540,48},0,fb);
    if(!status_gps_enabled)draw_target_icon(6,9,true);
    else if(status_gps_fix)draw_target_icon(6,9,false);
    else draw_search_icon(6,9);
    int left=46;
    if(status_unread){draw_envelope_icon(left,9);left+=36;char count[7];snprintf(count,sizeof(count),"%u",status_unread);text(count,left,13,3,0,true);left+=(int)strlen(count)*18+12;}
    if(status_channel_unread){text("#",left,13,3,0,true);left+=22;char count[7];snprintf(count,sizeof(count),"%u",status_channel_unread);text(count,left,13,3,0,true);}
    char clock_text[8];
    if(status_hour>=0)snprintf(clock_text,sizeof(clock_text),"%02d:%02d",status_hour,status_minute);
    else snprintf(clock_text,sizeof(clock_text),"--:--");
    centred(clock_text,13,3,0,true);
    char battery[8];
    if(status_battery>=0)snprintf(battery,sizeof(battery),"%d%%",status_battery);
    else snprintf(battery,sizeof(battery),"--%%");
    const int battery_x=530-(int)strlen(battery)*18;
    draw_battery_icon(battery_x-43,8);
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
    static const char* labels[]={"CONTACT","CHANNEL","DISCOVER","SETTINGS"};
    for(int i=0;i<4;++i){box(i*135,900,135,60,i==selected);const uint8_t color=i==selected?0xFF:0;text(labels[i],i*135+(135-(int)strlen(labels[i])*12)/2,920,2,color,true);}
}

static void draw_app_header(const char* title,bool back=false,const char* action=nullptr) {
    epd_hl_set_all_white(&display);draw_status_bar();
    if(back)text("< BACK",16,70,2,0,true);
    centred(title,64,4,0,true);
    if(action)text(action,540-(int)strlen(action)*12-16,70,2,0,true);
}

static void draw_list_entry(const UiListEntry& item,int y) {
    box(12,y,516,142);
    text(item.title,28,y+16,3,0,true);
    text(item.time,528-(int)strlen(item.time)*12-16,y+20,2,0,true);
    draw_wrapped(item.subtitle,28,y+60,36,2,0,false,2);
    if(item.unread){box(462,y+88,48,38,true);char n[6];snprintf(n,sizeof(n),"%u",item.unread);text(n,486-(int)strlen(n)*6,y+97,2,0xFF,true);}
}

static void draw_contacts() {
    draw_app_header("CONTACTS");
    for(size_t i=0;i<ui_data->contact_count()&&i<5;++i)draw_list_entry(ui_data->contact(i),120+i*150);
    draw_bottom_nav(0);
}

static void draw_channels() {
    draw_app_header("CHANNELS");
    for(size_t i=0;i<ui_data->channel_count()&&i<5;++i)draw_list_entry(ui_data->channel(i),120+i*150);
    draw_bottom_nav(1);
}

static void draw_message_bubble(const UiMessage& message,int y) {
    const int x=message.outgoing?82:12,w=446;box(x,y,w,142,message.outgoing);
    draw_wrapped(message.text,x+16,y+12,23,3,message.outgoing?0xFF:0,false,3);
    text(message.time,x+w-(int)strlen(message.time)*12-12,y+114,2,message.outgoing?0xFF:0,true);
}

static void draw_chat(bool channel) {
    draw_app_header(channel?"# GENERAL":"ALICE",true,channel?nullptr:"INFO");
    const size_t count=channel?ui_data->channel_message_count():ui_data->direct_message_count();
    const size_t shown=min((size_t)3,count),first=count-shown;
    for(size_t i=0;i<shown;++i)draw_message_bubble(channel?ui_data->channel_message(first+i):ui_data->direct_message(first+i),126+i*150);
    box(12,580,516,62);text(compose_text[0]?compose_text:"TAP TO WRITE A MESSAGE",28,600,2,0,false);
    if(keyboard_visible&&keyboard_message_mode)draw_keyboard();
    else {box(360,820,168,62,true);text("COMPOSE",381,841,3,0xFF,true);}
}

static void draw_contact_details() {
    draw_app_header("CONTACT",true);
    centred("ALICE",132,5,0,true);centred("CHAT NODE",184,2,0,true);
    text("LAST SEEN",24,250,2,0,true);text("JUST NOW",280,250,2);
    text("SIGNAL",24,304,2,0,true);text("SNR 8.5  RSSI -91",280,304,2);
    text("ROUTE",24,358,2,0,true);text("DIRECT",280,358,2);
    text("LOCATION",24,412,2,0,true);text("42.72 S  170.96 E",280,412,2);
    text("PUBLIC KEY",24,466,2,0,true);text("8A71...C42E",280,466,2);
    box(24,560,492,70);centred("START CONVERSATION",584,3,0,true);
    box(24,660,492,70);centred("DELETE CONTACT",684,3,0,true);
}

static void draw_discovery() {
    draw_app_header("DISCOVERED");
    static constexpr UiListEntry nodes[]={
        {"ARTHURS PASS", "CHAT NODE  2 MIN AGO", "",0},{"REPEATER 12", "REPEATER  SNR 6.2", "",0},
        {"FIELD SENSOR", "SENSOR  TEMPERATURE 14 C", "",0},{"ROOM WEST", "ROOM SERVER  1 HOUR AGO", "",0}};
    for(int i=0;i<4;++i)draw_list_entry(nodes[i],120+i*150);
    draw_bottom_nav(2);
}

static void settings_row(const char* title,const char* subtitle,int y) {
    box(12,y,516,112);text(title,28,y+14,3,0,true);text(subtitle,28,y+58,2);text(">",494,y+42,3,0,true);
}

static void draw_settings() {
    draw_app_header("SETTINGS");
    settings_row("NODE AND IDENTITY",local_mesh_node_name(),118);settings_row("RADIO",local_mesh_radio_summary(),238);
    settings_row("LOCATION AND GPS","POSITION, INTERVAL, ADVERT",358);settings_row("CONTACTS AND PRIVACY","AUTO ADD AND TELEMETRY",478);
    settings_row("BLUETOOTH COMPANION","RESTART IN COMPANION MODE",598);settings_row("ABOUT","FIRMWARE AND DEVICE INFO",718);
    draw_bottom_nav(3);
}

static void draw_radio_settings() {
    draw_app_header("RADIO",true);settings_row("REGION PRESET",PRESETS[selected_preset].title,140);
    settings_row("ACTIVE RADIO",local_mesh_radio_summary(),270);settings_row("PATH HASH MODE","1 BYTE",400);
    settings_row("TRANSMIT POWER","22 DBM",530);settings_row("ADVERT INTERVAL","ZERO HOP  60 MIN",660);
}

static void draw_gps_settings() {
    draw_app_header("LOCATION",true);settings_row("GPS POWER",local_mesh_gps_enabled()?"ENABLED":"DISABLED",160);
    settings_row("FIX STATUS",status_gps_fix?"POSITION FIXED":"NO FIX",300);settings_row("POSITION ADVERT","EVERY 30 MINUTES",440);
    box(24,600,492,70);centred(status_gps_fix?"CLEAR TEST FIX":"SIMULATE GPS FIX",624,3,0,true);
}

static void draw_display_settings() {
    draw_app_header("CONTACTS & PRIVACY",true);
    settings_row("AUTO ADD CONTACTS",local_mesh_privacy_value(0),130);settings_row("AUTO ADD MAX HOPS",local_mesh_privacy_value(1),248);
    settings_row("ADVERTISE LOCATION",local_mesh_privacy_value(2),366);settings_row("BASE TELEMETRY",local_mesh_privacy_value(3),484);
    settings_row("LOCATION TELEMETRY",local_mesh_privacy_value(4),602);settings_row("PACKET REPEATING",local_mesh_privacy_value(5),720);
    settings_row("STANDBY","LONG PRESS BOOT",440);settings_row("FULL REFRESH","EVERY 10 UPDATES",580);
}

static void draw_about() {
    draw_app_header("ABOUT",true);centred("MESHCORE T5 PAPER",150,4,0,true);centred(UI_VERSION,210,3,0,true);
    text("HARDWARE",24,300,2,0,true);text("LILYGO T5 PRO",250,300,2);
    text("MODE",24,354,2,0,true);text("LOCAL UI PROTOTYPE",250,354,2);
    text("DATA",24,408,2,0,true);text("MOCK PROVIDER",250,408,2);
    text("RADIO",24,462,2,0,true);text("NOT STARTED",250,462,2);
    draw_wrapped("This inspection build demonstrates navigation and layout. MeshCore integration follows after approval.",24,550,38,2,0,false,4);
}

static void draw_screen() {
    switch(screen){
        case Screen::Welcome:draw_welcome();break;case Screen::Presets:draw_presets();break;case Screen::CompanionConfirm:draw_companion_confirm();break;
        case Screen::Contacts:draw_contacts();break;case Screen::ContactChat:draw_chat(false);break;case Screen::ContactDetails:draw_contact_details();break;
        case Screen::Channels:draw_channels();break;case Screen::ChannelChat:draw_chat(true);break;case Screen::Discovery:draw_discovery();break;
        case Screen::Settings:draw_settings();break;case Screen::RadioSettings:draw_radio_settings();break;case Screen::GpsSettings:draw_gps_settings();break;
        case Screen::DisplaySettings:draw_display_settings();break;case Screen::About:draw_about();break;
    }
    draw_toast();
}

static void refresh(EpdDrawMode mode) {
    epd_poweron();
    const EpdDrawError err = epd_hl_update_screen(&display,mode,(int)epd_ambient_temperature());
    epd_poweroff();
    Serial.printf("[T5-UI] refresh=%d name='%s' preset=%s\n",err,node_name,PRESETS[selected_preset].title);
}

static bool i2c_read(uint16_t reg, uint8_t* data, size_t len) {
    uint8_t address[2] = {(uint8_t)(reg>>8),(uint8_t)reg};
    return i2c_master_write_read_device(I2C_NUM_0,GT911_ADDR,address,2,data,len,pdMS_TO_TICKS(20)) == ESP_OK;
}
static bool i2c_read8(uint8_t device,uint8_t reg,uint8_t* data,size_t len) {
    return i2c_master_write_read_device(I2C_NUM_0,device,&reg,1,data,len,pdMS_TO_TICKS(20))==ESP_OK;
}
static uint8_t from_bcd(uint8_t value) { return (value>>4)*10+(value&0x0F); }
static bool update_status_hardware() {
    const int8_t old_hour=status_hour,old_minute=status_minute;
    const int16_t old_battery=status_battery;
    uint8_t rtc[3]={};
    if(i2c_read8(0x51,0x02,rtc,sizeof(rtc))){
        const uint8_t hour=from_bcd(rtc[2]&0x3F),minute=from_bcd(rtc[1]&0x7F);
        if(hour<24&&minute<60){status_hour=hour;status_minute=minute;}
    }
    uint8_t gauge[2]={};
    if(i2c_read8(0x55,0x2C,gauge,sizeof(gauge))){
        const uint16_t soc=(uint16_t)(gauge[0]|((uint16_t)gauge[1]<<8));
        if(soc<=100)status_battery=(int16_t)soc;
    }
    const bool changed=old_hour!=status_hour||old_minute!=status_minute||old_battery!=status_battery;
    if(changed)Serial.printf("[T5-UI] status clock=%02d:%02d battery=%d%% direct=%u channel=%u gps=%s\n",
        status_hour,status_minute,status_battery,status_unread,status_channel_unread,
        status_gps_enabled?(status_gps_fix?"fix":"searching"):"off");
    return changed;
}
static void clear_touch() {
    uint8_t data[3] = {0x81,0x4E,0};
    i2c_master_write_to_device(I2C_NUM_0,GT911_ADDR,data,3,pdMS_TO_TICKS(20));
}
static bool touch_point(int16_t& x, int16_t& y) {
    uint8_t status=0;
    if (!i2c_read(0x814E,&status,1) || !(status&0x80)) { x=cached_touch_x; y=cached_touch_y; return was_pressed; }
    const uint8_t count=status&0x0F;
    if (!count || count>5) { clear_touch(); was_pressed=false; x=cached_touch_x; y=cached_touch_y; return false; }
    uint8_t p[8]={};
    if (!i2c_read(0x814F,p,sizeof(p))) return was_pressed;
    x=(int16_t)(p[1]|((uint16_t)p[2]<<8)); y=(int16_t)(p[3]|((uint16_t)p[4]<<8));
    cached_touch_x=x; cached_touch_y=y;
    clear_touch(); was_pressed=true; return true;
}

static bool legal_name_character(char c) { return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
static void append(char c) {
    if(keyboard_message_mode){size_t n=strlen(compose_text);if(n<48){compose_text[n]=c;compose_text[n+1]=0;}return;}
    if(!legal_name_character(c)){Serial.printf("[T5-UI] discarded illegal name character 0x%02X\n",(unsigned char)c);return;}
    if (replace_name_on_type) { node_name[0]=0; replace_name_on_type=false; }
    size_t n=strlen(node_name); if (n<20) { node_name[n]=c; node_name[n+1]=0; saved=false; }
}
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh) { return x>=bx&&x<bx+bw&&y>=by&&y<by+bh; }

static void open_screen(Screen next) { keyboard_visible=false;keyboard_message_mode=false;screen=next;draw_screen();refresh(MODE_GL16); }

static bool handle_message_keyboard(int16_t x,int16_t y) {
    if(!keyboard_visible||!keyboard_message_mode)return false;
    const char* numbers="1234567890";for(int i=0;numbers[i];++i)if(hit(x,y,15+i*52,618,49,62)){append(numbers[i]);draw_screen();refresh(MODE_DU);return true;}
    const char* upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};const char* lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbols[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};const char** rows=keyboard_symbols?symbols:(keyboard_upper?upper:lower);
    const int starts[]={15,41,93};const int ys[]={688,758,828};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i)if(hit(x,y,starts[r]+i*52,ys[r],49,62)){append(rows[r][i]);draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,12,828,76,62)){keyboard_upper=!keyboard_upper;keyboard_symbols=false;draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,460,828,68,62)){keyboard_symbols=!keyboard_symbols;draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,12,898,100,62)){size_t n=strlen(compose_text);if(n)compose_text[n-1]=0;draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,120,898,190,62)){append(' ');draw_screen();refresh(MODE_DU);return true;}
    if(hit(x,y,318,898,100,62)){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return true;}
    if(hit(x,y,426,898,102,62)){if(compose_text[0]){const bool ok=screen==Screen::ChannelChat?local_mesh_send_channel(selected_channel,compose_text):local_mesh_send_direct(selected_contact,compose_text);compose_text[0]=0;keyboard_visible=false;show_toast(ok?"MESSAGE QUEUED":"SEND FAILED");draw_screen();refresh(MODE_DU);}return true;}
    return true;
}

static bool handle_app_tap(int16_t x,int16_t y) {
    if(screen==Screen::Welcome||screen==Screen::Presets||screen==Screen::CompanionConfirm)return false;
    if((screen==Screen::ContactChat||screen==Screen::ChannelChat)&&handle_message_keyboard(x,y))return true;
    if(y>=900){const int tab=min(3,max(0,(int)x/135));open_screen(tab==0?Screen::Contacts:tab==1?Screen::Channels:tab==2?Screen::Discovery:Screen::Settings);return true;}
    switch(screen){
        case Screen::Contacts:
            for(size_t i=0;i<ui_data->contact_count()&&i<5;++i)if(hit(x,y,12,120+i*150,516,142)){selected_contact=i;open_screen(Screen::ContactChat);return true;}break;
        case Screen::Channels:
            for(size_t i=0;i<ui_data->channel_count()&&i<5;++i)if(hit(x,y,12,120+i*150,516,142)){selected_channel=i;open_screen(Screen::ChannelChat);return true;}break;
        case Screen::ContactChat:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Contacts);return true;}
            if(hit(x,y,430,48,110,70)){open_screen(Screen::ContactDetails);return true;}
            if(hit(x,y,12,580,516,62)||hit(x,y,360,820,168,62)){keyboard_message_mode=true;keyboard_visible=true;draw_screen();refresh(MODE_GL16);return true;}break;
        case Screen::ChannelChat:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Channels);return true;}
            if(hit(x,y,12,580,516,62)||hit(x,y,360,820,168,62)){keyboard_message_mode=true;keyboard_visible=true;draw_screen();refresh(MODE_GL16);return true;}break;
        case Screen::ContactDetails:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::ContactChat);return true;}
            if(hit(x,y,24,560,492,70)){open_screen(Screen::ContactChat);return true;}
            if(hit(x,y,24,660,492,70)){show_toast("DEMO ONLY");draw_screen();refresh(MODE_DU);return true;}break;
        case Screen::Discovery:
            for(int i=0;i<4;++i)if(hit(x,y,12,120+i*150,516,142)){show_toast("CONTACT ADDED");draw_screen();refresh(MODE_DU);return true;}break;
        case Screen::Settings:
            if(hit(x,y,12,118,516,112)){keyboard_message_mode=false;keyboard_visible=false;screen=Screen::Welcome;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,12,238,516,112)){open_screen(Screen::RadioSettings);return true;}
            if(hit(x,y,12,358,516,112)){open_screen(Screen::GpsSettings);return true;}
            if(hit(x,y,12,478,516,112)){open_screen(Screen::DisplaySettings);return true;}
            if(hit(x,y,12,598,516,112)){open_screen(Screen::CompanionConfirm);return true;}
            if(hit(x,y,12,718,516,112)){open_screen(Screen::About);return true;}break;
        case Screen::RadioSettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,140,516,112)){preset_return_screen=Screen::RadioSettings;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MODE_GL16);return true;}
            if(hit(x,y,12,400,516,112)){local_mesh_cycle_path_hash();show_toast("PATH MODE SAVED");draw_screen();refresh(MODE_DU);return true;}
            return true;
        case Screen::GpsSettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            if(hit(x,y,12,160,516,112)){local_mesh_apply_gps(!local_mesh_gps_enabled());show_toast(local_mesh_gps_enabled()?"GPS ENABLED":"GPS DISABLED");draw_screen();refresh(MODE_DU);return true;}
            if(hit(x,y,24,600,492,70)){status_gps_enabled=true;status_gps_fix=!status_gps_fix;show_toast(status_gps_fix?"GPS FIX ACQUIRED":"GPS FIX CLEARED");draw_screen();refresh(MODE_DU);return true;}break;
        case Screen::DisplaySettings:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}
            for(uint8_t i=0;i<6;++i)if(hit(x,y,12,130+i*118,516,112)){local_mesh_toggle_privacy(i);show_toast("SETTING SAVED");draw_screen();refresh(MODE_DU);return true;}return true;
        case Screen::About:
            if(hit(x,y,0,48,110,70)){open_screen(Screen::Settings);return true;}break;
        default:break;
    }
    return true;
}

static void handle_tap(int16_t x,int16_t y) {
    Serial.printf("[T5-UI] tap x=%d y=%d\n",x,y);
    if(handle_app_tap(x,y))return;
    if(screen==Screen::Presets) {
        if(y>=48&&y<132){screen=preset_return_screen;draw_screen();refresh(MODE_GL16);return;}
        for(int row=0;row<PRESETS_PER_PAGE;++row) if(hit(x,y,12,132+row*128,516,112)){
            const int index=preset_page*PRESETS_PER_PAGE+row;if(index<PRESET_COUNT){selected_preset=index;saved=false;screen=preset_return_screen;show_toast("PRESET SELECTED");draw_screen();refresh(MODE_GL16);}return;
        }
        const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
        if(hit(x,y,24,800,180,62)&&preset_page>0){preset_page--;draw_screen();refresh(MODE_GL16);return;}
        if(hit(x,y,336,800,180,62)&&preset_page+1<page_count){preset_page++;draw_screen();refresh(MODE_GL16);return;}
        return;
    }
    if(screen==Screen::CompanionConfirm){
        if(hit(x,y,30,500,220,72)){screen=setup_complete?Screen::Settings:Screen::Welcome;draw_screen();refresh(MODE_DU);return;}
        if(hit(x,y,290,500,220,72)){Serial.println("[T5-UI] companion mode confirmed");request_companion_mode();return;}
        return;
    }
    if(hit(x,y,30,180,480,64)){replace_name_on_type=true;keyboard_visible=true;Serial.println("[T5-UI] name selected; keyboard shown; next character replaces current name");draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,24,292,492,88)){preset_return_screen=Screen::Welcome;screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MODE_GL16);return;}
    if(hit(x,y,30,402,480,52)){screen=Screen::CompanionConfirm;draw_screen();refresh(MODE_GL16);return;}
    if(!keyboard_visible){if(hit(x,y,30,840,480,64)){keyboard_visible=true;draw_screen();refresh(MODE_GL16);}return;}
    const char* numbers="1234567890";for(int i=0;numbers[i];++i)if(hit(x,y,15+i*52,618,49,62)){append(numbers[i]);draw_screen();refresh(MODE_DU);return;}
    const char* upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};const char* lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbols[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};const char** rows=keyboard_symbols?symbols:(keyboard_upper?upper:lower);
    const int starts[]={15,41,93};const int ys[]={688,758,828};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i)if(hit(x,y,starts[r]+i*52,ys[r],49,62)){append(rows[r][i]);draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,12,828,76,62)){keyboard_upper=!keyboard_upper;keyboard_symbols=false;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,460,828,68,62)){keyboard_symbols=!keyboard_symbols;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,12,898,100,62)){size_t n=strlen(node_name);if(n)node_name[n-1]=0;saved=false;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,120,898,190,62)){return;}
    if(hit(x,y,318,898,100,62)){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return;}
    if(hit(x,y,426,898,102,62)){
        prefs.begin("t5-ui",false);prefs.putString("name",node_name);prefs.putUChar("preset_v2",selected_preset);prefs.putBool("complete",true);prefs.end();
        saved=true;setup_complete=true;toast_opens_main=true;show_toast("SETTINGS SAVED");draw_screen();refresh(MODE_DU);
    }
}

void ui_setup() {
    Serial.begin(115200); delay(200);
    Serial.printf("[T5-UI] onboarding %s boot heap=%u psram=%u; Bluetooth disabled\n",UI_VERSION,ESP.getFreeHeap(),ESP.getFreePsram());
    pinMode(FRONTLIGHT,OUTPUT);digitalWrite(FRONTLIGHT,HIGH);
    pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);pinMode(TOUCH_INT,OUTPUT);digitalWrite(TOUCH_INT,LOW);
    epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);epd_set_lcd_pixel_clock_MHz(17);
    delay(10);digitalWrite(TOUCH_RST,HIGH);delay(60);pinMode(TOUCH_INT,INPUT);
    display=epd_hl_init(EPD_BUILTIN_WAVEFORM);fb=epd_hl_get_framebuffer(&display);
    prefs.begin("t5-ui",true);String saved_name=prefs.getString("name","");selected_preset=prefs.getUChar("preset_v2",17);setup_complete=prefs.getBool("complete",false);prefs.end();
    if(selected_preset>=PRESET_COUNT)selected_preset=17;
    if(saved_name.length()){
        size_t out=0;
        for(size_t i=0;i<saved_name.length()&&out<20;++i){const char c=saved_name[i];if(legal_name_character(c))node_name[out++]=c;else Serial.printf("[T5-UI] discarded stored illegal name character 0x%02X\n",(unsigned char)c);}
        node_name[out]=0;
    }
    if(setup_complete){screen=Screen::Contacts;keyboard_visible=false;status_unread=3;status_channel_unread=4;}
    update_status_hardware();
    epd_hl_set_all_white(&display);centred("MESHCORE",290,7,0,true);centred(UI_VERSION,900,2);
    epd_poweron();epd_clear();epd_poweroff();refresh(MODE_GL16);delay(700);
    draw_screen();refresh(MODE_GL16);
    Serial.println("[T5-UI] touch ready; waiting for input");
}

void ui_loop() {
    static bool held=false;static int16_t start_x=0,start_y=0,last_x=0,last_y=0;
    int16_t x=0,y=0;const bool pressed=touch_point(x,y);
    if(pressed){if(!held){held=true;start_x=last_x=x;start_y=last_y=y;}else{last_x=x;last_y=y;}}
    if(!pressed&&held){
        held=false;const int dy=last_y-start_y;
        if(screen==Screen::Presets&&abs(dy)>60){
            const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
            int next=(int)preset_page+(dy<0?1:-1);if(next<0)next=0;if(next>=page_count)next=page_count-1;
            preset_page=(uint8_t)next;Serial.printf("[T5-UI] preset page=%u\n",preset_page+1);draw_screen();refresh(MODE_GL16);
        }else handle_tap(last_x,last_y);
    }
    static uint32_t last_status_poll=0;
    if(millis()-last_status_poll>=15000){
        last_status_poll=millis();
        if(update_status_hardware())status_dirty=true;
    }
    if(status_dirty&&!held){status_dirty=false;draw_screen();refresh(MODE_DU);}
    if(toast_visible&&(int32_t)(millis()-toast_until)>=0&&!held){
        toast_visible=false;
        if(toast_opens_main){toast_opens_main=false;screen=Screen::Contacts;keyboard_visible=false;keyboard_message_mode=false;status_unread=3;status_channel_unread=4;}
        draw_screen();refresh(MODE_DU);
    }
    delay(12);
}

void ui_status_set_unread(uint16_t count) {
    if(status_unread!=count){status_unread=count;status_dirty=true;}
}

void ui_status_set_channel_unread(uint16_t count) {
    if(status_channel_unread!=count){status_channel_unread=count;status_dirty=true;}
}

void ui_status_set_gps(bool enabled,bool has_fix) {
    if(status_gps_enabled!=enabled||status_gps_fix!=has_fix){
        status_gps_enabled=enabled;status_gps_fix=has_fix;status_dirty=true;
    }
}

void ui_use_data_provider(UiDataProvider* provider) {
    if (!provider) return;
    ui_data=provider;
    Serial.println("[T5-UI] live MeshCore data provider attached");
    draw_screen();
    refresh(MODE_GL16);
}

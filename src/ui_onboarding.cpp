#include <Arduino.h>
#include <Preferences.h>
#include <epdiy.h>
#include <driver/i2c.h>
#include <esp_heap_caps.h>
#include "ui_onboarding.h"

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "0.1.3"
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
static int16_t cached_touch_x = 0, cached_touch_y = 0;
enum class Screen : uint8_t { Welcome, Presets, CompanionConfirm };
static Screen screen = Screen::Welcome;
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
    box(x,y,w,56);
    text(label, x+(w-(int)strlen(label)*12)/2, y+18, 2, 0, true);
}

static void draw_keyboard() {
    const char* numbers="1234567890";
    for(int i=0;numbers[i];++i){char label[2]={numbers[i],0};key(label,15+i*52,500,49);}
    const char* letter_rows_upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    const char* letter_rows_lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbol_rows[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};
    const char** rows=keyboard_symbols?symbol_rows:(keyboard_upper?letter_rows_upper:letter_rows_lower);
    const int starts[]={15,41,93};const int ys[]={570,640,710};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i){char label[2]={rows[r][i],0};key(label,starts[r]+i*52,ys[r],49);}
    key(keyboard_upper?"SHIFT":"shift",12,710,76);
    key(keyboard_symbols?"ABC":"SYM",460,710,68);
    key("DEL",12,780,150);key("HIDE",170,780,150);key("SAVE",328,780,200);
}

static void draw_welcome() {
    epd_hl_set_all_white(&display);
    centred("MESHCORE", 42, 6, 0, true);
    centred("SET UP YOUR T5", 104, 3, 0, true);
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
    if(keyboard_visible){centred("ENTER A NAME",470,2,0,true);draw_keyboard();}
    else {box(30,500,480,64);centred("SHOW KEYBOARD",521,3,0,true);}
    if(saved)centred("SETTINGS SAVED",880,2,0,true);
}

static void draw_presets() {
    epd_hl_set_all_white(&display);
    text("< BACK",24,24,2,0,true);
    centred("RADIO PRESETS",72,4,0,true);
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
    centred("BLUETOOTH",120,5,0,true);centred("COMPANION MODE",180,4,0,true);
    centred("THE LOCAL UI WILL CLOSE",300,2);centred("UNTIL THE DEVICE RESTARTS",335,2);
    box(30,500,220,72);text("CANCEL",74,524,3,0,true);
    box(290,500,220,72,true);text("START",338,524,3,0xFF,true);
}

static void draw_screen() {
    if(screen==Screen::Welcome)draw_welcome();
    else if(screen==Screen::Presets)draw_presets();
    else draw_companion_confirm();
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
    if(!legal_name_character(c)){Serial.printf("[T5-UI] discarded illegal name character 0x%02X\n",(unsigned char)c);return;}
    if (replace_name_on_type) { node_name[0]=0; replace_name_on_type=false; }
    size_t n=strlen(node_name); if (n<20) { node_name[n]=c; node_name[n+1]=0; saved=false; }
}
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh) { return x>=bx&&x<bx+bw&&y>=by&&y<by+bh; }
static void handle_tap(int16_t x,int16_t y) {
    Serial.printf("[T5-UI] tap x=%d y=%d\n",x,y);
    if(screen==Screen::Presets) {
        if(y<125){screen=Screen::Welcome;draw_screen();refresh(MODE_GL16);return;}
        for(int row=0;row<PRESETS_PER_PAGE;++row) if(hit(x,y,12,132+row*128,516,112)){
            const int index=preset_page*PRESETS_PER_PAGE+row;if(index<PRESET_COUNT){selected_preset=index;saved=false;screen=Screen::Welcome;draw_screen();refresh(MODE_GL16);}return;
        }
        const uint8_t page_count=(PRESET_COUNT+PRESETS_PER_PAGE-1)/PRESETS_PER_PAGE;
        if(hit(x,y,24,800,180,62)&&preset_page>0){preset_page--;draw_screen();refresh(MODE_GL16);return;}
        if(hit(x,y,336,800,180,62)&&preset_page+1<page_count){preset_page++;draw_screen();refresh(MODE_GL16);return;}
        return;
    }
    if(screen==Screen::CompanionConfirm){
        if(hit(x,y,30,500,220,72)){screen=Screen::Welcome;draw_screen();refresh(MODE_DU);return;}
        if(hit(x,y,290,500,220,72)){Serial.println("[T5-UI] companion mode confirmed");request_companion_mode();return;}
        return;
    }
    if(hit(x,y,30,180,480,64)){replace_name_on_type=true;keyboard_visible=true;Serial.println("[T5-UI] name selected; keyboard shown; next character replaces current name");draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,24,292,492,88)){screen=Screen::Presets;preset_page=selected_preset/PRESETS_PER_PAGE;draw_screen();refresh(MODE_GL16);return;}
    if(hit(x,y,30,402,480,52)){screen=Screen::CompanionConfirm;draw_screen();refresh(MODE_GL16);return;}
    if(!keyboard_visible){if(hit(x,y,30,500,480,64)){keyboard_visible=true;draw_screen();refresh(MODE_GL16);}return;}
    const char* numbers="1234567890";for(int i=0;numbers[i];++i)if(hit(x,y,15+i*52,500,49,56)){append(numbers[i]);draw_screen();refresh(MODE_DU);return;}
    const char* upper[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};const char* lower[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    const char* symbols[]={"!@#$%^&*()","-_+=/\\:;\"",".,?'[]{}"};const char** rows=keyboard_symbols?symbols:(keyboard_upper?upper:lower);
    const int starts[]={15,41,93};const int ys[]={570,640,710};
    for(int r=0;r<3;++r)for(int i=0;rows[r][i];++i)if(hit(x,y,starts[r]+i*52,ys[r],49,56)){append(rows[r][i]);draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,12,710,76,56)){keyboard_upper=!keyboard_upper;keyboard_symbols=false;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,460,710,68,56)){keyboard_symbols=!keyboard_symbols;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,12,780,150,56)){size_t n=strlen(node_name);if(n)node_name[n-1]=0;saved=false;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,170,780,150,56)){keyboard_visible=false;draw_screen();refresh(MODE_GL16);return;}
    if(hit(x,y,328,780,200,56)){
        prefs.begin("t5-ui",false);prefs.putString("name",node_name);prefs.putUChar("preset_v2",selected_preset);prefs.end();
        saved=true;draw_screen();refresh(MODE_GL16);
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
    prefs.begin("t5-ui",true);String saved_name=prefs.getString("name","");selected_preset=prefs.getUChar("preset_v2",17);prefs.end();
    if(selected_preset>=PRESET_COUNT)selected_preset=17;
    if(saved_name.length()){
        size_t out=0;
        for(size_t i=0;i<saved_name.length()&&out<20;++i){const char c=saved_name[i];if(legal_name_character(c))node_name[out++]=c;else Serial.printf("[T5-UI] discarded stored illegal name character 0x%02X\n",(unsigned char)c);}
        node_name[out]=0;
    }
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
    delay(12);
}

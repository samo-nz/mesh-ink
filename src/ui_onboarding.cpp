#include <Arduino.h>
#include <Preferences.h>
#include <epdiy.h>
#include <driver/i2c.h>
#include <esp_heap_caps.h>

// UI milestone 0.1.0: standalone onboarding. Bluetooth, radio and GPS are not
// started in this target. Saved values are device-owned and will be handed to
// the MeshCore application adapter in the next milestone.
static constexpr char UI_VERSION[] = "0.1.0";
static constexpr uint8_t GT911_ADDR = 0x5D;
static constexpr gpio_num_t TOUCH_RST = GPIO_NUM_9;
static constexpr gpio_num_t TOUCH_INT = GPIO_NUM_3;
static constexpr gpio_num_t FRONTLIGHT = GPIO_NUM_11;

struct Glyph { char c; uint8_t r[7]; };
static constexpr Glyph FONT[] = {
 {' ',{0,0,0,0,0,0,0}},{'-',{0,0,0,31,0,0,0}},{'.',{0,0,0,0,0,6,6}},
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
};

static EpdiyHighlevelState display;
static uint8_t* fb = nullptr;
static Preferences prefs;
static char node_name[21] = "MY T5";
static uint8_t selected_preset = 0;
static bool saved = false;
static bool was_pressed = false;

struct Preset { const char* title; const char* detail; };
static constexpr Preset PRESETS[] = {
    {"NEW ZEALAND", "917.375 MHZ"}, {"AUSTRALIA", "915 MHZ"},
    {"UNITED STATES", "910 MHZ"}, {"EUROPE", "869.525 MHZ"}
};

static const uint8_t* glyph(char c) {
    for (const auto& g : FONT) if (g.c == c) return g.r;
    return FONT[0].r;
}

static void text(const char* s, int x, int y, int scale, uint8_t color = 0) {
    while (*s) {
        const uint8_t* rows = glyph(*s++);
        for (int ry=0; ry<7; ++ry) for (int rx=0; rx<5; ++rx)
            if (rows[ry] & (1 << (4-rx)))
                for (int dy=0; dy<scale; ++dy) for (int dx=0; dx<scale; ++dx)
                    epd_draw_pixel(x+rx*scale+dx, y+ry*scale+dy, color, fb);
        x += 6*scale;
    }
}

static void centred(const char* s, int y, int scale, uint8_t color = 0) {
    text(s, (540 - (int)strlen(s)*6*scale)/2, y, scale, color);
}

static void box(int x, int y, int w, int h, bool selected=false) {
    EpdRect r = {x,y,w,h};
    if (selected) epd_fill_rect(r, 0, fb);
    else { epd_fill_rect(r, 15, fb); epd_draw_rect(r, 0, fb); }
}

static void key(const char* label, int x, int y, int w) {
    box(x,y,w,56);
    text(label, x+(w-(int)strlen(label)*12)/2, y+18, 2);
}

static void draw_screen() {
    epd_hl_set_all_white(&display);
    centred("MESHCORE", 42, 6);
    centred("SET UP YOUR T5", 104, 3);
    text("YOUR NAME", 30, 154, 2);
    box(30,180,480,64);
    text(node_name, 48,199,3);
    text("RADIO PRESET", 30, 268, 2);
    for (int i=0;i<4;++i) {
        const int x = (i&1) ? 280 : 30;
        const int y = (i<2) ? 296 : 372;
        box(x,y,230,62,i==selected_preset);
        const uint8_t color = i==selected_preset ? 15 : 0;
        text(PRESETS[i].title,x+12,y+11,2,color);
        text(PRESETS[i].detail,x+12,y+38,1,color);
    }
    centred("ENTER A NAME", 468, 2);
    const char* rows[] = {"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
    const int starts[] = {15,41,93};
    const int ys[] = {525,600,675};
    for (int r=0;r<3;++r) for (int i=0;rows[r][i];++i) {
        char label[2] = {rows[r][i],0}; key(label,starts[r]+i*52,ys[r],49);
    }
    key("SPACE",30,755,280); key("DEL",320,755,90); key("SAVE",420,755,90);
    centred(saved ? "SETTINGS SAVED" : "BLUETOOTH OFF", 850, 2);
    centred(UI_VERSION, 910, 2);
}

static void refresh(EpdDrawMode mode) {
    digitalWrite(FRONTLIGHT,HIGH);
    epd_poweron();
    const EpdDrawError err = epd_hl_update_screen(&display,mode,(int)epd_ambient_temperature());
    epd_poweroff();
    digitalWrite(FRONTLIGHT,LOW);
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
    if (!i2c_read(0x814E,&status,1) || !(status&0x80)) return was_pressed;
    const uint8_t count=status&0x0F;
    if (!count || count>5) { clear_touch(); was_pressed=false; return false; }
    uint8_t p[8]={};
    if (!i2c_read(0x814F,p,sizeof(p))) return was_pressed;
    x=(int16_t)(p[1]|((uint16_t)p[2]<<8)); y=(int16_t)(p[3]|((uint16_t)p[4]<<8));
    clear_touch(); was_pressed=true; return true;
}

static void append(char c) {
    size_t n=strlen(node_name); if (n<20) { node_name[n]=c; node_name[n+1]=0; saved=false; }
}
static bool hit(int16_t x,int16_t y,int bx,int by,int bw,int bh) { return x>=bx&&x<bx+bw&&y>=by&&y<by+bh; }
static void handle_tap(int16_t x,int16_t y) {
    Serial.printf("[T5-UI] tap x=%d y=%d\n",x,y);
    for (int i=0;i<4;++i) {
        int bx=(i&1)?280:30, by=(i<2)?296:372;
        if (hit(x,y,bx,by,230,62)) { selected_preset=i; saved=false; draw_screen(); refresh(MODE_DU); return; }
    }
    const char* rows[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"}; const int starts[]={15,41,93}; const int ys[]={525,600,675};
    for(int r=0;r<3;++r) for(int i=0;rows[r][i];++i)
        if(hit(x,y,starts[r]+i*52,ys[r],49,56)){append(rows[r][i]);draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,30,755,280,56)){append(' ');draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,320,755,90,56)){size_t n=strlen(node_name);if(n)node_name[n-1]=0;saved=false;draw_screen();refresh(MODE_DU);return;}
    if(hit(x,y,420,755,90,56)){
        prefs.begin("t5-ui",false);prefs.putString("name",node_name);prefs.putUChar("preset",selected_preset);prefs.end();
        saved=true;draw_screen();refresh(MODE_GL16);
    }
}

void setup() {
    Serial.begin(115200); delay(200);
    Serial.printf("[T5-UI] onboarding %s boot heap=%u psram=%u; Bluetooth disabled\n",UI_VERSION,ESP.getFreeHeap(),ESP.getFreePsram());
    pinMode(FRONTLIGHT,OUTPUT);digitalWrite(FRONTLIGHT,HIGH);
    pinMode(TOUCH_RST,OUTPUT);digitalWrite(TOUCH_RST,LOW);pinMode(TOUCH_INT,OUTPUT);digitalWrite(TOUCH_INT,LOW);
    epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);epd_set_lcd_pixel_clock_MHz(17);
    delay(10);digitalWrite(TOUCH_RST,HIGH);delay(60);pinMode(TOUCH_INT,INPUT);
    display=epd_hl_init(EPD_BUILTIN_WAVEFORM);fb=epd_hl_get_framebuffer(&display);
    prefs.begin("t5-ui",true);String saved_name=prefs.getString("name","");selected_preset=min((uint8_t)3,prefs.getUChar("preset",0));prefs.end();
    if(saved_name.length()){saved_name.toUpperCase();strncpy(node_name,saved_name.c_str(),20);node_name[20]=0;}
    draw_screen();epd_poweron();epd_clear();epd_poweroff();refresh(MODE_GL16);
    Serial.println("[T5-UI] touch ready; waiting for input");
}

void loop() {
    static bool held=false;int16_t x=0,y=0;const bool pressed=touch_point(x,y);
    if(pressed&&!held){held=true;handle_tap(x,y);} if(!pressed)held=false;
    delay(12);
}

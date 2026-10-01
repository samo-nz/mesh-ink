#include <Arduino.h>
#include <string.h>
#include "companion_notice.h"
#include "hardware/display.h"
#include "hardware/power.h"
#include "hardware/buttons.h"
#include "t5_logging.h"
#include "meshcore_version.h"
#include "meshink_logo_bitmap.h"

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.0.0"
#endif

#define T5_TRACE(...) T5_DEBUGF(T5_LOG_BOARD, "[T5] " __VA_ARGS__)

// A tiny fixed glyph set avoids loading a font, a graphics task, or a UI
// framework into the companion-only image. Each row is a five-bit bitmap.
struct Glyph { char letter; uint8_t rows[7]; };
static constexpr Glyph notice_glyphs[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}},
    {'5',{31,16,16,30,1,1,30}}, {'6',{14,16,16,30,17,17,14}},
    {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,1,14}},
    {'.',{0,0,0,0,0,6,6}}, {'-',{0,0,0,31,0,0,0}},
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'X',{17,17,10,4,10,17,17}},
};

static void notice_text(const char* message, int x, int y, int scale, uint8_t* fb, bool bold = false) {
    for (const char* c = message; *c; ++c, x += 6 * scale) {
        char key=*c;
        if(key>='a'&&key<='z')key=(char)(key-'a'+'A');
        for (const Glyph& glyph : notice_glyphs) {
            if (glyph.letter != key) continue;
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (!(glyph.rows[row] & (1 << (4 - col)))) continue;
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) {
                            meshink_display_draw_pixel(x + col * scale + dx,
                                           y + row * scale + dy, 0, fb);
                            if (bold) meshink_display_draw_pixel(x + col * scale + dx + 1,
                                                     y + row * scale + dy, 0, fb);
                        }
                    }
                }
            }
            break;
        }
    }
}

static void notice_centred(const char* message, int y, int scale, uint8_t* fb, bool bold = false) {
    notice_text(message, (meshink_display_logical_width() - (int)strlen(message) * 6 * scale) / 2, y, scale, fb, bold);
}

static void notice_meshink_logo(int top, uint8_t* fb) {
    constexpr uint8_t shades[4]={0x00,0x55,0xAA,0xFF};
    const int width=meshink_display_logical_width();
    const int height=meshink_display_logical_height();
    const int target_width=(MESHINK_LOGO_WIDTH*width+270)/540;
    const int target_height=(MESHINK_LOGO_HEIGHT*height+480)/960;
    const int left=(width-target_width)/2;
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

void meshink_show_companion_notice() {
    meshink_power_frontlight_begin();
    meshink_power_frontlight_set(100);
    T5_TRACE("board: begin; frontlight on; display notice before MeshCore I2C\n");
    T5_TRACE("notice: epd_init, internal heap=%u, psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    meshink_display_init();
    T5_TRACE("notice: panel initialized\n");
    meshink_display_set_orientation(MeshInkOrientation::Portrait);
    MeshInkDisplayState display = meshink_display_state_init();
    uint8_t* fb = meshink_display_framebuffer(&display);
    T5_TRACE("notice: framebuffer=%p, heap=%u, psram=%u\n", fb, ESP.getFreeHeap(), ESP.getFreePsram());
    if (fb) {
        meshink_display_set_all_white(&display);
        notice_meshink_logo(160, fb);
        notice_centred("BLUETOOTH COMPANION MODE", 565, 3, fb, true);
        char hold_button[32];
        snprintf(hold_button,sizeof(hold_button),"HOLD %s BUTTON",meshink_primary_button_name());
        notice_centred(hold_button, 665, 2, fb, true);
        notice_centred("2 SECONDS TO EXIT", 705, 2, fb);
        notice_centred(T5_FIRMWARE_VERSION, 885, 2, fb);
        T5_TRACE("notice: text rendered, powering panel on\n");
        meshink_display_poweron();
        T5_TRACE("notice: full panel clear start\n");
        meshink_display_clear();
        T5_TRACE("notice: full panel clear complete\n");
        T5_TRACE("notice: refresh start\n");
        const MeshInkDisplayResult result = meshink_display_update_screen(
            &display, MeshInkRefreshMode::FastGray16, static_cast<int>(meshink_display_ambient_temperature()));
        meshink_display_poweroff();
        T5_TRACE("notice: refresh result=%d; panel power off\n", result);
    } else {
        meshink_display_poweroff();
        T5_TRACE("notice: framebuffer unavailable; panel power off\n");
    }
    // EPDiy 2.0 has no high-level teardown API. Its one-time buffers are
    // no longer needed after the panel update; reclaim PSRAM and DRAM for BLE.
    meshink_display_release_state(&display);
    T5_TRACE("notice: framebuffers reclaimed\n");
    // LCD data lines overlap the SX1262 SPI pins: release every display
    // peripheral before upstream MeshCore calls radio_init().
    meshink_display_deinit();
    T5_TRACE("notice: display deinitialized, heap=%u, psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    meshink_power_frontlight_set(0);
    T5_TRACE("board: display rendered; frontlight off; handing control to MeshCore\n");
}


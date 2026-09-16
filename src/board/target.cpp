#include <Arduino.h>
#include <SPI.h>
#include <epdiy.h>
#include <esp_heap_caps.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

#if T5_DIAGNOSTICS
#define T5_TRACE(...) Serial.printf("[T5] " __VA_ARGS__)
#else
#define T5_TRACE(...) do {} while (0)
#endif

T5Board board;

// Board mapping only. The upstream wrapper controls radio parameters and
// transmit/receive/power state through MeshCore.
static SPIClass radio_spi(FSPI);
static CustomSX1262 radio = new Module(
    P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, radio_spi);
CustomSX1262Wrapper radio_driver(radio, board);

static ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
static MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

// A tiny fixed glyph set avoids loading a font, a graphics task, or a UI
// framework into the companion-only image. Each row is a five-bit bitmap.
struct Glyph { char letter; uint8_t rows[7]; };
static constexpr Glyph notice_glyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}}, {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
};

static void notice_text(const char* message, int x, int y, int scale, uint8_t* fb) {
    for (const char* c = message; *c; ++c, x += 6 * scale) {
        for (const Glyph& glyph : notice_glyphs) {
            if (glyph.letter != *c) continue;
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (!(glyph.rows[row] & (1 << (4 - col)))) continue;
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx)
                            epd_draw_pixel(x + col * scale + dx,
                                           y + row * scale + dy, 0, fb);
                }
            }
            break;
        }
    }
}

static void show_companion_notice() {
    T5_TRACE("notice: epd_init, internal heap=%u, psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    T5_TRACE("notice: panel initialized\n");
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    epd_set_lcd_pixel_clock_MHz(17);
    EpdiyHighlevelState display = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    uint8_t* fb = epd_hl_get_framebuffer(&display);
    T5_TRACE("notice: framebuffer=%p, heap=%u, psram=%u\n", fb, ESP.getFreeHeap(), ESP.getFreePsram());
    if (fb) {
        epd_hl_set_all_white(&display);
        notice_text("MESHCORE", 90, 290, 7, fb);
        notice_text("BT COMPANION MODE", 63, 410, 4, fb);
        T5_TRACE("notice: text rendered, powering panel on\n");
        epd_poweron();
        T5_TRACE("notice: refresh start\n");
        const EpdDrawError result = epd_hl_update_screen(
            &display, MODE_GL16, static_cast<int>(epd_ambient_temperature()));
        epd_poweroff();
        T5_TRACE("notice: refresh result=%d; panel power off\n", result);
    } else {
        epd_poweroff();
        T5_TRACE("notice: framebuffer unavailable; panel power off\n");
    }
    // EPDiy 2.0 has no high-level teardown API. Its one-time buffers are
    // no longer needed after the panel update; reclaim PSRAM and DRAM for BLE.
    heap_caps_free(display.front_fb);
    heap_caps_free(display.back_fb);
    heap_caps_free(display.difference_fb);
    free(display.dirty_lines);
    heap_caps_free(display.dirty_columns);
    T5_TRACE("notice: framebuffers reclaimed\n");
    // LCD data lines overlap the SX1262 SPI pins: release every display
    // peripheral before upstream MeshCore calls radio_init().
    epd_deinit();
    T5_TRACE("notice: display deinitialized, heap=%u, psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

void T5Board::begin() {
    // EPDiy owns I2C bus 0 while it refreshes the panel. The upstream board
    // calls Wire.begin() on this same bus, so initialize MeshCore only after
    // epd_deinit() releases EPDiy's driver and interrupts.
    T5_TRACE("board: begin; display notice before MeshCore I2C\n");
    show_companion_notice();
    T5_TRACE("board: notice complete; MeshCore board/I2C begin\n");
    ESP32Board::begin();
    T5_TRACE("board: MeshCore I2C ready\n");
    T5_TRACE("board: disabling touch and frontlight\n");
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW);  // GT911 disabled in companion mode
    pinMode(11, OUTPUT);
    digitalWrite(11, LOW); // frontlight disabled
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
    T5_TRACE("board: GPS UART ready; internal heap=%u\n", ESP.getFreeHeap());
}

bool radio_init() {
    T5_TRACE("radio: begin clock and RTC\n");
    fallback_clock.begin();
    rtc_clock.begin(Wire);
    T5_TRACE("radio: SX1262 init on SPI pins 14/21/13\n");
    const bool ready = radio.std_init(&radio_spi);
    T5_TRACE("radio: SX1262 init=%d, heap=%u\n", ready, ESP.getFreeHeap());
    return ready;
}

mesh::LocalIdentity radio_new_identity() {
    T5_TRACE("identity: collecting SX1262 radio noise\n");
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}

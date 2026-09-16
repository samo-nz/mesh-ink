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
static uint32_t detected_gps_baud = 9600;
class T5GPS : public MicroNMEALocationProvider {
public:
    T5GPS() : MicroNMEALocationProvider(Serial1, &rtc_clock) {}
    void begin() override {
        Serial1.updateBaudRate(detected_gps_baud);
        MicroNMEALocationProvider::begin();
        T5_TRACE("gps: enabled by MeshCore sensor setting\n");
    }
    void stop() override {
        MicroNMEALocationProvider::stop();
        T5_TRACE("gps: disabled by MeshCore sensor setting\n");
    }
    void loop() override {
#if T5_DIAGNOSTICS
        const int pending = Serial1.available();
#endif
        MicroNMEALocationProvider::loop();
#if T5_DIAGNOSTICS
        static uint32_t last_report = 0;
        if (millis() - last_report >= 15000) {
            last_report = millis();
            T5_TRACE("gps: baud=%u incoming=%d fix=%d satellites=%ld lat=%ld lon=%ld\n",
                     Serial1.baudRate(), pending, isValid(), satellitesCount(),
                     getLatitude(), getLongitude());
        }
#endif
    }
};
static T5GPS gps;
EnvironmentSensorManager sensors(gps);

// BQ27220 on the T5 shared I2C bus. Read-only standard commands: voltage
// 0x08 (mV) and state-of-charge 0x2C (%). No calibration or gauge writes.
static constexpr uint8_t BQ27220_ADDR = 0x55;
static bool gauge_word(uint8_t command, uint16_t& result) {
    Wire.beginTransmission(BQ27220_ADDR);
    Wire.write(command);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(BQ27220_ADDR, static_cast<uint8_t>(2)) != 2) return false;
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    result = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
    return true;
}

uint16_t T5Board::getBattMilliVolts() {
    // MeshCore may ask repeatedly while composing device responses. Avoid
    // repetitive I2C traffic and keep the last valid voltage on read errors.
    static uint16_t cached_mv = 0;
    static uint32_t sampled_at = 0;
    const uint32_t now = millis();
    if (sampled_at != 0 && now - sampled_at < 30000) return cached_mv;
    sampled_at = now == 0 ? 1 : now;

    uint16_t voltage = 0;
    if (gauge_word(0x08, voltage) && voltage >= 2500 && voltage <= 5000) {
        cached_mv = voltage;
        uint16_t soc = 0;
        if (gauge_word(0x2C, soc) && soc <= 100)
            T5_TRACE("battery: BQ27220 voltage=%u mV SOC=%u%%\n", cached_mv, soc);
        else
            T5_TRACE("battery: BQ27220 voltage=%u mV; SOC unavailable\n", cached_mv);
    } else {
        T5_TRACE("battery: BQ27220 read failed or voltage invalid, cached=%u mV\n", cached_mv);
    }
    return cached_mv;
}

// A tiny fixed glyph set avoids loading a font, a graphics task, or a UI
// framework into the companion-only image. Each row is a five-bit bitmap.
struct Glyph { char letter; uint8_t rows[7]; };
static constexpr Glyph notice_glyphs[] = {
    {'0',{14,17,19,21,25,17,14}}, {'3',{30,1,1,14,1,1,30}},
    {'.',{0,0,0,0,0,6,6}},
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
        notice_text("0.0.3", 225, 900, 3, fb);
        T5_TRACE("notice: text rendered, powering panel on\n");
        epd_poweron();
        T5_TRACE("notice: full panel clear start\n");
        epd_clear();
        T5_TRACE("notice: full panel clear complete\n");
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
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);
    T5_TRACE("board: begin; frontlight on; display notice before MeshCore I2C\n");
    show_companion_notice();
    digitalWrite(11, LOW);
    T5_TRACE("board: display rendered; frontlight off; handing control to MeshCore\n");
    T5_TRACE("board: notice complete; MeshCore board/I2C begin\n");
    ESP32Board::begin();
    T5_TRACE("board: MeshCore I2C ready\n");
    getBattMilliVolts();
    T5_TRACE("board: disabling touch and frontlight\n");
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW);  // GT911 disabled in companion mode
    digitalWrite(11, LOW); // frontlight remains disabled in companion mode
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
    // LoRa and GPS share the PCA9535-controlled rail; radio initialization
    // ensures power is available before probing GPS. T5 boards carry either
    // a 9600-baud L76K or a 38400-baud MIA-M10Q. Sample NMEA here before
    // upstream sensors.begin() owns the UART, without changing radio state.
    if (ready) {
        bool found = false;
        auto hex_value = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        for (const uint32_t baud : {9600UL, 38400UL}) {
            Serial1.updateBaudRate(baud);
            char sentence[100] = {};
            size_t sentence_len = 0;
            const uint32_t started = millis();
            while (millis() - started < 2500) {
                while (Serial1.available()) {
                    const int c = Serial1.read();
                    if (c == '$') {
                        sentence[0] = '$';
                        sentence_len = 1;
                    } else if (sentence_len && c >= 32 && c <= 126 && sentence_len < sizeof(sentence) - 1) {
                        sentence[sentence_len++] = static_cast<char>(c);
                    } else if (sentence_len && (c == '\r' || c == '\n')) {
                        sentence[sentence_len] = 0;
                        char* star = strchr(sentence, '*');
                        if (star && star[1] && star[2] && sentence_len >= 9 &&
                            sentence[1] == 'G' && hex_value(star[1]) >= 0 && hex_value(star[2]) >= 0) {
                            uint8_t checksum = 0;
                            for (char* p = sentence + 1; p < star; ++p) checksum ^= static_cast<uint8_t>(*p);
                            const uint8_t expected = static_cast<uint8_t>((hex_value(star[1]) << 4) | hex_value(star[2]));
                            found = checksum == expected;
                        }
                        sentence_len = 0;
                    } else if (c != '\r' && c != '\n') {
                        sentence_len = 0;
                    }
                    if (found) break;
                }
                if (found) break;
                delay(5);
            }
            T5_TRACE("gps: probe %lu baud valid-NMEA=%d\n", baud, found);
            if (found) { detected_gps_baud = baud; break; }
        }
        if (!found) Serial1.updateBaudRate(9600);
        T5_TRACE("gps: selected baud=%u; MeshCore owns position and settings\n", Serial1.baudRate());
    }
    return ready;
}

mesh::LocalIdentity radio_new_identity() {
    T5_TRACE("identity: collecting SX1262 radio noise\n");
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}

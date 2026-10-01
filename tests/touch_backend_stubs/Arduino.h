#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <tuple>
#include <vector>

using gpio_num_t = int;

static constexpr int OUTPUT=1;
static constexpr int INPUT=0;
static constexpr int LOW=0;
static constexpr int HIGH=1;

namespace touch_stub {
inline uint32_t now_ms=0;
inline std::vector<std::tuple<int,int,int>> gpio_events;
inline std::vector<unsigned> delays;
inline std::vector<std::string> serial_lines;

inline void reset_arduino() {
    now_ms=0;
    gpio_events.clear();
    delays.clear();
    serial_lines.clear();
}
}

inline uint32_t millis() { return touch_stub::now_ms; }

inline void delay(unsigned ms) {
    touch_stub::delays.push_back(ms);
    touch_stub::now_ms+=ms;
}

inline void pinMode(gpio_num_t pin,int mode) {
    touch_stub::gpio_events.emplace_back(pin,mode,-1);
}

inline void digitalWrite(gpio_num_t pin,int value) {
    touch_stub::gpio_events.emplace_back(pin,-1,value);
}

struct TouchStubSerial {
    int printf(const char* fmt,...) {
        char buffer[512];
        va_list args;
        va_start(args,fmt);
        const int n=vsnprintf(buffer,sizeof(buffer),fmt,args);
        va_end(args);
        touch_stub::serial_lines.emplace_back(buffer);
        return n;
    }
    void println(const char* value) {
        touch_stub::serial_lines.emplace_back(std::string(value)+"\n");
    }
};

inline TouchStubSerial Serial;

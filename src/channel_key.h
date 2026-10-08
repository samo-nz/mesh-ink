#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Shared input validation only: the protocol helpers own key generation,
// persistence and cryptography. Do not print keys or put them in UI data.
namespace meshink_channel_key {
inline int digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool parse_hex(const char* text, uint8_t out[32], size_t& length) {
    length = 0;
    if (!text || !out) return false;
    const size_t count = strlen(text);
    if (count != 32 && count != 64) return false;
    uint8_t parsed[32] = {};
    for (size_t i = 0; i < count; i += 2) {
        const int a = digit(text[i]), b = digit(text[i + 1]);
        if (a < 0 || b < 0) return false;
        parsed[i / 2] = (uint8_t)((a << 4) | b);
    }
    memset(out, 0, 32);
    memcpy(out, parsed, count / 2);
    length = count / 2;
    return true;
}
inline bool valid_name(const char* name, size_t max_length) {
    if (!name) return false;
    const size_t size = strlen(name);
    if (!size || size > max_length) return false;
    for (size_t i = 0; i < size; ++i)
        if ((unsigned char)name[i] < 33 || (unsigned char)name[i] > 126)
            return false;
    return true;
}
}

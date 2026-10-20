#pragma once
#include <stddef.h>

// Suggest a concise radio nickname from a long device name.
// First alphanumeric character + first three after the last separator;
// without a separator, use the last three alphanumeric characters.
// ASCII-only because Meshtastic short names are at most four printable bytes.
inline void meshink_suggest_short_name(const char* long_name, char (&out)[5]) {
    out[0] = 0;
    if (!long_name) return;
    auto letter = [](char c) -> bool {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9');
    };
    auto upper = [](char c) -> char { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; };
    const char* first = nullptr;
    const char* last_separator = nullptr;
    for (const char* p = long_name; *p; ++p) {
        if (!first && letter(*p)) first = p;
        if (*p == '-' || *p == '_' || *p == ' ') last_separator = p;
    }
    if (!first) return;
    size_t used = 0;
    out[used++] = upper(*first);
    if (last_separator && last_separator[1]) {
        for (const char* p = last_separator + 1; *p && used < 4; ++p)
            if (letter(*p)) out[used++] = upper(*p);
    } else {
        char tail[3]{};
        size_t count = 0;
        for (const char* p = first + 1; *p; ++p) {
            if (!letter(*p)) continue;
            if (count < 3) tail[count++] = upper(*p);
            else { tail[0] = tail[1]; tail[1] = tail[2]; tail[2] = upper(*p); }
        }
        for (size_t i = 0; i < count && used < 4; ++i) out[used++] = tail[i];
    }
    out[used] = 0;
}

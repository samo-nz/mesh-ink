#pragma once
#include <stddef.h>
#include <stdio.h>
// Exact, finite allowlist for internal-flash restore scratch. Never broad
// directory matching, and never a live protocol, message or preference file.
namespace meshink_restore_scratch {
constexpr unsigned SLOTS=60;
inline bool make_path(char* dst,size_t capacity,unsigned slot,
                      bool legacy,bool previous){
    if(!dst||!capacity||slot>=SLOTS)return false;
    const int n=snprintf(dst,capacity,"/%s%02u.%s",
        legacy?"rst":"r2s",slot,previous?"old":"tmp");
    return n>0&&(size_t)n<capacity;
}
}

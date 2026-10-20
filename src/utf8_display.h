#pragma once
#include <stddef.h>
#include <stdint.h>

// Display-only decoder: never mutates radio payloads, journals or node names.
// Invalid sequences consume one byte and produce U+FFFD.
namespace meshink_utf8 {
struct Char { uint32_t codepoint; size_t bytes; };
inline Char next(const char* text,size_t available) {
    if(!text||!available||!text[0])return {0,0};
    const uint8_t a=(uint8_t)text[0];
    if(a<0x80)return {a,1};
    const size_t need=(a>=0xC2&&a<=0xDF)?2:
                      (a>=0xE0&&a<=0xEF)?3:
                      (a>=0xF0&&a<=0xF4)?4:0;
    if(!need||need>available)return {0xFFFD,1};
    uint32_t cp=a&((1U<<(7-need))-1);
    for(size_t i=1;i<need;++i){
        const uint8_t b=(uint8_t)text[i];
        if(!b||(b&0xC0)!=0x80)return {0xFFFD,1};
        cp=(cp<<6)|(b&0x3F);
    }
    if((need==2&&cp<0x80)||(need==3&&cp<0x800)||
       (need==4&&cp<0x10000)||cp>0x10FFFF||
       (cp>=0xD800&&cp<=0xDFFF))return {0xFFFD,1};
    return {cp,need};
}
inline size_t prior(const char* text,size_t length){
    if(!text||!length)return 0;
    size_t i=length-1;
    while(i&&(((uint8_t)text[i]&0xC0)==0x80))--i;
    const Char c=next(text+i,length-i);
    return c.bytes==length-i?i:length-1;
}
}

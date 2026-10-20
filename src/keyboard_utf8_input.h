#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "utf8_display.h"

// Fixed high-frequency shortcuts chosen from Unicode frequency groups.
// Deliberately static across devices; never adaptive.
static constexpr uint32_t MESHINK_KEYBOARD_FREQUENT_EMOJI[10]={
  0x1F602,0x2764,0x1F60D,0x1F923,0x1F60A,0x1F64F,0x1F62D,0x1F618,0x1F44D,0x1F605
};
static constexpr uint32_t MESHINK_KEYBOARD_ALL_EMOJI[]={
  0x1F602,0x2764,0x1F60D,0x1F923,0x1F60A,0x1F64F,0x1F62D,0x1F618,
  0x1F44D,0x1F605,0x1F44F,0x1F601,0x1F525,0x1F622,0x1F914,0x1F4AA,
  0x1F609,0x1F44C,0x1F60E,0x1F389,0x1F642,0x1F600,0x1F4AF,0x1F634,
  0x1F621,0x1F44E,0x1F4CD,0x1F680,0x274C,0x26A0,0x2B50,0x2705,
  0x1F44B,0x1F62E
};
inline void meshink_encode_utf8(uint32_t cp,char out[5]){
    memset(out,0,5);
    if(cp<0x80)out[0]=(char)cp;
    else if(cp<0x800){out[0]=(char)(0xC0|(cp>>6));out[1]=(char)(0x80|(cp&63));}
    else if(cp<0x10000){out[0]=(char)(0xE0|(cp>>12));out[1]=(char)(0x80|((cp>>6)&63));out[2]=(char)(0x80|(cp&63));}
    else{out[0]=(char)(0xF0|(cp>>18));out[1]=(char)(0x80|((cp>>12)&63));out[2]=(char)(0x80|((cp>>6)&63));out[3]=(char)(0x80|(cp&63));}
}
inline bool meshink_append_utf8(char* text,size_t capacity,size_t byte_limit,uint32_t cp){
    const size_t n=strlen(text);
    char encoded[5];meshink_encode_utf8(cp,encoded);
    const size_t bytes=strlen(encoded);
    if(!bytes||n+bytes>byte_limit||n+bytes>=capacity)return false;
    memcpy(text+n,encoded,bytes+1);
    return true;
}
inline void meshink_erase_utf8(char* text){
    size_t n=strlen(text);
    if(!n)return;
    size_t prior=meshink_utf8::prior(text,n);
    const auto last=meshink_utf8::next(text+prior,n-prior);
    if((last.codepoint==0xFE0F||last.codepoint==0x200D)&&prior)
        prior=meshink_utf8::prior(text,prior);
    text[prior]=0;
}
inline const char* meshink_letter_alternatives(char key){
    switch(key){
      case 'a':return "áàâäãåāą"; case 'A':return "ÁÀÂÄÃÅĀĄ";
      case 'c':return "çćč";case 'C':return "ÇĆČ";
      case 'e':return "éèêëēę";case 'E':return "ÉÈÊËĒĘ";
      case 'i':return "íìîïī";case 'I':return "ÍÌÎÏĪ";
      case 'l':return "ł";case 'L':return "Ł";
      case 'n':return "ñń";case 'N':return "ÑŃ";
      case 'o':return "óòôöõøō";case 'O':return "ÓÒÔÖÕØŌ";
      case 'r':return "ř";case 'R':return "Ř";
      case 's':return "śš";case 'S':return "ŚŠ";
      case 'u':return "úùûüū";case 'U':return "ÚÙÛÜŪ";
      case 'y':return "ýÿ";case 'Y':return "Ý";
      case 'z':return "źżž";case 'Z':return "ŹŻŽ";
      default:return nullptr;
    }
}

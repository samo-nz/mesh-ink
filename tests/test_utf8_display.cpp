#include "../src/utf8_display.h"
#include "../src/fonts/meshink_emoji.h"
#include <cassert>
#include <cstring>
#include <cstdio>

static void expect(const char* s,uint32_t cp,size_t bytes) {
    const auto c=meshink_utf8::next(s,strlen(s));
    assert(c.codepoint==cp&&c.bytes==bytes);
}
int main() {
    expect("A",'A',1);
    expect("\xC3\xA9",0xE9,2);      // é
    expect("\xC4\x81",0x101,2);     // ā
    expect("\xE4\xB8\xAD",0x4E2D,3); // 中: handled as a codepoint even without glyph
    expect("\xF0\x9F\x99\x82",0x1F642,4); // 🙂
    const char invalid[]="\xC0\xAF";
    assert(meshink_utf8::next(invalid,2).codepoint==0xFFFD);
    const char bad_cont[]="\xE2\x28\xA1";
    assert(meshink_utf8::next(bad_cont,3).bytes==1);
    const char truncated[]="\xF0\x9F";
    assert(meshink_utf8::next(truncated,2).bytes==1);
    const char surrogate[]="\xED\xA0\x80";
    assert(meshink_utf8::next(surrogate,3).codepoint==0xFFFD);
    const char too_high[]="\xF4\x90\x80\x80";
    assert(meshink_utf8::next(too_high,4).codepoint==0xFFFD);
    const char sample[]="M\xC4\x81ori \xF0\x9F\x99\x82";
    const size_t n=strlen(sample);
    assert(meshink_utf8::prior(sample,n)==n-4);
    assert(meshink_utf8::prior(sample,2)==1);
    assert(meshink_emoji_rows(0x1F642));
    assert(meshink_emoji_rows(0x2764));
    assert(meshink_emoji_rows(0x1F44D));
    assert(!meshink_emoji_rows(0x1F680)); // no glyph; use replacement
    puts("PASS: UTF-8 decoding, character boundaries and compact emoji glyphs");
}

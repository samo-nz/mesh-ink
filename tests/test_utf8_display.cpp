#include "../src/utf8_display.h"
#include "../src/fonts/meshink_emoji.h"
#include "../src/keyboard_utf8_input.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>

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
    assert(meshink_emoji_glyph(0x1F642,32));
    assert(meshink_emoji_glyph(0x2764,40));
    assert(meshink_emoji_glyph(0x1F44D,48));
    assert(meshink_emoji_supported(0x1F680)); // rocket added in expanded set
    assert(!meshink_emoji_supported(0x1F6F8)); // flying saucer still uses replacement
    for(const uint32_t cp : {0x1F642U,0x1F60AU,0x1F602U,0x2764U,0x1F44DU,0x1F44BU,0x2705U,0x1F600U,0x1F601U,0x1F923U,0x1F60DU,0x1F618U,0x1F609U,0x1F605U,0x1F622U,0x1F62DU,0x1F62EU,0x1F60EU,0x1F914U,0x1F634U,0x1F621U,0x1F44EU,0x1F44CU,0x1F64FU,0x1F44FU,0x1F4AAU,0x1F389U,0x1F525U,0x2B50U,0x1F4AFU,0x1F680U,0x1F4CDU,0x274CU,0x26A0U}) {
        for(const uint8_t size : {16,32,40,48,56}) {
            const auto* glyph=meshink_emoji_glyph(cp,size);
            assert(glyph&&glyph->bitmap);
            bool ink=false;
            for(size_t i=0;i<(size_t)size*size/8;i++)if(glyph->bitmap[i])ink=true;
            assert(ink);
        }
    }
    char compose[32]="a";
    assert(meshink_append_utf8(compose,sizeof(compose),12,0x101));
    assert(meshink_append_utf8(compose,sizeof(compose),12,0x1F602));
    assert(strcmp(compose,"aā😂")==0);
    meshink_erase_utf8(compose);
    assert(strcmp(compose,"aā")==0);
    meshink_erase_utf8(compose);
    assert(strcmp(compose,"a")==0);
    assert(meshink_append_utf8(compose,sizeof(compose),12,0x2764));
    assert(meshink_append_utf8(compose,sizeof(compose),12,0xFE0F));
    meshink_erase_utf8(compose);
    assert(strcmp(compose,"a")==0);
    assert(!meshink_append_utf8(compose,sizeof(compose),2,0x1F602));
    assert(strcmp(compose,"a")==0);
    assert(meshink_letter_alternatives('a')&&meshink_letter_alternatives('A'));
    assert(MESHINK_KEYBOARD_FREQUENT_EMOJI[0]==0x1F602);
    puts("PASS: UTF-8 decoding, character boundaries and compact emoji glyphs");
}

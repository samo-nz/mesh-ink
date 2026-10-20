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
    // Every exposed Phase 1 character must decode as a complete codepoint,
    // round-trip through the message composer, and delete cleanly.
    const char* all_keys=R"PHASE1(aAcCdDeEgGhHiIjJkKlLnNoOrRsStTuUwWyYzZ?!'"-.$%/=0123)PHASE1";
    for(const char* key=all_keys;*key;++key){
        const char* options=meshink_letter_alternatives(*key);
        assert(options);
        const size_t len=strlen(options);
        size_t count=0;
        for(size_t off=0;off<len;){
            const auto ch=meshink_utf8::next(options+off,len-off);
            assert(ch.bytes>=1&&ch.codepoint!=0xFFFD);
            char buffer[32]="X";
            assert(meshink_append_utf8(buffer,sizeof(buffer),30,ch.codepoint));
            meshink_erase_utf8(buffer);
            assert(strcmp(buffer,"X")==0);
            off+=ch.bytes;
            ++count;
        }
        assert(count>0&&count<=40);
    }
    // All 34 picker choices must split into ten frequent shortcuts and
    // exactly 24 unique landscape popup choices.
    constexpr size_t frequent_count=sizeof(MESHINK_KEYBOARD_FREQUENT_EMOJI)/sizeof(uint32_t);
    constexpr size_t all_count=sizeof(MESHINK_KEYBOARD_ALL_EMOJI)/sizeof(uint32_t);
    assert(frequent_count==10&&all_count==34);
    size_t remainder=0;
    bool retained_heart_eyes=false,frequent_smile=false;
    for(size_t i=0;i<all_count;++i){
        const uint32_t cp=MESHINK_KEYBOARD_ALL_EMOJI[i];
        for(size_t j=0;j<i;++j)assert(MESHINK_KEYBOARD_ALL_EMOJI[j]!=cp);
        bool frequent=false;
        for(size_t j=0;j<frequent_count;++j)if(MESHINK_KEYBOARD_FREQUENT_EMOJI[j]==cp)frequent=true;
        if(!frequent)++remainder;
        if(cp==0x1F60D)retained_heart_eyes=true;
    }
    for(size_t i=0;i<frequent_count;++i){
        if(MESHINK_KEYBOARD_FREQUENT_EMOJI[i]==0x1F642)frequent_smile=true;
        assert(MESHINK_KEYBOARD_FREQUENT_EMOJI[i]!=0x1F60D);
        for(size_t j=0;j<i;++j)assert(MESHINK_KEYBOARD_FREQUENT_EMOJI[i]!=MESHINK_KEYBOARD_FREQUENT_EMOJI[j]);
    }
    assert(remainder==24&&frequent_smile&&retained_heart_eyes);
    // Failed append at the byte boundary must not alter valid UTF-8.
    char bounded[8]="ABC";
    assert(meshink_append_utf8(bounded,sizeof(bounded),7,0x1F642));
    assert(!meshink_append_utf8(bounded,sizeof(bounded),7,'x'));
    assert(strcmp(bounded,"ABC🙂")==0);
    meshink_erase_utf8(bounded);
    assert(strcmp(bounded,"ABC")==0);
    assert(MESHINK_KEYBOARD_FREQUENT_EMOJI[0]==0x1F602);
    puts("PASS: UTF-8 decoding, character boundaries and compact emoji glyphs");
}

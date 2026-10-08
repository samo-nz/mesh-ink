#include <cassert>
#include <cstdint>
#include <cstring>
#include "channel_key.h"

// Host-only boundary tests for shared channel input validation.
// Nothing here contains actual user secrets.
int main() {
    using namespace meshink_channel_key;
    assert(valid_name("Public",12));
    assert(valid_name("Field-Chat",12));
    assert(!valid_name("",12));
    assert(!valid_name("Bad Name",12));
    assert(!valid_name("TooLongChannelName",12));
    assert(!valid_name("bad\nname",12));

    uint8_t key[32]{};
    size_t key_length=0;
    assert(parse_hex("00112233445566778899aabbccddeeff",key,key_length));
    assert(key_length==16);
    assert(key[0]==0x00 && key[1]==0x11 && key[15]==0xff);
    for(size_t i=16;i<32;++i) assert(key[i]==0);

    assert(parse_hex(
        "00112233445566778899AABBCCDDEEFF"
        "FEDCBA98765432100123456789ABCDEF",key,key_length));
    assert(key_length==32);
    assert(key[0]==0x00 && key[16]==0xfe && key[31]==0xef);

    assert(!parse_hex("",key,key_length));
    assert(!parse_hex("0123",key,key_length));
    assert(!parse_hex("00112233445566778899aabbccddeefG",key,key_length));
    assert(!parse_hex(nullptr,key,key_length));
    assert(!parse_hex("00112233445566778899aabbccddeeff",nullptr,key_length));
    return 0;
}

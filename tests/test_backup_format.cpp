#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "backup_format.h"

using namespace meshink_backup_format;

int main() {
    const uint8_t classic[]={'1','2','3','4','5','6','7','8','9'};
    assert((~crc32(classic,sizeof(classic)))==0xCBF43926U);
    const uint8_t first[]={'f','i','r','s','t'};
    const uint8_t second[]={' ','d','a','t','a'};
    uint32_t rolling=crc32(first,sizeof(first));
    rolling=crc32(second,sizeof(second),rolling);
    const uint8_t joined[]={'f','i','r','s','t',' ','d','a','t','a'};
    assert(rolling==crc32(joined,sizeof(joined)));

    const Header good{MAGIC,VERSION,1,7,3,0};
    assert(supported(good,1,60));
    assert(!supported(good,2,60));
    auto edited=good;
    edited.magic^=1;assert(!supported(edited,1,60));
    edited=good;edited.version++;assert(!supported(edited,1,60));
    edited=good;edited.categories=8;assert(!supported(edited,1,60));
    edited=good;edited.count=0;assert(!supported(edited,1,60));
    edited=good;edited.count=61;assert(!supported(edited,1,60));

    const uint8_t text[]={'m','e','s','h'};
    const uint32_t original=~crc32(text,sizeof(text));
    auto damaged=std::vector<uint8_t>(text,text+sizeof(text));
    assert((~crc32(damaged.data(),damaged.size()))==original);
    damaged[2]^=0x80;
    assert((~crc32(damaged.data(),damaged.size()))!=original);

    Part part{};
    part.category=1;
    std::strcpy(part.path,"/meshcore_messages.bin");
    part.size=sizeof(text);
    part.crc=original;
    assert(sizeof(part)==56);
    assert(sizeof(good)==12);
    return 0;
}

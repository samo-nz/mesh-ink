#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable SD-backup wire format. No Arduino, board or protocol dependencies.
// Keep layout/version explicit so host tests can catch accidental format drift.
namespace meshink_backup_format {
constexpr uint32_t MAGIC=0x3142494DU; // MIB1
constexpr uint16_t VERSION=1;
struct __attribute__((packed)) Header {
    uint32_t magic;
    uint16_t version;
    uint8_t protocol;
    uint8_t categories;
    uint16_t count;
    uint16_t reserved;
};
struct __attribute__((packed)) Part {
    uint8_t category;
    char path[47];
    uint32_t size;
    uint32_t crc;
};
static_assert(sizeof(Header)==12,"Backup header compatibility changed");
static_assert(sizeof(Part)==56,"Backup entry compatibility changed");
inline bool supported(const Header& header,uint8_t protocol,size_t max_entries){
    return header.magic==MAGIC&&header.version==VERSION&&
           header.protocol==protocol&&(protocol==1||protocol==2)&&
           header.count>0&&header.count<=max_entries&&
           (header.categories&~15U)==0&&header.categories;
}
inline uint32_t crc32(const uint8_t* data,size_t count,uint32_t crc=0xFFFFFFFFU){
    for(size_t i=0;i<count;++i){
        crc^=data[i];
        for(int bit=0;bit<8;++bit)
            crc=(crc>>1)^((crc&1)?0xEDB88320U:0);
    }
    return crc;
}
} // namespace meshink_backup_format

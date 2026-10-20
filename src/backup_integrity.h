#pragma once
#include <stddef.h>
#include <stdint.h>
namespace meshink_backup_integrity {
constexpr uint32_t MESSAGE_MAGIC=0x354D3554U;
constexpr uint32_t NODE_MAGIC=0x4D544E44U;
constexpr size_t JOURNAL_HEADER_BYTES=16;
constexpr uint16_t MESSAGE_CAPACITY=250;
constexpr size_t CORE_RECORD_BYTES=188;
constexpr size_t MT_RECORD_BYTES=264;
constexpr size_t NODE_HEADER_BYTES=20;
constexpr uint32_t MAX_UI_NODES=64;
constexpr size_t NODE_RECORD_BYTES=68;
struct Journal {uint32_t magic=0;uint16_t version=0,capacity=0,head=0,count=0;uint32_t sequence=0;};
struct Node {uint32_t magic=0,version=0,generation=0,count=0,checksum=0;};
static_assert(sizeof(Journal)==JOURNAL_HEADER_BYTES,"Journal ABI changed");
static_assert(sizeof(Node)==NODE_HEADER_BYTES,"Node ABI changed");
inline bool valid_journal(const Journal& h,size_t bytes,uint8_t protocol){
    if(protocol!=1&&protocol!=2)return false;
    const bool mt=protocol==2;
    return h.magic==MESSAGE_MAGIC&&h.version==(mt?4:3)&&
        h.capacity==MESSAGE_CAPACITY&&h.head<MESSAGE_CAPACITY&&
        h.count<=MESSAGE_CAPACITY&&bytes==JOURNAL_HEADER_BYTES+
        MESSAGE_CAPACITY*(mt?MT_RECORD_BYTES:CORE_RECORD_BYTES);
}
inline bool valid_node_header(const Node& h,size_t bytes){
    return h.magic==NODE_MAGIC&&h.version==1&&h.count<=MAX_UI_NODES&&
        bytes==NODE_HEADER_BYTES+h.count*NODE_RECORD_BYTES;
}
inline uint32_t fnv(uint32_t hash,const uint8_t* bytes,size_t count){
    for(size_t i=0;i<count;++i){hash^=bytes[i];hash*=16777619u;}
    return hash;
}
inline uint32_t node_seed(uint32_t gen,uint32_t count){
    const uint32_t x=fnv(2166136261u,(const uint8_t*)&gen,sizeof(gen));
    return fnv(x,(const uint8_t*)&count,sizeof(count));
}
}

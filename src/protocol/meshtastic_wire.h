#pragma once
// Minimal, bounded Meshtastic Position and DeviceMetrics wire codec.
// Leaf's vendored nanopb subset does not include position/telemetry messages.
// Fields below follow meshtastic/mesh.proto and telemetry.proto; no generic
// protobuf allocations or additional persistent storage are required.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace meshink_mt_wire {
struct Position {int32_t lat_e6=0,lon_e6=0;bool valid=false;};
inline uint32_t u32(const uint8_t* p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
inline void put_u32(uint8_t* p,uint32_t value){
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));
}
inline bool varint(const uint8_t* b,size_t len,size_t& off,uint64_t& out){
    out=0;
    for(unsigned i=0;i<10&&off<len;++i){
        const uint8_t byte=b[off++];
        if(i==9&&byte>1)return false;
        out|=(uint64_t)(byte&127)<<(7*i);
        if(!(byte&128))return true;
    }
    return false;
}
inline bool skip(const uint8_t* b,size_t len,size_t& off,unsigned wire){
    uint64_t v=0;
    if(wire==0)return varint(b,len,off,v);
    if(wire==1){if(len-off<8)return false;off+=8;return true;}
    if(wire==5){if(len-off<4)return false;off+=4;return true;}
    if(wire==2){
        if(!varint(b,len,off,v)||v>len-off)return false;
        off+=(size_t)v;return true;
    }
    return false;
}
inline Position decode_position(const uint8_t* data,size_t len){
    Position p{};if(!data)return p;
    size_t off=0;bool lat=false,lon=false;int32_t la=0,lo=0;
    while(off<len){
        uint64_t tag=0;if(!varint(data,len,off,tag)||!tag)return p;
        const unsigned wire=(unsigned)(tag&7);
        const uint64_t field=tag>>3;
        if((field==1||field==2)&&wire==5){
            if(len-off<4)return p;
            const int32_t value=(int32_t)u32(data+off);off+=4;
            if(field==1){la=value;lat=true;}else{lo=value;lon=true;}
        }else if(!skip(data,len,off,wire))return p;
    }
    if(lat&&lon&&la>=-900000000&&la<=900000000&&
       lo>=-1800000000&&lo<=1800000000){
        p.lat_e6=la/10;p.lon_e6=lo/10;p.valid=true;
    }
    return p;
}
inline size_t encode_position(uint8_t out[16],int32_t lat_e6,int32_t lon_e6){
    if(!out||lat_e6 < -90000000||lat_e6 > 90000000||
       lon_e6 < -180000000||lon_e6 > 180000000)return 0;
    out[0]=0x0d;put_u32(out+1,(uint32_t)((int64_t)lat_e6*10));
    out[5]=0x15;put_u32(out+6,(uint32_t)((int64_t)lon_e6*10));
    return 10;
}
// Telemetry.time=1 (fixed32), device_metrics=2 (length-delimited).
// DeviceMetrics.battery_level=1 (varint), voltage=2 (float).
inline size_t encode_device_telemetry(uint8_t out[32],uint32_t epoch,
                                      uint8_t battery,float volts){
    if(!out||battery>100||volts<0.0f||volts>10.0f)return 0;
    uint8_t nested[9]{0x08,battery,0x15,0,0,0,0};
    uint32_t bits=0;memcpy(&bits,&volts,sizeof(bits));
    put_u32(nested+3,bits);
    out[0]=0x0d;put_u32(out+1,epoch);
    out[5]=0x12;out[6]=7;
    memcpy(out+7,nested,7);
    return 14;
}
}

#pragma once
// Build-only adapter for the pinned official Meshtastic logger.
// Assemble one complete line BEFORE submitting it to Arduino HWCDC::write(),
// which serializes each buffer write across cores. No global SPI/UI mutex.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace meshink_atomic_log {
constexpr size_t CAPACITY=512;
inline size_t compose(char* out,size_t capacity,const char* level,
                      uint32_t local_seconds,uint32_t uptime_seconds,
                      const char* thread,const char* body,size_t body_size){
    if(!out||capacity<2||!body)return 0;
    if(!level)level="INFO ";
    int count=0;
    if(local_seconds){
        const uint32_t hms=local_seconds%86400U;
        count=snprintf(out,capacity,"%s | %02u:%02u:%02u %lu ",
            level,(unsigned)(hms/3600U),(unsigned)((hms/60U)%60U),
            (unsigned)(hms%60U),(unsigned long)uptime_seconds);
    }else{
        count=snprintf(out,capacity,"%s | ??:??:?? %lu ",
                       level,(unsigned long)uptime_seconds);
    }
    if(count<0)return 0;
    size_t used=(size_t)count<capacity?(size_t)count:capacity-1;
    const auto append=[&](const char* data,size_t length){
        if(!data||!length||used>=capacity-1)return;
        const size_t room=capacity-used-1;
        const size_t take=length<room?length:room;
        memcpy(out+used,data,take);
        used+=take;
        out[used]=0;
    };
    // Official logger prints the body's own "[Tag] " without also prefixing
    // the thread name. Keep the same rule for unchanged human-readable logs.
    bool has_tag=false;
    if(body_size&&body[0]=='['){
        const size_t scan=body_size<24?body_size:24;
        const char* close=(const char*)memchr(body,']',scan);
        has_tag=close && (size_t)(close-body+1)<body_size && close[1]==' ';
    }
    if(!has_tag&&thread&&thread[0]){
        append("[",1);append(thread,strlen(thread));append("] ",2);
    }
    append(body,body_size);
    // A complete record always ends on a newline; never leave a partial
    // record waiting for a second write, even when the buffer was exhausted.
    if(!used||out[used-1]!='\n'){
        if(used<capacity-1)out[used++]='\n';
        else out[used-1]='\n';
        out[used]=0;
    }
    return used;
}
}

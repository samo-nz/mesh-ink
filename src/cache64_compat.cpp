#include <Arduino.h>

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif

#if T5_CACHE64_EXPERIMENT
// PNGdec 1.1.6 assumes its optional ESP32-S3 SIMD helper is present whenever
// ARDUINO_ESP32S3_DEV is defined. In the Arduino+ESP-IDF cache64 experiment
// we intentionally omit ESP-DSP, so provide the exact scalar equivalent.
extern "C" void s3_rgb565(uint8_t* src,uint8_t* dest,int count,bool big_endian){
    if(!src||!dest||count<=0)return;
    for(int i=0;i<count;++i){
        const uint8_t r=src[0],g=src[1],b=src[2];
        uint16_t px=(uint16_t)((b>>3)|((uint16_t)(g>>2)<<5)|((uint16_t)(r>>3)<<11));
        if(big_endian)px=(uint16_t)((px>>8)|(px<<8));
        dest[0]=(uint8_t)(px&0xFF);
        dest[1]=(uint8_t)(px>>8);
        src+=4;
        dest+=2;
    }
}
#endif

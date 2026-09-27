#include <Arduino.h>

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif

#if T5_CACHE64_EXPERIMENT
// PNGdec's ESP32-S3 RGB565 routine is normally hidden behind an ESP-DSP
// feature header. The mixed Arduino+ESP-IDF cache64 environment does not
// compile standalone .S sources from this target reliably, so define the
// pinned PNGdec routine as a global assembler block in this guaranteed C++
// translation unit. This is the same Larry Bank / BitBank routine from
// PNGdec b862a89409962366fd704ed0445b983a710f034c.
__asm__(
".text\n"
".align 4\n"
".global s3_rgb565\n"
".type s3_rgb565,@function\n"
"s3_rgb565:\n"
"entry a1,16\n"
"addi.n a4,a4,7\n"
"movi.n a6,-8\n"
"and a4,a4,a6\n"
".top_rgb565:\n"
"ee.vld.128.ip q0,a2,16\n"
"ee.vld.128.ip q1,a2,16\n"
"ee.xorq q4,q4,q4\n"
"ee.vcmp.eq.s16 q2,q2,q2\n"
"ee.vunzip.16 q0,q1\n"
"ee.vsubs.s16 q3,q4,q2\n"
"movi.n a6,10\n"
"wsr.sar a6\n"
"ee.vmul.u16 q5,q0,q3\n"
"movi.n a6,3\n"
"wsr.sar a6\n"
"ee.vmul.s16 q6,q0,q3\n"
"ee.vmul.s16 q7,q1,q3\n"
"movi.n a6,5\n"
"wsr.sar a6\n"
"ee.vsl.32 q4,q3\n"
"ee.vsubs.s16 q4,q4,q3\n"
"ee.andq q6,q4,q6\n"
"ee.andq q7,q4,q7\n"
"ee.vsl.32 q5,q5\n"
"movi.n a6,11\n"
"wsr.sar a6\n"
"ee.vsl.32 q6,q6\n"
"ee.orq q6,q6,q5\n"
"ee.orq q6,q6,q7\n"
"mv.qr q5,q6\n"
"beqi a5,0,.rgb565_out\n"
"ee.vunzip.8 q6,q5\n"
"ee.vzip.8 q5,q6\n"
".rgb565_out:\n"
"ee.vst.128.ip q5,a3,16\n"
"addi.n a4,a4,-8\n"
"bnez.n a4,.top_rgb565\n"
"retw.n\n"
);
#endif

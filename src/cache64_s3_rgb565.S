//
// ESP32-S3 SIMD optimized RGB8888 -> RGB565 conversion.
// Original routine by Larry Bank / BitBank Software, from PNGdec pinned at:
// b862a89409962366fd704ed0445b983a710f034c (s3_simd_rgb565.S).
//
// PNGdec normally gates this routine behind ESP-DSP's
// dsps_fft2r_platform.h. MeshInk cache64 intentionally does not depend on
// ESP-DSP, so compile the pinned routine directly for the cache64 build.
//
#if defined(T5_CACHE64_EXPERIMENT) && T5_CACHE64_EXPERIMENT

    .text
    .align 4
    .global s3_rgb565
    .type   s3_rgb565,@function

s3_rgb565:
    entry  a1,16
    addi.n a4,a4,7
    movi.n a6,-8
    and  a4,a4,a6
.top_rgb565:
    ee.vld.128.ip      q0,a2,16
    ee.vld.128.ip      q1,a2,16
    ee.xorq            q4,q4,q4
    ee.vcmp.eq.s16     q2,q2,q2
    ee.vunzip.16       q0,q1
    ee.vsubs.s16       q3,q4,q2
    movi.n             a6,10
    wsr.sar            a6
    ee.vmul.u16        q5,q0,q3
    movi.n             a6,3
    wsr.sar            a6
    ee.vmul.s16        q6,q0,q3
    ee.vmul.s16        q7,q1,q3
    movi.n             a6,5
    wsr.sar            a6
    ee.vsl.32          q4,q3
    ee.vsubs.s16       q4,q4,q3
    ee.andq            q6,q4,q6
    ee.andq            q7,q4,q7
    ee.vsl.32          q5,q5
    movi.n             a6,11
    wsr.sar            a6
    ee.vsl.32          q6,q6
    ee.orq             q6,q6,q5
    ee.orq             q6,q6,q7
    mv.qr              q5,q6
    beqi               a5,0,.rgb565_out
    ee.vunzip.8        q6,q5
    ee.vzip.8          q5,q6
.rgb565_out:
    ee.vst.128.ip      q5,a3,16
    addi.n             a4,a4,-8
    bnez.n             a4,.top_rgb565
    retw.n

#endif

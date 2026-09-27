#include <Arduino.h>

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif

#if T5_CACHE64_EXPERIMENT
// The cache64 build previously supplied a scalar s3_rgb565() compatibility
// function because PNGdec gates its ESP32-S3 assembly behind ESP-DSP.
// MeshInk now compiles the pinned PNGdec S3 assembly routine directly from
// cache64_s3_rgb565.S, so no C++ replacement is needed here.
#endif

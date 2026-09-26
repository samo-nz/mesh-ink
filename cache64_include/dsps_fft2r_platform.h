#pragma once
// PNGdec 1.1.6's optional ESP32-S3 SIMD RGB565 assembly includes the
// ESP-DSP feature header unconditionally. The cache64 experiment does not
// depend on ESP-DSP, so disable only that optional conversion routine here.
#define dsps_fft2r_sc16_aes3_enabled 0

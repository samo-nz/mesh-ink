#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} H752LegacyRect;

void epd_init(void);
void epd_poweron(void);
void epd_poweroff(void);
void epd_clear(void);
void epd_poweroff_all(void);
void epd_clear_area(H752LegacyRect area);
void epd_clear_area_cycles(H752LegacyRect area,int32_t cycles,int32_t cycle_time);
void epd_draw_grayscale_image(H752LegacyRect area,uint8_t* data);
void epd_draw_pixel(int32_t x,int32_t y,uint8_t color,uint8_t* framebuffer);
void epd_draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint8_t color,uint8_t* framebuffer);
void epd_fill_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint8_t color,uint8_t* framebuffer);

#ifdef __cplusplus
}
#endif

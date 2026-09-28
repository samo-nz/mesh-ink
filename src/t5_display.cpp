#include "t5_display.h"

void t5_display_init(){
    epd_init(&epd_board_v7,&ED047TC1,EPD_LUT_64K);
}
void t5_display_deinit(){epd_deinit();}
void t5_display_set_rotation(enum EpdRotation rotation){epd_set_rotation(rotation);}
enum EpdRotation t5_display_get_rotation(){return epd_get_rotation();}
void t5_display_set_pixel_clock_mhz(int mhz){epd_set_lcd_pixel_clock_MHz(mhz);}
int t5_display_width(){return epd_width();}
int t5_display_height(){return epd_height();}
float t5_display_ambient_temperature(){return epd_ambient_temperature();}

EpdiyHighlevelState t5_display_hl_init(){return epd_hl_init(EPD_BUILTIN_WAVEFORM);}
uint8_t* t5_display_framebuffer(EpdiyHighlevelState* state){return epd_hl_get_framebuffer(state);}
void t5_display_set_all_white(EpdiyHighlevelState* state){epd_hl_set_all_white(state);}
EpdDrawError t5_display_update_screen(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature){
    return epd_hl_update_screen(state,mode,temperature);
}
EpdDrawError t5_display_update_area(EpdiyHighlevelState* state,EpdDrawMode mode,int temperature,EpdRect area){
    return epd_hl_update_area(state,mode,temperature,area);
}

void t5_display_poweron(){epd_poweron();}
void t5_display_poweroff(){epd_poweroff();}
void t5_display_clear(){epd_clear();}
void t5_display_draw_pixel(int x,int y,uint8_t color,uint8_t* framebuffer){epd_draw_pixel(x,y,color,framebuffer);}
void t5_display_draw_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer){epd_draw_rect(rect,color,framebuffer);}
void t5_display_fill_rect(EpdRect rect,uint8_t color,uint8_t* framebuffer){epd_fill_rect(rect,color,framebuffer);}

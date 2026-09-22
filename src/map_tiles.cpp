#include <Arduino.h>
#include <SD.h>
#include <PNGdec.h>
#include <epdiy.h>
#include <math.h>
#include "map_tiles.h"
#include "board/target.h"

namespace {
PNG png; File file; uint8_t* target=nullptr;
struct DrawContext { int dx,dy,crop_x,crop_y,crop_size; } ctx;
void* png_open(const char* name,int32_t* size){file=SD.open(name,FILE_READ);if(!file)return nullptr;*size=file.size();return &file;}
void png_close(void*){if(file)file.close();}
int32_t png_read(PNGFILE*,uint8_t* data,int32_t length){return file.read(data,length);}
int32_t png_seek(PNGFILE*,int32_t position){return file.seek(position)?position:-1;}
int png_draw(PNGDRAW* row){
    static uint16_t pixels[256];png.getLineAsRGB565(row,pixels,PNG_RGB565_LITTLE_ENDIAN,0xffffffff);
    if(row->y<ctx.crop_y||row->y>=ctx.crop_y+ctx.crop_size)return 1;
    const int out_y=ctx.dy+(row->y-ctx.crop_y)*256/ctx.crop_size;
    const int next_y=ctx.dy+(row->y-ctx.crop_y+1)*256/ctx.crop_size;
    for(int sx=ctx.crop_x;sx<ctx.crop_x+ctx.crop_size;++sx){
        const uint16_t c=pixels[sx];
        const uint8_t raw=(uint8_t)min(255U,(unsigned)((((c>>11)&31)*77+((c>>5)&63)*75+(c&31)*29)>>5));
        // E-paper needs stronger separation than a colour LCD. Expand the
        // useful mid-tones so roads/coastlines don't disappear into white.
        // Deliberately posterize colour tiles for the ED047TC1. Subtle
        // cartographic colours that look fine on LCD otherwise vanish on paper.
        // Keep light land/water light, but force roads, borders and labels into
        // visibly separated darker bands.
        const uint8_t gray=raw<105?0x10:raw<145?0x40:raw<180?0x70:raw<210?0xA0:raw<232?0xD0:0xF0;
        const int ox=ctx.dx+(sx-ctx.crop_x)*256/ctx.crop_size,next_x=ctx.dx+(sx-ctx.crop_x+1)*256/ctx.crop_size;
        if(next_x>0&&ox<540&&next_y>118&&out_y<900)epd_fill_rect({max(0,ox),max(118,out_y),min(540,next_x)-max(0,ox),min(900,next_y)-max(118,out_y)},gray,target);
    }return 1;
}
bool draw_tile(int z,int x,int y,int dx,int dy,int& reused){
    const int n=1<<z;x=(x%n+n)%n;if(y<0||y>=n)return false;
    for(int d=0;d<=min(z,6);++d){const int pz=z-d,px=x>>d,py=y>>d;char path[64];snprintf(path,sizeof(path),"/maps/%d/%d/%d.png",pz,px,py);if(!SD.exists(path))continue;
        const int divisions=1<<d;ctx={dx,dy,(x&(divisions-1))*256/divisions,(y&(divisions-1))*256/divisions,256/divisions};
        if(png.open(path,png_open,png_close,png_read,png_seek,png_draw)==PNG_SUCCESS){png.decode(nullptr,0);png.close();if(d)++reused;return true;}
    }return false;
}
}

MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,int height,double lat,double lon,uint8_t zoom){
    static bool attempted=false,ready=false;if(!attempted){attempted=true;pinMode(12,OUTPUT);digitalWrite(12,HIGH);ready=SD.begin(12,t5_shared_spi(),10000000);Serial.printf("[T5-MAP] SD init=%d\n",ready);}
    MapRenderResult result{ready,0,0};if(!ready)return result;target=framebuffer;lat=max(-85.0511,min(85.0511,lat));const double scale=256.0*(1<<zoom);const double cx=(lon+180.0)/360.0*scale;const double rad=lat*PI/180.0;const double cy=(1.0-log(tan(rad)+1.0/cos(rad))/PI)/2.0*scale;
    const int left=(int)floor(cx-width/2.0),top=(int)floor(cy-height/2.0);const int tx0=(int)floor(left/256.0),ty0=(int)floor(top/256.0);
    for(int ty=ty0;ty*256<top+height;++ty)for(int tx=tx0;tx*256<left+width;++tx){int reused=0;if(draw_tile(zoom,tx,ty,x+tx*256-left,y+ty*256-top,reused)){result.tiles++;result.reused+=reused;}}
    return result;
}

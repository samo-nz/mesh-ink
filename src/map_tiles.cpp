#include <Arduino.h>
#include <SD.h>
#include <PNGdec.h>
#include <epdiy.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include "map_tiles.h"
#include "board/target.h"

namespace {
constexpr int TILE_SIZE=256;
constexpr size_t TILE_BYTES=TILE_SIZE*TILE_SIZE/2;
// 48 native 4-bit grayscale source tiles = 1.5 MiB of PSRAM.
// At native zoom the 540x782
// map viewport spans at most 4x5 tiles, leaving room to pan in both directions.
constexpr size_t CACHE_SLOTS=48;
constexpr size_t ABSENT_SLOTS=128;
struct Tile {
    uint8_t* bits;
    int z,x,y;
    uint32_t age;
    bool valid;
};
struct AbsentTile {int z,x,y;bool valid;};
Tile tile_cache[CACHE_SLOTS]{};
AbsentTile absent_tiles[ABSENT_SLOTS]{};
size_t absent_cursor=0;
uint32_t cache_age=0;
PNG png;
File file;
uint8_t* target=nullptr;
uint8_t* decode_bits=nullptr;
struct DrawContext {int dx,dy,crop_x,crop_y,crop_size,tile_x,tile_y;};
DrawContext ctx{};
bool cache_memory_warning=false;

void* png_open(const char* name,int32_t* size) {
    file=SD.open(name,FILE_READ);
    if(!file)return nullptr;
    *size=file.size();
    return &file;
}
void png_close(void*) {if(file)file.close();}
int32_t png_read(PNGFILE*,uint8_t* data,int32_t length) {return file.read(data,length);}
int32_t png_seek(PNGFILE*,int32_t position) {return file.seek(position)?position:-1;}

// Keep source brightness in the 4-bit RAM cache, independent of how the
// panel is driven. Compose a consistent binary map from that brightness.
uint8_t gray_level(uint16_t colour) {
    const unsigned raw=min(255U,(unsigned)((((colour>>11)&31)*77+
                               ((colour>>5)&63)*75+(colour&31)*29)>>5));
    return (uint8_t)min(15U,(raw+8U)/17U);
}
// Dither against WORLD pixel coordinates, not screen coordinates: an unchanged
// map feature must keep the same black/white pattern after a small pan.
bool map_black(uint8_t level,int world_x,int world_y) {
    static constexpr uint8_t bayer4[16]={
        0, 8, 2,10,12, 4,14, 6, 3,11, 1, 9,15, 7,13, 5
    };
    const unsigned brightness=(unsigned)level*17U;
    // Keep world-anchored, panel-pixel dithering but restore map contrast:
    // the 2x test washed out terrain; the former 7x screen-space test
    // over-darkened it and had phase-dependent solid enlargement blocks.
    // 4x isolates contrast from the now-corrected scale/dither alignment.
    const unsigned darkness=min(255U,(255U-brightness)*4U);
    const unsigned threshold=16U*bayer4[((unsigned)world_y&3U)*4U+
                                      ((unsigned)world_x&3U)]+8U;
    return darkness>threshold;
}
uint8_t tile_level(const Tile& tile,int sx,int sy) {
    const size_t offset=(size_t)sy*TILE_SIZE+(size_t)sx;
    const uint8_t packed=tile.bits[offset>>1];
    return (offset&1U)?(uint8_t)(packed&0x0FU):(uint8_t)(packed>>4);
}
void fill_clipped(int x0,int y0,int x1,int y1,uint8_t colour) {
    const int left=max(0,x0),top=max(118,y0);
    const int right=min(540,x1),bottom=min(900,y1);
    if(left<right&&top<bottom)
        epd_fill_rect({left,top,right-left,bottom-top},colour,target);
}
// PNG callbacks keep source luminance in PSRAM. If allocation fails,
// decode directly to the framebuffer with the SAME monochrome map palette.
int png_draw(PNGDRAW* row) {
    static uint16_t pixels[TILE_SIZE];
    if(row->y<0||row->y>=TILE_SIZE)return 1;
    png.getLineAsRGB565(row,pixels,PNG_RGB565_LITTLE_ENDIAN,0xffffffff);
    if(decode_bits) {
        for(int sx=0;sx<TILE_SIZE;++sx) {
            const size_t offset=(size_t)row->y*TILE_SIZE+(size_t)sx;
            const uint8_t level=gray_level(pixels[sx]);
            uint8_t& packed=decode_bits[offset>>1];
            if(offset&1U)packed=(uint8_t)((packed&0xF0U)|level);
            else packed=(uint8_t)((packed&0x0FU)|(level<<4));
        }
        return 1;
    }
    if(row->y<ctx.crop_y||row->y>=ctx.crop_y+ctx.crop_size)return 1;
    // Rare low-PSRAM fallback: draw the same per-DISPLAY-pixel world-anchored
    // pattern as the cached path, rather than duplicating one dither sample
    // across an enlarged source pixel.
    const int y0=max(118,ctx.dy+
        (row->y-ctx.crop_y)*TILE_SIZE/ctx.crop_size);
    const int y1=min(900,ctx.dy+
        (row->y-ctx.crop_y+1)*TILE_SIZE/ctx.crop_size);
    for(int py=y0;py<y1;++py) {
        for(int sx=ctx.crop_x;sx<ctx.crop_x+ctx.crop_size;++sx) {
            const uint8_t level=gray_level(pixels[sx]);
            const int x0=max(0,ctx.dx+
                (sx-ctx.crop_x)*TILE_SIZE/ctx.crop_size);
            const int x1=min(540,ctx.dx+
                (sx-ctx.crop_x+1)*TILE_SIZE/ctx.crop_size);
            for(int px=x0;px<x1;++px)
                epd_fill_rect({px,py,1,1},
                    map_black(level,ctx.tile_x*TILE_SIZE+px-ctx.dx,
                                   ctx.tile_y*TILE_SIZE+py-ctx.dy)?0x00:0xFF,
                    target);
        }
    }
    return 1;
}
Tile* find_cached(int z,int x,int y) {
    for(auto& entry:tile_cache)
        if(entry.valid&&entry.z==z&&entry.x==x&&entry.y==y) {
            entry.age=++cache_age;
            return &entry;
        }
    return nullptr;
}
bool previously_absent(int z,int x,int y) {
    for(const auto& entry:absent_tiles)
        if(entry.valid&&entry.z==z&&entry.x==x&&entry.y==y)return true;
    return false;
}
void mark_absent(int z,int x,int y) {
    AbsentTile& entry=absent_tiles[absent_cursor++%ABSENT_SLOTS];
    entry={z,x,y,true};
}
Tile* acquire_slot() {
    Tile* oldest=&tile_cache[0];
    for(auto& entry:tile_cache) {
        if(!entry.valid){oldest=&entry;break;}
        if(entry.age<oldest->age)oldest=&entry;
    }
    if(!oldest->bits) {
        oldest->bits=(uint8_t*)heap_caps_malloc(TILE_BYTES,
                            MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!oldest->bits) {
            if(!cache_memory_warning) {
                Serial.println("[T5-MAP] tile PSRAM unavailable; decoding without tile cache");
                cache_memory_warning=true;
            }
            return nullptr;
        }
    }
    oldest->valid=false;  // Never serve a partially decoded or evicted tile.
    memset(oldest->bits,0xFF,TILE_BYTES);
    oldest->age=++cache_age;
    return oldest;
}
// Preserve the old source-tile hierarchy: exact zoom first, then up to six
// parent levels. Cache the SOURCE PNG, not a viewport-specific tile image;
// different pan positions and child zooms can reuse the same decoded data.
bool load_source(int z,int x,int y,const DrawContext& draw,
                 MapRenderResult& result,Tile*& output,bool& direct) {
    output=find_cached(z,x,y);
    if(output){++result.ram_hits;return true;}
    if(previously_absent(z,x,y))return false;
    char path[64];
    snprintf(path,sizeof(path),"/maps/%d/%d/%d.png",z,x,y);
    ++result.sd_checks;
    if(!SD.exists(path)){mark_absent(z,x,y);return false;}
    if(png.open(path,png_open,png_close,png_read,png_seek,png_draw)!=PNG_SUCCESS)
        return false;
    if(png.getWidth()!=TILE_SIZE||png.getHeight()!=TILE_SIZE) {
        Serial.printf("[T5-MAP] wrong PNG size: %s\n",path);
        png.close();
        return false;
    }
    Tile* slot=acquire_slot();
    ctx=draw;
    decode_bits=slot?slot->bits:nullptr;
    const int decode_status=png.decode(nullptr,0);
    png.close();
    decode_bits=nullptr;
    if(decode_status!=PNG_SUCCESS) {
        if(slot)slot->valid=false;
        Serial.printf("[T5-MAP] PNG decode failed: %s status=%d\n",
                      path,decode_status);
        return false;
    }
    ++result.disk_decodes;
    if(slot) {
        slot->z=z;slot->x=x;slot->y=y;slot->valid=true;
        output=slot;
    } else direct=true;
    return true;
}
void draw_cached(const Tile& tile,const DrawContext& draw) {
    // Compose at actual panel-pixel resolution. Sampling/dithering once per
    // enlarged SOURCE pixel would turn one 2x/4x/8x block all black or all
    // white depending on a single changing threshold phase.
    const int x0=max(0,draw.dx),x1=min(540,draw.dx+TILE_SIZE);
    const int y0=max(118,draw.dy),y1=min(900,draw.dy+TILE_SIZE);
    if(x0>=x1||y0>=y1)return;
    for(int py=y0;py<y1;++py) {
        const int sy=draw.crop_y+(py-draw.dy)*draw.crop_size/TILE_SIZE;
        const int world_y=draw.tile_y*TILE_SIZE+py-draw.dy;
        int run_x=x0;
        int sx=draw.crop_x+(x0-draw.dx)*draw.crop_size/TILE_SIZE;
        bool black=map_black(tile_level(tile,sx,sy),
                             draw.tile_x*TILE_SIZE+x0-draw.dx,world_y);
        for(int px=x0+1;px<x1;++px) {
            sx=draw.crop_x+(px-draw.dx)*draw.crop_size/TILE_SIZE;
            const bool next_black=map_black(tile_level(tile,sx,sy),
                draw.tile_x*TILE_SIZE+px-draw.dx,world_y);
            if(next_black!=black) {
                epd_fill_rect({run_x,py,px-run_x,1},
                              black?0x00:0xFF,target);
                run_x=px;
                black=next_black;
            }
        }
        epd_fill_rect({run_x,py,x1-run_x,1},black?0x00:0xFF,target);
    }
}
bool draw_tile(int zoom,int x,int y,int dx,int dy,MapRenderResult& result) {
    const int n=1<<zoom;
    x=(x%n+n)%n;
    if(y<0||y>=n)return false;
    for(int depth=0;depth<=min(zoom,6);++depth) {
        const int source_zoom=zoom-depth;
        const int parent_x=x>>depth,parent_y=y>>depth;
        const int subdivisions=1<<depth;
        const DrawContext draw={dx,dy,
            (x&(subdivisions-1))*TILE_SIZE/subdivisions,
            (y&(subdivisions-1))*TILE_SIZE/subdivisions,
            TILE_SIZE/subdivisions,x,y};
        Tile* tile=nullptr;bool direct=false;
        if(!load_source(source_zoom,parent_x,parent_y,draw,result,tile,direct))
            continue;
        if(tile)draw_cached(*tile,draw);
        // A low-memory decode drew the same requested tile directly.
        ++result.tiles;
        if(depth)++result.reused;
        else ++result.native;
        // The initial range is only a placeholder. Do not claim the requested
        // zoom was loaded when all displayed tiles came from parent tiles.
        if(result.tiles==1) {
            result.min_source_zoom=(uint8_t)source_zoom;
            result.max_source_zoom=(uint8_t)source_zoom;
        } else {
            result.min_source_zoom=min(result.min_source_zoom,(uint8_t)source_zoom);
            result.max_source_zoom=max(result.max_source_zoom,(uint8_t)source_zoom);
        }
        return true;
    }
    return false;
}
} // namespace

MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,
                                int height,double lat,double lon,uint8_t zoom) {
    static bool attempted=false,ready=false;
    if(!attempted) {
        attempted=true;
        pinMode(12,OUTPUT);digitalWrite(12,HIGH);
        ready=SD.begin(12,t5_shared_spi(),10000000);
        Serial.printf("[T5-MAP] SD init=%d\n",ready);
    }
    MapRenderResult result{ready,0,0,0,0,zoom,zoom,0,0,0};
    if(!ready)return result;
    target=framebuffer;
    lat=max(-85.0511,min(85.0511,lat));
    const double world=256.0*(1<<zoom);
    const double centre_x=(lon+180.0)/360.0*world;
    const double rad=lat*PI/180.0;
    const double centre_y=(1.0-log(tan(rad)+1.0/cos(rad))/PI)/2.0*world;
    const int left=(int)floor(centre_x-width/2.0);
    const int top=(int)floor(centre_y-height/2.0);
    const int tx0=(int)floor(left/256.0),ty0=(int)floor(top/256.0);
    for(int ty=ty0;ty*256<top+height;++ty)
        for(int tx=tx0;tx*256<left+width;++tx)
            if(!draw_tile(zoom,tx,ty,
                          x+tx*256-left,y+ty*256-top,result))
                ++result.missing;
    Serial.printf("[T5-MAP] mode=WORLD_DITHER_4X zoom=%u source_z=%u-%u tiles=%u native=%u reused=%u missing=%u RAM=%u PNG=%u SD_checks=%u centre=%.5f,%.5f\n",
                  zoom,result.min_source_zoom,result.max_source_zoom,
                  result.tiles,result.native,result.reused,result.missing,
                  result.ram_hits,result.disk_decodes,result.sd_checks,lat,lon);
    return result;
}

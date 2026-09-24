#include <Arduino.h>
#include "t5_logging.h"
#include <SD.h>
#include <PNGdec.h>
#include <epdiy.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "map_tiles.h"
#include "pmtiles_reader.h"
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
// Loose PNGs take priority. Archives are discovered once per SD mount.
constexpr size_t MAX_ARCHIVES=8;
constexpr size_t SOURCE_PATH_BYTES=160;
char archive_paths[MAX_ARCHIVES][SOURCE_PATH_BYTES]{};
size_t archive_count=0;
bool archives_discovered=false;
bool png_range_active=false;
uint32_t png_range_start=0,png_range_length=0;
bool zoom_folder_known[25]{},zoom_folder_present[25]{};
bool sd_mounted=false,map_io_failed=false;
uint32_t map_archive_lookup_ms=0,map_png_decode_ms=0,map_compose_ms=0;
uint32_t sd_retry_after=0,sd_media_epoch=0;
constexpr uint32_t SD_RETRY_MS=1500;
void reset_sd_caches() {
    for(auto& tile:tile_cache)tile.valid=false;
    memset(absent_tiles,0,sizeof(absent_tiles));
    absent_cursor=0;
    memset(zoom_folder_known,0,sizeof(zoom_folder_known));
    memset(zoom_folder_present,0,sizeof(zoom_folder_present));
    archive_count=0;
    archives_discovered=false;
    png_range_active=false;
    png_range_start=png_range_length=0;
    pmtiles_reset();
}
void mark_sd_unavailable() {
    if(file)file.close();
    reset_sd_caches(); // close all archive handles before unmounting
    if(sd_mounted)SD.end();
    sd_mounted=false;
    map_io_failed=false;
    sd_retry_after=millis()+SD_RETRY_MS;
    ++sd_media_epoch;
    Serial.println("[T5-MAP] SD unavailable; map caches invalidated");
}
bool media_ready(bool probe=true) {
    if(!sd_mounted) {
        if((int32_t)(millis()-sd_retry_after)<0)return false;
        pinMode(12,OUTPUT);digitalWrite(12,HIGH);
        SD.end();
        if(!SD.begin(12,t5_shared_spi(),10000000)) {
            sd_retry_after=millis()+SD_RETRY_MS;
            return false;
        }
        sd_mounted=true;
        reset_sd_caches();
        ++sd_media_epoch;
        Serial.println("[T5-MAP] SD mounted; map caches reset");
    }
    if(probe) {
        // Do not use SD.readRAW() as a card-presence oracle: an otherwise
        // readable card may reject the raw-sector probe, which previously
        // caused an endless mount/unmount loop. Check the *same filesystem
        // read path* the map renderer uses, and require consecutive failures
        // before treating a probe error as physical removal.
        bool readable=false;
        if(archives_discovered&&archive_count) {
            File witness=SD.open(archive_paths[0],FILE_READ);
            uint8_t magic[8]{};
            readable=witness&&witness.read(magic,sizeof(magic))==sizeof(magic)
                &&memcmp(magic,"PMTiles",7)==0&&magic[7]==3;
            if(witness)witness.close();
        } else {
            File maps=SD.open("/maps");
            readable=maps&&maps.isDirectory();
            if(maps)maps.close();
            // No map folder is a valid inserted card state, but the absence
            // of a witness cannot establish removal. Never unmount here.
            if(!readable) {
                File root=SD.open("/");
                readable=root&&root.isDirectory();
                if(root)root.close();
            }
        }
        static uint8_t consecutive_probe_failures=0;
        if(readable) {
            consecutive_probe_failures=0;
        } else if(archives_discovered&&archive_count) {
            if(++consecutive_probe_failures>=2) {
                consecutive_probe_failures=0;
                Serial.println("[T5-MAP] archive read failed twice; resetting SD");
                mark_sd_unavailable();
                return false;
            }
            Serial.println("[T5-MAP] SD archive probe failed once; verifying on next poll");
        } else {
            // An empty card has nothing to read as a reliable witness.
            // Only a genuine tile read failure triggers remounting here.
            consecutive_probe_failures=0;
        }
    }
    return true;
}
// Scans /maps/*.pmtiles and /maps/<name>/*.pmtiles so files copied
// directly from common map downloaders work without being renamed.
void add_archive(const char* parent,const char* name) {
    if(!name||archive_count>=MAX_ARCHIVES)return;
    const char* basename=strrchr(name,'/');
    basename=basename?basename+1:name;
    const size_t len=strlen(basename);
    if(len<8||strcasecmp(basename+len-8,".pmtiles"))return;
    char absolute[SOURCE_PATH_BYTES];
    const int written=snprintf(absolute,sizeof(absolute),"%s/%s",
                               parent,basename);
    if(written<=0||written>=int(sizeof(absolute)))return;
    for(size_t i=0;i<archive_count;++i)
        if(!strcmp(archive_paths[i],absolute))return;
    strcpy(archive_paths[archive_count++],absolute);
}
bool is_zoom_folder(const char* name) {
    if(!name||!*name)return false;
    for(const char* p=name;*p;++p)
        if(*p<'0'||*p>'9')return false;
    return true;
}
void discover_archives() {
    if(archives_discovered)return;
    File directory=SD.open("/maps");
    if(!directory||!directory.isDirectory()) {
        if(directory)directory.close();
        return;
    }
    archives_discovered=true;
    File candidate=directory.openNextFile();
    while(candidate) {
        const char* entry_name=candidate.name();
        const char* basename=entry_name?strrchr(entry_name,'/'):nullptr;
        basename=basename?basename+1:entry_name;
        if(candidate.isDirectory()&&basename&&!is_zoom_folder(basename)) {
            char folder[SOURCE_PATH_BYTES];
            const int written=snprintf(folder,sizeof(folder),
                                       "/maps/%s",basename);
            if(written>0&&written<int(sizeof(folder))) {
                // Keep folder scanning shallow: loose XYZ tile directories
                // can contain tens of thousands of PNG files.
                File subdir=SD.open(folder);
                if(subdir&&subdir.isDirectory()) {
                    File nested=subdir.openNextFile();
                    while(nested&&archive_count<MAX_ARCHIVES) {
                        if(!nested.isDirectory())
                            add_archive(folder,nested.name());
                        nested.close();
                        nested=subdir.openNextFile();
                    }
                }
                if(subdir)subdir.close();
            }
        } else if(!candidate.isDirectory()) {
            add_archive("/maps",entry_name);
        }
        candidate.close();
        if(archive_count==MAX_ARCHIVES)break;
        candidate=directory.openNextFile();
    }
    directory.close();
    if(archive_count)
        Serial.printf("[T5-MAP] found %u PMTiles archive(s) on SD\n",
                      (unsigned)archive_count);
}


void* png_open(const char* name,int32_t* size) {
    file=SD.open(name,FILE_READ);
    if(!file){
        Serial.printf("[T5-MAP] tile file open failed: %s range=%u\n",
                      name,(unsigned)png_range_active);
        map_io_failed=true;
        return nullptr;
    }
    if(png_range_active) {
        if(!png_range_length||png_range_length>INT32_MAX||
           !file.seek(png_range_start)) {
            file.close();
            return nullptr;
        }
        *size=(int32_t)png_range_length;
    } else *size=file.size();
    return &file;
}
void png_close(void*) {if(file)file.close();}
int32_t png_read(PNGFILE*,uint8_t* data,int32_t length) {
    if(length<=0)return 0;
    // PNGdec may request a whole 2048-byte input buffer even when a PNG
    // has fewer bytes remaining. Returning only the remaining bytes is the
    // documented read-callback behaviour, NOT a failed SD transaction.
    const uint64_t pos=file.position();
    const uint64_t end=png_range_active
        ? (uint64_t)png_range_start+png_range_length
        : (uint64_t)file.size();
    if(png_range_active&&pos<png_range_start){
        map_io_failed=true; // unexpected seek outside PMTiles tile range
        return 0;
    }
    if(pos>=end)return 0; // clean EOF for both loose PNGs and PMTiles ranges
    const int32_t allowed=(int32_t)min((uint64_t)length,end-pos);
    const int32_t n=file.read(data,allowed);
    if(n!=allowed){
        Serial.printf("[T5-MAP] tile SD read failed: got=%ld expected=%ld pos=%lu end=%llu\n",
                      (long)n,(long)allowed,(unsigned long)file.position(),
                      (unsigned long long)end);
        map_io_failed=true;
    }
    return n;
}
int32_t png_seek(PNGFILE*,int32_t position) {
    if(position<0 || (png_range_active &&
       (uint32_t)position>png_range_length))return -1;
    const uint64_t absolute=(uint64_t)(png_range_active?png_range_start:0)+
                            (uint32_t)position;
    if(absolute>UINT32_MAX||!file.seek((uint32_t)absolute)){
        Serial.printf("[T5-MAP] tile seek failed: position=%ld range=%lu\n",
                      (long)position,(unsigned long)png_range_length);
        map_io_failed=true;
        return -1;
    }
    return position;
}

// Keep source brightness in the 4-bit RAM cache, independent of how the
// panel is driven. Compose a consistent binary map from that brightness.
uint8_t gray_level(uint16_t colour) {
    const unsigned raw=min(255U,(unsigned)((((colour>>11)&31)*77+
                               ((colour>>5)&63)*75+(colour&31)*29)>>5));
    return (uint8_t)min(15U,(raw+8U)/17U);
}
// Dither against WORLD pixel coordinates, not screen coordinates.
// This lookup reproduces the existing 16 brightness levels and all 16
// Bayer phases exactly, but avoids repeating the darkness calculation for
// every framebuffer pixel.
const uint16_t* map_black_masks() {
    static uint16_t masks[16]{};
    static bool ready=false;
    if(!ready) {
        static constexpr uint8_t bayer4[16]={
            0, 8, 2,10,12, 4,14, 6, 3,11, 1, 9,15, 7,13, 5
        };
        for(unsigned level=0;level<16;++level) {
            const unsigned brightness=level*17U;
            const unsigned darkness=min(255U,((255U-brightness)*5U+1U)/2U);
            for(unsigned phase=0;phase<16;++phase)
                if(darkness>16U*bayer4[phase]+8U)
                    masks[level]|=(uint16_t)(1U<<phase);
        }
        ready=true;
    }
    return masks;
}
bool map_black(uint8_t level,int world_x,int world_y) {
    const unsigned phase=(((unsigned)world_y&3U)<<2)|
                          ((unsigned)world_x&3U);
    return (map_black_masks()[level]&(1U<<phase))!=0;
}
uint8_t tile_level(const Tile& tile,int sx,int sy) {
    const size_t offset=(size_t)sy*TILE_SIZE+(size_t)sx;
    const uint8_t packed=tile.bits[offset>>1];
    return (offset&1U)?(uint8_t)(packed&0x0FU):(uint8_t)(packed>>4);
}
void fill_clipped(int x0,int y0,int x1,int y1,uint8_t colour) {
    const int left=max(0,x0),top=max(48,y0);
    const int right=min(540,x1),bottom=min(900,y1);
    if(left<right&&top<bottom)
        epd_fill_rect({left,top,right-left,bottom-top},colour,target);
}
// PNG callbacks keep source luminance in PSRAM. If allocation fails,
// decode directly to the framebuffer with the SAME monochrome map palette.
int png_draw(PNGDRAW* row) {
    static uint16_t pixels[TILE_SIZE];
    // PNGdec writes iWidth RGB565 pixels into the caller's buffer.
    // Reject unexpected rows rather than risking an overwrite.
    if(row->y<0||row->y>=TILE_SIZE||row->iWidth!=TILE_SIZE)return 0;
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
    const int y0=max(48,ctx.dy+
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
    if(map_io_failed||pmtiles_had_io_error())return false;
    if(previously_absent(z,x,y))return false;
    char path[SOURCE_PATH_BYTES];
    snprintf(path,sizeof(path),"/maps/%d/%d/%d.png",z,x,y);
    PmtilesPngRange range{};
    // One test per zoom when only PMTiles exist; loose PNGs still win.
    if(z>=0&&z<25&&!zoom_folder_known[z]) {
        char folder[24];
        snprintf(folder,sizeof(folder),"/maps/%d",z);
        zoom_folder_present[z]=SD.exists(folder);
        zoom_folder_known[z]=true;
        ++result.sd_checks;
    }
    bool loose_present=false;
    if(z>=0&&z<25&&zoom_folder_present[z]){
        loose_present=SD.exists(path);
        ++result.sd_checks;
    }
    if(!loose_present) {
        discover_archives();
        bool found=false;
        for(size_t i=0;i<archive_count;++i) {
            ++result.sd_checks;
            const uint32_t lookup_started=millis();
            const bool found_in_archive=pmtiles_find_png(archive_paths[i],z,x,y,range);
            map_archive_lookup_ms+=millis()-lookup_started;
            if(found_in_archive) {
                snprintf(path,sizeof(path),"%s",archive_paths[i]);
                found=true;
                break;
            }
            if(pmtiles_had_io_error()){
                Serial.printf("[T5-MAP] archive lookup I/O failed: %s z=%d x=%d y=%d\n",
                              archive_paths[i],z,x,y);
                map_io_failed=true;
                return false;
            }
        }
        if(!found){mark_absent(z,x,y);return false;}
    }
    png_range_active=range.length!=0;
    png_range_start=range.offset;
    png_range_length=range.length;
    const int open_status=png.open(path,png_open,png_close,
                                  png_read,png_seek,png_draw);
    if(open_status!=PNG_SUCCESS) {
        if(file)file.close();
        png_range_active=false;
        return false;
    }
    if(png.getWidth()!=TILE_SIZE||png.getHeight()!=TILE_SIZE) {
        Serial.printf("[T5-MAP] wrong PNG size: %s\n",path);
        png.close();
        png_range_active=false;
        return false;
    }
    Tile* slot=acquire_slot();
    ctx=draw;
    decode_bits=slot?slot->bits:nullptr;
    const uint32_t decode_started=millis();
    const int decode_status=png.decode(nullptr,0);
    map_png_decode_ms+=millis()-decode_started;
    png.close();
    png_range_active=false;
    decode_bits=nullptr;
    if(map_io_failed) {
        if(slot)slot->valid=false;
        return false;
    }
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
    // Keep the exact same WORLD-anchored dithering and panel-pixel sampling.
    // crop_size is always 256 / 2^depth: use a shift rather than a source
    // coordinate division for every pixel, and reuse the packed source row.
    const int x0=max(0,draw.dx),x1=min(540,draw.dx+TILE_SIZE);
    const int y0=max(48,draw.dy),y1=min(900,draw.dy+TILE_SIZE);
    if(x0>=x1||y0>=y1)return;
    unsigned shift=0;
    while((TILE_SIZE>>shift)>draw.crop_size)++shift;
    const uint16_t* masks=map_black_masks();
    const int world_x_base=draw.tile_x*TILE_SIZE-draw.dx;
    for(int py=y0;py<y1;++py) {
        const int sy=draw.crop_y+((py-draw.dy)>>shift);
        const uint8_t* source_row=tile.bits+(size_t)sy*(TILE_SIZE/2);
        const int world_y=draw.tile_y*TILE_SIZE+py-draw.dy;
        const unsigned row_phase=((unsigned)world_y&3U)<<2;
        const auto is_black=[&](int px)->bool {
            const int sx=draw.crop_x+((px-draw.dx)>>shift);
            const uint8_t packed=source_row[sx>>1];
            const unsigned level=(sx&1)?(packed&0x0FU):(packed>>4);
            const unsigned phase=row_phase|
                                 ((unsigned)(world_x_base+px)&3U);
            return (masks[level]&(1U<<phase))!=0;
        };
        int run_x=x0;
        bool black=is_black(x0);
        for(int px=x0+1;px<x1;++px) {
            const bool next_black=is_black(px);
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
        if(tile) {
            const uint32_t compose_started=millis();
            draw_cached(*tile,draw);
            map_compose_ms+=millis()-compose_started;
        }
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

bool map_tiles_media_ready(){return media_ready(true);}
uint32_t map_tiles_media_epoch(){return sd_media_epoch;}

MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,
                                int height,double lat,double lon,uint8_t zoom) {
    const uint32_t map_render_started=millis();
    map_archive_lookup_ms=map_png_decode_ms=map_compose_ms=0;
    const bool ready=media_ready(false);
    MapRenderResult result{ready,0,0,0,0,zoom,zoom,0,0,0};
    if(!ready)return result;
    map_io_failed=false;
    pmtiles_begin_frame();
    target=framebuffer;
    lat=max(-85.0511,min(85.0511,lat));
    const double world=256.0*(1<<zoom);
    const double centre_x=(lon+180.0)/360.0*world;
    const double rad=lat*PI/180.0;
    const double centre_y=(1.0-log(tan(rad)+1.0/cos(rad))/PI)/2.0*world;
    const int left=(int)floor(centre_x-width/2.0);
    const int top=(int)floor(centre_y-height/2.0);
    const int tx0=(int)floor(left/256.0),ty0=(int)floor(top/256.0);
    for(int ty=ty0;ty*256<top+height&&!map_io_failed&&!pmtiles_had_io_error();++ty)
        for(int tx=tx0;tx*256<left+width&&!map_io_failed&&!pmtiles_had_io_error();++tx)
            if(!draw_tile(zoom,tx,ty,
                          x+tx*256-left,y+ty*256-top,result))
                ++result.missing;
    const bool failed=map_io_failed||pmtiles_had_io_error();
    pmtiles_end_frame();
    if(failed) {
        // A failed *tile* operation is not proof the card was removed: an
        // individual PNG/file may be absent, truncated or briefly unreadable.
        // Check independent filesystem I/O before unmounting the entire card.
        const bool storage_responds=media_ready(true);
        Serial.printf("[T5-MAP] map tile read failed; SD witness=%s (no blind unmount)\n",
                      storage_responds?"OK":"UNAVAILABLE");
        result.sd_ready=storage_responds;
        result.tiles=0; // partial frame must never become cached as complete
    }
    // On-device breakdown identifies whether further optimization should
    // target archive lookup, PNG decode, or screen-pixel composition.
    Serial.printf("[T5-MAP] render=%lu ms archive_lookup=%lu png_decode=%lu compose=%lu ms tiles=%u PNG=%u RAM=%u missing=%u\n",
                  (unsigned long)(millis()-map_render_started),
                  (unsigned long)map_archive_lookup_ms,
                  (unsigned long)map_png_decode_ms,
                  (unsigned long)map_compose_ms,
                  (unsigned)result.tiles,(unsigned)result.disk_decodes,
                  (unsigned)result.ram_hits,(unsigned)result.missing);
    T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] mode=WORLD_DITHER_2P5X zoom=%u source_z=%u-%u tiles=%u native=%u reused=%u missing=%u RAM=%u PNG=%u SD_checks=%u centre=%.5f,%.5f\n",
                  zoom,result.min_source_zoom,result.max_source_zoom,
                  result.tiles,result.native,result.reused,result.missing,
                  result.ram_hits,result.disk_decodes,result.sd_checks,lat,lon);
    return result;
}

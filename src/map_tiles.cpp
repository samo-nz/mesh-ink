#include <Arduino.h>
#include "t5_logging.h"
#include <PNGdec.h>
#include "hardware/display.h"
#include "hardware/storage.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "map_tiles.h"
#include "map_gray.h"
#include "pmtiles_reader.h"

namespace {
constexpr int TILE_SIZE=256;
constexpr size_t TILE_BYTES=TILE_SIZE*TILE_SIZE/2;
// Wide pan/zoom cache: 96 native 4-bit source tiles can
// occupy up to 3 MiB PSRAM. A failed allocation falls back to direct decode.
constexpr size_t CACHE_SLOTS=96;
constexpr size_t ABSENT_SLOTS=128;
struct Tile {
    uint8_t* bits;
    int z,x,y;
    uint32_t age;
    bool valid;
    bool from_pmtiles;
};
struct AbsentTile {int z,x,y;bool valid;};
Tile tile_cache[CACHE_SLOTS]{};
AbsentTile absent_tiles[ABSENT_SLOTS]{};
size_t absent_cursor=0;
uint32_t cache_age=0;
// PNGdec's S3 SIMD path assumes its internal RGBA row buffer is 16-byte
// aligned. In the hybrid cache64 link a normally declared PNG object landed
// at +8 mod 16, making every vector source row misaligned. Align the whole
// decoder object so PNGdec's internal ucPixels row remains vector-safe.
alignas(16) PNG png;
#if T5_CACHE64_EXPERIMENT
extern "C" void s3_rgb565(uint8_t* src,uint8_t* dest,int count,bool big_endian);
#endif
MeshInkStorageFile file;                 // owns loose PNG file handles
MeshInkStorageFile* png_file=nullptr;    // borrows the already open PMTiles archive file
uint8_t* target=nullptr;
MeshInkRect render_clip{0,0,0,0};
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
uint32_t sd_retry_after=0,sd_media_epoch=0;
constexpr uint32_t SD_RETRY_MS=1500;
// Physical card wiring, SPI ownership and the field-tested bus clock are
// selected by the storage backend rather than by Maps.
// Per-render PMTiles/PNG timing. PNG decode includes its nested range I/O;
// range seek/read counters are logged separately so CPU decode can be inferred.
uint32_t perf_pmt_lookup_us=0,perf_pmt_range_seek_us=0,perf_pmt_range_read_us=0;
uint32_t perf_pmt_preload_seek_us=0,perf_pmt_preload_read_us=0;
uint32_t perf_pmt_decode_us=0,perf_loose_decode_us=0,perf_compose_us=0;
uint32_t perf_pmt_range_bytes=0,perf_pmt_preload_bytes=0;
uint16_t perf_pmt_seek_calls=0,perf_pmt_read_calls=0,perf_pmt_preload_reads=0;

inline uint32_t map_perf_now_us() {
#if T5_LOG_MAP
    return micros();
#else
    return 0;
#endif
}

// PMTiles payload scratch lives in PSRAM. SD reads are staged through aligned
// internal RAM because direct SD DMA into PSRAM has previously been unreliable
// on this combined Arduino+ESP-IDF/cache64 build.
uint8_t* pmt_png_buffer=nullptr;
size_t pmt_png_capacity=0;
alignas(4) uint8_t pmt_io_stage[4096];

void reset_map_perf() {
    perf_pmt_lookup_us=perf_pmt_range_seek_us=perf_pmt_range_read_us=0;
    perf_pmt_preload_seek_us=perf_pmt_preload_read_us=0;
    perf_pmt_decode_us=perf_loose_decode_us=perf_compose_us=0;
    perf_pmt_range_bytes=perf_pmt_preload_bytes=0;
    perf_pmt_seek_calls=perf_pmt_read_calls=perf_pmt_preload_reads=0;
}
bool ensure_pmt_png_buffer(size_t n) {
    if(n<=pmt_png_capacity&&pmt_png_buffer)return true;
    if(!n||n>(size_t)-1-4095U) {
        Serial.printf("[T5-MAP] rejected impossible PMTiles preload size=%llu\n",
                      (unsigned long long)n);
        return false;
    }
    const size_t wanted=(n+4095U)&~(size_t)4095U;
    uint8_t* next=(uint8_t*)heap_caps_malloc(
        wanted,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!next)return false;
    free(pmt_png_buffer);
    pmt_png_buffer=next;
    pmt_png_capacity=wanted;
    return true;
}
bool preload_pmt_png(MeshInkStorageFile* archive,uint32_t offset,uint32_t length) {
    if(!archive||!length||!ensure_pmt_png_buffer(length))return false;
    const uint32_t seek_started=map_perf_now_us();
    const bool seek_ok=archive->position()==offset||archive->seek(offset);
    perf_pmt_preload_seek_us+=(uint32_t)(map_perf_now_us()-seek_started);
    if(!seek_ok){map_io_failed=true;return false;}
    uint32_t copied=0;
    while(copied<length) {
        const size_t chunk=min((size_t)(length-copied),sizeof(pmt_io_stage));
        const uint32_t read_started=map_perf_now_us();
        const size_t got=archive->read(pmt_io_stage,chunk);
        perf_pmt_preload_read_us+=(uint32_t)(map_perf_now_us()-read_started);
        ++perf_pmt_preload_reads;
        perf_pmt_preload_bytes+=(uint32_t)got;
        if(got!=chunk){map_io_failed=true;return false;}
        memcpy(pmt_png_buffer+copied,pmt_io_stage,chunk);
        copied+=(uint32_t)chunk;
    }
    return true;
}
void reset_sd_caches() {
    for(auto& tile:tile_cache)tile.valid=false;
    memset(absent_tiles,0,sizeof(absent_tiles));
    absent_cursor=0;
    memset(zoom_folder_known,0,sizeof(zoom_folder_known));
    memset(zoom_folder_present,0,sizeof(zoom_folder_present));
    archive_count=0;
    archives_discovered=false;
    free(pmt_png_buffer);
    pmt_png_buffer=nullptr;
    pmt_png_capacity=0;
    png_range_active=false;
    png_range_start=png_range_length=0;
    pmtiles_reset();
}
void mark_sd_unavailable() {
    if(file)file.close();
    png_file=nullptr;
    reset_sd_caches(); // close all archive handles before unmounting
    if(sd_mounted)meshink_storage_end();
    sd_mounted=false;
    map_io_failed=false;
    sd_retry_after=millis()+SD_RETRY_MS;
    ++sd_media_epoch;
    Serial.println("[T5-MAP] SD unavailable; map caches invalidated");
}
bool media_ready(bool probe=true) {
    if(!sd_mounted) {
        if((int32_t)(millis()-sd_retry_after)<0)return false;
        if(!meshink_storage_begin()) {
            sd_retry_after=millis()+SD_RETRY_MS;
            return false;
        }
        sd_mounted=true;
        reset_sd_caches();
        ++sd_media_epoch;
        T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] SD mounted; SPI clock requested=%lu MHz\n",
                      (unsigned long)(meshink_storage_bus_hz()/1000000));
    }
    if(probe) {
        // Do not use SD.readRAW() as a card-presence oracle: an otherwise
        // readable card may reject the raw-sector probe, which previously
        // caused an endless mount/unmount loop. Check the *same filesystem
        // read path* the map renderer uses, and require consecutive failures
        // before treating a probe error as physical removal.
        bool readable=false;
        if(archives_discovered&&archive_count) {
            MeshInkStorageFile witness=meshink_storage_open(archive_paths[0]);
            uint8_t magic[8]{};
            readable=witness&&witness.read(magic,sizeof(magic))==sizeof(magic)
                &&memcmp(magic,"PMTiles",7)==0&&magic[7]==3;
            if(witness)witness.close();
        } else {
            MeshInkStorageFile maps=meshink_storage_open("/maps");
            readable=maps&&maps.isDirectory();
            if(maps)maps.close();
            // No map folder is a valid inserted card state, but the absence
            // of a witness cannot establish removal. Never unmount here.
            if(!readable) {
                MeshInkStorageFile root=meshink_storage_open("/");
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
// Scans /maps and one non-numeric subfolder level for PMTiles archives.
// Prefer the usual .pmtiles suffix, but also recognise a v3 PMTiles header so
// oddly named/truncated files from FAT/SD tooling are still usable.
bool has_pmtiles_magic(const char* path) {
    MeshInkStorageFile probe=meshink_storage_open(path);
    if(!probe||probe.isDirectory()){if(probe)probe.close();return false;}
    uint8_t magic[8]{};
    const bool ok=probe.read(magic,sizeof(magic))==sizeof(magic)&&
        memcmp(magic,"PMTiles",7)==0&&magic[7]==3;
    probe.close();
    return ok;
}
void add_archive(const char* parent,const char* name) {
    if(!name||archive_count>=MAX_ARCHIVES)return;
    const char* basename=strrchr(name,'/');
    basename=basename?basename+1:name;
    char absolute[SOURCE_PATH_BYTES];
    const int written=snprintf(absolute,sizeof(absolute),"%s/%s",
                               parent,basename);
    if(written<=0||written>=int(sizeof(absolute)))return;
    for(size_t i=0;i<archive_count;++i)
        if(!strcmp(archive_paths[i],absolute))return;
    const size_t len=strlen(basename);
    const bool suffix=len>=8&&!strcasecmp(basename+len-8,".pmtiles");
    const bool header=has_pmtiles_magic(absolute);
    T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] archive-scan file=%s suffix=%u header=%u\n",
                  absolute,(unsigned)suffix,(unsigned)header);
    if(!suffix&&!header)return;
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
    MeshInkStorageFile directory=meshink_storage_open("/maps");
    if(!directory||!directory.isDirectory()) {
        if(directory)directory.close();
        return;
    }
    archives_discovered=true;
    MeshInkStorageFile candidate=directory.openNextFile();
    while(candidate) {
        const char* entry_name=candidate.name();
        const char* basename=entry_name?strrchr(entry_name,'/'):nullptr;
        basename=basename?basename+1:entry_name;
        T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] archive-scan entry=%s dir=%u base=%s\n",
                      entry_name?entry_name:"(null)",
                      (unsigned)candidate.isDirectory(),
                      basename?basename:"(null)");
        if(candidate.isDirectory()&&basename&&is_zoom_folder(basename)) {
            unsigned zoom=0;
            for(const char* p=basename;*p;++p)zoom=zoom*10U+(unsigned)(*p-'0');
            if(zoom<25U)zoom_folder_present[zoom]=true;
        } else if(candidate.isDirectory()&&basename) {
            char folder[SOURCE_PATH_BYTES];
            const int written=snprintf(folder,sizeof(folder),
                                       "/maps/%s",basename);
            if(written>0&&written<int(sizeof(folder))) {
                // Keep folder scanning shallow: loose XYZ tile directories
                // can contain tens of thousands of PNG files.
                MeshInkStorageFile subdir=meshink_storage_open(folder);
                if(subdir&&subdir.isDirectory()) {
                    MeshInkStorageFile nested=subdir.openNextFile();
                    while(nested&&archive_count<MAX_ARCHIVES) {
                        T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] archive-scan nested=%s dir=%u\n",
                                      nested.name()?nested.name():"(null)",
                                      (unsigned)nested.isDirectory());
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
        candidate=directory.openNextFile();
    }
    directory.close();
    // A complete shallow /maps scan is enough to know every loose zoom
    // folder without recursively enumerating x/y tile directories.
    for(unsigned zoom=0;zoom<25U;++zoom)zoom_folder_known[zoom]=true;
    if(archive_count)
        T5_DEBUGF(T5_LOG_MAP,"[T5-MAP] found %u PMTiles archive(s) on SD\n",
                      (unsigned)archive_count);
    else
        T5_DEBUGLN(T5_LOG_MAP,"[T5-MAP] no PMTiles archives found under /maps or /maps/<name>");
}


void* png_open(const char* name,int32_t* size) {
    // A loose PNG was already opened when its path was checked; reuse it
    // rather than performing a second FAT directory lookup. Archive PNGs
    // continue borrowing their reader's per-frame handle.
    png_file=png_range_active ? pmtiles_frame_file(name)
                              : (file ? &file : nullptr);
    if(!png_file) {
        file=meshink_storage_open(name);
        if(!file){
            Serial.printf("[T5-MAP] tile file open failed: %s range=%u\n",
                          name,(unsigned)png_range_active);
            map_io_failed=true;
            return nullptr;
        }
        png_file=&file;
    }
    if(png_range_active) {
        const bool valid_range=png_range_length&&png_range_length<=INT32_MAX;
        bool range_seek_ok=false;
        if(valid_range&&png_file->position()==png_range_start) {
            range_seek_ok=true;
        } else if(valid_range) {
            const uint32_t seek_started=map_perf_now_us();
            range_seek_ok=png_file->seek(png_range_start);
            perf_pmt_range_seek_us+=(uint32_t)(map_perf_now_us()-seek_started);
            ++perf_pmt_seek_calls;
        }
        if(!range_seek_ok){
            if(png_file==&file&&file)file.close();
            png_file=nullptr;
            map_io_failed=true;
            return nullptr;
        }
        *size=(int32_t)png_range_length;
    } else *size=png_file->size();
    return png_file;
}
void png_close(void*) {
    // 'file' owns only loose PNG handles. Always release it, including if
    // PNGdec rejected a preopened loose PNG before invoking its open callback.
    // A PMTiles archive handle is borrowed separately and is never closed here.
    if(file)file.close();
    png_file=nullptr;
}
int32_t png_read(PNGFILE*,uint8_t* data,int32_t length) {
    if(length<=0)return 0;
    if(!png_file){map_io_failed=true;return 0;}
    // PNGdec may request a 2048-byte buffer even at the end of a PNG.
    // A partial final chunk is normal, not an SD failure.
    const uint64_t pos=png_file->position();
    const uint64_t end=png_range_active
        ? (uint64_t)png_range_start+png_range_length
        : (uint64_t)png_file->size();
    if(png_range_active&&pos<png_range_start){
        map_io_failed=true;
        return 0;
    }
    if(pos>=end)return 0;
    const int32_t allowed=(int32_t)min((uint64_t)length,end-pos);
    const uint32_t read_started=png_range_active?map_perf_now_us():0;
    const int32_t n=png_file->read(data,allowed);
    if(png_range_active) {
        perf_pmt_range_read_us+=(uint32_t)(map_perf_now_us()-read_started);
        ++perf_pmt_read_calls;
        if(n>0)perf_pmt_range_bytes+=(uint32_t)n;
    }
    if(n!=allowed){
        Serial.printf("[T5-MAP] tile SD read failed: got=%ld expected=%ld pos=%lu end=%llu\n",
                      (long)n,(long)allowed,(unsigned long)png_file->position(),
                      (unsigned long long)end);
        map_io_failed=true;
    }
    return n;
}
int32_t png_seek(PNGFILE*,int32_t position) {
    if(!png_file){map_io_failed=true;return -1;}
    if(position<0 || (png_range_active &&
       (uint32_t)position>png_range_length))return -1;
    const uint64_t absolute=(uint64_t)(png_range_active?png_range_start:0)+
                            (uint32_t)position;
    bool seek_ok=false;
    if(absolute<=UINT32_MAX&&png_file->position()==(uint32_t)absolute) {
        seek_ok=true;
    } else if(absolute<=UINT32_MAX) {
        const uint32_t seek_started=png_range_active?map_perf_now_us():0;
        seek_ok=png_file->seek((uint32_t)absolute);
        if(png_range_active) {
            perf_pmt_range_seek_us+=(uint32_t)(map_perf_now_us()-seek_started);
            ++perf_pmt_seek_calls;
        }
    }
    if(!seek_ok){
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
    return meshink_map_gray::level_from_rgb565(colour);
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
// PNG callbacks keep source luminance in PSRAM. If allocation fails,
// decode directly to the framebuffer with the SAME monochrome map palette.
int png_draw(PNGDRAW* row) {
    // ESP32-S3 PIE 128-bit stores force their destination address to a
    // 16-byte boundary. PNGdec's s3_rgb565() uses ee.vst.128.ip, so the row
    // buffer must be explicitly aligned or converted pixels can be shifted
    // into the preceding bytes and leave a bright strip at a tile edge.
    alignas(16) static uint16_t pixels[TILE_SIZE];
    // Reject unexpected rows rather than risking an overwrite.
    if(row->y<0||row->y>=TILE_SIZE||row->iWidth!=TILE_SIZE)return 0;

    // Normal cached map tiles only need 4-bit luminance. For RGBA PNGs,
    // collapse RGBA -> RGB565 -> grayscale -> packed-nibbles into one pass.
    // level_from_rgb888() deliberately applies the same 5/6/5 quantization
    // before luminance, so the resulting map shades are bit-for-bit identical.
    if(decode_bits&&row->iPixelType==PNG_PIXEL_TRUECOLOR_ALPHA) {
        static bool direct_gray_logged=false;
        const uint8_t* source=row->pPixels;
        uint8_t* dest=decode_bits+(size_t)row->y*(TILE_SIZE/2);
        for(int sx=0;sx<TILE_SIZE;sx+=2) {
            const uint8_t high=meshink_map_gray::level_from_rgb888(
                source[0],source[1],source[2]);
            source+=4;
            const uint8_t low=meshink_map_gray::level_from_rgb888(
                source[0],source[1],source[2]);
            source+=4;
            *dest++=(uint8_t)((high<<4)|low);
        }
        if(!direct_gray_logged&&row->y==0) {
            T5_DEBUGF(T5_LOG_MAP,"[T5-PNG-GRAY] direct-rgba=1 src-mod16=%u\n",
                          (unsigned)((uintptr_t)row->pPixels&15U));
            direct_gray_logged=true;
        }
        return 1;
    }
#if T5_CACHE64_EXPERIMENT
    // PNGdec's S3 helper uses 128-bit PIE loads/stores. The normal Arduino
    // build happened to give PNGdec an aligned internal row, but the hybrid
    // cache64 ESP-IDF link can place that large decoder object differently.
    // Keep the zero-copy fast path when PNGdec's row is aligned; otherwise
    // stage only this RGBA row into an explicitly aligned scratch buffer.
    if(row->iPixelType==PNG_PIXEL_TRUECOLOR_ALPHA) {
        alignas(16) static uint8_t simd_source[TILE_SIZE*4];
        static bool simd_alignment_logged=false;
        uint8_t* source=row->pPixels;
        const unsigned source_mod=(unsigned)((uintptr_t)source&15U);
        const unsigned dest_mod=(unsigned)((uintptr_t)pixels&15U);
        const bool staged=source_mod!=0U;
        if(staged) {
            memcpy(simd_source,source,TILE_SIZE*4);
            source=simd_source;
        }
        if(!simd_alignment_logged&&row->y==0) {
            T5_DEBUGF(T5_LOG_MAP,"[T5-PNG-SIMD] src-mod16=%u dst-mod16=%u staged=%u\n",
                          source_mod,dest_mod,staged?1U:0U);
            simd_alignment_logged=true;
        }
        s3_rgb565(source,(uint8_t*)pixels,row->iWidth,false);
    } else
#endif
    {
        png.getLineAsRGB565(row,pixels,PNG_RGB565_LITTLE_ENDIAN,0xffffffff);
    }
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
    const int y0=max(render_clip.y,ctx.dy+
        (row->y-ctx.crop_y)*TILE_SIZE/ctx.crop_size);
    const int y1=min(render_clip.y+render_clip.height,ctx.dy+
        (row->y-ctx.crop_y+1)*TILE_SIZE/ctx.crop_size);
    for(int py=y0;py<y1;++py) {
        for(int sx=ctx.crop_x;sx<ctx.crop_x+ctx.crop_size;++sx) {
            const uint8_t level=gray_level(pixels[sx]);
            const int x0=max(render_clip.x,ctx.dx+
                (sx-ctx.crop_x)*TILE_SIZE/ctx.crop_size);
            const int x1=min(render_clip.x+render_clip.width,ctx.dx+
                (sx-ctx.crop_x+1)*TILE_SIZE/ctx.crop_size);
            for(int px=x0;px<x1;++px)
                meshink_display_fill_rect({px,py,1,1},
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
                 MapRenderResult& result,Tile*& output,bool& direct,
                 bool& from_pmtiles) {
    from_pmtiles=false;
    output=find_cached(z,x,y);
    if(output){
        ++result.ram_hits;
        from_pmtiles=output->from_pmtiles;
        return true;
    }
    if(map_io_failed||pmtiles_had_io_error())return false;
    if(previously_absent(z,x,y))return false;
    char path[SOURCE_PATH_BYTES];
    snprintf(path,sizeof(path),"/maps/%d/%d/%d.png",z,x,y);
    PmtilesPngRange range{};
    // One test per zoom when only PMTiles exist; loose PNGs still win.
    if(z>=0&&z<25&&!zoom_folder_known[z]) {
        char folder[24];
        snprintf(folder,sizeof(folder),"/maps/%d",z);
        zoom_folder_present[z]=meshink_storage_exists(folder);
        zoom_folder_known[z]=true;
        ++result.sd_checks;
    }
    bool loose_present=false;
    bool selected_pmtiles=false;
    if(z>=0&&z<25&&zoom_folder_present[z]){
        // Opening the file is itself a complete presence check. If present,
        // PNGdec reuses this handle instead of opening the same path again.
        file=meshink_storage_open(path);
        loose_present=(bool)file;
        if(loose_present&&file.isDirectory()){
            file.close();
            loose_present=false;
        }
        ++result.sd_checks;
    }
    if(!loose_present) {
        discover_archives();
        bool found=false;
        for(size_t i=0;i<archive_count;++i) {
            ++result.sd_checks;
            const uint32_t lookup_started=map_perf_now_us();
            const bool found_in_archive=pmtiles_find_png(archive_paths[i],z,x,y,range);
            perf_pmt_lookup_us+=(uint32_t)(map_perf_now_us()-lookup_started);
            if(found_in_archive) {
                snprintf(path,sizeof(path),"%s",archive_paths[i]);
                found=true;
                selected_pmtiles=true;
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
    bool pmt_preloaded=false;
    if(selected_pmtiles&&range.length) {
        MeshInkStorageFile* archive=pmtiles_frame_file(path);
        if(archive&&ensure_pmt_png_buffer(range.length)) {
            if(!preload_pmt_png(archive,range.offset,range.length))return false;
            pmt_preloaded=true;
        }
    }
    png_range_active=selected_pmtiles&&!pmt_preloaded&&range.length!=0;
    png_range_start=range.offset;
    png_range_length=range.length;
    const int open_status=pmt_preloaded
        ? png.openRAM(pmt_png_buffer,(int)range.length,png_draw)
        : png.open(path,png_open,png_close,png_read,png_seek,png_draw);
    if(open_status!=PNG_SUCCESS) {
        if(!pmt_preloaded)
            png_close(nullptr); // only closes a loose-file handle, not PMTiles
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
    const uint32_t decode_started=map_perf_now_us();
    const int decode_status=png.decode(nullptr,0);
    const uint32_t decode_elapsed=(uint32_t)(map_perf_now_us()-decode_started);
    if(selected_pmtiles)perf_pmt_decode_us+=decode_elapsed;
    else perf_loose_decode_us+=decode_elapsed;
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
    if(selected_pmtiles)++result.pmtiles_decodes;
    else ++result.loose_decodes;
    from_pmtiles=selected_pmtiles;
    if(slot) {
        slot->z=z;slot->x=x;slot->y=y;slot->from_pmtiles=selected_pmtiles;slot->valid=true;
        output=slot;
    } else direct=true;
    return true;
}
void draw_cached(const Tile& tile,const DrawContext& draw) {
    const MeshInkGray4DitherBlit blit={
        tile.bits,
        TILE_SIZE,
        {draw.crop_x,draw.crop_y,draw.crop_size,draw.crop_size},
        {draw.dx,draw.dy,TILE_SIZE,TILE_SIZE},
        render_clip,
        draw.tile_x*TILE_SIZE,
        draw.tile_y*TILE_SIZE,
        map_black_masks()
    };
    meshink_display_blit_gray4_dithered(target,blit);
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
        Tile* tile=nullptr;bool direct=false,from_pmtiles=false;
        if(!load_source(source_zoom,parent_x,parent_y,draw,result,tile,direct,
                        from_pmtiles))
            continue;
        if(tile) {
            const uint32_t compose_started=map_perf_now_us();
            draw_cached(*tile,draw);
            perf_compose_us+=(uint32_t)(map_perf_now_us()-compose_started);
        }
        // A low-memory decode drew the same requested tile directly.
        ++result.tiles;
        if(from_pmtiles)++result.pmtiles_tiles;
        else ++result.loose_tiles;
        if(depth){
            ++result.reused;
            if(from_pmtiles)++result.parent_pmtiles;
            else ++result.parent_loose;
            const int visible_x0=max(render_clip.x,dx);
            const int visible_y0=max(render_clip.y,dy);
            const int visible_x1=min(render_clip.x+render_clip.width,dx+TILE_SIZE);
            const int visible_y1=min(render_clip.y+render_clip.height,dy+TILE_SIZE);
            const int visible_w=max(0,visible_x1-visible_x0);
            const int visible_h=max(0,visible_y1-visible_y0);
            result.parent_visible_pixels+=(uint32_t)visible_w*(uint32_t)visible_h;
            if(visible_w==TILE_SIZE&&visible_h==TILE_SIZE)
                ++result.parent_full_tiles;
            else
                ++result.parent_edge_tiles;
        }else{
            ++result.native;
            if(from_pmtiles)++result.native_pmtiles;
            else ++result.native_loose;
        }
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
void map_tiles_warm_storage(){
    const uint32_t started=millis();
    if(!media_ready(false)){
        Serial.println("[T5-MAP-WARM] SD unavailable; deferred until Maps");
        return;
    }
    discover_archives();
    unsigned loose_zooms=0;
    for(unsigned z=0;z<25U;++z)if(zoom_folder_present[z])++loose_zooms;
    bool archive_warm=false;
    if(archive_count)archive_warm=pmtiles_warm_archive(archive_paths[0]);
    const PmtilesPerfStats warm=pmtiles_perf_stats();
    T5_DEBUGF(T5_LOG_MAP,"[T5-MAP-WARM] elapsed=%lums loose-zooms=%u archives=%u first-pmt=%u archive-open=%luus prepare=%luus meta-read=%luus/%u\n",
                  (unsigned long)(millis()-started),loose_zooms,
                  (unsigned)archive_count,archive_warm?1U:0U,
                  (unsigned long)warm.archive_open_us,
                  (unsigned long)warm.prepare_us,
                  (unsigned long)warm.metadata_read_us,
                  (unsigned)warm.metadata_reads);
}
uint32_t map_tiles_media_epoch(){return sd_media_epoch;}

MapRenderResult map_tiles_render(uint8_t* framebuffer,int x,int y,int width,
                                int height,double lat,double lon,uint8_t zoom) {
    const uint32_t started=millis(); // per-render performance instrumentation
    reset_map_perf();
    const bool ready=media_ready(false);
    MapRenderResult result{};
    result.sd_ready=ready;
    result.min_source_zoom=zoom;
    result.max_source_zoom=zoom;
    if(!ready)return result;
    map_io_failed=false;
    pmtiles_begin_frame();
    target=framebuffer;
    render_clip={x,y,width,height};
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
    const PmtilesPerfStats pmt_perf=pmtiles_perf_stats();
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
    T5_DEBUGF(T5_LOG_MAP,"[T5-MAP-PERF] z=%u total=%lums lookup=%luus archive-open=%luus prepare=%luus meta-seek=%luus meta-read=%luus/%u meta-bytes=%lu inflate=%luus index=%luus leaf-loads=%u preload-seek=%luus preload-read=%luus/%u preload-bytes=%lu range-seek=%luus/%u range-read=%luus/%u range-bytes=%lu pmt-decode=%luus loose-decode=%luus compose=%luus sd-checks=%u\n",
                  (unsigned)zoom,(unsigned long)(millis()-started),
                  (unsigned long)perf_pmt_lookup_us,
                  (unsigned long)pmt_perf.archive_open_us,
                  (unsigned long)pmt_perf.prepare_us,
                  (unsigned long)pmt_perf.metadata_seek_us,
                  (unsigned long)pmt_perf.metadata_read_us,
                  (unsigned)pmt_perf.metadata_reads,
                  (unsigned long)pmt_perf.metadata_bytes,
                  (unsigned long)pmt_perf.inflate_us,
                  (unsigned long)pmt_perf.index_parse_us,
                  (unsigned)pmt_perf.leaf_loads,
                  (unsigned long)perf_pmt_preload_seek_us,
                  (unsigned long)perf_pmt_preload_read_us,
                  (unsigned)perf_pmt_preload_reads,
                  (unsigned long)perf_pmt_preload_bytes,
                  (unsigned long)perf_pmt_range_seek_us,
                  (unsigned)perf_pmt_seek_calls,
                  (unsigned long)perf_pmt_range_read_us,
                  (unsigned)perf_pmt_read_calls,
                  (unsigned long)perf_pmt_range_bytes,
                  (unsigned long)perf_pmt_decode_us,
                  (unsigned long)perf_loose_decode_us,
                  (unsigned long)perf_compose_us,
                  (unsigned)result.sd_checks);
    T5_DEBUGF(T5_LOG_MAP,"[T5-MAP-FAST] zoom=%u render=%lu ms png=%u ram=%u tiles=%u native=%u parent=%u src=%u-%u loose=%u pmtiles=%u native-loose=%u native-pmt=%u parent-loose=%u parent-pmt=%u parent-edge=%u parent-full=%u parent-px=%lu decode-loose=%u decode-pmtiles=%u missing=%u\n",
                  (unsigned)zoom,(unsigned long)(millis()-started),
                  (unsigned)result.disk_decodes,(unsigned)result.ram_hits,
                  (unsigned)result.tiles,(unsigned)result.native,
                  (unsigned)result.reused,(unsigned)result.min_source_zoom,
                  (unsigned)result.max_source_zoom,(unsigned)result.loose_tiles,
                  (unsigned)result.pmtiles_tiles,(unsigned)result.native_loose,
                  (unsigned)result.native_pmtiles,(unsigned)result.parent_loose,
                  (unsigned)result.parent_pmtiles,(unsigned)result.parent_edge_tiles,
                  (unsigned)result.parent_full_tiles,
                  (unsigned long)result.parent_visible_pixels,
                  (unsigned)result.loose_decodes,(unsigned)result.pmtiles_decodes,
                  (unsigned)result.missing);
    return result;
}

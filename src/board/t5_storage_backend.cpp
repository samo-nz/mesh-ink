#include "t5_storage_backend.h"
#include "target.h"
#include "board_profile.h"

#include <Arduino.h>
#include <SD.h>
#include <SPIFFS.h>
#include <esp_partition.h>

namespace {
// Preserve the field-tested map/PMTiles access rate. Keeping this in the
// backend prevents application code from knowing board electrical tuning.
constexpr uint32_t T5_STORAGE_SPI_HZ=25000000;
// The Arduino SD uninit transaction lifetime is fixed at build time; only
// end a successful mount, and never access SD's VFS before the first mount.
// This state is shared by Maps, screenshots and offline Backup/Restore.
bool sd_started=false;
MeshInkFirstFormatNotice first_format_notice=nullptr;
}

void meshink_storage_set_first_format_notice(MeshInkFirstFormatNotice notice){
    first_format_notice=notice;
}

bool meshink_storage_mount_internal_safe() {
    if(SPIFFS.begin(false))return true;
    // A failed mount of a populated partition must never become a silent wipe.
    // Only a fully erased, first-install SPIFFS partition may be formatted.
    const auto* part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                             ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                             "spiffs");
    if(!part)return false;
    uint8_t bytes[256]{};
    for(size_t offset=0;offset<part->size;offset+=sizeof(bytes)){
        const size_t length=min(sizeof(bytes),(size_t)(part->size-offset));
        if(esp_partition_read(part,offset,bytes,length)!=ESP_OK)return false;
        for(size_t i=0;i<length;++i)
            if(bytes[i]!=0xFF){
                Serial.println("[T5-STORAGE] mount failed on populated flash; autoformat refused");
                return false;
            }
    }
    Serial.println("[T5-STORAGE] partition fully erased; initializing SPIFFS once");
    // The safe, erased-only decision is made before displaying first-boot
    // preparation; never show a formatting notice for populated/corrupt flash.
    if(first_format_notice)first_format_notice();
    return SPIFFS.begin(true);
}

bool meshink_storage_begin() {
    // A pre-protocol backup/restore may have mounted SD already; Maps must
    // reuse it, not close a live filesystem and shared SPI transaction.
    if(sd_started){
        File root=SD.open("/",FILE_READ);
        const bool ready=root&&root.isDirectory();
        if(root)root.close();
        if(ready)return true;
        // A failed mounted filesystem must be ended before trying new media.
        // The pinned Arduino SD uninit lifetime bug is patched at build time.
        SD.end();
        sd_started=false;
    }
    pinMode(T5_PIN_SD_CS,OUTPUT);
    digitalWrite(T5_PIN_SD_CS,HIGH);
    // First-use setup may defer radio_init, so configure the shared SPI pins
    // before Arduino SD.begin tries its own default S3 pin mapping.
    SPIClass& shared_spi=t5_shared_spi();
    shared_spi.begin(T5_PIN_SPI_SCLK,T5_PIN_SPI_MISO,T5_PIN_SPI_MOSI);
    sd_started=SD.begin(T5_PIN_SD_CS,shared_spi,T5_STORAGE_SPI_HZ);
    return sd_started;
}

bool meshink_storage_media_ready() {
    // Retry the mount on every explicit request, never on a timer.
    if(!meshink_storage_begin())return false;
    File root=SD.open("/",FILE_READ);
    const bool ready=root&&root.isDirectory();
    if(root)root.close();
    if(!ready)meshink_storage_end();
    return ready;
}

void meshink_storage_end() {
    if(sd_started){SD.end();sd_started=false;}
}

MeshInkStorageFile meshink_storage_open(const char* path) {
    return path&&sd_started ? SD.open(path,FILE_READ) : MeshInkStorageFile();
}

MeshInkStorageFile meshink_storage_open_write(const char* path) {
    return path&&sd_started ? SD.open(path,FILE_WRITE) : MeshInkStorageFile();
}

bool meshink_storage_exists(const char* path) {
    return path&&sd_started&&SD.exists(path);
}

bool meshink_storage_remove(const char* path){return path&&sd_started&&SD.remove(path);}
bool meshink_storage_rename(const char* from,const char* to){return from&&to&&sd_started&&SD.rename(from,to);}

uint32_t meshink_storage_bus_hz() {
    return T5_STORAGE_SPI_HZ;
}

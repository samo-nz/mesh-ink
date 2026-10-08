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
    return SPIFFS.begin(true);
}

bool meshink_storage_begin() {
    pinMode(T5_PIN_SD_CS,OUTPUT);
    digitalWrite(T5_PIN_SD_CS,HIGH);
    SD.end();
    // First-use Meshtastic setup defers LoRa/radio_init until a region is
    // selected. RadioLib therefore has not yet configured this shared SPI
    // bus when Maps warms the SD card. Arduino SD.begin() calls spi.begin()
    // with default ESP32-S3 pins if we don't start it explicitly.
    // SPIClass::begin is a no-op if the radio already started the bus.
    SPIClass& shared_spi=t5_shared_spi();
    shared_spi.begin(T5_PIN_SPI_SCLK,T5_PIN_SPI_MISO,T5_PIN_SPI_MOSI);
    return SD.begin(T5_PIN_SD_CS,shared_spi,T5_STORAGE_SPI_HZ);
}

bool meshink_storage_media_ready() {
    File root=SD.open("/",FILE_READ);
    if(root&&root.isDirectory()){root.close();return true;}
    if(root)root.close();
    // An inserted card may be mounted after startup without opening Maps.
    if(!meshink_storage_begin())return false;
    root=SD.open("/",FILE_READ);
    const bool ready=root&&root.isDirectory();
    if(root)root.close();
    return ready;
}

void meshink_storage_end() {
    SD.end();
}

MeshInkStorageFile meshink_storage_open(const char* path) {
    return path ? SD.open(path,FILE_READ) : MeshInkStorageFile();
}

MeshInkStorageFile meshink_storage_open_write(const char* path) {
    return path ? SD.open(path,FILE_WRITE) : MeshInkStorageFile();
}

bool meshink_storage_exists(const char* path) {
    return path && SD.exists(path);
}

bool meshink_storage_remove(const char* path){return path&&SD.remove(path);}
bool meshink_storage_rename(const char* from,const char* to){return from&&to&&SD.rename(from,to);}

uint32_t meshink_storage_bus_hz() {
    return T5_STORAGE_SPI_HZ;
}

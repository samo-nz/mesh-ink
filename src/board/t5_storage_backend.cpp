#include "t5_storage_backend.h"
#include "target.h"
#include "board_profile.h"

#include <Arduino.h>
#include <SD.h>

namespace {
// Preserve the field-tested map/PMTiles access rate. Keeping this in the
// backend prevents application code from knowing board electrical tuning.
constexpr uint32_t T5_STORAGE_SPI_HZ=25000000;
}

bool meshink_storage_begin() {
    pinMode(T5_PIN_SD_CS,OUTPUT);
    digitalWrite(T5_PIN_SD_CS,HIGH);
    SD.end();
    return SD.begin(T5_PIN_SD_CS,t5_shared_spi(),T5_STORAGE_SPI_HZ);
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

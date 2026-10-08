#pragma once

#include <FS.h>
#include <stdint.h>

using MeshInkStorageFile = fs::File;

// Removable-storage surface used by Maps/PMTiles and explicit user exports.
bool meshink_storage_begin();
// Generic mount/readability check shared by Maps and SD backup/restore.
bool meshink_storage_media_ready();
void meshink_storage_end();
MeshInkStorageFile meshink_storage_open(const char* path);
MeshInkStorageFile meshink_storage_open_write(const char* path);
bool meshink_storage_exists(const char* path);
bool meshink_storage_remove(const char* path);
bool meshink_storage_rename(const char* from,const char* to);
uint32_t meshink_storage_bus_hz();

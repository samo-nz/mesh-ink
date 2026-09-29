#pragma once

#include <FS.h>
#include <stdint.h>

using MeshInkStorageFile = fs::File;

// Read-only removable-storage surface used by Maps/PMTiles.
bool meshink_storage_begin();
void meshink_storage_end();
MeshInkStorageFile meshink_storage_open(const char* path);
bool meshink_storage_exists(const char* path);
uint32_t meshink_storage_bus_hz();

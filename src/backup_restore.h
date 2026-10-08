#pragma once
#include <stdint.h>
#include <stddef.h>

enum MeshInkBackupCategory : uint8_t {
    MESHINK_BACKUP_MESSAGES = 1,
    MESHINK_BACKUP_NODES = 2,
    MESHINK_BACKUP_SETTINGS = 4
};

struct MeshInkBackupInfo {
    char filename[32]{};
    uint8_t protocol=0;
    uint8_t categories=0;
    uint8_t entries=0;
    uint32_t bytes=0;
};

size_t meshink_backup_list(uint8_t protocol,MeshInkBackupInfo* out,size_t capacity);
bool meshink_backup_create(uint8_t protocol,uint8_t categories,
                           char* saved_path,size_t path_len);
bool meshink_backup_restore(uint8_t protocol,const char* filename,uint8_t categories);
uint8_t meshink_backup_categories(uint8_t protocol,const char* filename);
const char* meshink_backup_error();
// A partially applied restore has quiesced the radio and must reboot.
bool meshink_backup_restore_requires_restart();
// Run after SPIFFS mounts but before any protocol reads its files/NVS.
bool meshink_backup_recover_pending();

#pragma once
#include <stdint.h>
#include <stddef.h>

enum MeshInkBackupCategory : uint8_t {
    MESHINK_BACKUP_MESSAGES = 1,
    MESHINK_BACKUP_NODES = 2,
    MESHINK_BACKUP_SETTINGS = 4,
    MESHINK_BACKUP_DEVICE_SETTINGS = 8
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
// The operation request is durable in NVS; no protocol or message writer has
// been initialized when run_pending_on_boot reads/changes internal flash.
bool meshink_backup_schedule(uint8_t protocol,uint8_t categories,
                             bool restore,const char* filename=nullptr);
void meshink_backup_cancel_request();
bool meshink_backup_has_pending_request();
bool meshink_backup_run_pending_on_boot(); // false: unsafe to start any protocol
bool meshink_backup_take_boot_result(char* out,size_t length);

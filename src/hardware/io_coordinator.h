#pragma once
// Flash/display arbitration: MeshInk owns physical persistence timing,
// Meshtastic retains its own RAM-based protocol configuration and file formats.
#include <stdint.h>
void meshink_io_display_begin();
void meshink_io_display_end();
bool meshink_io_flash_try_begin();
bool meshink_io_flash_begin_wait(uint32_t timeout_ms);
void meshink_io_flash_end();
void meshink_io_defer_meshtastic_save(int segments);
int meshink_io_take_deferred_meshtastic_save();
class MeshInkFlashGuard {
public:
    explicit MeshInkFlashGuard(bool acquired) : acquired_(acquired) {}
    ~MeshInkFlashGuard(){if(acquired_) meshink_io_flash_end();}
    MeshInkFlashGuard(const MeshInkFlashGuard&)=delete;
    MeshInkFlashGuard& operator=(const MeshInkFlashGuard&)=delete;
private:
    bool acquired_;
};

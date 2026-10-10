#pragma once

// Called by the selected protocol backend after MeshInk's board/storage startup.
// Upstream Meshtastic exclusively owns the networking engine while selected.
bool meshink_meshtastic_native_begin();
void meshink_meshtastic_native_loop();
// Feed upstream position networking from MeshInk's existing GNSS service.
void meshink_meshtastic_native_gps_update();
void meshink_meshtastic_native_stop();

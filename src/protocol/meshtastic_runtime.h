#pragma once

// Called by the selected protocol backend after MeshInk's board/storage startup.
// Upstream Meshtastic exclusively owns the networking engine while selected.
bool meshink_meshtastic_native_begin();
void meshink_meshtastic_native_loop();
void meshink_meshtastic_native_stop();

#pragma once

// Boot-exclusive Meshtastic Bluetooth phone mode. No MeshInk UI or Meshtastic
// second firmware main() runs after the static notice has been released.
void meshink_meshtastic_request_companion();
bool meshink_meshtastic_consume_companion_request();
void meshink_meshtastic_companion_setup();
void meshink_meshtastic_companion_loop();
void meshink_meshtastic_companion_prepare_exit();
bool meshink_meshtastic_companion_active();

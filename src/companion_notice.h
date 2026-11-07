#pragma once

// Application-owned Bluetooth companion splash. Hardware sequencing remains
// behind display/power/button services; board packages do not own MeshInk UI.
// Optional protocol-supplied pairing detail is already known before this
// display starts; MeshCore continues to use the original no-detail splash.
void meshink_show_companion_notice(const char* pairing_line=nullptr);

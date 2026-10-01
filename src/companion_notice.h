#pragma once

// Application-owned Bluetooth companion splash. Hardware sequencing remains
// behind display/power/button services; board packages do not own MeshInk UI.
void meshink_show_companion_notice();

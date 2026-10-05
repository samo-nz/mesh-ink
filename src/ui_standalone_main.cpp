#include "ui_onboarding.h"

// The UI-only diagnostic target has no MeshCore/T5 board runtime. Keep the
// generic early-radio hook linkable there without pulling in target.cpp.
void meshink_board_start_local_radio_settle() {}
// Modular UI startup can optionally yield to a live MeshCore runtime between
// hardware stages. The UI-only diagnostic target deliberately has none.
void local_mesh_service_startup() {}

void setup() { ui_setup(); ui_finish_startup(); }
void loop() { ui_loop(); }


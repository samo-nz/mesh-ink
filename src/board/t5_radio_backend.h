#pragma once

#include <Mesh.h>
#include "../hardware/radio_types.h"

// Generic runtime-facing radio surface. The selected board backend owns the
// concrete LoRa chip/wrapper, initialization, statistics and power mechanics.
mesh::Radio& meshink_radio_meshcore();
bool meshink_radio_initialize();
uint32_t meshink_radio_rng_seed();
void meshink_radio_apply_params(float freq,float bw,uint8_t sf,uint8_t cr);
void meshink_radio_power_off();
MeshInkRadioStats meshink_radio_stats();
MeshInkRadioFailureClass meshink_radio_classify_failure();
const char* meshink_radio_name();

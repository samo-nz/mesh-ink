#pragma once

#include "../hardware/radio_types.h"

class PhysicalLayer;
namespace mesh { class Radio; }

// Generic runtime-facing radio surface. The selected board backend owns the
// concrete LoRa chip/wrapper, initialization, statistics and power mechanics.
mesh::Radio& meshink_radio_meshcore();
PhysicalLayer* meshink_radio_radiolib();
bool meshink_radio_set_lora_crc(uint8_t bytes);
bool meshink_radio_initialize();
bool meshink_radio_resume_rx_wake();
bool meshink_radio_resume_retained_wake();
uint32_t meshink_radio_rng_seed();
void meshink_radio_apply_params(float freq,float bw,uint8_t sf,uint8_t cr);
void meshink_radio_power_off();
MeshInkRadioStats meshink_radio_stats();
MeshInkRadioFailureClass meshink_radio_classify_failure();
const char* meshink_radio_name();

// Shared board services for the *native* Meshtastic SX1262Interface.
// Unlike meshink_radio_initialize(), these do not initialize MeshCore's
// CustomSX1262Wrapper. Only the reboot-selected protocol controls the chip.
class SPIClass;
MeshInkSX126xModuleConfig meshink_radio_native_module_config();
SPIClass& meshink_radio_native_spi_bus();
void meshink_radio_prepare_native_spi_bus();

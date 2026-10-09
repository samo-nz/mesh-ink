#include "target.h"
#include "t5_radio_backend.h"

mesh::Radio& meshink_radio_meshcore() {
    return radio_driver;
}

PhysicalLayer* meshink_radio_radiolib() {
    return t5_radio_physical_layer();
}

bool meshink_radio_set_lora_crc(uint8_t bytes) {
    return t5_radio_set_lora_crc(bytes);
}

bool meshink_radio_initialize() {
    return radio_init();
}

bool meshink_radio_resume_rx_wake() {
    return radio_resume_rx_wake();
}

bool meshink_radio_resume_retained_wake() {
    return radio_resume_retained_wake();
}

uint32_t meshink_radio_rng_seed() {
    return radio_driver.getRngSeed();
}

void meshink_radio_apply_params(float freq,float bw,uint8_t sf,uint8_t cr) {
    radio_driver.setParams(freq,bw,sf,cr);
}

void meshink_radio_power_off() {
    radio_driver.powerOff();
}

MeshInkRadioStats meshink_radio_stats() {
    MeshInkRadioStats stats{};
    stats.continuous_rx=radio_driver.isInRecvMode();
    stats.packets_received=radio_driver.getPacketsRecv();
    stats.receive_errors=radio_driver.getPacketsRecvErrors();
    stats.packets_sent=radio_driver.getPacketsSent();
    stats.boosted_gain=radio_driver.getRxBoostedGainMode();
    return stats;
}

MeshInkRadioFailureClass meshink_radio_classify_failure() {
    return t5_classify_radio_failure();
}

const char* meshink_radio_name() {
    return "SX1262";
}

// This T5 backend is the only owner of H752-01 pin assignments and
// radio-bus electrical setup. Official Meshtastic code receives these through
// the generic hardware/radio.h surface, never through a board header.
MeshInkSX126xModuleConfig meshink_radio_native_module_config() {
    return {P_LORA_NSS,P_LORA_DIO_1,P_LORA_RESET,P_LORA_BUSY,2.4f,true};
}

SPIClass& meshink_radio_native_spi_bus() {
    return t5_shared_spi();
}

void meshink_radio_prepare_native_spi_bus() {
    // The selected protocol is responsible for later radio init and ISR
    // attachment; this prepares the common board-owned SPI bus only.
    t5_prepare_native_radio_spi();
}

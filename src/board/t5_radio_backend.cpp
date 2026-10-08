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

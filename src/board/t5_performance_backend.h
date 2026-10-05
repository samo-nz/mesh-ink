#pragma once
#include <stdint.h>
#include <esp32-hal-cpu.h>

inline uint32_t meshink_performance_cpu_mhz() {
    return getCpuFrequencyMhz();
}

inline bool meshink_performance_set_cpu_mhz(uint32_t mhz) {
    return setCpuFrequencyMhz(mhz);
}

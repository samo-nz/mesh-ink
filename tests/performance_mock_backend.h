#pragma once
#include <stdint.h>

inline uint32_t meshink_performance_mock_mhz=80;
inline unsigned meshink_performance_mock_sets=0;

inline uint32_t meshink_performance_cpu_mhz() {
    return meshink_performance_mock_mhz;
}

inline bool meshink_performance_set_cpu_mhz(uint32_t mhz) {
    ++meshink_performance_mock_sets;
    meshink_performance_mock_mhz=mhz;
    return true;
}

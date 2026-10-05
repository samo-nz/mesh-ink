#define MESHINK_PERFORMANCE_BACKEND_HEADER "performance_mock_backend.h"
#include "hardware/performance.h"

#include <cassert>
#include <iostream>

int main() {
    assert(meshink_performance_cpu_mhz()==80);
    assert(meshink_performance_set_cpu_mhz(240));
    assert(meshink_performance_cpu_mhz()==240);
    assert(meshink_performance_mock_sets==1);
    assert(meshink_performance_set_cpu_mhz(80));
    assert(meshink_performance_cpu_mhz()==80);
    assert(meshink_performance_mock_sets==2);
    std::cout << "PASS: generic CPU performance contract compiles with a non-T5 backend.\n";
    return 0;
}

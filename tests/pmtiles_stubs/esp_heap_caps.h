#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4

// Host simulation lets tests force an internal-low-memory path without T5.
extern size_t mock_internal_largest_free;
extern size_t mock_internal_total_free;
extern size_t mock_spiram_allocations;
static inline size_t heap_caps_get_largest_free_block(int caps) {
    return (caps & MALLOC_CAP_INTERNAL) ? mock_internal_largest_free : 1U*1024U*1024U;
}
static inline size_t heap_caps_get_free_size(int caps) {
    return (caps & MALLOC_CAP_INTERNAL) ? mock_internal_total_free : 1U*1024U*1024U;
}
static inline void* heap_caps_malloc(size_t n,int caps) {
    if ((caps & MALLOC_CAP_INTERNAL) && n>mock_internal_largest_free) return nullptr;
    if (caps & MALLOC_CAP_SPIRAM) ++mock_spiram_allocations;
    return malloc(n);
}
static inline bool heap_caps_check_integrity_all(bool) { return true; }

#pragma once
// Host stand-in for ESP-IDF heap_caps_* — route PSRAM allocations to stdlib.
#include <cstdlib>
#include <cstdint>

#define MALLOC_CAP_8BIT    (1 << 2)
#define MALLOC_CAP_SPIRAM  (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT 0

static inline void* heap_caps_malloc(size_t size, uint32_t)            { return malloc(size); }
static inline void* heap_caps_calloc(size_t n, size_t size, uint32_t)  { return calloc(n, size); }
static inline void* heap_caps_realloc(void* p, size_t size, uint32_t)  { return realloc(p, size); }
static inline void  heap_caps_free(void* p)                            { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t)                 { return 4u * 1024 * 1024; }
static inline size_t heap_caps_get_total_size(uint32_t)                { return 8u * 1024 * 1024; }
static inline size_t heap_caps_get_largest_free_block(uint32_t)        { return 2u * 1024 * 1024; }

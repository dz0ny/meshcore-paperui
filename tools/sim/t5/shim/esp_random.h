#pragma once
#include <cstdint>
#include <cstdlib>
static inline uint32_t esp_random() { return (uint32_t)rand(); }

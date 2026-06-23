#pragma once
#include <cstdint>
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFF
#define configMAX_PRIORITIES 25
#define pdMS_TO_TICKS(ms) (ms)

#pragma once
#include "FreeRTOS.h"
static inline void vTaskDelay(TickType_t) {}
static inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
static inline void taskYIELD() {}

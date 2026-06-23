#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline int xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }

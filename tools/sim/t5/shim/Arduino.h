#pragma once
// Host stand-in for <Arduino.h> for the T5 LVGL sim. Provides the handful of
// Arduino surface the UI layer touches (millis, Serial, String, delay), nothing
// hardware. Pure-logic stores (waypoint/trail) only need the integer types.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <freertos/FreeRTOS.h>   // board.h declares a SemaphoreHandle_t mutex

using std::min;
using std::max;

#ifndef F
#define F(x) (x)
#endif
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#endif
#ifndef pgm_read_word
#define pgm_read_word(a) (*(const uint16_t*)(a))
#endif

uint32_t millis();          // defined in ../sim_display.cpp (shared with the mono sim)
inline void delay(uint32_t) {}
inline void yield() {}

// Arduino String → std::string is close enough for the bits the UI uses.
using String = std::string;

// Minimal Serial — swallow everything.
struct SimSerial {
    void begin(unsigned long) {}
    template <class T> void print(T) {}
    template <class T> void println(T) {}
    void println() {}
    void printf(const char*, ...) {}
};
static SimSerial Serial;

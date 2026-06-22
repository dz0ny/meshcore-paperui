#pragma once
// Minimal host stand-in for <Adafruit_GFX.h> — just the GFXfont/GFXglyph structs
// that lemon_font.h needs. The sim rasterises glyphs itself, so none of the GFX
// drawing class is required.
#include "Arduino.h"

typedef struct {
    uint16_t bitmapOffset;
    uint8_t  width;
    uint8_t  height;
    uint8_t  xAdvance;
    int8_t   xOffset;
    int8_t   yOffset;
} GFXglyph;

typedef struct {
    uint8_t*  bitmap;
    GFXglyph* glyph;
    uint16_t  first;
    uint16_t  last;
    uint8_t   yAdvance;
} GFXfont;

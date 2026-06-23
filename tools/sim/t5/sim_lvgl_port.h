#pragma once
#include <stdint.h>
// Host LVGL port for the T5 e-paper sim. Replaces ui_port_epaper.cpp (epdiy) with
// a plain software-rendered L8 framebuffer that the harness saves as a PNG or
// blits to a <canvas>.
namespace sim_lvgl {
void     set_size(int w, int h);   // call before ui::port::init()
void     set_millis(uint32_t ms);  // drive the LVGL tick clock
uint32_t get_millis();
const uint8_t* framebuffer();      // L8: 0=black .. 255=white, row stride = stride()
int      width();
int      height();
int      stride();
// Inject a touch event for the next lv_timer_handler (pressed=false releases).
void     touch(int x, int y, bool pressed);
}

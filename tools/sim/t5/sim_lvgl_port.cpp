// Host LVGL port for the T5 e-paper sim — see sim_lvgl_port.h.
//
// The device port (src/ui/ui_port_epaper.cpp) renders LVGL at I1 and packs into
// epdiy's framebuffer with rotation. Here we render at L8 (LV_COLOR_DEPTH 8, the
// e-ink build's native color depth) straight into a full-screen buffer that IS
// the framebuffer — no panel, no rotation, no epdiy. Saves as grayscale.
#include "sim_lvgl_port.h"
#include "ui_port.h"          // the real ui::port interface the screens call
#include "lvgl.h"
#include <cstdlib>
#include <cstring>

namespace sim_lvgl {

static int      g_w = 540, g_h = 960;
static int      g_stride = 540;
static uint8_t* g_fb = nullptr;
static uint32_t g_ms = 1;

static lv_display_t* g_disp = nullptr;
static lv_indev_t*   g_touch = nullptr;
static int  g_tx = 0, g_ty = 0;
static bool g_tpressed = false;

void set_size(int w, int h) { g_w = w; g_h = h; }
void set_millis(uint32_t ms) { g_ms = ms; }
uint32_t get_millis() { return g_ms; }
const uint8_t* framebuffer() { return g_fb; }
int width()  { return g_w; }
int height() { return g_h; }
int stride() { return g_stride; }
void touch(int x, int y, bool pressed) { g_tx = x; g_ty = y; g_tpressed = pressed; }

static uint32_t tick_cb() { return g_ms; }

// DIRECT render mode: the render buffer is the whole screen, so it already holds
// the final image — just acknowledge the flush.
static void flush_cb(lv_display_t* disp, const lv_area_t* /*area*/, uint8_t* /*px*/) {
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point.x = g_tx;
    data->point.y = g_ty;
    data->state = g_tpressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace sim_lvgl

// ---- ui::port implementation expected by the screens -----------------------
namespace ui::port {

void init() {
    using namespace sim_lvgl;
    lv_init();
    lv_tick_set_cb(tick_cb);

    g_stride = (int)lv_draw_buf_width_to_stride(g_w, LV_COLOR_FORMAT_L8);
    g_fb = (uint8_t*)malloc((size_t)g_stride * g_h);
    memset(g_fb, 0xFF, (size_t)g_stride * g_h);   // white paper

    g_disp = lv_display_create(g_w, g_h);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_L8);
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_buffers(g_disp, g_fb, NULL, (uint32_t)g_stride * g_h,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_group_t* group = lv_group_create();
    lv_group_set_default(group);

    g_touch = lv_indev_create();
    lv_indev_set_type(g_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch, touch_read_cb);
    lv_indev_set_display(g_touch, g_disp);
}

void set_refresh_mode(int) {}
int  get_refresh_mode() { return UI_REFRESH_MODE_NORMAL; }
void full_refresh() {}
void full_clean() {}
void touch_enable() {}
void touch_disable() {}
void set_backlight(int) {}
int  get_backlight() { return 0; }
const char* get_backlight_name() { return "Auto"; }
bool is_backlight_auto() { return false; }   // no auto-backlight side effects in the sim
void set_brightness(int) {}
int  get_brightness() { return 2; }
const char* get_brightness_name() { return "High"; }
int  get_brightness_pwm() { return 255; }
void apply_backlight() {}

} // namespace ui::port

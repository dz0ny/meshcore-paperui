// Interactive WASM harness for the LVGL T5 e-paper UI. Same real UI stack as the
// native sim_t5 renderer, but driven from JavaScript: boot, jump to a screen,
// inject touch (canvas taps/drags -> the LVGL pointer indev), and advance time.
// The page copies the L8 framebuffer (sim_pixels/sim_w/sim_h/sim_stride) into a
// <canvas> after each call.
#include "sim_lvgl_port.h"
#include "ui/ui_port.h"
#include "ui/ui_screen_mgr.h"
#include "ui/ui_theme.h"
#include "ui/screen_ids.h"
#include "ui/components/statusbar.h"
#include "model.h"
#include "lvgl.h"
#include <cstdint>
#include <emscripten.h>

namespace model { void sim_seed(); }
extern void sim_set_millis(uint32_t ms);

// ---- screen lifecycles (same curated set as sim_t5.cpp) --------------------
namespace ui { namespace screen {
#define S(ns) namespace ns { extern screen_lifecycle_t lifecycle; }
    S(home) S(contacts) S(chat) S(settings) S(gps) S(battery) S(mesh_settings)
    S(status) S(set_display) S(set_gps) S(set_mesh) S(lock)
    S(contact_detail) S(msg_detail) S(set_ble) S(compose) S(trail) S(quick_reply)
    S(compass) S(team) S(waypoints) S(waypoint_detail)
#undef S
}}

static void register_all() {
    using namespace ui::screen;
    using ui::screen_mgr::register_screen;
    register_screen(SCREEN_HOME, &home::lifecycle);
    register_screen(SCREEN_CONTACTS, &contacts::lifecycle);
    register_screen(SCREEN_CHAT, &chat::lifecycle);
    register_screen(SCREEN_SETTINGS, &settings::lifecycle);
    register_screen(SCREEN_GPS, &gps::lifecycle);
    register_screen(SCREEN_BATTERY, &battery::lifecycle);
    register_screen(SCREEN_MESH, &mesh_settings::lifecycle);
    register_screen(SCREEN_STATUS, &status::lifecycle);
    register_screen(SCREEN_SET_DISPLAY, &set_display::lifecycle);
    register_screen(SCREEN_SET_GPS, &set_gps::lifecycle);
    register_screen(SCREEN_SET_MESH, &set_mesh::lifecycle);
    register_screen(SCREEN_LOCK, &lock::lifecycle);
    register_screen(SCREEN_CONTACT_DETAIL, &contact_detail::lifecycle);
    register_screen(SCREEN_MSG_DETAIL, &msg_detail::lifecycle);
    register_screen(SCREEN_SET_BLE, &set_ble::lifecycle);
    register_screen(SCREEN_COMPOSE, &compose::lifecycle);
    register_screen(SCREEN_TRAIL, &trail::lifecycle);
    register_screen(SCREEN_QUICKREPLY, &quick_reply::lifecycle);
    register_screen(SCREEN_COMPASS, &compass::lifecycle);
    register_screen(SCREEN_TEAM, &team::lifecycle);
    register_screen(SCREEN_WAYPOINTS, &waypoints::lifecycle);
    register_screen(SCREEN_WAYPOINT_DETAIL, &waypoint_detail::lifecycle);
}

static uint32_t g_ms = 1;
static void set_ms(uint32_t ms) { g_ms = ms; sim_set_millis(ms); sim_lvgl::set_millis(ms); }
static void pump() { lv_timer_handler(); }

extern "C" {

EMSCRIPTEN_KEEPALIVE
void sim_boot(int lang) {
    // One-time LVGL/UI init — lv_init(), statusbar, and registration must not run
    // twice. Re-boot (the page's Reset) just re-seeds the model and returns home.
    static bool inited = false;
    set_ms(1);
    model::sim_seed();
    if (!inited) {
        sim_lvgl::set_size(540, 960);
        ui::port::init();
        ui::theme::init();
        ui::statusbar::create();
        ui::screen_mgr::init();
        register_all();
        inited = true;
    }
    lv_anim_delete_all();
    ui::screen_mgr::switch_to(SCREEN_HOME, false);
    for (int i = 0; i < 4; i++) { g_ms += 33; set_ms(g_ms); pump(); }
}

// Jump straight to a screen id (the page's screen picker).
EMSCRIPTEN_KEEPALIVE
void sim_goto(int id) {
    lv_anim_delete_all();
    ui::screen_mgr::switch_to(id, false);
    for (int i = 0; i < 6; i++) { g_ms += 33; set_ms(g_ms); pump(); }
}

// Inject a pointer event at panel coordinates (state: 1=press, 0=release) and
// run a frame so LVGL processes it.
EMSCRIPTEN_KEEPALIVE
void sim_touch(int x, int y, int pressed) {
    g_ms += 16; set_ms(g_ms);
    sim_lvgl::touch(x, y, pressed != 0);
    pump();
}

// Advance time and run a frame (clock/toast timers, momentum settle).
EMSCRIPTEN_KEEPALIVE
void sim_tick(int dt_ms) {
    g_ms += (uint32_t)(dt_ms > 0 ? dt_ms : 0);
    set_ms(g_ms);
    pump();
}

EMSCRIPTEN_KEEPALIVE const uint8_t* sim_pixels() { return sim_lvgl::framebuffer(); }
EMSCRIPTEN_KEEPALIVE int sim_w() { return sim_lvgl::width(); }
EMSCRIPTEN_KEEPALIVE int sim_h() { return sim_lvgl::height(); }
EMSCRIPTEN_KEEPALIVE int sim_stride() { return sim_lvgl::stride(); }

} // extern "C"

int main() { return 0; }

// Native screen renderer for the LVGL T5 e-paper UI. Builds a real screen with
// the real ui_screen_mgr + ui_kit_lvgl + real screen code, renders to the host
// L8 framebuffer (sim_lvgl_port), and saves a PNG — no hardware, no flashing.
//
//   ./sim_t5 <screen> [out.png]     e.g.  ./sim_t5 home home.png
//   ./sim_t5 all                    contact sheet of every screen -> all.png
#include "sim_lvgl_port.h"
#include "ui/ui_port.h"
#include "ui/ui_screen_mgr.h"
#include "ui/ui_theme.h"
#include "ui/screen_ids.h"
#include "ui/components/statusbar.h"
#include "model.h"
#include "mesh/mesh_task.h"
#include "trail_store.h"
#include "lvgl.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace model { void sim_seed(); }

// Arduino millis() is shared with the mono sim (sim_display.cpp). Keep it in sync
// with the LVGL tick clock.
extern void sim_set_millis(uint32_t ms);
extern bool write_gray_png(const char* path, int W, int H, const unsigned char* buf);

// ---- screen lifecycles -----------------------------------------------------
namespace ui { namespace screen {
#define S(ns) namespace ns { extern screen_lifecycle_t lifecycle; }
    S(home) S(contacts) S(chat) S(settings) S(gps) S(battery) S(mesh_settings)
    S(status) S(set_display) S(set_gps) S(set_mesh) S(lock)
    S(contact_detail) S(msg_detail) S(set_ble) S(compose) S(trail) S(quick_reply)
    S(compass) S(team) S(waypoints) S(waypoint_detail) S(discovery)
#undef S
    namespace contact_detail { void set_contact(const char*, int32_t, int32_t, uint8_t, bool, const uint8_t*); }
    namespace msg_detail { void set_message(int); }
    namespace compose { void set_recipient(const char*); }
    namespace compass { void set_target_pos(const char*, int32_t, int32_t); }
    namespace waypoint_detail { void set_index(int); }
}}

using ui::screen_mgr::register_screen;

static void register_all() {
    using namespace ui::screen;
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
    register_screen(SCREEN_DISCOVERY, &discovery::lifecycle);
}

static void seed_trail_timing();   // defined below

// Screens that early-return without a selection: seed one so they draw.
static void pre_seed(int id) {
    using namespace ui::screen;
    static const uint8_t key[32] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };
    switch (id) {
        case SCREEN_CONTACT_DETAIL: contact_detail::set_contact("Ana \xF0\x9F\x9A\x80", 46070000, 14520000, model::CONTACT_TYPE_CHAT, true, key); break;
        case SCREEN_MSG_DETAIL:     msg_detail::set_message(0); break;
        case SCREEN_COMPOSE:        compose::set_recipient("Ana \xF0\x9F\x9A\x80"); break;
        case SCREEN_COMPASS:        compass::set_target_pos("Hut", 46070000, 14520000); break;
        case SCREEN_WAYPOINT_DETAIL: waypoint_detail::set_index(0); break;
        case SCREEN_TRAIL:          seed_trail_timing(); break;
        default: break;
    }
}

struct Entry { const char* name; int id; };
static const Entry SCREENS[] = {
    {"home", SCREEN_HOME}, {"contacts", SCREEN_CONTACTS}, {"chat", SCREEN_CHAT},
    {"compose", SCREEN_COMPOSE}, {"contact_detail", SCREEN_CONTACT_DETAIL},
    {"msg_detail", SCREEN_MSG_DETAIL}, {"quick_reply", SCREEN_QUICKREPLY},
    {"settings", SCREEN_SETTINGS}, {"set_display", SCREEN_SET_DISPLAY},
    {"set_gps", SCREEN_SET_GPS}, {"set_mesh", SCREEN_SET_MESH}, {"set_ble", SCREEN_SET_BLE},
    {"gps", SCREEN_GPS}, {"battery", SCREEN_BATTERY}, {"mesh", SCREEN_MESH},
    {"status", SCREEN_STATUS}, {"lock", SCREEN_LOCK},
    {"trail", SCREEN_TRAIL}, {"team", SCREEN_TEAM}, {"compass", SCREEN_COMPASS},
    {"waypoints", SCREEN_WAYPOINTS}, {"waypoint_detail", SCREEN_WAYPOINT_DETAIL},
    {"discovery", SCREEN_DISCOVERY},
};
static const int N_SCREENS = (int)(sizeof(SCREENS) / sizeof(SCREENS[0]));

static uint32_t g_ms = 1;
static void set_ms(uint32_t ms) { g_ms = ms; sim_set_millis(ms); sim_lvgl::set_millis(ms); }
static void pump(int frames) {
    for (int i = 0; i < frames; i++) { g_ms += 33; set_ms(g_ms); lv_timer_handler(); }
}

// elapsedSeconds() on the trail is millis()-based, so the few ms of render time
// would read as an absurd speed. Re-anchor the session ~23 min in the past for a
// realistic distance/time/pace.
static void seed_trail_timing() {
    TrailStore& t = model::trail;
    t.setActive(false); t.clear();
    set_ms(1);
    t.setActive(true);                      // session_start_ms = 1
    int32_t la = 46050000, lo = 14500000;
    for (int i = 0; i < 24; i++) { t.addPoint(la, lo, model::epoch_now + i * 70, 4); la += 520; lo += 300; }
    set_ms(1 + 23u * 60u * 1000u);          // 23 min elapsed
}

static void show_screen(int id) {
    // Kill any scrolling-label animations left by the previous screen before it's
    // torn down, else their timers fire on freed labels (montage mode only —
    // per-screen renders each run in a fresh process).
    lv_anim_delete_all();
    pre_seed(id);
    ui::screen_mgr::switch_to(id, false);
    pump(6);
    lv_anim_delete_all();
}

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "home";
    const char* out   = argc > 2 ? argv[2] : "t5.png";

    sim_lvgl::set_size(540, 960);
    g_ms = 1; sim_set_millis(g_ms); sim_lvgl::set_millis(g_ms);
    model::sim_seed();

    ui::port::init();
    ui::theme::init();
    ui::statusbar::create();
    ui::screen_mgr::init();
    register_all();

    const int W = sim_lvgl::width(), H = sim_lvgl::height(), st = sim_lvgl::stride();

    if (strcmp(which, "all") == 0) {
        const int cols = 5, lbl = 0, gap = 6;
        const int rows = (N_SCREENS + cols - 1) / cols;
        const int cw = W + gap, ch = H + gap;
        const int GW = cols * cw + gap, GH = rows * ch + gap;
        std::vector<uint8_t> img(GW * GH, 180);
        for (int i = 0; i < N_SCREENS; i++) {
            show_screen(SCREENS[i].id);
            const uint8_t* fb = sim_lvgl::framebuffer();
            int cx = gap + (i % cols) * cw, cy = gap + (i / cols) * ch;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    img[(cy + y) * GW + (cx + x)] = fb[y * st + x];
        }
        if (!write_gray_png("all.png", GW, GH, img.data())) return 1;
        printf("wrote all.png (%dx%d, %d screens)\n", GW, GH, N_SCREENS);
        return 0;
    }

    int id = -1;
    for (const auto& e : SCREENS) if (strcmp(e.name, which) == 0) id = e.id;
    if (id < 0) {
        fprintf(stderr, "unknown screen '%s'. available:", which);
        for (const auto& e : SCREENS) fprintf(stderr, " %s", e.name);
        fprintf(stderr, "\n");
        return 2;
    }

    show_screen(id);

    // The framebuffer stride may exceed width (alignment) — pack to W before save.
    const uint8_t* fb = sim_lvgl::framebuffer();
    std::vector<uint8_t> packed((size_t)W * H);
    for (int y = 0; y < H; y++) memcpy(&packed[(size_t)y * W], &fb[(size_t)y * st], W);
    if (!write_gray_png(out, W, H, packed.data())) { fprintf(stderr, "png write failed\n"); return 1; }
    printf("wrote %s (%dx%d) for screen '%s'\n", out, W, H, which);
    return 0;
}

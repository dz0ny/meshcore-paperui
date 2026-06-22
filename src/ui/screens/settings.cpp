#include "settings.h"
#include "../screen_ids.h"      // SCREEN_* ids (lvgl-free, shared with mono)
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#ifdef BOARD_WIO_L1
#include "../../mesh/mesh_task.h"   // inline buzzer / GPS-in-advert toggles
#endif

// Settings menu — ported to the ui::kit facade (no direct lv_* use).

namespace ui::screen::settings {

using namespace ui::kit;

static void on_gps(void*)     { ui::screen_mgr::push(SCREEN_SET_GPS, true); }
static void on_mesh(void*)    { ui::screen_mgr::push(SCREEN_SET_MESH, true); }
static void on_display(void*) { ui::screen_mgr::push(SCREEN_SET_DISPLAY, true); }
static void on_ble(void*)     { ui::screen_mgr::push(SCREEN_SET_BLE, true); }
#ifdef BOARD_WIO_L1
static void on_battery(void*)   { ui::screen_mgr::push(SCREEN_BATTERY, true); }
static void on_provision(void*) { ui::screen_mgr::push(SCREEN_PROVISION, true); }

// Single-toggle settings that used to be their own screens — flipped in place.
static Handle lbl_buzzer = nullptr;
static Handle lbl_advert = nullptr;
static void on_buzzer(void*) {
    bool en = !mesh::task::get_buzzer_enabled();
    mesh::task::set_buzzer_enabled(en);
    if (lbl_buzzer) set_text(lbl_buzzer, i18n::t(en ? i18n::T_ON : i18n::T_OFF));
}
static void on_advert(void*) {
    bool en = !mesh::task::get_advert_location();
    mesh::task::set_advert_location(en);
    if (lbl_advert) set_text(lbl_advert, i18n::t(en ? i18n::T_ON : i18n::T_OFF));
}
#endif
#ifndef BOARD_WIO_L1
static void on_storage(void*) { ui::screen_mgr::push(SCREEN_SET_STORAGE, true); }
static void on_debug(void*)   { ui::screen_mgr::push(SCREEN_SETTINGS_DEBUG, true); }
static void on_device(void*)  { ui::screen_mgr::push(SCREEN_SETTINGS_DEVICE, true); }
#endif

static void create(Handle parent) {
    Handle menu = list(parent);
#ifdef BOARD_WIO_L1
    // Wio mono build: only the radio-relevant, fully-portable settings screens.
    // Each radio submenu links to its own status page; Battery has no settings
    // page so its status sits directly here.
    menu_row(menu, i18n::t(i18n::T_DISPLAY),       on_display, nullptr);
    lbl_buzzer = toggle_item(menu, i18n::t(i18n::T_BUZZER),
                             i18n::t(mesh::task::get_buzzer_enabled() ? i18n::T_ON : i18n::T_OFF),
                             on_buzzer, nullptr);
    menu_row(menu, i18n::t(i18n::T_BLUETOOTH),     on_ble,     nullptr);
    menu_row(menu, i18n::t(i18n::T_GPS_SETTINGS),  on_gps,     nullptr);
    menu_row(menu, i18n::t(i18n::T_MESH_SETTINGS), on_mesh,    nullptr);
    menu_row(menu, i18n::t(i18n::T_BATTERY),       on_battery, nullptr);
    lbl_advert = toggle_item(menu, i18n::t(i18n::T_ADVERT_GPS),
                             i18n::t(mesh::task::get_advert_location() ? i18n::T_ON : i18n::T_OFF),
                             on_advert, nullptr);
    menu_row(menu, i18n::t(i18n::T_PROVISION),     on_provision, nullptr);
#else
    menu_row(menu, "Display",       on_display, nullptr);
    menu_row(menu, "Bluetooth",     on_ble,     nullptr);
    menu_row(menu, "GPS Settings",  on_gps,     nullptr);
    menu_row(menu, "Mesh Settings", on_mesh,    nullptr);
    menu_row(menu, "Storage",       on_storage, nullptr);
    menu_row(menu, "Debug",         on_debug,   nullptr);
    menu_row(menu, "Device",        on_device,  nullptr);
#endif
}

static void entry() {}
static void exit_fn() {}
static void destroy() {
#ifdef BOARD_WIO_L1
    lbl_buzzer = nullptr; lbl_advert = nullptr;
#endif
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::settings

# t-paper

MeshCore mesh radio firmware with LVGL e-paper UI for LilyGo T5S3 4.7" e-paper PRO (ESP32-S3).

## Build & Flash

```bash
uvx platformio run              # build
uvx platformio run -t upload    # flash (hold BOOT+RESET if device won't connect)
```

## Architecture

- **Core 0**: MeshCore companion radio (mesh networking, BLE companion app)
- **Core 1**: LVGL v9 UI (e-paper display, touch input)
- **Bridge**: FreeRTOS queues between cores (mesh_bridge.h)
- **Model**: Reactive data model (model.h) — single source of truth for all UI screens

## Key Directories

- `src/` — application code
  - `board.h/cpp` — hardware init (screen, touch, PMU, GPS, RTC, SD)
  - `model.h/cpp` — reactive data model updated every 2s
  - `main.cpp` — entry point: board::init() → mesh::task::start(0) → ui::task::start(1)
  - `mesh/` — MeshCore task, bridge queues, companion radio integration
  - `mesh/companion/` — MeshCore companion radio files (MyMesh, DataStore, target)
  - `ui/` — LVGL screens, components, port layer
  - `ui/components/` — reusable UI: statusbar, nav_button, msg_list
  - `ui/screens/` — screen implementations (home, contacts, chat, discovery, etc.)
- `lib/` — vendored libraries (MeshCore, epdiy, lvgl config, SensorLib, BQ27220, etc.)
- `boards/` — PlatformIO board definition

## UI Design Rules

- **Minimum font size 24pt** — anything smaller is unreadable on e-ink
- **Never use dim/gray colors** — only EPD_COLOR_TEXT (0x000000 black) and EPD_COLOR_BG (0xFFFFFF white)
- **Namespace-based components** — no OOP classes for UI, use C++ namespaces
- **All screens get a statusbar** — added automatically by screen manager on the top layer
- **No animations** — e-paper can't handle them, `LV_ANIM_DEF_TIME 0`
- **No scroll momentum or elastic bounce** — disabled globally via `lv_indev_set_scroll_throw(100)` and per-list via `LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM`

### Fonts

- `lv_font_noto_24` — statusbar, secondary info, sender names (Latin Extended + symbols)
- `lv_font_noto_28` — message text, body text, values (Latin Extended + symbols)
- `lv_font_montserrat_bold_30` — menus, titles, settings labels, back button
- `lv_font_montserrat_bold_80` — lock screen clock
- `lv_font_montserrat_bold_120` — home screen clock

### Screen Structure (every screen follows this pattern)

```cpp
namespace ui::screen::my_screen {

static lv_obj_t* scr = NULL;

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void create(lv_obj_t* parent) {
    scr = parent;
    // 1. Back button — ALWAYS first, consistent across all screens
    ui::nav::back_button(parent, "Screen Title", on_back);

    // 2. Content area — use scroll_list for lists, or manual layout
    lv_obj_t* list = ui::nav::scroll_list(parent);
    // ... add items to list ...
}

static void entry() { /* called on screen activation / re-activation */ }
static void exit_fn() { /* cleanup timers etc */ }
static void destroy() { scr = NULL; /* null all widget pointers */ }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };
} // namespace
```

### Layout Constants

All layout values live in `src/ui/layout/epaper.h` as `UI_*` defines. Always use these defines — never hardcode pixel values.

| Element | Define prefix | e-paper |
|---------|--------------|---------|
| Splash title | `UI_SPLASH_TITLE_Y` | -60 |
| Splash subtitle | `UI_SPLASH_SUB_Y` | 60 |
| Splash version | `UI_SPLASH_VER_Y` | 100 |
| Splash status | `UI_SPLASH_STATUS_Y` | 160 |
| Statusbar | `UI_STATUSBAR_*` | y=5, h=50 |
| Back button | `UI_BACK_BTN_*` | y=50, h=70 |
| scroll_list | `UI_SCROLL_LIST_*` | 85% h, y=-10 |
| menu_item | `UI_MENU_ITEM_*` | 85px h, 15px pad |
| text_button | `UI_TEXT_BTN_*` | 80px h, 12px radius |
| ext_click_area | `UI_EXT_CLICK_*` | 35/25/25 |

### UI Components (ui::nav namespace in nav_button.h)

- `back_button(parent, title, cb)` — top-left back arrow + title. Use on ALL sub-screens.
- `menu_item(parent, icon, label, cb, data)` — navigation row with right arrow (→). For screens that push to another screen.
- `toggle_item(parent, label, value, cb, data)` — settings row with label left, value right. Returns the value label for updating. For tap-to-cycle settings.
- `text_button(parent, text, cb, data)` — large action button (black bg, white text, rounded). For destructive/important actions (Delete, Add Contact, Factory Reset).
- `scroll_list(parent)` — scrollable flex-column container with elastic/momentum disabled. Use for ALL list screens.

### Toast Notifications (ui::toast namespace)

```cpp
ui::toast::show("Contact added");           // default 2s
ui::toast::show("Error message", 3000);     // custom duration
```
Shows overlay below statusbar on top layer. Auto-dismisses. Use for action feedback.

### Empty States

When a list has no items, show centered placeholder text:
```cpp
if (count == 0) {
    lv_obj_t* empty = lv_label_create(list);
    lv_obj_set_width(empty, lv_pct(100));
    lv_obj_set_flex_grow(empty, 1);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_bold_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(empty, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(empty, "\n\n\nNo items yet");
    return;
}
```

### Screen Registration

1. Add `SCREEN_MY_SCREEN = N` to `ui_theme.h` enum
2. Create `screens/my_screen.h` + `screens/my_screen.cpp`
3. Include in `ui_task.cpp` and register: `ui::screen_mgr::register_screen(N, &ui::screen::my_screen::lifecycle)`

## Display

- LVGL v9.5 with LV_COLOR_FORMAT_L8 (8-bit grayscale, no RGB conversion needed)
- RENDER_MODE_DIRECT — LVGL only redraws dirty widgets
- Flush packs L8 → 4-bit nibbles with inlined rotation for epdiy
- Partial area updates via `epd_hl_update_area()` — only changed regions refresh
- Skip-unchanged optimization — compares frame against previous, skips if identical
- E-paper refresh modes: Normal (GL16), Fast (DU), Neat (GC16 with white clear)
- No full-screen invalidation — LVGL tracks dirty areas automatically

## MeshCore Integration

- Companion radio protocol via BLE (SerialBLEInterface)
- DataStore persists identity/prefs to SPIFFS, contacts/channels to SD (falls back to SPIFFS if no SD)
- Identity persists across reboots (SPIFFS /identity/_main.id)
- Discovery screen shows recently heard nodes (getRecentlyHeard)
- Auto-add disabled — contacts added explicitly from Discovery
- Radio params (freq, BW, SF, CR, TX power) configurable from Settings > Mesh

## Sleep

- Configurable timeout: Off, 1, 2, 5, 15, 30 min
- Lock screen shows clock, unread messages, battery
- ESP32-S3 light sleep with wake on LoRa DIO1 (GPIO 10) + touch INT (GPIO 3)
- Mesh mutex grabbed before sleep to ensure radio is idle

## Hardware

- Board: LilyGo T5S3 4.7" e-paper PRO
- MCU: ESP32-S3 with PSRAM
- Display: 540x960 4.7" e-paper via epdiy
- Touch: GT911 capacitive (I2C)
- Radio: SX1262 LoRa (shared SPI with SD card)
- GPS: L76K or u-blox M10Q (auto-detected)
- PMU: BQ25896 (charger) + BQ27220 (fuel gauge)
- RTC: PCF8563
- Storage: SPIFFS (identity/prefs), SD card (contacts/channels/messages)
- Advert blobs: in-memory ring buffer (32 slots), not persisted to flash

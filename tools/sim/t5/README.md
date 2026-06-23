# LVGL T5 ePaper UI simulator (native + WASM)

Runs the **real** LVGL e-paper UI on your host — the actual `ui_kit_lvgl.cpp`,
`ui_screen_mgr.cpp`, theme, components and screen `.cpp` files — with only the
hardware/data layers swapped:

- **epdiy panel → software L8 framebuffer** (`sim_lvgl_port.cpp` implements the
  real `ui::port::` interface; LVGL renders at `LV_COLOR_FORMAT_L8`, saved as
  grayscale or blitted to a `<canvas>`).
- **`<Arduino.h>` / `<esp_*>` / `<SD.h>` / drivers → host shims** (`shim/`), all
  resolved via `-I` order. `board.h`'s e-paper driver block is guarded behind
  `MESHUI_SIM` (the same pattern the mono sim uses for `ui_kit_mono.cpp`).
- **model / mesh / board / nvs → fixtures** (`sim_t5_stubs.cpp`, with
  `model::sim_seed()` providing sample contacts, messages, waypoints, a trail…).
- **PSRAM `lv_malloc_core` → stdlib** (`sim_lv_mem.c`).

## Native — render screens to PNG

```sh
tools/sim/t5/build_t5.sh
tools/sim/t5/build/sim_t5 home home.png    # one screen
tools/sim/t5/build/sim_t5 all              # contact sheet of every screen
```

First build compiles LVGL (~463 files) into a cached `build/liblvgl_sim.a`;
later builds reuse it. Screens: `home contacts chat compose contact_detail
msg_detail quick_reply settings set_display set_gps set_mesh set_ble gps battery
mesh status lock trail team compass waypoints waypoint_detail`.

`tools/gen_t5_shots.sh` renders the showcase set into `assets/t5-*.png` for the
web-flasher gallery.

## Interactive — WASM build for the browser

```sh
tools/sim/t5/build_t5_web.sh                       # -> web/sim_t5.{js,wasm}
python3 -m http.server -d tools/sim/t5/web 8000    # serve it
open http://localhost:8000
```

Needs Emscripten (`brew install emscripten`). Tap and drag the canvas like the
real touchscreen — canvas pointer events map to panel coordinates and feed the
LVGL pointer indev (`sim_touch`); a screen picker, language toggle and reset sit
above it. The page reads the L8 framebuffer (`sim_pixels`/`sim_w`/`sim_h`/
`sim_stride`) into a `<canvas>` after each call. Exports live in `sim_t5_web.cpp`.
This is the same build embedded as the **Live preview** in the T5 web-flasher panel.

## Excluded screens

A few screens are left out of the sim build because they're deeply
hardware/mesh-coupled and low value as a UI preview: `set_storage` (SD/SPIFFS/
epdiy), `sensors`/`ping` (live telemetry + MeshCore advert parsing), `map` (SD
tiles), `touch_debug`, `settings_debug`, `settings_device`. `discovery` is
excluded pending a fix to a latent null-label deref in its row builder (it walks
the `toggle_item` widget tree one level too deep — harmless on ESP32, segfaults
on host).

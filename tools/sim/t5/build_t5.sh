#!/usr/bin/env bash
# Build the native LVGL T5 screen renderer. Output: tools/sim/t5/sim_t5
# Usage: tools/sim/t5/build_t5.sh && tools/sim/t5/sim_t5 home home.png
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../../..
LVGL="$ROOT/.pio/libdeps/t5-epaper/lvgl"
OUT=build
mkdir -p "$OUT/fonts"

CC=${CC:-cc} CXX=${CXX:-c++} AR=${AR:-ar}
"./build_lvgl_lib.sh" "$OUT"

# shim/ first so its <Arduino.h>/<esp_*>/<SD.h>… win over the real toolchain ones;
# ../ for the shared InternalFileSystem.h + sim_display.cpp.
INC="-Ishim -I.. -I$ROOT/src -I$ROOT/src/ui -I$ROOT/lib -I$LVGL"
# Mirror the project build flags the screens reference (platformio.ini env_common).
DEFS="-DLV_CONF_INCLUDE_SIMPLE -DBOARD_EPAPER=1 -DMESHUI_SIM=1 \
  -DMAX_CONTACTS=50 -DMAX_GROUP_CHANNELS=40 \
  -DENABLE_PRIVATE_KEY_IMPORT=1 -DENABLE_PRIVATE_KEY_EXPORT=1 \
  -DBLE_PIN_CODE=123456 -DENV_INCLUDE_GPS=1"
CFLAGS="$DEFS $INC"

# C sources (fonts + lv_mem) must compile as C — file-scope const has internal
# linkage in C++ and the font tables wouldn't export.
$CC -O1 $CFLAGS -c sim_lv_mem.c -o "$OUT/sim_lv_mem.o"
FONT_OBJS=()
for f in noto_14 noto_15 noto_16 noto_24 noto_28 montserrat_bold_30 montserrat_bold_80 montserrat_bold_120; do
    o="$OUT/fonts/$f.o"; FONT_OBJS+=("$o")
    [ -f "$o" ] && [ "$o" -nt "$ROOT/src/fonts/$f.c" ] && continue
    $CC -O1 $CFLAGS -c "$ROOT/src/fonts/$f.c" -o "$o"
done

SCREENS=(
    home contacts chat settings gps battery mesh_settings status
    set_display set_gps set_mesh lock contact_detail msg_detail
    set_ble compose trail quick_reply compass team
    waypoints waypoint_detail discovery
)
SCREEN_SRC=()
for s in "${SCREENS[@]}"; do SCREEN_SRC+=("$ROOT/src/ui/screens/$s.cpp"); done

$CXX -std=c++17 -O1 $CFLAGS \
    sim_t5.cpp sim_lvgl_port.cpp sim_t5_stubs.cpp \
    ../sim_display.cpp \
    "$ROOT/src/ui/kit/ui_kit_lvgl.cpp" \
    "$ROOT/src/ui/ui_screen_mgr.cpp" \
    "$ROOT/src/ui/ui_theme.cpp" \
    "$ROOT/src/ui/components/statusbar.cpp" \
    "$ROOT/src/ui/components/nav_button.cpp" \
    "$ROOT/src/ui/components/msg_list.cpp" \
    "$ROOT/src/ui/components/toast.cpp" \
    "${SCREEN_SRC[@]}" \
    "$OUT/sim_lv_mem.o" "${FONT_OBJS[@]}" \
    "$OUT/liblvgl_sim.a" \
    -o "$OUT/sim_t5"
echo "built $OUT/sim_t5"

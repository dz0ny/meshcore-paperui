#!/usr/bin/env bash
# Build the interactive WASM LVGL T5 simulator. Output: web/sim_t5.{js,wasm}
# Then:  python3 -m http.server -d web 8000  &&  open http://localhost:8000/t5.html
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../../..
LVGL="$ROOT/.pio/libdeps/t5-epaper/lvgl"
OUT=build-wasm
mkdir -p web "$OUT/fonts"

if ! command -v em++ >/dev/null 2>&1; then
    echo "error: em++ (Emscripten) not found on PATH (brew install emscripten)." >&2
    exit 1
fi
# Emscripten needs Python >=3.10; prepend a newer one if the default is Xcode's 3.9.
if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' 2>/dev/null; then
    for p in python3.14 python3.13 python3.12 python3.11 python3.10; do
        command -v "$p" >/dev/null 2>&1 && { PATH="$(dirname "$(command -v "$p")"):$PATH"; break; }
    done
fi

CC=emcc AR=emar ./build_lvgl_lib.sh "$OUT"

INC="-Ishim -I.. -I$ROOT/src -I$ROOT/src/ui -I$ROOT/lib -I$LVGL"
DEFS="-DLV_CONF_INCLUDE_SIMPLE -DBOARD_EPAPER=1 -DMESHUI_SIM=1 \
  -DMAX_CONTACTS=50 -DMAX_GROUP_CHANNELS=40 \
  -DENABLE_PRIVATE_KEY_IMPORT=1 -DENABLE_PRIVATE_KEY_EXPORT=1 \
  -DBLE_PIN_CODE=123456 -DENV_INCLUDE_GPS=1"
CFLAGS="$DEFS $INC"

emcc -O2 $CFLAGS -c sim_lv_mem.c -o "$OUT/sim_lv_mem.o"
FONT_OBJS=()
for f in noto_14 noto_15 noto_16 noto_24 noto_28 montserrat_bold_30 montserrat_bold_80 montserrat_bold_120; do
    o="$OUT/fonts/$f.o"; FONT_OBJS+=("$o")
    [ -f "$o" ] && [ "$o" -nt "$ROOT/src/fonts/$f.c" ] && continue
    emcc -O2 $CFLAGS -c "$ROOT/src/fonts/$f.c" -o "$o"
done

SCREENS=(
    home contacts chat settings gps battery mesh_settings status
    set_display set_gps set_mesh lock contact_detail msg_detail
    set_ble compose trail quick_reply compass team waypoints waypoint_detail
)
SCREEN_SRC=()
for s in "${SCREENS[@]}"; do SCREEN_SRC+=("$ROOT/src/ui/screens/$s.cpp"); done

em++ -std=c++17 -O2 $CFLAGS \
    sim_t5_web.cpp sim_lvgl_port.cpp sim_t5_stubs.cpp \
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
    -sMODULARIZE=1 -sEXPORT_NAME=SimT5Module \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
    -sEXPORTED_FUNCTIONS=_main,_sim_boot,_sim_goto,_sim_touch,_sim_tick,_sim_pixels,_sim_w,_sim_h,_sim_stride \
    -o web/sim_t5.js

echo "built web/sim_t5.js (+ sim_t5.wasm)"

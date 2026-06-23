#!/usr/bin/env bash
# De-risk build: LVGL + project fonts + sim port -> render a label to PNG.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../../..
LVGL="$ROOT/.pio/libdeps/t5-epaper/lvgl"
OUT=build
mkdir -p "$OUT"

CC=cc CXX=c++ AR=ar ./build_lvgl_lib.sh "$OUT"

FLAGS="-DLV_CONF_INCLUDE_SIMPLE -DBOARD_EPAPER=1 -I$ROOT/lib -I$LVGL -I$ROOT/src -I$ROOT/src/ui -I."

# C sources (fonts + lv_mem) MUST compile as C: in C++ a file-scope `const`
# (the font tables) has internal linkage and wouldn't export.
cc -O1 $FLAGS -c sim_lv_mem.c -o "$OUT/sim_lv_mem.o"
cc -O1 $FLAGS -c "$ROOT/src/fonts/noto_28.c" -o "$OUT/noto_28.o"

c++ -std=c++17 -O1 $FLAGS \
    derisk_main.cpp sim_lvgl_port.cpp \
    ../sim_display.cpp \
    "$OUT/sim_lv_mem.o" "$OUT/noto_28.o" \
    "$OUT/liblvgl_sim.a" \
    -o "$OUT/derisk"
echo "built $OUT/derisk"

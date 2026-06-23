#!/usr/bin/env bash
# Compile LVGL v9 (project lv_conf.h) into a cached static lib for the T5 sim.
# Slow (~463 files) but only re-runs when liblvgl_sim.a is missing. Set CC/CXX
# (e.g. emcc/em++) + AR to target WASM. Pass the build dir as $1 (for .o files).
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../../..
LVGL="$ROOT/.pio/libdeps/t5-epaper/lvgl"

CC=${CC:-cc}
AR=${AR:-ar}
OUTDIR=${1:-build}
LIB="$OUTDIR/liblvgl_sim.a"
mkdir -p "$OUTDIR/obj"

if [ -f "$LIB" ]; then echo "cached $LIB"; exit 0; fi

CFLAGS="-Os -DLV_CONF_INCLUDE_SIMPLE -DBOARD_EPAPER=1 -I$ROOT/lib -I$LVGL"
objs=()
i=0
while IFS= read -r f; do
    o="$OUTDIR/obj/lv_$i.o"; i=$((i+1)); objs+=("$o")
    [ -f "$o" ] && [ "$o" -nt "$f" ] && continue
    $CC $CFLAGS -c "$f" -o "$o"
done < <(find "$LVGL/src" -name '*.c')

echo "archiving $LIB ($i objects)"
"$AR" rcs "$LIB" "${objs[@]}"
echo "built $LIB"

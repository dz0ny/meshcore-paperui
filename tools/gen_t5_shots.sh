#!/usr/bin/env bash
# Regenerate the T5 ePaper screen images used by the web flasher slideshow.
# Renders the real LVGL screens via the native simulator (tools/sim/t5) into
# assets/t5-*.png — 540x960 8-bit grayscale, matching the e-paper panel.
set -euo pipefail
cd "$(dirname "$0")/.."

tools/sim/t5/build_t5.sh

SCREENS=(home chat contacts compose contact_detail gps battery mesh trail compass waypoints settings)
for s in "${SCREENS[@]}"; do
    SIM_LANG=en tools/sim/t5/build/sim_t5 "$s" "assets/t5-$s.png" >/dev/null
    echo "assets/t5-$s.png"
done
echo "done — ${#SCREENS[@]} screens"

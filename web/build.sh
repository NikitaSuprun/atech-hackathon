#!/bin/bash
# Build the browser digital-twin WASM module: the REAL BrainOS + TftDashboard +
# Adafruit fonts, compiled with emscripten, output as a single self-contained
# host/dashboard/dashboard.js (wasm embedded, so it loads from file:// too).
#
# Mirrors sim/Makefile.menu's source set + flags, swapping g++ -> emcc.
# Prereq: emscripten on PATH (brew install emscripten) and the vendored Adafruit
# GFX under .pio (run `.venv/bin/pio pkg install -d firmware/brain` if missing).
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

GFXDIR="firmware/brain/.pio/libdeps/esp32-s3-devkitc-1/Adafruit GFX Library"
if [ ! -f "$GFXDIR/Adafruit_GFX.cpp" ]; then
  echo "ERROR: vendored Adafruit GFX missing ($GFXDIR). Run: .venv/bin/pio pkg install -d firmware/brain" >&2
  exit 1
fi

GAMES="eggcatch snake pong racing flappy doodlejump invaders jukebox ambient demo"
INC="-Imodules/console_os -Iinclude -Igames/_sdk -Isim -Isim/host_gfx_shim"
for g in $GAMES; do INC="$INC -Igames/$g"; done

emcc -std=c++14 -O2 -DCONSOLE_OS_HOST -DARDUINO=100 $INC -I"$GFXDIR" \
  web/main_wasm.cpp \
  modules/console_os/brain_os.cpp \
  modules/console_os/builtin_games.cpp \
  modules/console_os/tft_dashboard.cpp \
  include/console/themes.cpp \
  "$GFXDIR/Adafruit_GFX.cpp" \
  -s MODULARIZE=1 -s EXPORT_NAME=TwinModule -s ENVIRONMENT=web -s SINGLE_FILE=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_twin_create","_twin_apply_nav","_twin_render","_twin_tft_buffer","_twin_tft_w","_twin_tft_h"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAPU16"]' \
  -o host/dashboard/dashboard.js

echo "built host/dashboard/dashboard.js ($(du -h host/dashboard/dashboard.js | cut -f1), wasm embedded)"

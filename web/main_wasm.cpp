// WASM entry for the browser digital twin.
//
// Runs the REAL BrainOS + TftDashboard (same construction as sim/menu_dump.cpp),
// driven by the device's streamed ~10-byte nav state via BrainOS::applyTwinNav,
// and exposes the 160x80 RGB565 TFT buffer to JS. No pixel streaming — the twin
// re-runs the exact same C++ dashboard + Adafruit fonts, so the browser TFT is
// byte-for-byte what the panel shows.
#include <emscripten.h>
#include <stdint.h>

#include "brain_os.h"
#include "builtin_games.h"
#include "console/host_proto.h"
#include "console/themes.h"
#include "host_tft.h"
#include "tft_dashboard.h"

using namespace console_os;

namespace {
struct Twin {
    GameRegistry          reg;
    console::ThemeManager themes{THEMES, THEME_COUNT};
    MemorySettingsStore   store;
    BrainOS               os{reg, themes, /*audio*/ nullptr, store};
    HostTft               tft;
    TftDashboard          dash{tft};
};
Twin* g = nullptr;
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void twin_create(uint32_t seed) {
    if (!g) g = new Twin();
    registerBuiltinGames(g->reg);
    g->os.setSeed(seed ? seed : 0x1234u);
    g->os.begin();
    g->dash.begin();
}

// Force the OS to the device's streamed nav snapshot (mirror-only).
EMSCRIPTEN_KEEPALIVE void twin_apply_nav(int mode, int menuSel, int gameIdx, int settingsRow,
                                         int overlay, int themeIndex, int brightness,
                                         int volume, int flags) {
    if (!g) return;
    console::BoardNav nav{};
    nav.mode            = uint8_t(mode);
    nav.menuSel         = uint8_t(menuSel);
    nav.activeGameIndex = int8_t(gameIdx);
    nav.settingsRow     = uint8_t(settingsRow);
    nav.overlay         = uint8_t(overlay);
    nav.themeIndex      = uint8_t(themeIndex);
    nav.brightness      = uint8_t(brightness);
    nav.volume          = uint8_t(volume);
    nav.flags           = uint8_t(flags);
    g->os.applyTwinNav(nav);
}

// Render one dashboard frame at wall-clock nowMs (eased scroll/wash advance here).
EMSCRIPTEN_KEEPALIVE void twin_render(uint32_t nowMs) {
    if (g) g->dash.render(g->os, nowMs);
}

// Pointer to the 160x80 RGB565 canvas (native-endian) inside the WASM heap.
EMSCRIPTEN_KEEPALIVE const uint16_t* twin_tft_buffer() { return g ? g->tft.buffer() : nullptr; }
EMSCRIPTEN_KEEPALIVE int twin_tft_w() { return g ? g->tft.width() : 160; }
EMSCRIPTEN_KEEPALIVE int twin_tft_h() { return g ? g->tft.height() : 80; }

}  // extern "C"

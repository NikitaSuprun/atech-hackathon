// End-to-end integration test — the ONE path no other test covered. It composes
// the REAL Brain OS and the REAL screen board over a single in-memory link and
// proves the screen reproduces exactly what the OS drew. Path exercised:
//
//   BrainOS -> LinkFrameSink (frameEncode) -> LinkLoopback (COBS) ->
//   ScreenRenderBoard (frameDecodeInto) -> ScreenRenderer -> capture tiles
//
// The brain and screen share ONE LinkLoopback: the OS's sink enqueues datagrams,
// the board dequeues them — a faithful host stand-in for the UART wire. Until
// now test_os stopped at a memcpy CaptureSink and test_console started from
// synthetic frames; the seam between them (encode -> wire -> decode) was untested.
// Build/run: make -C modules/console_e2e test
#include <cstdio>
#include <cstring>

#include "brain_os.h"
#include "builtin_games.h"
#include "console/frame_proto.h"  // from565/to565 for the expected-value math
#include "console/themes.h"       // THEMES, THEME_COUNT
#include "link_frame_sink.h"
#include "link_loopback.h"
#include "runner.h"  // host::NullAudio, host::InputSampler
#include "screen_render_board.h"

using namespace console_os;
using console::Color;
using console::SCREEN_PX;
// NB: TICK_MS is defined in both console:: (config.h) and the global pong scope
// (pong_config.h) — qualify to avoid the ambiguity (dedup is a Stage 7 item).

static int g_fails = 0;
static void check(bool ok, const char* what) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fails++;
}

// Host tile: records the last pixels + brightness the renderer pushed.
struct CapTile : console::TileSink {
    uint8_t px[9][3] = {};
    uint8_t lastBright = 0;
    int     shows = 0;
    void setPixel(int chip, uint8_t r, uint8_t g, uint8_t b) override {
        if (chip >= 0 && chip < 9) { px[chip][0] = r; px[chip][1] = g; px[chip][2] = b; }
    }
    void show() override { shows++; }
    void setBrightness(uint8_t b) override { lastBright = b; }
    bool anyLit() const {
        for (int c = 0; c < 9; ++c)
            if (px[c][0] || px[c][1] || px[c][2]) return true;
        return false;
    }
};

// Does the screen's decoded canvas equal the OS framebuffer after RGB565?
static bool screenMatchesBrain(const Color* screen, const Color* brain) {
    for (int i = 0; i < SCREEN_PX; ++i) {
        Color expect = console::from565(console::to565(brain[i]));
        if (memcmp(&screen[i], &expect, sizeof(Color)) != 0) return false;
    }
    return true;
}

int main() {
    printf("Console end-to-end integration test (brain -> wire -> screen)\n");

    // ---- brain side ----
    GameRegistry reg;
    registerBuiltinGames(reg);
    console::ThemeManager     themes(THEMES, THEME_COUNT);
    host::NullAudio           audio;
    MemorySettingsStore       store;
    LinkLoopback              wire;  // the shared in-memory link
    console_os::LinkFrameSink sink(wire);
    BrainOS                   os(reg, themes, &audio, store, &sink);
    os.setSeed(0x1234u);
    os.begin();

    // ---- screen side (real board, SAME wire) ----
    CapTile            cap[NUM_TILES];
    console::TileSink* tiles[NUM_TILES];
    for (int t = 0; t < NUM_TILES; ++t) tiles[t] = &cap[t];
    ScreenRenderBoard board(wire);
    uint32_t          now = 0;
    board.begin(tiles, NUM_TILES, os.lightProfile(), now);
    check(board.ok(), "screen board came up (12 tiles bound, link up)");
    check(cap[0].lastBright == os.lightProfile().wallBrightness,
          "initial LightProfile brightness reached the tiles");

    host::InputSampler in;

    // ---- pump the menu; the screen must mirror the brain every frame ----
    bool menuMatch = true, everLit = false;
    for (int f = 0; f < 12; ++f) {
        now += console::TICK_MS;
        os.tick(in.sample(), console::TICK_MS);  // draw + emit one frame into the wire
        board.tick(now);                // drain + decode + render one tick
        if (!screenMatchesBrain(board.canvas(), os.frame())) menuMatch = false;
        for (int t = 0; t < NUM_TILES; ++t)
            if (cap[t].anyLit()) everLit = true;
    }
    check(menuMatch, "screen canvas mirrors the brain framebuffer every menu frame");
    check(everLit, "the real renderer lit LEDs from the decoded frame");

    // ---- launch the first game; frames must keep mirroring ----
    in.setButton(0, true);
    now += console::TICK_MS;
    os.tick(in.sample(), console::TICK_MS);
    board.tick(now);
    in.setButton(0, false);
    check(os.mode() == Mode::Game, "launched into a game");
    bool gameMatch = true;
    for (int f = 0; f < 20; ++f) {
        now += console::TICK_MS;
        os.tick(in.sample(), console::TICK_MS);
        board.tick(now);
        if (!screenMatchesBrain(board.canvas(), os.frame())) gameMatch = false;
    }
    check(gameMatch, "screen mirrors the brain through 20 in-game frames");

    // ---- theme switch exercises the SET_LIGHT_PROFILE path; frames still mirror ----
    os.themes().next();
    bool themeMatch = true;
    for (int f = 0; f < 6; ++f) {
        now += console::TICK_MS;
        os.tick(in.sample(), console::TICK_MS);
        board.tick(now);
        if (!screenMatchesBrain(board.canvas(), os.frame())) themeMatch = false;
    }
    check(themeMatch, "screen keeps mirroring the brain across a theme switch");
    check(cap[0].lastBright == os.lightProfile().wallBrightness,
          "new theme's LightProfile crossed the wire to the tiles");

    printf(g_fails ? "\nE2E TEST FAILED (%d)\n" : "\nE2E TEST OK\n", g_fails);
    return g_fails ? 1 : 0;
}

// Headless smoke + acceptance test for Brain OS. No hardware, no TTY: it drives
// the OS one fixed tick at a time through the full journey the spec calls for —
//   boot -> menu (lists >=1 game incl. demo) -> launch -> pause overlay ->
//   exit-to-menu -> settings -> live theme switch -> back to a restyled menu —
// asserting state + framebuffer colours at each step. Exits non-zero on any fail.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "brain_os.h"
#include "builtin_games.h"
#include "console/color.h"
#include "console/config.h"
#include "console/themes.h"
#include "runner.h"  // host::InputSampler

using namespace console_os;
using console::Color;
using console::SCREEN_PX;
using console::SCREEN_W;
using console::TICK_MS;

// ---- test doubles ----

// Observes the OS's setVolume calls so we can assert VOLUME wiring.
class CaptureAudio : public console::Audio {
public:
    void tone(uint16_t, uint16_t) override {}
    void note(uint8_t, uint16_t) override {}
    void melody(const char*) override {}
    void stop() override {}
    bool playing() const override { return false; }
    void setVolume(float v) override { lastVol = v; calls++; }
    float lastVol = -1.0f;
    int   calls = 0;
};

// Captures the emitted frame + light profile (the screen would render these).
class CaptureSink : public FrameSink {
public:
    void frame(const Color* px, uint16_t seq) override {
        memcpy(last, px, sizeof(last));
        lastSeq = seq;
        frames++;
    }
    void light(const console::LightProfile& lp) override {
        lastLight = lp;
        lights++;
    }
    Color                 last[SCREEN_PX] = {};
    console::LightProfile lastLight = {};
    uint16_t              lastSeq = 0;
    int                   frames = 0;
    int                   lights = 0;
};

// ---- harness ----

static int g_fails = 0;
static void check(bool ok, const char* what) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fails++;
}

static bool eqC(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static host::InputSampler g_s;
static void step(BrainOS& os) { os.tick(g_s.sample(), TICK_MS); }

int main() {
    printf("Brain OS headless acceptance test\n");

    GameRegistry reg;
    registerBuiltinGames(reg);
    console::ThemeManager themes(THEMES, THEME_COUNT);
    CaptureAudio audio;
    MemorySettingsStore store;
    CaptureSink sink;
    BrainOS os(reg, themes, &audio, store, &sink);
    os.setSeed(0x1234u);
    os.begin();

    // ---- boot: on the menu, registry lists >=1 game incl. the demo ----
    printf("boot -> menu\n");
    check(os.mode() == Mode::Menu, "boots into the menu");
    check(reg.count() >= 1, "registry lists >=1 game");
    check(reg.indexOf("demo") >= 0, "the demo game is enumerated");
    check(fabsf(audio.lastVol - Settings{}.volume / 255.0f) < 0.01f, "begin applied master volume");
    check(sink.lights >= 1, "begin emitted an initial LightProfile");

    // render one menu frame under the boot theme (index 0), keep it for later
    step(os);
    const uint8_t idxA = themes.index();
    Color frameA[SCREEN_PX];
    memcpy(frameA, os.frame(), sizeof(frameA));
    check(os.menuSel() == 0, "cursor starts on the first game");
    check(sink.frames >= 1, "OS emitted a frame");

    // ---- launch the first menu game (cursor on index 0) ----
    const char* firstGame = reg.at(0).name;
    printf("menu -> launch %s\n", firstGame);
    g_s.setButton(0, true);
    step(os);  // knob0 press -> Launch
    check(os.mode() == Mode::Game, "press launches into Game mode");
    console::Game* g = os.activeGame();
    check(g != nullptr, "a game instance is active");
    check(g && strcmp(g->meta().name, firstGame) == 0, "the launched game is the first menu entry");
    g_s.setButton(0, false);
    step(os);
    for (int i = 0; i < 5; ++i) step(os);  // let it run a few ticks
    check(os.mode() == Mode::Game, "game keeps running");

    // ---- pause overlay via the reserved chord (both buttons) ----
    printf("game -> overlay (chord)\n");
    g_s.setButton(0, true);
    g_s.setButton(1, true);
    step(os);  // both down same tick -> chord
    check(os.overlayOpen(), "both-button chord opens the overlay");
    check(os.mode() == Mode::Game, "overlay floats over the game (still Game mode)");
    check(os.activeGame() == g, "game is NOT torn down under the overlay");

    // ---- RESTART: same title, fresh instance, overlay closed ----
    printf("overlay -> restart\n");
    g_s.setButton(0, false);
    g_s.setButton(1, false);
    step(os);  // release both -> overlay arms
    g_s.rotate(0, +1);
    step(os);  // RESUME -> RESTART (one step per tick)
    g_s.setButton(0, true);
    step(os);  // select RESTART
    check(os.mode() == Mode::Game && !os.overlayOpen(), "RESTART relaunches into the game");
    check(os.activeGame() && strcmp(os.activeGame()->meta().name, firstGame) == 0,
          "RESTART keeps the same title");
    g_s.setButton(0, false);
    step(os);

    // ---- NEXT: jump to the following title without a menu round-trip ----
    printf("overlay -> next game\n");
    g_s.setButton(0, true);
    g_s.setButton(1, true);
    step(os);  // chord -> overlay
    g_s.setButton(0, false);
    g_s.setButton(1, false);
    step(os);  // arm
    for (int k = 0; k < 2; ++k) {
        g_s.rotate(0, +1);
        step(os);  // -> NEXT (sel 2)
    }
    g_s.setButton(0, true);
    step(os);  // select NEXT
    check(os.mode() == Mode::Game && !os.overlayOpen(), "NEXT relaunches into a game");
    check(os.activeGame() && strcmp(os.activeGame()->meta().name, reg.at(1).name) == 0,
          "NEXT jumps to the 2nd title");
    g_s.setButton(0, false);
    step(os);

    // ---- exit-to-menu from the overlay ----
    printf("overlay -> exit to menu\n");
    g_s.setButton(0, true);
    g_s.setButton(1, true);
    step(os);  // chord -> overlay
    g_s.setButton(0, false);
    g_s.setButton(1, false);
    step(os);  // arm
    for (int k = 0; k < 3; ++k) {
        g_s.rotate(0, +1);
        step(os);  // scroll RESUME -> RESTART -> NEXT -> EXIT
    }
    g_s.setButton(0, true);
    step(os);  // select EXIT
    check(os.mode() == Mode::Menu, "EXIT returns to the menu");
    check(os.activeGame() == nullptr, "game torn down on exit");
    g_s.setButton(0, false);
    step(os);

    // ---- open settings (scroll to the trailing SETTINGS row, press) ----
    printf("menu -> settings\n");
    for (int k = 0; k < reg.count(); ++k) {
        g_s.rotate(0, +1);
        step(os);
    }
    check(os.menuSel() == reg.count(), "cursor lands on the SETTINGS row");
    g_s.setButton(0, true);
    step(os);
    check(os.mode() == Mode::Settings, "press opens settings");
    g_s.setButton(0, false);
    step(os);

    // ---- VOLUME row wiring ----
    printf("settings: VOLUME\n");
    check(os.settingsRow() == int(SettingsRow::Volume), "opens on the VOLUME row");
    uint8_t volBefore = os.settings().volume;
    g_s.rotate(1, -1);
    step(os);  // knob1 lowers volume
    check(os.settings().volume < volBefore, "knob turns VOLUME down");
    check(fabsf(audio.lastVol - os.settings().volume / 255.0f) < 0.01f,
          "VOLUME change drives Audio::setVolume");

    // ---- BRIGHTNESS row wiring (rides in the LightProfile) ----
    printf("settings: BRIGHTNESS\n");
    g_s.rotate(0, +1);
    step(os);  // -> BRIGHTNESS row
    check(os.settingsRow() == int(SettingsRow::Brightness), "navigates to BRIGHTNESS");
    uint8_t wallBefore = os.lightProfile().wallBrightness;
    g_s.rotate(1, -1);
    step(os);  // lower brightness
    check(os.lightProfile().wallBrightness < wallBefore,
          "BRIGHTNESS lowers the emitted LightProfile.wallBrightness");

    // ---- THEME picker: live restyle + persistence ----
    printf("settings: THEME switch\n");
    g_s.rotate(0, +1);
    step(os);  // -> THEME row
    check(os.settingsRow() == int(SettingsRow::Theme), "navigates to THEME");
    const uint8_t idxBefore = themes.index();
    const int lightsBefore = sink.lights;
    g_s.rotate(1, +1);
    step(os);  // knob1 -> next theme
    const uint8_t idxAfter = themes.index();
    check(idxAfter != idxBefore, "THEME picker changes the active theme index");
    check(os.settings().themeIndex == idxAfter, "settings track the new theme index");
    check(sink.lights > lightsBefore, "theme switch re-emits the LightProfile");
    Settings persisted;
    check(store.load(persisted), "settings were persisted to the store");
    check(persisted.themeIndex == idxAfter, "persisted theme index matches the pick");

    // ---- back to the menu, re-rendered in the new theme's colours ----
    printf("settings -> menu (restyled)\n");
    g_s.setButton(0, true);
    step(os);  // press -> Back to menu
    check(os.mode() == Mode::Menu, "press returns to the menu");
    g_s.setButton(0, false);
    step(os);
    for (int k = 0; k < reg.count(); ++k) {
        g_s.rotate(0, -1);
        step(os);  // scroll cursor back to game 0 to isolate the theme delta
    }
    check(os.menuSel() == 0, "cursor back on the first game");
    Color frameB[SCREEN_PX];
    memcpy(frameB, os.frame(), sizeof(frameB));

    // The menu backdrop is the ambient scene now (theme-token colors, cleared to
    // ROLE_BG each frame), so each theme's frame CONTAINS that theme's background —
    // proving the shell restyled, without depending on a static menu-pixel layout.
    Color bgA = THEMES[idxA].role[console::ROLE_BG];
    Color bgB = THEMES[idxAfter].role[console::ROLE_BG];
    auto hasColor = [](const Color* f, Color c) {
        for (int i = 0; i < SCREEN_PX; ++i) if (eqC(f[i], c)) return true;
        return false;
    };
    check(!eqC(bgA, bgB), "the two themes have distinct backgrounds");
    check(hasColor(frameA, bgA), "menu backdrop shows theme A's background");
    check(hasColor(frameB, bgB), "menu backdrop shows theme B's background");
    check(memcmp(frameA, frameB, sizeof(frameA)) != 0, "menu framebuffer changed after restyle");

    // ---- OS-owned fixed-rate loop: pump wall-clock, expect TICK_MS stepping ----
    printf("os-owned loop (pump)\n");
    int f0 = sink.frames;
    console::Input zero{};
    os.pump(1000, zero);               // first call anchors the clock (0 elapsed)
    os.pump(1000 + 3 * TICK_MS + 2, zero);  // ~3 fixed steps are due
    check(sink.frames - f0 == 3, "pump() runs the due fixed TICK_MS steps");

    // ---- persistence stub: the file-backed store round-trips settings ----
    printf("settings store (file stub)\n");
    {
        const char* p = "./.brainos_test.bin";
        remove(p);
        FileSettingsStore fs(p);
        Settings none;
        check(!fs.load(none), "file store: fresh (no file) -> load() is false");
        Settings w;
        w.themeIndex = 3;
        w.volume = 42;
        w.brightness = 99;
        fs.save(w);
        Settings r;
        check(fs.load(r), "file store: load() true after save");
        check(r.themeIndex == 3 && r.volume == 42 && r.brightness == 99,
              "file store round-trips theme/volume/brightness");
        remove(p);
    }

    // ---- report ----
    printf("\nregistry (%d games + SETTINGS):\n", reg.count());
    for (int i = 0; i < reg.count(); ++i) printf("  [%d] %s\n", i, reg.at(i).name);
    printf("active theme now: [%d] %s\n", themes.index(), themes.active().name);

    printf(g_fails ? "\nBRAIN OS TEST FAILED (%d)\n" : "\nBRAIN OS TEST OK\n", g_fails);
    return g_fails ? 1 : 0;
}

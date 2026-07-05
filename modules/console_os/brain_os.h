#pragma once
#include <stdint.h>
#include "console/audio.h"
#include "console/canvas.h"
#include "console/color.h"
#include "console/config.h"
#include "console/game.h"
#include "console/input.h"
#include "console/theme.h"
#include "frame_sink.h"
#include "game_registry.h"
#include "menu.h"
#include "overlay.h"
#include "settings.h"
#include "settings_view.h"
#include "ui_audio.h"

#include "ambient.h"  // reused as the menu-mode LED backdrop (theme-colored scenes)

// Brain OS — the on-device shell. It owns the fixed-rate loop, the one framebuffer
// every app draws into, the active theme, master volume, brightness, and the app
// lifecycle (menu <-> game, with a pause overlay that never tears the game down).
//
// The OS owns the fixed TICK_HZ step. Two ways to drive it:
//   tick(in, dtMs)   — advance exactly one fixed step (tests / a caller that
//                      already sampled input at TICK_HZ).
//   pump(nowMs, in)  — feed wall-clock; the OS accumulates and runs as many fixed
//                      TICK_MS steps as are due (catch-up capped). Deltas/edges
//                      apply on the first sub-step only; catch-up steps see input
//                      held-but-quiet, so a burst never double-counts a detent.
//
// Everything the shell draws goes through theme tokens (see menu/settings/overlay).

namespace console_os {

enum class Mode : uint8_t { Boot, Menu, Settings, Game };

class BrainOS {
public:
    BrainOS(GameRegistry& reg, console::ThemeManager& themes, console::Audio* audio,
            SettingsStore& store, FrameSink* sink = nullptr)
        : reg_(reg), themes_(themes), audio_(audio), store_(store), sink_(sink),
          sfx_(audio), canvas_(buf_, console::SCREEN_W, console::SCREEN_H) {}

    // Load persisted settings (or defaults), apply them, and land on the menu.
    void begin();

    // Advance exactly one fixed OS tick.
    void tick(const console::Input& in, uint32_t dtMs);

    // Wall-clock driver: run every fixed step that is due since the last call.
    void pump(uint32_t nowMs, const console::Input& in);

    void setSeed(uint32_t s) { seed_ = s; }

    // ---- outputs ----
    const console::Color* frame() const { return buf_; }
    // Active theme's LightProfile with wallBrightness scaled by the brightness
    // setting — this is where the screen-side brightness rides to the wall.
    console::LightProfile lightProfile() const;

    // ---- introspection (telemetry / tests) ----
    Mode                   mode() const { return mode_; }
    const console::Theme&  theme() const { return themes_.active(); }
    console::ThemeManager& themes() { return themes_; }
    const console::ThemeManager& themes() const { return themes_; }
    const GameRegistry&    registry() const { return reg_; }
    const Settings&        settings() const { return settings_; }
    console::Game*         activeGame() const { return game_; }
    int                    activeGameIndex() const { return gameIdx_; }  // -1 in menu
    bool                   overlayOpen() const { return ov_.open; }
    int                    overlaySel() const { return ov_.sel; }
    int                    menuSel() const { return menu_.sel; }
    int                    settingsRow() const { return set_.row; }

private:
    void updateActive(const console::Input& in, uint32_t dtMs);
    void drawActive();
    void emit();

    void launch(int gameIndex);
    void openOverlay();
    void closeOverlay();
    void exitToMenu();

    GameRegistry&          reg_;
    console::ThemeManager& themes_;
    console::Audio*        audio_;
    SettingsStore&         store_;
    FrameSink*             sink_;
    UiAudio                sfx_;

    Settings        settings_;
    console::Color  buf_[console::SCREEN_PX] = {};
    console::Canvas canvas_;

    Mode          mode_ = Mode::Boot;
    MenuState     menu_;
    SettingsState set_;
    OverlayState  ov_;
    ambient::AmbientGame ambientBg_;  // menu-mode backdrop on the 6x18 display

    console::Game*       game_    = nullptr;
    int                  gameIdx_ = -1;
    console::GameContext ctx_     = {};
    uint32_t             seed_    = 0x1234u;

    uint16_t             seq_       = 0;
    bool                 haveLight_ = false;
    console::LightProfile lastLight_ = {};

    // pump() fixed-timestep accumulator
    static constexpr int      MAX_CATCHUP_STEPS  = 4;
    static constexpr uint16_t LIGHT_RESEND_TICKS = 100;  // ~2s at TICK_HZ — heal a late display
    uint32_t accMs_    = 0;
    uint32_t lastNow_  = 0;
    bool     pumpInit_ = false;
};

}  // namespace console_os

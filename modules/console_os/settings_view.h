#pragma once
#include <stdint.h>
#include "console/audio.h"
#include "console/canvas.h"
#include "console/input.h"
#include "console/theme.h"
#include "os_gfx.h"
#include "settings.h"

// The settings shell. Two knobs: knob[0] rotate picks the row (VOLUME /
// BRIGHTNESS / THEME), knob[1] rotate adjusts that row's value live, knob[0]
// press returns to the menu. Adjusting VOLUME drives Audio::setVolume; adjusting
// THEME calls ThemeManager and the whole shell repaints in the new palette on
// the same frame (the live preview). Every change persists immediately.

namespace console_os {

enum class SettingsRow : uint8_t { Volume, Brightness, Theme, Music, COUNT };
enum class SettingsAction : uint8_t { None, Back };

struct SettingsState {
    int      row = 0;
    uint32_t tMs = 0;
};

// Everything the settings shell is allowed to mutate, injected by the OS.
struct SettingsCtx {
    console::ThemeManager& themes;
    console::Audio*        audio;
    Settings&             settings;  // live values (themeIndex/volume/brightness)
    SettingsStore*        store;     // persist-on-change (may be null)
};

namespace detail {

inline uint8_t clampAdd(uint8_t v, int delta) {
    int n = int(v) + delta;
    return uint8_t(n < 0 ? 0 : (n > 255 ? 255 : n));
}

}  // namespace detail

inline SettingsAction settingsUpdate(SettingsState& s, SettingsCtx& ctx,
                                     const console::Input& in, uint32_t dtMs) {
    s.tMs += dtMs;
    const int rows = int(SettingsRow::COUNT);

    int nav = sdk::signi(int(in.knob[0].delta));
    if (nav != 0) s.row = sdk::clampi(s.row + nav, 0, rows - 1);

    int adj = int(in.knob[1].delta);
    bool changed = false;
    if (adj != 0) {
        switch (SettingsRow(s.row)) {
            case SettingsRow::Volume: {
                uint8_t nv = detail::clampAdd(ctx.settings.volume, adj * VOL_STEP);
                if (nv != ctx.settings.volume) {
                    ctx.settings.volume = nv;
                    if (ctx.audio) ctx.audio->setVolume(nv / 255.0f);
                    changed = true;
                }
                break;
            }
            case SettingsRow::Brightness: {
                uint8_t nb = detail::clampAdd(ctx.settings.brightness, adj * BRIGHT_STEP);
                if (nb != ctx.settings.brightness) {
                    ctx.settings.brightness = nb;  // OS re-emits LightProfile next tick
                    changed = true;
                }
                break;
            }
            case SettingsRow::Theme: {
                int steps = adj > 0 ? adj : -adj;
                for (int i = 0; i < steps; ++i) adj > 0 ? ctx.themes.next() : ctx.themes.prev();
                ctx.settings.themeIndex = ctx.themes.index();
                changed = true;
                break;
            }
            case SettingsRow::Music: {
                bool nm = adj > 0;  // knob1 right = on, left = off
                if (nm != ctx.settings.menuMusic) { ctx.settings.menuMusic = nm; changed = true; }
                break;
            }
            default: break;
        }
    }
    if (changed && ctx.store) ctx.store->save(ctx.settings);

    if (in.knob[0].justPressed) return SettingsAction::Back;
    return SettingsAction::None;
}

inline void settingsDraw(const SettingsState& s, const SettingsCtx& ctx,
                         console::Canvas& c, const console::Theme& t) {
    using namespace console;
    c.clear(t.c(ROLE_BG));

    const SettingsRow row = SettingsRow(s.row);
    const char* title = row == SettingsRow::Volume       ? "VOLUME"
                        : row == SettingsRow::Brightness ? "BRIGHT"
                        : row == SettingsRow::Theme      ? "THEME"
                                                         : "MUSIC";

    // Row selector pips down the left rail.
    gfx::pipsV(c, 0, 2, int(SettingsRow::COUNT), s.row, t.c(ROLE_ACCENT), t.c(ROLE_DIM));

    // Title marquees along the top band in the ink token.
    uint8_t glow = gfx::breathe(s.tMs, t.motion.blinkMs, 170, 255);
    gfx::label(c, 1, title, gfx::dim(t.c(ROLE_INK), glow), s.tMs);

    // Value visualisation for the current row.
    if (row == SettingsRow::Volume) {
        gfx::meter(c, 0, 9, SCREEN_W, 3, ctx.settings.volume, t.c(ROLE_ACCENT), t.c(ROLE_DIM));
    } else if (row == SettingsRow::Brightness) {
        gfx::meter(c, 0, 9, SCREEN_W, 3, ctx.settings.brightness, t.c(ROLE_GOOD), t.c(ROLE_DIM));
    } else if (row == SettingsRow::Theme) {
        // THEME: name marquee + one pip per theme, active pip glowing.
        gfx::label(c, 8, t.name, gfx::dim(t.c(ROLE_ACCENT), glow), s.tMs + 300);
        int n = ctx.themes.count();
        int x0 = (SCREEN_W - n) / 2;
        if (x0 < 0) x0 = 0;
        gfx::pipsH(c, x0, 15, n, ctx.themes.index(),
                   gfx::dim(t.c(ROLE_ACCENT2), glow), t.c(ROLE_DIM), 1);
    } else {
        // MUSIC: a full bar when on, empty when off.
        gfx::meter(c, 0, 9, SCREEN_W, 3, ctx.settings.menuMusic ? 255 : 0,
                   gfx::dim(t.c(ROLE_ACCENT2), glow), t.c(ROLE_DIM));
    }
}

}  // namespace console_os

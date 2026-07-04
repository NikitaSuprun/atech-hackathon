#pragma once
#include <stdint.h>
#include "console/canvas.h"
#include "console/input.h"
#include "console/theme.h"
#include "game_registry.h"
#include "os_gfx.h"

// The launcher. It enumerates the registry plus a trailing synthetic SETTINGS
// row, all on one knob: knob[0] rotate scrolls the list, knob[0] press activates
// the highlighted row (launch a game, or open settings). Smooth + glowing: the
// selected title marquees across the strip and breathes in the accent colour;
// a "just moved" flash brightens the pips on each step.

namespace console_os {

enum class MenuAction : uint8_t { None, Launch, OpenSettings };

struct MenuState {
    int      sel      = 0;
    uint32_t tMs      = 0;
    uint32_t sinceMov = 1u << 30;  // ms since last selection change (flash on move)
};

// itemCount = games + 1 (SETTINGS). Returns the action; the caller reads .sel.
inline MenuAction menuUpdate(MenuState& m, int gameCount, const console::Input& in,
                             uint32_t dtMs) {
    m.tMs += dtMs;
    m.sinceMov += dtMs;
    const int items = gameCount + 1;

    int d = sdk::signi(int(in.knob[0].delta));
    if (d != 0) {
        m.sel = ((m.sel + d) % items + items) % items;
        m.sinceMov = 0;
    }
    if (in.knob[0].justPressed)
        return m.sel < gameCount ? MenuAction::Launch : MenuAction::OpenSettings;
    return MenuAction::None;
}

inline void menuDraw(const MenuState& m, const GameRegistry& reg, console::Canvas& c,
                     const console::Theme& t) {
    using namespace console;
    c.clear(t.c(ROLE_BG));

    const int  games = reg.count();
    const int  items = games + 1;
    const bool onSettings = m.sel >= games;
    const char* name = onSettings ? "SETTINGS" : reg.at(m.sel).name;

    // Framing rails top/bottom in the dim tier.
    c.hline(0, 0, SCREEN_W, t.c(ROLE_DIM));
    c.hline(0, SCREEN_H - 1, SCREEN_W, t.c(ROLE_DIM));

    // Position pips down the left rail; the current one pops (and flashes on move).
    uint8_t moveGlow = m.sinceMov < 120 ? 255 : gfx::breathe(m.tMs, t.motion.blinkMs, 120, 255);
    Color   pipHi = gfx::dim(t.c(ROLE_ACCENT2), moveGlow);
    int     pipTop = (SCREEN_H - items * 2) / 2;
    if (pipTop < 2) pipTop = 2;
    gfx::pipsV(c, 0, pipTop, items, m.sel, pipHi, t.c(ROLE_DIM));

    // Selected title: marquee in the accent, breathing to sell the glow.
    uint8_t glow = gfx::breathe(m.tMs, t.motion.blinkMs, 150, 255);
    Color   ink  = gfx::dim(onSettings ? t.c(ROLE_ACCENT2) : t.c(ROLE_ACCENT), glow);
    gfx::label(c, 6, name, ink, m.tMs);

    // Animated underline in the secondary accent — the "smooth" motion cue.
    uint8_t uglow = gfx::breathe(m.tMs + t.motion.blinkMs / 3, t.motion.blinkMs, 40, 160);
    c.hline(1, 13, SCREEN_W - 2, gfx::dim(t.c(ROLE_ACCENT2), uglow));
}

}  // namespace console_os

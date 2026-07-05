#pragma once
#include <stdint.h>
#include "console/canvas.h"
#include "console/input.h"
#include "console/theme.h"
#include "os_gfx.h"

// The system overlay: a Playdate-style pause menu the OS floats OVER any running
// game without tearing it down. Opened by a reserved chord (both knob buttons
// held) so it never collides with a game's own single-button use. The game frame
// stays underneath (dimmed) — RESUME just closes the overlay and the game runs
// on from exactly where it froze; EXIT tears it down back to the launcher.
//
// Arming: the chord that opens the overlay is itself two held buttons, so a
// press is ignored until BOTH buttons have been released once (armed_). That
// stops the opening chord from instantly selecting an item.

namespace console_os {

enum class OverlayAction : uint8_t { None, Resume, Restart, Next, Exit };

// Options in wheel order: RESUME, RESTART (re-init this game), NEXT (jump to the
// next title without a menu round-trip), EXIT (back to the launcher).
constexpr int kOverlayOpts = 4;

struct OverlayState {
    bool     open  = false;
    int      sel   = 0;      // 0=RESUME 1=RESTART 2=NEXT 3=EXIT
    bool     armed = false;  // both buttons released since open?
    uint32_t tMs   = 0;

    void begin() { open = true; sel = 0; armed = false; tMs = 0; }
    void reset() { open = false; sel = 0; armed = false; tMs = 0; }
};

// True on the tick the chord (both buttons, one just crossed down) engages.
inline bool overlayChord(const console::Input& in) {
    return in.knob[0].down && in.knob[1].down &&
           (in.knob[0].justPressed || in.knob[1].justPressed);
}

inline OverlayAction overlayUpdate(OverlayState& o, const console::Input& in,
                                   uint32_t dtMs) {
    o.tMs += dtMs;
    if (!o.armed && !in.knob[0].down && !in.knob[1].down) o.armed = true;

    int d = sdk::signi(int(in.knob[0].delta));
    if (d != 0) o.sel = sdk::clampi(o.sel + d, 0, kOverlayOpts - 1);

    if (o.armed && in.knob[0].justPressed) {
        switch (o.sel) {
            case 0:  return OverlayAction::Resume;
            case 1:  return OverlayAction::Restart;
            case 2:  return OverlayAction::Next;
            default: return OverlayAction::Exit;
        }
    }
    return OverlayAction::None;
}

// Draws on top of whatever the game already rendered into the canvas: dims that
// backdrop, then floats a framed panel with a pause glyph + the current choice.
inline void overlayDraw(const OverlayState& o, console::Canvas& c,
                        const console::Theme& t) {
    using namespace console;

    // Dim the live game frame so the panel reads on top of it.
    Color* px = c.buffer();
    for (int i = 0; i < SCREEN_W * SCREEN_H; ++i) px[i] = console::scale(px[i], 70);

    // Panel body + breathing accent border.
    const int y0 = 3, y1 = SCREEN_H - 4;
    c.fill(0, y0, SCREEN_W, y1 - y0 + 1, t.c(ROLE_BG));
    uint8_t bglow = gfx::breathe(o.tMs, t.motion.blinkMs, 120, 255);
    c.rect(0, y0, SCREEN_W, y1 - y0 + 1, gfx::dim(t.c(ROLE_ACCENT), bglow));

    // Pause glyph: two bars at the top of the panel.
    c.vline(2, y0 + 2, 3, t.c(ROLE_ACCENT2));
    c.vline(3, y0 + 2, 3, t.c(ROLE_ACCENT2));

    // Current choice marquee (one option shown at a time; scrolls if too wide).
    static const char* const kOpts[kOverlayOpts] = {"RESUME", "RESTART", "NEXT", "EXIT"};
    uint8_t tglow = gfx::breathe(o.tMs, t.motion.blinkMs, 160, 255);
    gfx::label(c, 9, kOpts[o.sel], gfx::dim(t.c(ROLE_INK), tglow), o.tMs);

    // Four option pips at the panel foot (columns 1..4).
    gfx::pipsH(c, 1, y1 - 1, kOverlayOpts, o.sel, t.c(ROLE_ACCENT), t.c(ROLE_DIM), 1);
}

}  // namespace console_os

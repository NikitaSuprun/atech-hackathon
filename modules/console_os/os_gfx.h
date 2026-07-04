#pragma once
#include <stdint.h>
#include "console/canvas.h"
#include "console/color.h"
#include "console/config.h"
#include "console/theme.h"
#include "sdk.h"

// Shared UI primitives for the OS shells (menu / settings / overlay). Every one
// takes colours the caller pulled from theme tokens (t.c(ROLE_*), t.cat, t.ramp)
// — nothing here knows a hex value, so a theme switch restyles the whole shell.

namespace console_os {
namespace gfx {

using console::Canvas;
using console::Color;

// Triangle breathe in [lo,hi] over periodMs — the "glow". periodMs comes from
// the theme's MotionProfile.blinkMs so warm themes pulse slower than neon.
inline uint8_t breathe(uint32_t tMs, uint16_t periodMs, uint8_t lo, uint8_t hi) {
    if (periodMs == 0) return hi;
    uint32_t ph   = tMs % periodMs;
    uint32_t half = periodMs / 2u;
    if (half == 0) return hi;
    uint32_t tri = ph < half ? ph : (periodMs - ph);  // 0..half..0
    return uint8_t(lo + (uint32_t(hi - lo) * tri) / half);
}

// Brightness-scale a token colour (integer, MCU-safe).
inline Color dim(Color c, uint8_t s) { return console::scale(c, s); }

// Draw s so it reads within the 6px-wide window: centre it if it fits, else
// scroll it right->left on a continuous loop (a full blank gap between reps).
inline void label(Canvas& cv, int y, const char* s, Color fg, uint32_t tMs,
                  uint32_t msPerPx = 70) {
    int tw = sdk::textWidth(s);
    if (tw <= cv.width()) {
        sdk::drawText(cv, (cv.width() - tw) / 2, y, s, fg);
        return;
    }
    int cycle = tw + cv.width() + sdk::GLYPH_ADVANCE;  // off-screen lead-in + out
    int off   = int((tMs / msPerPx) % uint32_t(cycle));
    sdk::drawText(cv, cv.width() - off, y, s, fg);
}

// Horizontal fill meter across the full width: value/255 of the cells lit in
// `on`, the rest in `off`. Used for VOLUME / BRIGHTNESS.
inline void meter(Canvas& cv, int x, int y, int w, int h, uint8_t value, Color on,
                  Color off) {
    int lit = (int(value) * w + 127) / 255;  // rounded
    for (int i = 0; i < w; ++i) cv.fill(x + i, y, 1, h, i < lit ? on : off);
}

// A row/column of position pips: index `sel` glows in `hi`, others sit in `lo`.
inline void pipsV(Canvas& cv, int x, int y, int n, int sel, Color hi, Color lo,
                  int step = 2) {
    for (int i = 0; i < n; ++i) cv.pixel(x, y + i * step, i == sel ? hi : lo);
}
inline void pipsH(Canvas& cv, int x, int y, int n, int sel, Color hi, Color lo,
                  int step = 2) {
    for (int i = 0; i < n; ++i) cv.pixel(x + i * step, y, i == sel ? hi : lo);
}

}  // namespace gfx
}  // namespace console_os

#include "tft_dashboard.h"

#include <stdio.h>

#include "brain_os.h"
#include "console/color.h"
#include "console/theme.h"
#include "os_gfx.h"       // gfx::breathe
#include "tft_icons.h"

namespace console_os {

using console::Color;
using console::scale;
using console::to565;

namespace {

// Convert a token color to RGB565, dimmed by the global brightness setting.
inline uint16_t C(Color c, uint8_t b) { return to565(scale(c, b)); }

// Float blend a->b (f in 0..1). The dashboard's one bit of non-integer math —
// device-side presentation only, and the S3 has an FPU.
inline Color mix(Color a, Color b, float f) {
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return {uint8_t(a.r + (b.r - a.r) * f), uint8_t(a.g + (b.g - a.g) * f),
            uint8_t(a.b + (b.b - a.b) * f)};
}

// Per-card identity. Games take a theme category hue (so a theme switch re-tints
// the whole strip); the trailing SETTINGS card takes the primary accent.
Color cardAccent(const BrainOS& os, const console::Theme& t, int i) {
    if (i >= os.registry().count()) return t.c(console::ROLE_ACCENT);
    return t.cat[i % console::CAT_N];
}
const Icon& cardIcon(const BrainOS& os, int i) {
    return i >= os.registry().count() ? icons::SETTINGS : gameIcon(i);
}
const char* cardName(const BrainOS& os, int i) {
    return i >= os.registry().count() ? "SETTINGS" : os.registry().at(i).name;
}
const char* cardTag(const BrainOS& os, int i) {
    return i >= os.registry().count() ? "SOUND  LIGHT  THEME" : gameTag(i);
}

inline float clamp01(float v) { return v < 0 ? 0.f : (v > 1 ? 1.f : v); }

}  // namespace

void TftDashboard::render(const BrainOS& os, uint32_t nowMs) {
    if (!init_) {
        init_ = true;
        startMs_ = nowMs;
        lastMs_ = nowMs;
        Color a = os.theme().cat[0];
        wash_[0] = a.r;
        wash_[1] = a.g;
        wash_[2] = a.b;
        scroll_ = float(os.menuSel());
    }
    dt_ = float(nowMs - lastMs_);
    if (dt_ < 1) dt_ = 1;
    if (dt_ > 60) dt_ = 60;
    lastMs_ = nowMs;
    nowMs_ = nowMs;

    const console::Theme& t = os.theme();
    uint8_t bright = os.settings().brightness;
    if (bright < 80) bright = 80;  // keep the UI legible at low wall brightness

    if (nowMs - startMs_ < 1600u) {  // branded power-on intro
        drawBoot(t, bright, nowMs);
        tft_.present();
        return;
    }

    switch (os.mode()) {
        case Mode::Settings: drawSettings(os, t, bright); break;
        case Mode::Game:     drawGame(os, t, bright); break;
        case Mode::Menu:
        case Mode::Boot:
        default:             drawMenu(os, t, bright); break;
    }
    tft_.present();
}

void TftDashboard::drawBoot(const console::Theme& t, uint8_t bright, uint32_t nowMs) {
    using namespace console;
    Color bg = t.c(ROLE_BG), acc = t.c(ROLE_ACCENT), acc2 = t.c(ROLE_ACCENT2), ink = t.c(ROLE_INK);
    tft_.clear(C(bg, bright));
    float e = (nowMs - startMs_) / 1000.f;
    tft_.text(FontId::SansBig, 80, 30, "ATECH", C(mix(bg, acc, clamp01((e - 0.15f) / 0.5f)), bright),
              Align::Center);
    tft_.text(FontId::SansBig, 80, 50, "ARCADE",
              C(mix(bg, acc, clamp01((e - 0.45f) / 0.5f)), bright), Align::Center);
    int w = int(92 * clamp01((e - 0.9f) / 0.6f));
    tft_.fillRect(80 - w / 2, 62, w, 2, C(acc2, bright));
    tft_.text(FontId::Small, 80, 71, "BUILD-IT-YOURSELF CONSOLE",
              C(mix(bg, ink, clamp01((e - 1.1f) / 0.5f) * 0.75f), bright), Align::Center);
}

void TftDashboard::drawMenu(const BrainOS& os, const console::Theme& t, uint8_t bright) {
    using namespace console;
    const int games = os.registry().count();
    const int items = games + 1;  // + trailing SETTINGS card
    int sel = os.menuSel();
    if (sel < 0) sel = 0;
    if (sel > items - 1) sel = items - 1;

    float a = dt_ * 0.016f;
    if (a > 1) a = 1;
    scroll_ += (float(sel) - scroll_) * a;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > items - 1) scroll_ = float(items - 1);

    int lo = int(scroll_);
    if (lo > items - 1) lo = items - 1;
    int hi = lo + 1;
    if (hi > items - 1) hi = items - 1;
    float frac = scroll_ - lo;
    int ci = int(scroll_ + 0.5f);
    if (ci > items - 1) ci = items - 1;

    // Eased background wash toward the centered card's hue.
    Color wt = mix(cardAccent(os, t, lo), cardAccent(os, t, hi), frac);
    float aw = dt_ * 0.011f;
    if (aw > 1) aw = 1;
    wash_[0] += (wt.r - wash_[0]) * aw;
    wash_[1] += (wt.g - wash_[1]) * aw;
    wash_[2] += (wt.b - wash_[2]) * aw;
    Color wash{uint8_t(wash_[0]), uint8_t(wash_[1]), uint8_t(wash_[2])};

    Color bg = t.c(ROLE_BG);
    tft_.clear(C(bg, bright));

    // Soft spotlight behind the hero (nested blended circles, brightest at core).
    for (int ri = 5; ri >= 1; --ri)
        tft_.fillCircle(80, 26, ri * 6 + 2, C(mix(bg, wash, 0.09f * (6 - ri)), bright));

    // Filmstrip: icons slide with scroll; size + tint fall off from center.
    const int STRIDE = 48;
    for (int i = lo - 1; i <= hi + 1; ++i) {
        if (i < 0 || i >= items) continue;
        float d = float(i) - scroll_;
        float ad = d < 0 ? -d : d;
        if (ad > 2.2f) continue;
        int x = 80 + int(d * STRIDE);
        float near = 1.f - ad / 1.6f;
        if (near < 0) near = 0;
        int cell = 1 + int(near * 2.0f + 0.5f);
        if (cell > 3) cell = 3;
        Color ic = mix(bg, cardAccent(os, t, i), 0.28f + near * 0.72f);
        drawIcon(tft_, cardIcon(os, i), x, 26, cell, C(ic, bright));
    }

    // Name + tagline of the centered card, crossfading to the next as you scroll.
    drawCard(os, t, bright, lo, 1.f - frac, int(-frac * 10));
    if (hi != lo) drawCard(os, t, bright, hi, frac, int((1.f - frac) * 10));

    // Position dots (games + settings); the current one pops in its hue.
    int n = items;
    int gap = 12;
    if (n > 1) {
        int gmax = 150 / (n - 1);
        if (gap > gmax) gap = gmax;
    }
    int x0 = 80 - (n - 1) * gap / 2;
    for (int i = 0; i < n; ++i) {
        bool on = (i == ci);
        tft_.fillCircle(x0 + i * gap, 70, on ? 2 : 1,
                        C(on ? cardAccent(os, t, i) : t.c(ROLE_DIM), bright));
    }

    // Top bar (theme name / index) + control hint.
    char idx[8];
    if (ci < games) {
        snprintf(idx, sizeof(idx), "%d/%d", ci + 1, games);
    } else {
        idx[0] = 'S'; idx[1] = 'E'; idx[2] = 'T'; idx[3] = '\0';
    }
    tft_.text(FontId::Small, 156, 6, idx, C(t.c(ROLE_INK), bright), Align::Right);
    tft_.text(FontId::Small, 4, 6, t.name, C(t.c(ROLE_DIM), bright), Align::Left);
    tft_.text(FontId::Small, 80, 76, "TURN TO BROWSE", C(t.c(ROLE_DIM), bright), Align::Center);
}

void TftDashboard::drawCard(const BrainOS& os, const console::Theme& t, uint8_t bright, int idx,
                            float alpha, int dx) {
    using namespace console;
    if (alpha <= 0.03f) return;
    Color bg = t.c(ROLE_BG);
    uint16_t period = t.motion.blinkMs ? t.motion.blinkMs : 1200;
    uint8_t glow = gfx::breathe(nowMs_, period, 175, 255);
    Color acc = cardAccent(os, t, idx);
    tft_.text(FontId::SansBig, 80 + dx, 49, cardName(os, idx),
              C(mix(bg, scale(acc, glow), alpha), bright), Align::Center);
    tft_.text(FontId::Small, 80 + dx, 61, cardTag(os, idx),
              C(mix(bg, t.c(ROLE_INK), alpha * 0.7f), bright), Align::Center);
}

void TftDashboard::drawSettings(const BrainOS& os, const console::Theme& t, uint8_t bright) {
    using namespace console;
    Color bg = t.c(ROLE_BG);
    tft_.clear(C(bg, bright));
    int row = os.settingsRow();
    const Settings& s = os.settings();

    tft_.text(FontId::SansBold, 8, 10, "SETTINGS", C(t.c(ROLE_ACCENT), bright), Align::Left);
    tft_.text(FontId::Small, 156, 7, t.name, C(t.c(ROLE_DIM), bright), Align::Right);

    static const char* const labels[4] = {"VOLUME", "BRIGHTNESS", "THEME", "MENU MUSIC"};
    for (int i = 0; i < 4; ++i) {
        int y = 26 + i * 12;
        bool on = (i == row);
        if (on) {
            tft_.fillRect(4, y - 6, 152, 12, C(mix(bg, t.c(ROLE_ACCENT), 0.16f), bright));
            tft_.text(FontId::Small, 9, y, ">", C(t.c(ROLE_ACCENT), bright), Align::Left);
        }
        tft_.text(FontId::Small, 18, y, labels[i], C(on ? t.c(ROLE_ACCENT) : t.c(ROLE_INK), bright),
                  Align::Left);
        if (i < 2) {
            int val = (i == 0) ? s.volume : s.brightness;
            const int bx = 94, bw = 54;
            tft_.fillRect(bx, y - 3, bw, 6, C(t.c(ROLE_DIM), bright));
            tft_.fillRect(bx, y - 3, bw * val / 255, 6,
                          C(i == 0 ? t.c(ROLE_ACCENT2) : t.c(ROLE_GOOD), bright));
        } else if (i == 2) {
            int nT = os.themes().count(), active = os.themes().index();
            for (int j = 0; j < nT && j < 6; ++j) {
                const int sw = 10, xx = 94 + j * (sw + 1);
                tft_.fillRect(xx, y - 4, sw, 8, C(os.themes().at(j).c(ROLE_ACCENT), bright));
                if (j == active) tft_.drawRect(xx - 1, y - 5, sw + 2, 10, C(t.c(ROLE_INK), bright));
            }
        } else {
            tft_.text(FontId::Small, 148, y, s.menuMusic ? "ON" : "OFF",
                      C(s.menuMusic ? t.c(ROLE_ACCENT2) : t.c(ROLE_DIM), bright), Align::Right);
        }
    }
    tft_.text(FontId::Small, 80, 75, "KNOB 2 ADJUSTS", C(t.c(ROLE_DIM), bright), Align::Center);
}

void TftDashboard::drawGame(const BrainOS& os, const console::Theme& t, uint8_t bright) {
    using namespace console;
    Color bg = t.c(ROLE_BG);
    tft_.clear(C(bg, bright));
    int gi = os.activeGameIndex();
    const char* nm = (gi >= 0 && gi < os.registry().count()) ? os.registry().at(gi).name : "GAME";
    Color acc = (gi >= 0) ? cardAccent(os, t, gi) : t.c(ROLE_ACCENT);
    uint16_t period = t.motion.blinkMs ? t.motion.blinkMs : 1200;
    uint8_t glow = gfx::breathe(nowMs_, period, 180, 255);

    if (!os.overlayOpen()) {
        tft_.text(FontId::Small, 80, 15, "NOW PLAYING", C(t.c(ROLE_DIM), bright), Align::Center);
        tft_.text(FontId::SansBig, 80, 38, nm, C(scale(acc, glow), bright), Align::Center);
        tft_.fillRect(45, 52, 70, 2, C(scale(acc, glow), bright));
        tft_.text(FontId::Small, 80, 68, "HOLD BOTH KNOBS TO PAUSE", C(t.c(ROLE_DIM), bright),
                  Align::Center);
    } else {
        tft_.text(FontId::SansBold, 80, 14, "PAUSED", C(t.c(ROLE_ACCENT), bright), Align::Center);
        int osel = os.overlaySel();
        for (int i = 0; i < kOverlayOpts; ++i) {
            int y = 32 + i * 12;
            bool on = (i == osel);
            if (on) tft_.fillRect(34, y - 5, 92, 11, C(mix(bg, t.c(ROLE_ACCENT), 0.18f), bright));
            tft_.text(FontId::Small, 80, y, kOverlayLabels[i],
                      C(on ? t.c(ROLE_ACCENT) : t.c(ROLE_INK), bright), Align::Center);
        }
    }
}

}  // namespace console_os

#pragma once
#include <stdint.h>
#include "color.h"

// A Theme is a coherent design-token bundle that restyles the WHOLE console —
// menu, every game, the knob rings, the TFT, and the glow itself. Games read
// tokens by MEANING (theme.c(ROLE_BALL), theme.cat[i]); they never see hex.
// THEMES[] is static flash data shared by both boards + sim + eval.

namespace console {

// Semantic colour roles. APPEND new roles at the end so THEMES[] data stays valid.
enum Role : uint8_t {
    ROLE_BG,
    ROLE_DIM,
    ROLE_ACCENT,
    ROLE_ACCENT2,
    ROLE_INK,       // TFT text
    ROLE_P1,
    ROLE_P2,
    ROLE_BALL,
    ROLE_NET,
    ROLE_FOOD,
    ROLE_HAZARD,
    ROLE_GOOD,
    ROLE_NEUTRAL,
    ROLE_COUNT
};

constexpr int CAT_N  = 8;   // categorical hues (Tetris pieces, 2048 tiles, ...)
constexpr int RAMP_N = 5;   // gradient stops for fire/plasma/VU/trails

// How the screen adapter's light-engine glows & decays. Sent to the screen on
// theme change (a few bytes). This is why "warm" FEELS different from "neon",
// not just looks different.
struct LightProfile {
    uint8_t wallBrightness;  // global LED brightness (respect the power ceiling)
    uint8_t dimLevel;        // the "dim" palette tier
    uint8_t decay;           // trail persistence per tick, 0..255 (0 = none)
    uint8_t bloom;           // glow spread, 0..255
    uint8_t gamma;           // brightness curve, fixed-point /100 (100 = ~1.0)
};

// Timings/easing that shape "feel" — snappy vs smooth.
struct MotionProfile {
    uint16_t transitionMs;   // menu/app transitions
    uint16_t blinkMs;        // cursor/attention blink period
    uint8_t  ease;           // 0 linear, 1 easeOutCubic, 2 spring
};

enum RingStyleId : uint8_t { RING_TRAIL_DOT, RING_SOLID_ARC, RING_PULSE };
enum TftStyleId : uint8_t { TFT_BOLD, TFT_MONO, TFT_SOFT };

struct Theme {
    const char*   name;
    Color         role[ROLE_COUNT];
    Color         cat[CAT_N];
    Color         ramp[RAMP_N];
    LightProfile  light;
    MotionProfile motion;
    RingStyleId   ring;
    TftStyleId    tft;
    // audio: one shared set in v1; a per-theme AudioSet* is a later data add.

    Color    c(Role r) const { return role[r]; }
    uint16_t c565(Role r) const { return to565(role[r]); }
};

// The registry. THEMES[] is defined once (generated from the Figma tokens) and
// shared everywhere; the OS persists the active index to NVS across reboot.
class ThemeManager {
public:
    ThemeManager(const Theme* themes, uint8_t count) : themes_(themes), count_(count) {}

    const Theme& active() const { return themes_[idx_]; }
    uint8_t index() const { return idx_; }
    uint8_t count() const { return count_; }
    const Theme& at(uint8_t i) const { return themes_[i < count_ ? i : 0]; }

    void setActive(uint8_t i) {
        if (i < count_) idx_ = i;
    }
    void next() { idx_ = uint8_t((idx_ + 1) % count_); }
    void prev() { idx_ = uint8_t((idx_ + count_ - 1) % count_); }

private:
    const Theme* themes_;
    uint8_t      count_;
    uint8_t      idx_ = 0;
};

}  // namespace console

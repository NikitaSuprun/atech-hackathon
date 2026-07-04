#pragma once
#include <stdint.h>

// The one true colour type for the whole platform. Games, the OS, the screen
// adapter, and the TFT adapter all speak this — killing the three drifting
// player-colour definitions that exist today (LED rgb888 vs TFT rgb565 vs py).

namespace console {

struct Color {
    uint8_t r, g, b;
};

constexpr Color BLACK{0, 0, 0};
constexpr Color WHITE{255, 255, 255};

// Single source of truth for the TFT adapter: 8-bit RGB -> RGB565.
constexpr uint16_t to565(Color c) {
    return uint16_t(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

// Brightness scale by s/255 (integer math, no float needed on the MCU).
constexpr Color scale(Color c, uint8_t s) {
    return {uint8_t(c.r * s / 255), uint8_t(c.g * s / 255), uint8_t(c.b * s / 255)};
}

// Linear blend a->b, t in 0..255.
constexpr Color lerp(Color a, Color b, uint8_t t) {
    return {uint8_t(a.r + (b.r - a.r) * t / 255), uint8_t(a.g + (b.g - a.g) * t / 255),
            uint8_t(a.b + (b.b - a.b) * t / 255)};
}

}  // namespace console

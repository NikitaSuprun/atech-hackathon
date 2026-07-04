#pragma once
#include <stdint.h>

// Pure contract — the logical wall framebuffer. No Arduino includes allowed here.
// Wall is 6 px wide x 18 px tall (12 NeoPixel 3x3 tiles, 2 tile-cols x 6 tile-rows),
// viewed facing the LEDs: (0,0) = top-left, x grows right, y grows down.
// P2 defends the top edge (y=0), P1 defends the bottom edge (y=H-1).

namespace pong {

constexpr int W = 6;
constexpr int H = 18;

struct Color {
    uint8_t r, g, b;
};

struct Frame {
    Color px[W * H];

    Color& at(int x, int y) { return px[y * W + x]; }
    const Color& at(int x, int y) const { return px[y * W + x]; }

    void clear() {
        for (int i = 0; i < W * H; ++i) px[i] = {0, 0, 0};
    }
};

}  // namespace pong

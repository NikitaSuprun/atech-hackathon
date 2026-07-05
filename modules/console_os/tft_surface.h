#pragma once
#include <stdint.h>

// A tiny, Arduino-free 160x80 RGB565 drawing surface. The TFT dashboard draws
// against THIS, so the rendering logic stays decoupled from — and buildable
// without — the Adafruit driver. Colors are RGB565; the dashboard computes in
// console::Color and converts (to565) at the call site. Kept minimal: only the
// primitives the dashboard actually uses.

namespace console_os {

// Which face to render text in. The device backend maps these to Adafruit GFX
// fonts (classic 5x7 + FreeSansBold 9pt/12pt).
enum class FontId : uint8_t { Small, SansBold, SansBig };

// Horizontal anchoring of text at x. Vertical is always centered on y.
enum class Align : uint8_t { Left, Center, Right };

class Tft {
public:
    virtual ~Tft() {}

    virtual void clear(uint16_t c) = 0;
    virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
    virtual void drawRect(int x, int y, int w, int h, uint16_t c) = 0;
    virtual void fillCircle(int x, int y, int r, uint16_t c) = 0;

    // Text anchored horizontally per `a`, vertically centered on `y`.
    virtual void text(FontId f, int x, int y, const char* s, uint16_t c,
                      Align a = Align::Left) = 0;

    // Flush a completed frame (device: push the canvas to the panel).
    virtual void present() = 0;
};

}  // namespace console_os

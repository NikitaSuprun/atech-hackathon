#pragma once
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include "st7735_tft.h"
#include "tft_surface.h"

// Device backend for the TFT dashboard: adapts the vendored ST7735_TFT driver
// (canvas-mode double buffer) to the Arduino-free Tft interface. Text uses
// Adafruit GFX's crisp 1-bit FreeSansBold faces; textBounds gives us alignment.

namespace console_os {

class St7735Surface : public Tft {
public:
    explicit St7735Surface(ST7735_TFT& tft) : t_(tft) {}

    void clear(uint16_t c) override { t_.fillScreen(c); }
    void fillRect(int x, int y, int w, int h, uint16_t c) override { t_.fillRect(x, y, w, h, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) override { t_.drawRect(x, y, w, h, c); }
    void fillCircle(int x, int y, int r, uint16_t c) override { t_.fillCircle(x, y, r, c); }

    void text(FontId f, int x, int y, const char* s, uint16_t c, Align a) override {
        applyFont(f);
        int16_t bx, by;
        uint16_t bw, bh;
        t_.textBounds(s, &bx, &by, &bw, &bh);
        int left = (a == Align::Left) ? x : (a == Align::Center) ? x - int(bw) / 2 : x - int(bw);
        int top = y - int(bh) / 2;
        t_.setTextColor(c);
        t_.setCursor(left - bx, top - by);  // shift so the glyph box lands at (left,top)
        t_.print(s);
    }

    void present() override { t_.display(); }

private:
    void applyFont(FontId f) {
        t_.setTextSize(1);
        switch (f) {
            case FontId::Small:    t_.setFont(nullptr); break;
            case FontId::SansBold: t_.setFont(&FreeSansBold9pt7b); break;
            case FontId::SansBig:  t_.setFont(&FreeSansBold12pt7b); break;
        }
    }

    ST7735_TFT& t_;
};

}  // namespace console_os

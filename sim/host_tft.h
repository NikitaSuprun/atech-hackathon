#pragma once
#include <stdint.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include "tft_surface.h"

// Host backend for the TFT dashboard — TRUE pixel parity with the device.
//
// The on-device St7735Surface (firmware/brain/src/tft_st7735.h) draws into an
// Adafruit GFXcanvas16 and renders text with Adafruit GFX's drawChar + the
// FreeSansBold faces. HostTft wraps the SAME GFXcanvas16 + SAME fonts and mirrors
// St7735Surface's text() alignment math byte-for-byte, so a host render of the
// TftDashboard matches what the panel shows. present() is a no-op: the caller
// reads buffer() (160x80 RGB565) straight out of the canvas.

namespace console_os {

class HostTft : public Tft {
public:
    HostTft() : cv_(160, 80) {}

    void clear(uint16_t c) override { cv_.fillScreen(c); }
    void fillRect(int x, int y, int w, int h, uint16_t c) override { cv_.fillRect(x, y, w, h, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) override { cv_.drawRect(x, y, w, h, c); }
    void fillCircle(int x, int y, int r, uint16_t c) override { cv_.fillCircle(x, y, r, c); }

    // Byte-for-byte the same as St7735Surface::text (getTextBounds -> left/top ->
    // setCursor(left-bx, top-by) -> setTextColor -> print).
    void text(FontId f, int x, int y, const char* s, uint16_t c, Align a) override {
        applyFont(f);
        int16_t  bx, by;
        uint16_t bw, bh;
        cv_.getTextBounds(s, (int16_t)0, (int16_t)0, &bx, &by, &bw, &bh);
        int left = (a == Align::Left) ? x : (a == Align::Center) ? x - int(bw) / 2 : x - int(bw);
        int top = y - int(bh) / 2;
        cv_.setTextColor(c);
        cv_.setCursor(left - bx, top - by);
        cv_.print(s);
    }

    void present() override {}

    // 160x80 RGB565 framebuffer, native-endian uint16_t (Adafruit GFXcanvas16
    // stores exactly what was written; on device this same buffer is pushed to
    // the panel via drawRGBBitmap).
    const uint16_t* buffer() const { return cv_.getBuffer(); }
    int             width() const { return 160; }
    int             height() const { return 80; }

private:
    void applyFont(FontId f) {
        cv_.setTextSize(1);
        switch (f) {
            case FontId::Small:    cv_.setFont(nullptr); break;
            case FontId::SansBold: cv_.setFont(&FreeSansBold9pt7b); break;
            case FontId::SansBig:  cv_.setFont(&FreeSansBold12pt7b); break;
        }
    }

    GFXcanvas16 cv_;
};

}  // namespace console_os

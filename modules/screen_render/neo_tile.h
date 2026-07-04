#pragma once
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include "screen_render.h"

// Hardware TileSink: drives ONE WS2812 line per 3x3 tile (the live "line A").
// The stock atech NeoPixelGrid drives BOTH line A (WS2812/RGB) and line B
// (SK6812/RGBW) on every show(); line B is dead on this screen, so dropping it
// halves the RMT work per tile (~2x refresh headroom). If a future panel is the
// RGBW variant, point this at line B instead — one constructor argument.
//
// Chip index 0..8 is the compositor's serpentine chip order; brightness is
// clamped to MAX_BRIGHTNESS (20%) exactly like the stock driver.
class NeoTile : public console::TileSink {
public:
    static const uint8_t NUM_LEDS = 9;
    static const uint8_t MAX_BRIGHTNESS = 51;

    explicit NeoTile(int dataPin) : strip_(NUM_LEDS, dataPin, NEO_GRB + NEO_KHZ800) {}

    void begin() {
        strip_.begin();
        strip_.setBrightness(MAX_BRIGHTNESS);
        strip_.clear();
        strip_.show();
    }

    void setPixel(int chip, uint8_t r, uint8_t g, uint8_t b) override {
        if (chip >= 0 && chip < NUM_LEDS) strip_.setPixelColor(chip, r, g, b);
    }
    void show() override { strip_.show(); }
    void setBrightness(uint8_t b) override {
        strip_.setBrightness(b > MAX_BRIGHTNESS ? MAX_BRIGHTNESS : b);
    }

private:
    Adafruit_NeoPixel strip_;
};

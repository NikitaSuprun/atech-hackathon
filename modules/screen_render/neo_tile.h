#pragma once
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include "screen_render.h"

// Hardware TileSink for the SK6812 / RGBW panels, which are wired to each tile's
// "line B" and driven as NEO_GRBW (white channel held at 0). Line A is
// unpopulated on this revision, so we drive line B only: ONE show() per tile,
// which keeps the full 50 Hz refresh headroom. (The stock driver drove line A;
// on these panels that's a dead wire — the wall stayed dark. For a WS2812 /
// line-A panel, point the pin at line A and switch NEO_GRBW -> NEO_GRB.)
//
// Chip index 0..8 is the compositor's serpentine chip order; brightness is
// clamped to MAX_BRIGHTNESS (20%) exactly like the stock driver.
class NeoTile : public console::TileSink {
public:
    static const uint8_t NUM_LEDS = 9;
    static const uint8_t MAX_BRIGHTNESS = 51;

    explicit NeoTile(int dataPinB) : strip_(NUM_LEDS, dataPinB, NEO_GRBW + NEO_KHZ800) {}

    void begin() {
        strip_.begin();
        strip_.setBrightness(MAX_BRIGHTNESS);
        strip_.clear();
        strip_.show();
    }

    void setPixel(int chip, uint8_t r, uint8_t g, uint8_t b) override {
        if (chip >= 0 && chip < NUM_LEDS) strip_.setPixelColor(chip, strip_.Color(r, g, b, 0));
    }
    void show() override { strip_.show(); }
    void setBrightness(uint8_t b) override {
        strip_.setBrightness(b > MAX_BRIGHTNESS ? MAX_BRIGHTNESS : b);
    }

private:
    Adafruit_NeoPixel strip_;
};

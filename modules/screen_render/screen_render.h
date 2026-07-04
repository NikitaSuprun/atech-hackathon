#pragma once
#include <stdint.h>
#include <string.h>
#include "compositor.h"
#include "console/color.h"
#include "console/config.h"
#include "console/theme.h"
#include "light_engine.h"

// The DUMB renderer: it knows nothing about games. It takes a logical
// Color[SCREEN_PX] frame the brain drew, eases the light engine toward it,
// maps logical pixels to physical LEDs by REUSING the pong compositor
// (serpentine + per-tile rotation + dirty-tile cache), and pushes only the
// changed tiles to the LEDs.
//
// It drives LEDs through the abstract TileSink so this whole file is Arduino-
// free and host-testable end to end. The hardware sink (neo_tile.h) drives a
// SINGLE WS2812 line (line A); the dead SK6812 line-B is dropped for ~2x
// show() headroom. A capture sink stands in for the tests.

namespace console {

// One physical 3x3 tile (9 chips, chip index 0..8 in compositor/serpentine order).
struct TileSink {
    virtual void setPixel(int chip, uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void show() = 0;
    virtual void setBrightness(uint8_t b) = 0;
    virtual ~TileSink() {}
};

class ScreenRenderer {
public:
    // tiles[] in wiring/port order (must match TILE_MAP); n should be NUM_TILES.
    void begin(TileSink** tiles, int n, const LightProfile& lp) {
        n_ = n > NUM_TILES ? NUM_TILES : n;
        for (int t = 0; t < n_; ++t) tiles_[t] = tiles[t];
        comp_.begin();
        light_.reset();
        setLightProfile(lp);
        for (int i = 0; i < SCREEN_PX; ++i) target_[i] = BLACK;
    }

    // A theme change swaps the glow/decay feel and the global wall brightness.
    void setLightProfile(const LightProfile& lp) {
        lp_ = lp;
        for (int t = 0; t < n_; ++t)
            if (tiles_[t]) tiles_[t]->setBrightness(lp_.wallBrightness);
    }

    // Adopt a new logical frame and render one glow tick toward it. Call at the
    // console tick rate (TICK_HZ). force=true repaints every tile (state change
    // or the periodic heartbeat that heals WS2812 glitches).
    void renderFrame(const Color target[SCREEN_PX], bool force = false) {
        for (int i = 0; i < SCREEN_PX; ++i) target_[i] = target[i];
        tick(force);
    }

    // Re-ease toward the last frame without a new one (glow keeps settling /
    // trails keep fading when the brain sends frames slower than TICK_HZ).
    void tick(bool force = false) {
        light_.step(target_, lp_, out_);
        pushDirty(force);
    }

    const LightProfile& lightProfile() const { return lp_; }

private:
    void pushDirty(bool force) {
        // console::Color and pong::Color share a packed 3-byte layout, so the
        // eased buffer feeds the pong compositor as-is.
        static_assert(sizeof(Color) == 3, "Color must be packed RGB");
        static_assert(sizeof(phys_.px) == size_t(SCREEN_PX) * 3, "frame size");
        static_assert(pong::W * pong::H == SCREEN_PX, "logical geometry drift");
        memcpy(phys_.px, out_, sizeof(phys_.px));
        uint8_t buf[TILE_BYTES];
        for (int t = 0; t < n_; ++t) {
            TileSink* s = tiles_[t];
            if (!s) continue;
            if (!comp_.composeTile(t, phys_, buf, force)) continue;
            for (int i = 0; i < LEDS_PER_TILE; ++i)
                s->setPixel(i, buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]);
            s->show();
        }
    }

    LightEngine  light_;
    LightProfile lp_ = {};
    Compositor   comp_;
    TileSink*    tiles_[NUM_TILES] = {};
    Color        target_[SCREEN_PX];
    Color        out_[SCREEN_PX];
    pong::Frame  phys_;
    int          n_ = 0;
};

}  // namespace console

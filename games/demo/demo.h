#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Smallest end-to-end console::Game: one ball bounces around the 6x18 field,
// BALL role on BG role. A knob steers horizontal direction; a button press flips
// vertical. Proves the runner, terminal sim, and headless frame-dump end-to-end.

namespace demo {

class DemoGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed);
        x_ = rng_.range(1, console::SCREEN_W - 2);
        y_ = rng_.range(1, console::SCREEN_H - 2);
        vx_ = rng_.chance(0.5f) ? 1 : -1;
        vy_ = rng_.chance(0.5f) ? 1 : -1;
        accMs_ = 0;
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        if (in.knob[0].delta > 0) vx_ = 1;
        else if (in.knob[0].delta < 0) vx_ = -1;
        if (in.knob[0].justPressed || in.knob[1].justPressed) vy_ = -vy_;

        accMs_ += dtMs;
        if (accMs_ < STEP_MS) return;
        accMs_ -= STEP_MS;

        x_ += vx_;
        if (x_ <= 0) { x_ = 0; vx_ = 1; }
        else if (x_ >= console::SCREEN_W - 1) { x_ = console::SCREEN_W - 1; vx_ = -1; }
        y_ += vy_;
        if (y_ <= 0) { y_ = 0; vy_ = 1; }
        else if (y_ >= console::SCREEN_H - 1) { y_ = console::SCREEN_H - 1; vy_ = -1; }
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        c.clear(t.c(console::ROLE_BG));
        c.pixel(x_, y_, t.c(console::ROLE_BALL));
    }

private:
    static constexpr uint32_t STEP_MS = 60;   // ~1 cell / 3 ticks: watchable on the wall

    console::GameMeta meta_{"demo", nullptr, 1};
    sdk::Rng rng_{1};
    int      x_ = 3, y_ = 9, vx_ = 1, vy_ = 1;
    uint32_t accMs_ = 0;
};

}  // namespace demo

#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Flappy, mapped to the 6-wide x 18-tall PORTRAIT matrix in the CLASSIC
// orientation: the bird holds a fixed column near the left and its VERTICAL
// position is governed by gravity in the tall (18-row) axis, which is exactly
// where a flap-to-stay-aloft game wants its room. A flap (either knob button)
// gives an upward impulse. "Pipes" are 1-column vertical barriers with a fixed
// vertical GAP that scroll in from the right across the narrow (6-col) axis,
// giving a short, honest reaction window. Score ticks up per pipe cleared;
// crashing on a pipe or the ground flashes HAZARD, then restarts instantly
// (a tap skips the flash) so the one-more-tap loop never stops.
//
// Sub-cell fixed point (units of 1/256 cell) keeps 50 Hz gravity smooth on a
// screen only 18 rows tall. All randomness comes from ctx.rngSeed and init()
// fully resets state, so runs are bit-for-bit replayable.

namespace flappy {

class FlappyGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed);
        reset();
    }

    void update(const console::Input& in, uint32_t) override {
        bool flap = in.knob[0].justPressed || in.knob[1].justPressed;

        if (state_ == CRASH) {
            // one-more-tap: a flap restarts now; otherwise auto-restart so the
            // world keeps scrolling even with no player (headless dump).
            if (flap || --crashTimer_ <= 0) reset();
            return;
        }

        if (flap) birdVy_ = FLAP_VY;
        birdVy_ += GRAVITY;
        if (birdVy_ > VY_TERM) birdVy_ = VY_TERM;
        birdY_ += birdVy_;
        // the ceiling only bonks (clamp); the ground kills
        if (birdY_ <= 0) { birdY_ = 0; birdVy_ = 0; }
        if (birdY_ >= GROUND_Y) { crash(); return; }

        for (int i = 0; i < pipeCount_; ++i) pipes_[i].x -= SCROLL;

        int br = birdRow();
        for (int i = 0; i < pipeCount_; ++i) {
            int col = roundCol(pipes_[i].x);
            if (col == BIRD_X) {
                if (br < pipes_[i].gapTop || br >= pipes_[i].gapTop + GAP) { crash(); return; }
            } else if (col < BIRD_X && !pipes_[i].scored) {
                pipes_[i].scored = true;
                ++score_;
            }
        }

        // drop pipes that scrolled off the left, compacting the array
        int n = 0;
        for (int i = 0; i < pipeCount_; ++i)
            if (pipes_[i].x > -Y_SCALE) pipes_[n++] = pipes_[i];
        pipeCount_ = n;

        // keep the stream full: once the rightmost pipe passes the trigger, add another
        int maxX = 0;
        bool any = false;
        for (int i = 0; i < pipeCount_; ++i)
            if (!any || pipes_[i].x > maxX) { maxX = pipes_[i].x; any = true; }
        if (!any || maxX <= SPAWN_TRIGGER) spawnPipe(ENTER_X);
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        c.clear(t.c(console::ROLE_BG));

        for (int i = 0; i < pipeCount_; ++i) {
            int col = roundCol(pipes_[i].x);
            if (col < 0 || col >= console::SCREEN_W) continue;
            int gt = pipes_[i].gapTop, gb = gt + GAP - 1;
            for (int y = 0; y < console::SCREEN_H; ++y) {
                // leave the passable gap empty
                if (y >= gt && y <= gb) continue;
                // the two pipe cells framing the gap glow GOOD to read as an opening
                bool edge = (y == gt - 1) || (y == gb + 1);
                c.pixel(col, y, t.c(edge ? console::ROLE_GOOD : console::ROLE_NEUTRAL));
            }
        }

        int br = birdRow();
        if (state_ == CRASH) {
            // blink a HAZARD frame, mark the wreck, print the score
            if ((crashTimer_ / 3) % 2 == 0)
                c.rect(0, 0, console::SCREEN_W, console::SCREEN_H, t.c(console::ROLE_HAZARD));
            c.pixel(BIRD_X, br, t.c(console::ROLE_HAZARD));
            drawScore(c, t.c(console::ROLE_INK));
        } else {
            c.pixel(BIRD_X, br, t.c(console::ROLE_P1));
        }
    }

private:
    enum State : uint8_t { PLAY, CRASH };
    struct Pipe { int x; int gapTop; bool scored; };

    // Tuning. Positions are fixed point (1 cell = Y_SCALE units) stepped once per
    // 20 ms tick: gravity/flap act on the tall axis, SCROLL on the narrow one.
    static constexpr int Y_SCALE       = 256;
    static constexpr int BIRD_X        = 1;
    static constexpr int GROUND_Y      = (console::SCREEN_H - 1) * Y_SCALE;
    static constexpr int SPAWN_Y       = 7 * Y_SCALE;
    static constexpr int GRAVITY       = 6;
    static constexpr int FLAP_VY       = -90;
    static constexpr int VY_TERM       = 130;
    static constexpr int SCROLL        = 24;
    static constexpr int GAP           = 6;
    static constexpr int GAP_DRIFT     = 3;
    static constexpr int START_X       = 5 * Y_SCALE;
    static constexpr int ENTER_X       = console::SCREEN_W * Y_SCALE;
    static constexpr int SPAWN_TRIGGER = 3 * Y_SCALE;
    static constexpr int MAX_PIPES     = 4;
    static constexpr int CRASH_TICKS   = 18;

    void reset() {
        birdY_ = SPAWN_Y;
        birdVy_ = 0;
        score_ = 0;
        state_ = PLAY;
        crashTimer_ = 0;
        pipeCount_ = 0;
        lastGapTop_ = (console::SCREEN_H - GAP) / 2;
        spawnPipe(START_X);
    }

    void crash() {
        state_ = CRASH;
        crashTimer_ = CRASH_TICKS;
    }

    void spawnPipe(int x) {
        if (pipeCount_ >= MAX_PIPES) return;
        Pipe& p = pipes_[pipeCount_++];
        p.x = x;
        // wander the gap gently from the last one so runs flow instead of teleport
        int lo = 1, hi = console::SCREEN_H - GAP - 2;
        int gt = lastGapTop_ + rng_.range(-GAP_DRIFT, GAP_DRIFT);
        p.gapTop = gt < lo ? lo : (gt > hi ? hi : gt);
        lastGapTop_ = p.gapTop;
        p.scored = false;
    }

    static int roundCol(int xmc) { return (xmc + Y_SCALE / 2) / Y_SCALE; }

    int birdRow() const {
        int r = (birdY_ + Y_SCALE / 2) / Y_SCALE;
        return r < 0 ? 0 : (r > console::SCREEN_H - 1 ? console::SCREEN_H - 1 : r);
    }

    void drawScore(console::Canvas& c, console::Color fg) const {
        uint32_t s = score_;
        int digits = 1;
        while (s >= 10) { s /= 10; ++digits; }
        int w = digits * sdk::GLYPH_ADVANCE - 1;
        int sx = (console::SCREEN_W - w) / 2;
        if (sx < 0) sx = 0;
        sdk::drawNumber(c, sx, 2, score_, fg);
    }

    console::GameMeta meta_{"flappy", nullptr, 1};
    sdk::Rng rng_{1};
    Pipe     pipes_[MAX_PIPES] = {};
    int      pipeCount_ = 0;
    int      birdY_ = SPAWN_Y;
    int      birdVy_ = 0;
    int      lastGapTop_ = 5;
    uint32_t score_ = 0;
    State    state_ = PLAY;
    int      crashTimer_ = 0;
};

}  // namespace flappy

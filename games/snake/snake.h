#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Classic Snake on the 6x18 field. The knob is a RELATIVE rudder: a CW detent
// (delta>0) turns right of the current heading, CCW turns left; the knob button
// pauses while alive and restarts from the death screen. Eating FOOD grows the
// snake, respawns food at a random free cell, and nudges the step rate faster.
// Hitting a wall or itself flashes HAZARD, then resets. Deterministic: every
// random draw comes from the seeded sdk::Rng, so a fixed seed replays identically.

namespace snake {

class SnakeGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed);
        reset();
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        const console::Knob& k = in.knob[0];

        if (k.justPressed) {
            if (dead_) { reset(); return; }
            paused_ = !paused_;
        }
        if (paused_) return;

        if (dead_) {
            flashMs_ += dtMs;
            if (flashMs_ >= FLASH_MS) reset();
            return;
        }

        applyTurn(k.delta);

        accMs_ += dtMs;
        if (accMs_ < stepMs_) return;
        accMs_ -= stepMs_;
        step();
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        // death: blink the whole field HAZARD/BG until reset
        if (dead_) {
            bool on = (flashMs_ / FLASH_BLINK_MS) % 2 == 0;
            c.clear(on ? t.c(console::ROLE_HAZARD) : t.c(console::ROLE_BG));
            return;
        }
        c.clear(t.c(console::ROLE_BG));
        c.pixel(food_.x, food_.y, t.c(console::ROLE_FOOD));
        for (int i = 0; i < len_; ++i) {
            console::Role role = (i == 0) ? console::ROLE_ACCENT : console::ROLE_P1;
            c.pixel(body_[i].x, body_[i].y, t.c(role));
        }
    }

private:
    struct Cell { int8_t x, y; };

    static constexpr int      START_LEN       = 3;
    static constexpr uint32_t BASE_STEP_MS    = 160;
    static constexpr uint32_t STEP_RAMP_MS    = 6;
    static constexpr uint32_t MIN_STEP_MS     = 80;
    static constexpr uint32_t FLASH_MS        = 500;
    static constexpr uint32_t FLASH_BLINK_MS  = 100;

    // 90-degree turns in screen coords (y grows downward)
    static Cell rotCW(Cell d)  { return {(int8_t)(-d.y), (int8_t)(d.x)}; }
    static Cell rotCCW(Cell d) { return {(int8_t)(d.y), (int8_t)(-d.x)}; }
    static bool same(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }

    void reset() {
        paused_ = false;
        dead_ = false;
        flashMs_ = 0;
        accMs_ = 0;
        score_ = 0;
        len_ = START_LEN;
        // start mid-field heading up, tail trailing downward
        for (int i = 0; i < len_; ++i)
            body_[i] = {(int8_t)(console::SCREEN_W / 2), (int8_t)(console::SCREEN_H / 2 + i)};
        dir_ = {0, -1};
        nextDir_ = dir_;
        stepMs_ = BASE_STEP_MS;
        spawnFood();
    }

    // One quarter-turn per detent, right of heading for CW. Never accept the exact
    // reverse of the committed heading, so a fast spin can't fold the snake onto its neck.
    void applyTurn(int32_t delta) {
        Cell d = nextDir_;
        for (int i = 0; i < delta; ++i) d = rotCW(d);
        for (int i = 0; i > delta; --i) d = rotCCW(d);
        if (len_ > 1 && d.x == -dir_.x && d.y == -dir_.y) return;
        nextDir_ = d;
    }

    void step() {
        dir_ = nextDir_;
        Cell head = {(int8_t)(body_[0].x + dir_.x), (int8_t)(body_[0].y + dir_.y)};

        if (head.x < 0 || head.x >= console::SCREEN_W ||
            head.y < 0 || head.y >= console::SCREEN_H) { die(); return; }

        bool eating = same(head, food_);
        // the tail vacates its cell this step unless we grow into it
        int tail = len_ - 1;
        for (int i = 0; i < len_; ++i) {
            if (i == tail && !eating) continue;
            if (same(head, body_[i])) { die(); return; }
        }

        if (eating && len_ < console::SCREEN_PX) len_++;
        for (int i = len_ - 1; i > 0; --i) body_[i] = body_[i - 1];
        body_[0] = head;

        if (eating) {
            score_++;
            int s = (int)BASE_STEP_MS - (len_ - START_LEN) * (int)STEP_RAMP_MS;
            stepMs_ = (uint32_t)sdk::clampi(s, (int)MIN_STEP_MS, (int)BASE_STEP_MS);
            spawnFood();
        }
    }

    void die() {
        dead_ = true;
        flashMs_ = 0;
    }

    // Uniformly pick one of the free cells; a full board means the snake won, so restart.
    void spawnFood() {
        int free = console::SCREEN_PX - len_;
        if (free <= 0) { reset(); return; }
        int pick = (int)rng_.below((uint32_t)free);
        for (int y = 0; y < console::SCREEN_H; ++y)
            for (int x = 0; x < console::SCREEN_W; ++x) {
                Cell c = {(int8_t)x, (int8_t)y};
                bool occupied = false;
                for (int i = 0; i < len_; ++i)
                    if (same(c, body_[i])) { occupied = true; break; }
                if (occupied) continue;
                if (pick-- == 0) { food_ = c; return; }
            }
    }

    console::GameMeta meta_{"snake", nullptr, 1};
    sdk::Rng rng_{1};
    Cell     body_[console::SCREEN_PX];
    int      len_ = START_LEN;
    Cell     dir_{0, -1};
    Cell     nextDir_{0, -1};
    Cell     food_{0, 0};
    uint32_t accMs_ = 0;
    uint32_t stepMs_ = BASE_STEP_MS;
    uint32_t flashMs_ = 0;
    uint32_t score_ = 0;
    bool     dead_ = false;
    bool     paused_ = false;
};

}  // namespace snake

#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Egg-catch ("Nu, pogodi!" / Elektronika IM-02) on the 6x18 portrait grid.
// Four chutes feed two left and two right catch points; the wolf's basket
// auto-catches an egg the instant it rests on that landing cell. A dropped egg
// is a miss (1 penalty, or 1/2 while the blinking hare bonus is up); 3 penalties
// end the game. Game A runs 3 chutes, Game B all 4. Speed ramps with catches and
// eases briefly each 100 points; score wraps at 999.
//
// Controls:
//   knob[0] rotate  -> basket LEFT / RIGHT
//   knob[1] rotate  -> basket UP (positive) / DOWN (negative)
//   knob[0] press   -> start Game A (3 chutes)
//   knob[1] press   -> start Game B (4 chutes)
// With no input the game self-plays an attract demo (eggs fall, basket catches).

namespace eggcatch {

class EggCatchGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed ? ctx.rngSeed : 0xE66C47Cu);
        startAttract();
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        blinkClock_ += dtMs;
        switch (state_) {
            case ATTRACT:  updateAttract(in, dtMs); break;
            case PLAY:     updatePlay(in, dtMs);    break;
            case GAMEOVER: updateGameOver(in, dtMs); break;
        }
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        c.clear(t.c(console::ROLE_BG));
        if (state_ == GAMEOVER) { drawGameOver(c, t); return; }

        // faint markers for the live catch points (targets)
        for (int l = 0; l < N_LANES; ++l)
            if (activeLane(l)) c.pixel(laneX(l), laneY(l, CATCH), t.c(console::ROLE_DIM));

        drawWolf(c, t);

        for (int l = 0; l < N_LANES; ++l)
            if (eggActive_[l]) c.pixel(laneX(l), laneY(l, eggStep_[l]), t.c(console::ROLE_GOOD));

        for (int i = 0; i < FLASH_N; ++i)
            if (flashes_[i].t)
                c.pixel(flashes_[i].x, flashes_[i].y,
                        t.c(flashes_[i].kind == FK_CATCH ? console::ROLE_ACCENT : console::ROLE_HAZARD));

        if (hareActive_ && blinkOn()) drawHare(c, t);
        drawPips(c, t);
    }

private:
    enum State { ATTRACT, PLAY, GAMEOVER };
    enum FlashKind { FK_CATCH, FK_MISS };

    static constexpr int N_LANES = 4;
    static constexpr int N_STEPS = 5;
    static constexpr int CATCH   = N_STEPS - 1;

    static constexpr uint32_t FALL_MAX = 440, FALL_MIN = 170;
    static constexpr uint32_t SPAWN_MAX = 740, SPAWN_MIN = 380;
    static constexpr int      RAMP = 120;
    static constexpr uint32_t EASE_MS = 1400;
    static constexpr uint32_t BLINK_MS = 250;
    static constexpr uint32_t HARE_PERIOD = 4200, HARE_SHOW = 2200;
    static constexpr uint32_t GAMEOVER_MS = 4500;
    static constexpr uint32_t SCROLL_PXMS = 90;
    static constexpr int      FLASH_N = 4;
    static constexpr uint32_t FLASH_MS = 220;
    static constexpr int      OVER_HALVES = 6;

    // lane order: 0 = left-up, 1 = left-down, 2 = right-up, 3 = right-down.
    // outer columns (1,4) land high; inner columns (2,3) land low.
    static int laneX(int l) {
        static const uint8_t X[N_LANES] = {1, 2, 4, 3};
        return X[l];
    }
    static int laneY(int l, int s) {
        static const uint8_t Y[N_LANES][N_STEPS] = {
            {1, 4, 7, 10, 13},
            {3, 6, 9, 12, 15},
            {1, 4, 7, 10, 13},
            {3, 6, 9, 12, 15},
        };
        return Y[l][s];
    }
    static int iabs(int v) { return v < 0 ? -v : v; }

    bool activeLane(int l) const { return l < gameLanes_; }
    int  basketLane() const { return side_ * 2 + (up_ ? 0 : 1); }
    void setBasketLane(int l) { side_ = l >= 2 ? 1 : 0; up_ = (l % 2 == 0); }
    bool over() const { return penaltyHalves_ >= OVER_HALVES; }
    bool blinkOn() const { return (blinkClock_ / BLINK_MS) % 2 == 0; }

    void startAttract() {
        state_ = ATTRACT;
        demo_ = true;
        gameLanes_ = N_LANES;
        resetRound();
    }
    void startGame(bool gameB) {
        state_ = PLAY;
        demo_ = false;
        gameLanes_ = gameB ? 4 : 3;
        resetRound();
    }
    void resetRound() {
        for (int l = 0; l < N_LANES; ++l) { eggActive_[l] = false; eggStep_[l] = 0; }
        for (int i = 0; i < FLASH_N; ++i) flashes_[i] = {};
        score_ = 0; lastHundred_ = 0; catches_ = 0; penaltyHalves_ = 0;
        fallAccum_ = 0; spawnAccum_ = SPAWN_MAX; easeTimer_ = 0;
        hareActive_ = false; hareTimer_ = HARE_PERIOD - 1000;
        overClock_ = 0; scrollClock_ = 0;
        side_ = 0; up_ = false;
    }

    void updateAttract(const console::Input& in, uint32_t dt) {
        if (in.knob[0].justPressed) { startGame(false); return; }
        if (in.knob[1].justPressed) { startGame(true); return; }
        aiControl();
        stepWorld(dt);
        if (over()) startAttract();
    }
    void updatePlay(const console::Input& in, uint32_t dt) {
        if (in.knob[0].delta > 0) side_ = 1;
        else if (in.knob[0].delta < 0) side_ = 0;
        if (in.knob[1].delta > 0) up_ = true;
        else if (in.knob[1].delta < 0) up_ = false;
        stepWorld(dt);
        if (over()) { state_ = GAMEOVER; overClock_ = 0; scrollClock_ = 0; }
    }
    void updateGameOver(const console::Input& in, uint32_t dt) {
        overClock_ += dt; scrollClock_ += dt;
        if (in.knob[0].justPressed) { startGame(false); return; }
        if (in.knob[1].justPressed) { startGame(true); return; }
        if (overClock_ >= GAMEOVER_MS) startAttract();
    }

    // steer the demo basket onto the egg nearest to landing
    void aiControl() {
        int best = -1, bestStep = -1;
        for (int l = 0; l < N_LANES; ++l)
            if (eggActive_[l] && eggStep_[l] > bestStep) { bestStep = eggStep_[l]; best = l; }
        if (best >= 0) setBasketLane(best);
    }

    void stepWorld(uint32_t dt) {
        updateHare(dt);
        easeTimer_ = easeTimer_ > dt ? easeTimer_ - dt : 0;
        for (int i = 0; i < FLASH_N; ++i)
            flashes_[i].t = flashes_[i].t > dt ? flashes_[i].t - dt : 0;

        // catch first so an egg resting on the basket is taken, never dropped
        int bl = basketLane();
        for (int l = 0; l < N_LANES; ++l)
            if (eggActive_[l] && eggStep_[l] == CATCH && l == bl) catchEgg(l);

        fallAccum_ += dt;
        uint32_t fm = fallMs();
        while (fallAccum_ >= fm) { fallAccum_ -= fm; fallStep(); fm = fallMs(); }

        spawnAccum_ += dt;
        if (spawnAccum_ >= spawnMs() && trySpawn()) spawnAccum_ = 0;
    }

    void updateHare(uint32_t dt) {
        hareTimer_ += dt;
        if (!hareActive_ && hareTimer_ >= HARE_PERIOD) { hareActive_ = true; hareTimer_ = 0; }
        else if (hareActive_ && hareTimer_ >= HARE_SHOW) { hareActive_ = false; hareTimer_ = 0; }
    }

    void fallStep() {
        for (int l = 0; l < N_LANES; ++l)
            if (eggActive_[l] && ++eggStep_[l] >= N_STEPS) missEgg(l);
    }

    void catchEgg(int l) {
        eggActive_[l] = false;
        catches_++;
        score_ = (score_ + 1) % 1000;
        int h = score_ / 100;
        if (h != lastHundred_) { lastHundred_ = h; easeTimer_ = EASE_MS; }
        addFlash(laneX(l), laneY(l, CATCH), FK_CATCH);
    }
    void missEgg(int l) {
        eggActive_[l] = false;
        if (hareActive_) { penaltyHalves_ += 1; hareActive_ = false; hareTimer_ = 0; }
        else penaltyHalves_ += 2;
        int y = laneY(l, CATCH) + 1;
        if (y >= console::SCREEN_H) y = console::SCREEN_H - 1;
        addFlash(laneX(l), y, FK_MISS);
    }

    // one egg per lane, and never two at step 0, so landings stay staggered
    bool trySpawn() {
        int freeLanes[N_LANES], n = 0;
        for (int l = 0; l < N_LANES; ++l) {
            if (eggActive_[l] && eggStep_[l] == 0) return false;
            if (activeLane(l) && !eggActive_[l]) freeLanes[n++] = l;
        }
        if (n == 0) return false;
        int pick = freeLanes[rng_.below((uint32_t)n)];
        eggActive_[pick] = true; eggStep_[pick] = 0;
        return true;
    }

    uint32_t fallMs() const {
        int d = catches_ < RAMP ? catches_ : RAMP;
        uint32_t f = FALL_MAX - (FALL_MAX - FALL_MIN) * (uint32_t)d / RAMP;
        return easeTimer_ ? f + f / 2 : f;
    }
    uint32_t spawnMs() const {
        int d = catches_ < RAMP ? catches_ : RAMP;
        return SPAWN_MAX - (SPAWN_MAX - SPAWN_MIN) * (uint32_t)d / RAMP;
    }

    void addFlash(int x, int y, FlashKind kind) {
        int slot = 0;
        for (int i = 1; i < FLASH_N; ++i)
            if (flashes_[i].t < flashes_[slot].t) slot = i;
        flashes_[slot] = {x, y, FLASH_MS, kind};
    }

    static void plotLine(console::Canvas& c, int x0, int y0, int x1, int y1, console::Color col) {
        int dx = iabs(x1 - x0), dy = -iabs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            c.pixel(x0, y0, col);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void drawWolf(console::Canvas& c, const console::Theme& t) {
        console::Color p1 = t.c(console::ROLE_P1);
        int bx = laneX(basketLane()), by = laneY(basketLane(), CATCH);
        int hx = side_ ? 3 : 2, hy = 16;
        plotLine(c, hx, hy, bx, by, t.c(console::ROLE_DIM));
        c.pixel(2, 17, p1); c.pixel(3, 17, p1);
        c.pixel(hx, hy, p1);
        c.pixel(bx, by, p1);
        if (by + 1 < console::SCREEN_H) c.pixel(bx, by + 1, p1);
    }

    void drawHare(console::Canvas& c, const console::Theme& t) {
        console::Color a2 = t.c(console::ROLE_ACCENT2);
        c.pixel(4, 0, a2); c.pixel(5, 0, a2);
    }

    void drawPips(console::Canvas& c, const console::Theme& t) {
        console::Color hz = t.c(console::ROLE_HAZARD), dim = t.c(console::ROLE_DIM);
        for (int i = 0; i < 3; ++i) {
            int full = (i + 1) * 2;
            if (penaltyHalves_ >= full) c.pixel(i, 0, hz);
            else if (penaltyHalves_ == full - 1) c.pixel(i, 0, blinkOn() ? hz : dim);
            else c.pixel(i, 0, dim);
        }
    }

    void drawGameOver(console::Canvas& c, const console::Theme& t) {
        drawPips(c, t);
        char buf[8];
        int n = score_, i = 0, tmp[8], j = 0;
        if (n == 0) buf[i++] = '0';
        while (n) { tmp[j++] = n % 10; n /= 10; }
        while (j) buf[i++] = char('0' + tmp[--j]);
        buf[i] = 0;
        int w = sdk::textWidth(buf);
        int cycle = console::SCREEN_W + w + 2;
        int px = (int)((scrollClock_ / SCROLL_PXMS) % (uint32_t)cycle);
        int x = console::SCREEN_W - px;
        sdk::drawText(c, x, 6, buf, t.c(blinkOn() ? console::ROLE_ACCENT2 : console::ROLE_ACCENT));
    }

    console::GameMeta meta_{"eggcatch", nullptr, 1};
    sdk::Rng rng_{0xE66C47Cu};

    State state_ = ATTRACT;
    bool  demo_ = true;
    int   gameLanes_ = N_LANES;

    bool eggActive_[N_LANES] = {};
    int  eggStep_[N_LANES] = {};

    int score_ = 0, lastHundred_ = 0, catches_ = 0, penaltyHalves_ = 0;
    uint32_t fallAccum_ = 0, spawnAccum_ = 0, easeTimer_ = 0;
    uint32_t hareTimer_ = 0;
    bool     hareActive_ = false;
    uint32_t blinkClock_ = 0;

    int  side_ = 0;
    bool up_ = false;

    struct Flash { int x, y; uint32_t t; FlashKind kind; };
    Flash flashes_[FLASH_N] = {};

    uint32_t overClock_ = 0, scrollClock_ = 0;
};

}  // namespace eggcatch

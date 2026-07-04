#pragma once
#include <stdint.h>
#include <math.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Ambient: the console's always-on, zero-player "desk object" mode. A pack of
// generative effects auto-cycles on the 6x18 grid, each painted ONLY from the
// active theme's ramp[]/cat[]/roles (no hex). It stays alive with no input (so it
// doubles as a headless idle screen); knob[0] rotation hand-picks an effect and a
// knob[0] press reseeds the current one.

namespace ambient {

class AmbientGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed ? ctx.rngSeed : 0xC0FFEEu);
        buildSinLut();
        effect_ = E_LIFE;
        effectMs_ = 0;
        timeMs_ = 0;
        clockMs_ = CLOCK_START_MS;
        reseed();
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        timeMs_ += dtMs;
        clockMs_ += dtMs;

        // knob rotation hand-picks the next/prev effect
        if (in.knob[0].delta > 0) setEffect((effect_ + 1) % EFFECT_COUNT);
        else if (in.knob[0].delta < 0) setEffect((effect_ + EFFECT_COUNT - 1) % EFFECT_COUNT);
        // a press reseeds the current effect's generative content
        if (in.knob[0].justPressed) reseed();

        // hands-free auto-advance so it keeps evolving as an idle screen
        effectMs_ += dtMs;
        if (effectMs_ >= EFFECT_MS) setEffect((effect_ + 1) % EFFECT_COUNT);

        advance(dtMs);
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        switch (effect_) {
            case E_LIFE:   drawLife(c, t); break;
            case E_FIRE:   drawFire(c, t); break;
            case E_PLASMA: drawPlasma(c, t); break;
            case E_MATRIX: drawMatrix(c, t); break;
            case E_CLOCK:  drawClock(c, t); break;
            default: break;
        }
    }

private:
    static constexpr int W = console::SCREEN_W;
    static constexpr int H = console::SCREEN_H;

    enum Effect : uint8_t { E_LIFE, E_FIRE, E_PLASMA, E_MATRIX, E_CLOCK, EFFECT_COUNT };

    // timing / tuning
    static constexpr uint32_t EFFECT_MS      = 15000;
    static constexpr uint32_t LIFE_STEP_MS   = 130;
    static constexpr float    LIFE_DENSITY   = 0.35f;
    static constexpr int      LIFE_STALL     = 4;
    static constexpr int      LIFE_MAX_GEN   = 180;
    static constexpr int      AGE_MAX        = 8;
    static constexpr uint32_t FIRE_STEP_MS   = 45;
    static constexpr int      FIRE_SRC_MIN   = 170;
    static constexpr int      FIRE_COOL      = 30;
    static constexpr int      MAT_SPD_MIN    = 70;
    static constexpr int      MAT_SPD_MAX    = 170;
    static constexpr int      MAT_TRAIL_MIN  = 3;
    static constexpr int      MAT_TRAIL_MAX  = 9;
    static constexpr int      MAT_GAP_MAX    = 9;
    static constexpr uint32_t MAT_FLICK_MS   = 90;
    static constexpr uint32_t CLOCK_START_MS = (10u * 3600u + 30u * 60u) * 1000u;
    static constexpr int      CLOCK_HR_Y     = 2;
    static constexpr int      CLOCK_SEP_Y    = 8;
    static constexpr int      CLOCK_MIN_Y    = 10;

    // ---------- shared helpers ----------

    static int iabs(int v) { return v < 0 ? -v : v; }

    void buildSinLut() {
        for (int i = 0; i < 256; ++i)
            sinLut_[i] = (int16_t)(sinf((float)i * 6.2831853f / 256.0f) * 127.0f);
    }
    // -127..127 sine; angle wraps mod 256
    int isin(int a) const { return sinLut_[(uint8_t)a]; }

    // map f in 0..255 across the 5 ramp stops with linear interpolation
    console::Color rampColor(const console::Theme& t, int f) const {
        if (f < 0) f = 0;
        if (f > 255) f = 255;
        const int seg = console::RAMP_N - 1;
        int scaled = f * seg;
        int i = scaled / 255;
        if (i >= seg) return t.ramp[seg];
        return console::lerp(t.ramp[i], t.ramp[i + 1], (uint8_t)(scaled - i * 255));
    }

    void setEffect(int e) {
        effect_ = (uint8_t)e;
        effectMs_ = 0;
        reseed();
    }
    void reseed() {
        stepAcc_ = 0;
        switch (effect_) {
            case E_LIFE:   seedLife(); break;
            case E_FIRE:   seedFire(); break;
            case E_PLASMA: seedPlasma(); break;
            case E_MATRIX: seedMatrix(); break;
            default: break;
        }
    }
    void advance(uint32_t dtMs) {
        switch (effect_) {
            case E_LIFE:   stepLife(dtMs); break;
            case E_FIRE:   stepFire(dtMs); break;
            case E_MATRIX: stepMatrix(dtMs); break;
            default: break;
        }
    }

    // ---------- Conway's Game of Life (wrap edges; reseed on stall) ----------

    void seedLife() {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint8_t v = rng_.chance(LIFE_DENSITY) ? 1 : 0;
                life_[y][x] = v;
                age_[y][x] = v;
            }
        lifeStale_ = 0;
        lifeGens_ = 0;
        lifeHash_ = 0;
        lifeHash2_ = 1;
    }
    int lifeNeighbors(int x, int y) const {
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                n += life_[(y + dy + H) % H][(x + dx + W) % W];
            }
        return n;
    }
    void lifeGen() {
        uint8_t nxt[H][W];
        int pop = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int n = lifeNeighbors(x, y);
                bool live = life_[y][x] ? (n == 2 || n == 3) : (n == 3);
                nxt[y][x] = live ? 1 : 0;
                pop += live ? 1 : 0;
            }
        // commit next state, age survivors, fold a hash for stall detection
        uint32_t h = 2166136261u;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint8_t v = nxt[y][x];
                age_[y][x] = v ? (age_[y][x] < AGE_MAX ? (uint8_t)(age_[y][x] + 1) : AGE_MAX) : 0;
                life_[y][x] = v;
                h = (h ^ v) * 16777619u;
            }
        ++lifeGens_;
        // stuck = extinct, still-life (== last), or period-2 oscillator (== two ago)
        bool stuck = (pop == 0) || (h == lifeHash_) || (h == lifeHash2_);
        lifeStale_ = stuck ? lifeStale_ + 1 : 0;
        lifeHash2_ = lifeHash_;
        lifeHash_ = h;
        if (pop == 0 || lifeStale_ >= LIFE_STALL || lifeGens_ >= LIFE_MAX_GEN) seedLife();
    }
    void stepLife(uint32_t dtMs) {
        stepAcc_ += dtMs;
        while (stepAcc_ >= LIFE_STEP_MS) {
            stepAcc_ -= LIFE_STEP_MS;
            lifeGen();
        }
    }
    void drawLife(console::Canvas& c, const console::Theme& t) {
        c.clear(t.c(console::ROLE_BG));
        // newborn cells flash at the bright ramp top, then settle toward mid-ramp
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (life_[y][x]) {
                    int a = age_[y][x];
                    int f = a <= 1 ? 255 : 255 - a * 105 / AGE_MAX;
                    c.pixel(x, y, rampColor(t, f));
                }
    }

    // ---------- Fire (hot bottom source, cool + drift upward) ----------

    void seedFire() {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) heat_[y][x] = 0;
        for (int x = 0; x < W; ++x) heat_[H - 1][x] = (uint8_t)rng_.range(FIRE_SRC_MIN, 255);
    }
    void fireGen() {
        for (int x = 0; x < W; ++x) heat_[H - 1][x] = (uint8_t)rng_.range(FIRE_SRC_MIN, 255);
        // each cell decays from the (slightly drifted) cell below it
        for (int y = H - 2; y >= 0; --y)
            for (int x = 0; x < W; ++x) {
                int rx = x + rng_.range(-1, 1);
                rx = rx < 0 ? 0 : (rx >= W ? W - 1 : rx);
                int v = (int)heat_[y + 1][rx] - (int)rng_.below(FIRE_COOL + 1);
                heat_[y][x] = (uint8_t)(v < 0 ? 0 : v);
            }
    }
    void stepFire(uint32_t dtMs) {
        stepAcc_ += dtMs;
        while (stepAcc_ >= FIRE_STEP_MS) {
            stepAcc_ -= FIRE_STEP_MS;
            fireGen();
        }
    }
    void drawFire(console::Canvas& c, const console::Theme& t) {
        c.clear(t.c(console::ROLE_BG));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (heat_[y][x]) c.pixel(x, y, rampColor(t, heat_[y][x]));
    }

    // ---------- Plasma (summed sines, ramp-mapped) ----------

    void seedPlasma() { plasmaPhase_ = (uint8_t)rng_.next(); }
    void drawPlasma(console::Canvas& c, const console::Theme& t) {
        uint8_t p = (uint8_t)((timeMs_ >> 4) + plasmaPhase_);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int r = iabs(x * 2 - (W - 1)) + iabs(y * 2 - (H - 1));
                int s = isin(x * 40 + p) + isin(y * 28 - p) +
                        isin((x + y) * 24 + p * 2) + isin(r * 12 - p);
                c.pixel(x, y, rampColor(t, (s + 512) >> 2));
            }
    }

    // ---------- Matrix rain (falling streaks in cat colours) ----------

    void respawnCol(int x, bool initial) {
        dropCat_[x] = (uint8_t)rng_.below(console::CAT_N);
        dropLen_[x] = (uint8_t)rng_.range(MAT_TRAIL_MIN, MAT_TRAIL_MAX);
        colSpeed_[x] = (uint16_t)rng_.range(MAT_SPD_MIN, MAT_SPD_MAX);
        colAcc_[x] = 0;
        dropY_[x] = initial ? -(int)rng_.below(H) : -(int)rng_.range(0, MAT_GAP_MAX);
    }
    void seedMatrix() {
        for (int x = 0; x < W; ++x) respawnCol(x, true);
    }
    void stepMatrix(uint32_t dtMs) {
        for (int x = 0; x < W; ++x) {
            colAcc_[x] += dtMs;
            while (colAcc_[x] >= colSpeed_[x]) {
                colAcc_[x] -= colSpeed_[x];
                if (++dropY_[x] - (int)dropLen_[x] >= H) respawnCol(x, false);
            }
        }
    }
    static uint32_t hash3(int x, int y, uint32_t z) {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + z * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
    void drawMatrix(console::Canvas& c, const console::Theme& t) {
        console::Color bg = t.c(console::ROLE_BG);
        c.clear(bg);
        for (int x = 0; x < W; ++x) {
            console::Color cat = t.cat[dropCat_[x]];
            int len = dropLen_[x];
            for (int k = 0; k < len; ++k) {
                int y = dropY_[x] - k;
                if (y < 0 || y >= H) continue;
                // bright ink head, cat-coloured trail fading to bg with a glyph shimmer
                if (k == 0) {
                    c.pixel(x, y, t.c(console::ROLE_INK));
                    continue;
                }
                int s = 255 - k * 255 / len;
                if ((hash3(x, y, (uint32_t)(timeMs_ / MAT_FLICK_MS)) & 7u) == 0) s = s * 2 / 3;
                c.pixel(x, y, console::lerp(bg, cat, (uint8_t)s));
            }
        }
    }

    // ---------- Pixel clock (fake free-running time; no RTC in sim) ----------

    void drawClock(console::Canvas& c, const console::Theme& t) {
        c.clear(t.c(console::ROLE_BG));
        uint32_t sec = clockMs_ / 1000u;
        int ss = (int)(sec % 60), mm = (int)((sec / 60) % 60), hh = (int)((sec / 3600) % 24);
        console::Color ch = t.c(console::ROLE_ACCENT), cm = t.c(console::ROLE_ACCENT2);
        // two 3x5 digits fill the 6-wide row exactly: HH stacked over MM
        sdk::drawChar(c, 0, CLOCK_HR_Y, (char)('0' + hh / 10), ch);
        sdk::drawChar(c, 3, CLOCK_HR_Y, (char)('0' + hh % 10), ch);
        sdk::drawChar(c, 0, CLOCK_MIN_Y, (char)('0' + mm / 10), cm);
        sdk::drawChar(c, 3, CLOCK_MIN_Y, (char)('0' + mm % 10), cm);
        // separator blinks once a second so the face reads as live
        if (ss & 1) {
            console::Color sep = t.c(console::ROLE_INK);
            c.pixel(1, CLOCK_SEP_Y, sep);
            c.pixel(4, CLOCK_SEP_Y, sep);
        }
        // bottom row sweeps a seconds progress bar
        int filled = ss * W / 60;
        for (int i = 0; i <= filled && i < W; ++i) c.pixel(i, H - 1, rampColor(t, 90 + i * 33));
    }

    // ---------- state ----------

    console::GameMeta meta_{"ambient", nullptr, 1};
    sdk::Rng rng_{1};
    uint8_t  effect_ = E_LIFE;
    uint32_t effectMs_ = 0;
    uint32_t timeMs_ = 0;
    uint32_t clockMs_ = CLOCK_START_MS;
    uint32_t stepAcc_ = 0;
    int16_t  sinLut_[256] = {};

    // life
    uint8_t  life_[H][W] = {};
    uint8_t  age_[H][W] = {};
    uint32_t lifeHash_ = 0, lifeHash2_ = 1;
    int      lifeStale_ = 0, lifeGens_ = 0;
    // fire
    uint8_t  heat_[H][W] = {};
    // plasma
    uint8_t  plasmaPhase_ = 0;
    // matrix
    int      dropY_[W] = {};
    uint8_t  dropLen_[W] = {};
    uint8_t  dropCat_[W] = {};
    uint16_t colSpeed_[W] = {};
    uint32_t colAcc_[W] = {};
};

}  // namespace ambient

#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Doodle Jump on the 6-wide x 18-tall PORTRAIT matrix. The climber AUTO-BOUNCES
// upward every time it LANDS on a platform (never on the way up — you pass through
// platforms while rising and only catch them while falling). knob[0] rotation
// steers LEFT/RIGHT with EDGE-WRAP (leave one side, reappear on the other), which
// pairs with platforms that drift toward the walls. Gravity governs the tall axis
// in 1/256-cell fixed point so 50 Hz motion stays smooth on only 18 rows. The
// camera never descends: it pins the player to the top third and scrolls the world
// DOWN as you climb; drop below the bottom edge and you die, then restart instantly.
// SPRING platforms (ACCENT) fling you ~2x higher; a rare HAZARD monster kills on
// touch but pops if you shoot it (knob[0] press). Score = greatest height reached.
// All randomness is seeded from ctx.rngSeed and init() fully resets, so a fixed
// seed replays bit-for-bit (the deterministic-runner CI gate).

namespace doodlejump {

class DoodleJumpGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed);
        reset();
    }

    void update(const console::Input& in, uint32_t) override {
        bool press = in.knob[0].justPressed || in.knob[1].justPressed;

        if (state_ == CRASH) {
            // instant restart: a press revives now, else auto-restart so the world
            // keeps climbing with no player (the headless dump / attract loop).
            if (press || --crashTimer_ <= 0) reset();
            return;
        }

        // horizontal: one cell per knob detent, wrapping around BOTH edges
        x_ += in.knob[0].delta * UNIT;
        const int span = W * UNIT;
        while (x_ < 0)     x_ += span;
        while (x_ >= span) x_ -= span;
        const int cx = x_ / UNIT;

        // shoot: a single upward pellet, fired on the knob press, pops the monster
        if (in.knob[0].justPressed && !shot_.alive) {
            shot_.alive = true;
            shot_.col = cx;
            shot_.hr = charHr();
        }

        // vertical: gravity, then integrate, capped under 1 cell/tick so a fast
        // fall can never tunnel straight through a platform row
        const int prevY = y_;
        vy_ -= GRAV;
        if (vy_ < -VTERM) vy_ = -VTERM;
        y_ += vy_;

        // land ONLY while descending, catching the platform we cross downward through
        if (vy_ <= 0) {
            for (int i = 0; i < platCount_; ++i) {
                const Plat& p = plats_[i];
                const int top = p.hr * UNIT;
                if (prevY >= top && y_ <= top && cx >= p.x && cx < p.x + p.w) {
                    y_ = top;
                    vy_ = p.spring ? VSPRING : VJUMP;
                    break;
                }
            }
        }

        const int hr = charHr();
        if (hr > maxHr_) maxHr_ = hr;

        // camera only ever climbs, pinning the player to the top third
        const int follow = maxHr_ - (H - 1 - TOP_THIRD);
        if (follow > camBase_) camBase_ = follow;

        // fell past the bottom edge -> dead
        if (hr < camBase_) { crash(); return; }

        // monster contact kills; a landed pellet pops it
        if (mon_.alive && mon_.col == cx && mon_.hr == hr) { crash(); return; }
        updateShot();

        generate();
        prune();
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        c.clear(t.c(console::ROLE_BG));

        for (int i = 0; i < platCount_; ++i) {
            const Plat& p = plats_[i];
            const int row = (H - 1) - (p.hr - camBase_);
            if (row < 0 || row >= H) continue;
            c.hline(p.x, row, p.w, t.c(p.spring ? console::ROLE_ACCENT : console::ROLE_GOOD));
        }
        if (mon_.alive) {
            const int row = (H - 1) - (mon_.hr - camBase_);
            c.pixel(mon_.col, row, t.c(console::ROLE_HAZARD));
        }
        if (shot_.alive) {
            const int row = (H - 1) - (shot_.hr - camBase_);
            c.pixel(shot_.col, row, t.c(console::ROLE_ACCENT2));
        }

        const int cx  = x_ / UNIT;
        const int row = (H - 1) - (fdiv(y_ + UNIT / 2, UNIT) - camBase_);
        if (state_ == CRASH) {
            // blink a HAZARD border, mark the wreck, print the height reached
            if ((crashTimer_ / 3) % 2 == 0) c.rect(0, 0, W, H, t.c(console::ROLE_HAZARD));
            c.pixel(cx, sdk::clampi(row, 0, H - 1), t.c(console::ROLE_HAZARD));
            drawScore(c, t.c(console::ROLE_INK));
        } else {
            c.pixel(cx, row, t.c(console::ROLE_P1));
        }
    }

private:
    enum State : uint8_t { PLAY, CRASH };
    struct Plat { int hr; int8_t x, w; bool spring; };
    struct Mon  { int hr; int8_t col; bool alive; };
    struct Shot { int hr; int8_t col; bool alive; };

    // Positions are fixed point (1 cell = UNIT units), stepped once per 20 ms tick.
    static constexpr int W    = console::SCREEN_W;
    static constexpr int H    = console::SCREEN_H;
    static constexpr int UNIT = 256;

    // Vertical feel: gravity per tick, bounce/spring impulses (~5 and ~10 rows of
    // air), a fall-speed cap kept < UNIT so a fast drop can't tunnel a platform,
    // and the screen row the camera pins the climber to (a third down from the top).
    static constexpr int GRAV      = 12;
    static constexpr int VJUMP     = 176;
    static constexpr int VSPRING   = 250;
    static constexpr int VTERM     = 240;
    static constexpr int TOP_THIRD = 6;

    // World: spawn height (pins the player to the top third at start), spawn/centre
    // columns, platform spacing (kept < jump height so every gap is reachable),
    // how far above the top edge to keep generating, and the platform pool size.
    static constexpr int START_HR   = 11;
    static constexpr int SPAWN_COL  = 2;
    static constexpr int CENTER_COL = 2;
    static constexpr int GAP_MIN    = 2;
    static constexpr int GAP_MAX    = 3;
    static constexpr int GEN_MARGIN = 3;
    static constexpr int MAX_PLAT   = 16;

    // Hazards: spring frequency, the climbed height that unlocks monsters and their
    // spawn rate, pellet speed, and how long the death flash holds before restart.
    static constexpr float SPRING_P    = 0.14f;
    static constexpr int   MONSTER_MIN = 40;
    static constexpr float MONSTER_P   = 0.15f;
    static constexpr int   SHOT_SPEED  = 2;
    static constexpr int   CRASH_TICKS = 15;

    void reset() {
        state_ = PLAY;
        crashTimer_ = 0;
        camBase_ = 0;
        x_ = SPAWN_COL * UNIT;
        y_ = START_HR * UNIT;
        // launch upward immediately so t=0 already bounces
        vy_ = VJUMP;
        maxHr_ = START_HR;
        mon_.alive = false;
        shot_.alive = false;

        // start already pinned in the top third over a full centre-lane staircase
        // (from the floor up to the launch pad at START_HR), so a fall is always
        // caught and the world scrolls DOWN from the very first climb — which is
        // what the headless attract dump must show.
        platCount_ = 0;
        for (int hr = START_HR & 1; hr <= START_HR; hr += 2) addPlat(hr, true);
        topHr_ = START_HR;
        generate();
    }

    void crash() {
        state_ = CRASH;
        crashTimer_ = CRASH_TICKS;
    }

    // place one platform at world height hr. A platform must COVER a chosen column:
    // forceCenter pins it to the centre lane (guaranteed catch for the starting
    // field); otherwise it favours the centre — which keeps a no-steering climb
    // alive so the attract dump scrolls, and the staircase reachable on a 6-wide
    // floor — but sometimes wanders to a wall lane (reachable via edge-wrap) for
    // variety, and may carry a SPRING or spawn the monster.
    void addPlat(int hr, bool forceCenter) {
        if (platCount_ >= MAX_PLAT) return;
        const int w = rng_.chance(0.65f) ? 2 : 1;
        int cover = CENTER_COL;
        if (!forceCenter && rng_.chance(0.32f))
            cover = sdk::clampi(CENTER_COL + rng_.range(-2, 2), 0, W - 1);
        const int lo = sdk::clampi(cover - (w - 1), 0, W - w);
        const int hi = sdk::clampi(cover, 0, W - w);

        Plat& p = plats_[platCount_++];
        p.hr = hr;
        p.x  = (int8_t)rng_.range(lo, hi);
        p.w  = (int8_t)w;
        p.spring = !forceCenter && (hr - START_HR > 6) && rng_.chance(SPRING_P);

        if (!forceCenter && !mon_.alive && (maxHr_ - START_HR) > MONSTER_MIN && rng_.chance(MONSTER_P)) {
            mon_.alive = true;
            mon_.col   = (int8_t)sdk::clampi(p.x, 0, W - 1);
            mon_.hr    = hr + 1;
        }
    }

    // keep the world above the camera full, at reachable spacing
    void generate() {
        const int ceilHr = camBase_ + H + GEN_MARGIN;
        while (topHr_ < ceilHr && platCount_ < MAX_PLAT) {
            topHr_ += rng_.range(GAP_MIN, GAP_MAX);
            addPlat(topHr_, false);
        }
    }

    // drop platforms/actors that scrolled below the bottom edge, compacting the array
    void prune() {
        int n = 0;
        for (int i = 0; i < platCount_; ++i)
            if (plats_[i].hr >= camBase_ - 1) plats_[n++] = plats_[i];
        platCount_ = n;
        if (mon_.alive && mon_.hr < camBase_ - 1) mon_.alive = false;
    }

    void updateShot() {
        if (!shot_.alive) return;
        shot_.hr += SHOT_SPEED;
        if (mon_.alive && shot_.col == mon_.col && shot_.hr >= mon_.hr) {
            mon_.alive = false;
            shot_.alive = false;
            return;
        }
        if (shot_.hr > camBase_ + H + 2) shot_.alive = false;
    }

    int charHr() const { return fdiv(y_, UNIT); }

    // floor division so height stays correct if a fatal fall dips y_ below zero
    static int fdiv(int a, int b) {
        int q = a / b, r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) --q;
        return q;
    }

    uint32_t score() const { return (uint32_t)(maxHr_ - START_HR); }

    void drawScore(console::Canvas& c, console::Color fg) const {
        uint32_t s = score();
        int digits = 1;
        for (uint32_t v = s; v >= 10; v /= 10) ++digits;
        int w = digits * sdk::GLYPH_ADVANCE - 1;
        int sx = (W - w) / 2;
        if (sx < 0) sx = 0;
        sdk::drawNumber(c, sx, 2, s, fg);
    }

    console::GameMeta meta_{"doodlejump", nullptr, 1};
    sdk::Rng rng_{1};

    Plat  plats_[MAX_PLAT] = {};
    int   platCount_ = 0;
    // highest platform world-height generated so far
    int   topHr_ = START_HR;

    Mon   mon_{0, 0, false};
    Shot  shot_{0, 0, false};

    // fixed-point horizontal cell and world height (up = larger), velocity in units/tick
    int   x_ = SPAWN_COL * UNIT;
    int   y_ = START_HR * UNIT;
    int   vy_ = 0;
    // world height shown on the bottom screen row, and the greatest height reached (score)
    int   camBase_ = 0;
    int   maxHr_ = START_HR;
    State state_ = PLAY;
    int   crashTimer_ = 0;
};

}  // namespace doodlejump

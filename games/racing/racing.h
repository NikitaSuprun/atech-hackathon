#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Racing — a vertical car-dodge in the Brick-Game tradition, on the 6x18
// portrait field. Cols 0 and 5 are grass verges; the road (cols 1-4) is split
// into two 2-wide lanes. Your 2x2 car holds the bottom two rows and hops between
// lanes; 2x2 oncoming cars fall from the top. YOU own the scroll speed via the
// throttle knob — the authentic quirk where the road only moves as fast as you
// dare. Touch an oncoming car and you crash, flash, and restart the round.
//
// Controls (two rotary knobs, each with a push button):
//   knob[0] rotate .... steer between the left and right lane
//   knob[0] press ..... brake (drop to the slowest scroll speed)
//   knob[1] rotate .... throttle (raise/lower the scroll speed yourself)

namespace racing {

class RacingGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed);
        resetRound();
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        // Crash freeze: hold the flashing wreck, then restart the round.
        if (crashTimer_ > 0) {
            if (dtMs >= crashTimer_) resetRound();
            else crashTimer_ -= dtMs;
            return;
        }

        // Steer: one lane per rotation direction.
        lane_ = sdk::clampi(lane_ + sdk::signi(in.knob[0].delta), 0, LANES - 1);
        // Brake: slam the throttle back to the slowest setting.
        if (in.knob[0].justPressed) speed_ = MIN_SPEED;
        // Throttle: the player alone sets how fast the road rushes up.
        speed_ = sdk::clampi(speed_ + (int)in.knob[1].delta, MIN_SPEED, MAX_SPEED);

        // Steering into a car already level with us is an instant crash.
        if (collided()) { crash(); return; }

        // Scroll the world down on the throttle-driven interval.
        accMs_ += dtMs;
        uint32_t step = stepMs();
        while (accMs_ >= step) {
            accMs_ -= step;
            scrollStep();
            if (collided()) { crash(); return; }
        }
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        // Crash: blink the whole field between hazard and background.
        if (crashTimer_ > 0) {
            bool on = ((crashTimer_ / CRASH_BLINK_MS) & 1u) == 0;
            c.clear(on ? t.c(console::ROLE_HAZARD) : t.c(console::ROLE_BG));
            return;
        }

        c.clear(t.c(console::ROLE_BG));

        // Verges plus streaming edge dashes that sell the downward motion.
        console::Color verge = t.c(console::ROLE_DIM);
        console::Color mark = t.c(console::ROLE_NET);
        c.vline(VERGE_L, 0, console::SCREEN_H, verge);
        c.vline(VERGE_R, 0, console::SCREEN_H, verge);
        for (int y = 0; y < console::SCREEN_H; ++y) {
            int ph = (y - scrollOffset_) % DASH_PERIOD;
            if (ph < 0) ph += DASH_PERIOD;
            if (ph < DASH_LEN) {
                c.pixel(VERGE_L, y, mark);
                c.pixel(VERGE_R, y, mark);
            }
        }

        // Oncoming traffic.
        console::Color hz = t.c(console::ROLE_HAZARD);
        for (int i = 0; i < obsN_; ++i)
            c.fill(laneX(obs_[i].lane), obs_[i].y, CAR_W, CAR_H, hz);

        // Your car.
        c.fill(laneX(lane_), PLAYER_TOP, CAR_W, CAR_H, t.c(console::ROLE_P1));
    }

private:
    struct Car {
        int lane;
        int y;   // top row of the 2x2 block
    };

    // Field geometry.
    static constexpr int LANES     = 2;
    static constexpr int LANE_W    = 2;
    static constexpr int ROAD_L    = 1;
    static constexpr int VERGE_L   = 0;
    static constexpr int VERGE_R   = console::SCREEN_W - 1;
    static constexpr int CAR_W     = 2;
    static constexpr int CAR_H     = 2;
    static constexpr int PLAYER_TOP = console::SCREEN_H - CAR_H;

    // Throttle -> scroll interval. Higher speed = shorter step = faster + richer.
    static constexpr int MIN_SPEED = 1;
    static constexpr int MAX_SPEED = 9;
    static constexpr int START_SPEED = 5;
    static constexpr int SLOW_STEP_MS = 380;
    static constexpr int STEP_PER_LEVEL = 40;

    // Traffic spawning: one car per gap keeps at least one lane always open.
    static constexpr int MAX_OBS = 8;
    static constexpr int MIN_GAP = 4;
    static constexpr int MAX_GAP = 7;

    // Verge dash animation and crash flash.
    static constexpr int      DASH_PERIOD    = 4;
    static constexpr int      DASH_LEN       = 2;
    static constexpr uint32_t CRASH_MS       = 500;
    static constexpr uint32_t CRASH_BLINK_MS = 100;

    static int laneX(int lane) { return ROAD_L + lane * LANE_W; }

    uint32_t stepMs() const {
        return (uint32_t)(SLOW_STEP_MS - (speed_ - MIN_SPEED) * STEP_PER_LEVEL);
    }

    void addCar(int lane, int y) {
        if (obsN_ < MAX_OBS) obs_[obsN_++] = {lane, y};
    }

    void spawnCar() {
        addCar(rng_.range(0, LANES - 1), -CAR_H);
        spawnCountdown_ = rng_.range(MIN_GAP, MAX_GAP);
    }

    void scrollStep() {
        scrollOffset_ = (scrollOffset_ + 1) % DASH_PERIOD;
        // Distance banked, weighted by speed so bolder throttle scores faster.
        score_ += 1 + (uint32_t)speed_;
        for (int i = 0; i < obsN_;) {
            if (++obs_[i].y >= console::SCREEN_H) obs_[i] = obs_[--obsN_];
            else ++i;
        }
        if (--spawnCountdown_ <= 0) spawnCar();
    }

    bool collided() const {
        for (int i = 0; i < obsN_; ++i) {
            if (obs_[i].lane != lane_) continue;
            if (obs_[i].y + CAR_H - 1 >= PLAYER_TOP && obs_[i].y <= PLAYER_TOP + CAR_H - 1)
                return true;
        }
        return false;
    }

    void crash() { crashTimer_ = CRASH_MS; }

    void resetRound() {
        lane_ = 0;
        speed_ = START_SPEED;
        accMs_ = 0;
        scrollOffset_ = 0;
        score_ = 0;
        crashTimer_ = 0;
        obsN_ = 0;
        // Seed the road with one car so the field is never empty at the line.
        addCar(rng_.range(0, LANES - 1), 4);
        spawnCountdown_ = MIN_GAP;
    }

    console::GameMeta meta_{"racing", nullptr, 1};
    sdk::Rng rng_{1};

    Car      obs_[MAX_OBS];
    int      obsN_ = 0;
    int      lane_ = 0;
    int      speed_ = START_SPEED;
    int      spawnCountdown_ = MIN_GAP;
    int      scrollOffset_ = 0;
    uint32_t accMs_ = 0;
    uint32_t crashTimer_ = 0;
    uint32_t score_ = 0;
};

}  // namespace racing

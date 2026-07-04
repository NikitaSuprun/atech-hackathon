#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Space Invaders on the 6-wide x 18-tall PORTRAIT grid. A compact 4-wide x
// 3-tall block of invaders marches sideways, drops a row and reverses at each
// edge, and so descends toward the ship on the bottom row. The ship tracks
// knob[0] and fires bullets straight up on a knob[0] press (a few in flight at
// once); invaders rain bombs it must dodge. Clearing a wave spawns the next,
// faster one. A bomb hit or invaders reaching the ship costs a life; out of
// lives ends the game, then it restarts. With no input the ship auto-plays an
// attract demo (chase + auto-fire + dodge), so the headless dump still shows the
// march descending plus the ship and its bullets.
//
// All randomness comes from ctx.rngSeed and init() fully resets state, so runs
// are bit-for-bit replayable.
//
// Controls:
//   knob[0] rotate -> move ship LEFT / RIGHT
//   knob[0] press  -> fire; also restarts from the game-over screen
// Any knob[0] activity leaves the attract demo and starts a fresh game.

namespace invaders {

class InvadersGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed ? ctx.rngSeed : 0x1274DE45u);
        startAttract();
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        blinkClock_ += dtMs;
        switch (state_) {
            case ATTRACT:  updateAttract(in, dtMs);  break;
            case PLAY:     updatePlay(in, dtMs);      break;
            case GAMEOVER: updateGameOver(in, dtMs);  break;
        }
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        c.clear(t.c(console::ROLE_BG));
        if (state_ == GAMEOVER) { drawGameOver(c, t); return; }
        drawInvaders(c, t);
        drawBombs(c, t);
        drawBullets(c, t);
        drawShip(c, t);
        drawLives(c, t);
    }

private:
    enum State { ATTRACT, PLAY, GAMEOVER };

    static constexpr int SHIP_Y      = console::SCREEN_H - 1;
    static constexpr int FORM_COLS   = 4;
    static constexpr int FORM_ROWS   = 3;
    static constexpr int INVADERS    = FORM_COLS * FORM_ROWS;
    static constexpr int FORM_X0     = 1;
    static constexpr int FORM_Y0     = 1;
    static constexpr int FORM_XMAX   = console::SCREEN_W - FORM_COLS;
    static constexpr int REACH_ROW   = SHIP_Y - 1;
    static constexpr int MAX_LIVES   = 3;
    static constexpr int MAX_BULLETS = 3;
    static constexpr int MAX_BOMBS   = 3;
    static constexpr int POINTS      = 10;

    static constexpr uint32_t BULLET_MS      = 60;
    static constexpr uint32_t BOMB_MS        = 130;
    static constexpr uint32_t MARCH_MS_BASE  = 380;
    static constexpr uint32_t MARCH_MS_MIN   = 160;
    static constexpr uint32_t MARCH_MS_STEP  = 45;
    static constexpr uint32_t BOMB_MIN_MS    = 520;
    static constexpr uint32_t BOMB_JITTER_MS = 520;
    static constexpr uint32_t AUTOFIRE_MS    = 360;
    static constexpr uint32_t SHIP_AI_MS     = 90;
    static constexpr uint32_t GRACE_MS       = 600;
    static constexpr uint32_t BLINK_MS       = 120;
    static constexpr uint32_t GAMEOVER_MS    = 4200;
    static constexpr uint32_t SCROLL_PXMS    = 90;

    struct Shot { bool active; int x, y; };

    void startAttract() { state_ = ATTRACT; resetGame(); }
    void startPlay()    { state_ = PLAY;    resetGame(); }

    // Full reset so init() (and every fresh game) is deterministic for a seed.
    void resetGame() {
        wave_  = 1;
        lives_ = MAX_LIVES;
        score_ = 0;
        shipX_ = console::SCREEN_W / 2;
        clearShots();
        graceMs_ = 0;
        blinkClock_ = 0;
        overClock_ = 0;
        scrollClock_ = 0;
        shipAiAccum_ = 0;
        autofireAccum_ = 0;
        setupWave();
    }

    void setupWave() {
        for (int r = 0; r < FORM_ROWS; ++r)
            for (int col = 0; col < FORM_COLS; ++col) alive_[r][col] = true;
        aliveCount_ = INVADERS;
        formX_ = FORM_X0;
        formY_ = FORM_Y0;
        formDir_ = 1;
        uint32_t dec = (uint32_t)(wave_ - 1) * MARCH_MS_STEP;
        marchMs_ = dec >= MARCH_MS_BASE - MARCH_MS_MIN ? MARCH_MS_MIN : MARCH_MS_BASE - dec;
        marchAccum_ = 0;
        bulletAccum_ = 0;
        bombAccum_ = 0;
        bombSpawnAccum_ = 0;
        bombSpawnMs_ = BOMB_MIN_MS;
    }

    void nextWave() {
        wave_++;
        clearShots();
        graceMs_ = GRACE_MS;
        setupWave();
    }

    void clearShots() {
        for (int i = 0; i < MAX_BULLETS; ++i) bullets_[i] = {false, 0, 0};
        for (int i = 0; i < MAX_BOMBS; ++i)   bombs_[i]   = {false, 0, 0};
    }

    void updateAttract(const console::Input& in, uint32_t dt) {
        if (in.knob[0].justPressed || in.knob[0].delta != 0) { startPlay(); return; }
        aiUpdate(dt);
        stepWorld(dt);
    }

    void updatePlay(const console::Input& in, uint32_t dt) {
        shipX_ = sdk::clampi(shipX_ + (int)in.knob[0].delta, 0, console::SCREEN_W - 1);
        if (in.knob[0].justPressed) fire();
        stepWorld(dt);
    }

    void updateGameOver(const console::Input& in, uint32_t dt) {
        overClock_ += dt;
        scrollClock_ += dt;
        if (in.knob[0].justPressed) { startPlay(); return; }
        if (overClock_ >= GAMEOVER_MS) startAttract();
    }

    // Attract pilot: dodge a bomb bearing down our column, else slide under the
    // lowest invader; fire on a steady cadence so the dump shows live bullets.
    void aiUpdate(uint32_t dt) {
        shipAiAccum_ += dt;
        if (shipAiAccum_ >= SHIP_AI_MS) {
            shipAiAccum_ -= SHIP_AI_MS;
            int dir = threatDir();
            if (dir == 0) {
                int tx = aimColumn();
                dir = tx > shipX_ ? 1 : (tx < shipX_ ? -1 : 0);
            }
            shipX_ = sdk::clampi(shipX_ + dir, 0, console::SCREEN_W - 1);
        }
        autofireAccum_ += dt;
        if (autofireAccum_ >= AUTOFIRE_MS) { autofireAccum_ -= AUTOFIRE_MS; fire(); }
    }

    int threatDir() const {
        for (int i = 0; i < MAX_BOMBS; ++i)
            if (bombs_[i].active && bombs_[i].x == shipX_ && bombs_[i].y >= SHIP_Y - 5)
                return shipX_ < console::SCREEN_W / 2 ? 1 : -1;
        return 0;
    }

    int aimColumn() const {
        int best = shipX_, bestRow = -1;
        for (int r = FORM_ROWS - 1; r >= 0; --r)
            for (int col = 0; col < FORM_COLS; ++col)
                if (alive_[r][col] && r > bestRow) { bestRow = r; best = formX_ + col; }
        return best;
    }

    void stepWorld(uint32_t dt) {
        if (graceMs_ > dt) graceMs_ -= dt; else graceMs_ = 0;

        bulletAccum_ += dt;
        while (bulletAccum_ >= BULLET_MS) {
            bulletAccum_ -= BULLET_MS;
            for (int i = 0; i < MAX_BULLETS; ++i)
                if (bullets_[i].active && --bullets_[i].y < 0) bullets_[i].active = false;
        }

        marchAccum_ += dt;
        while (marchAccum_ >= marchMs_) {
            marchAccum_ -= marchMs_;
            marchStep();
        }

        bombAccum_ += dt;
        while (bombAccum_ >= BOMB_MS) {
            bombAccum_ -= BOMB_MS;
            for (int i = 0; i < MAX_BOMBS; ++i)
                if (bombs_[i].active && ++bombs_[i].y >= console::SCREEN_H) bombs_[i].active = false;
        }

        bombSpawnAccum_ += dt;
        if (graceMs_ == 0 && aliveCount_ > 0 && bombSpawnAccum_ >= bombSpawnMs_) {
            bombSpawnAccum_ = 0;
            bombSpawnMs_ = BOMB_MIN_MS + rng_.below(BOMB_JITTER_MS);
            spawnBomb();
        }

        resolveHits();
        resolveBombs();
    }

    // At an edge the block drops and reverses instead of stepping; reaching the
    // ship's doorstep costs a life and resets the block to the top.
    void marchStep() {
        int nx = formX_ + formDir_;
        if (nx < 0 || nx > FORM_XMAX) {
            formDir_ = -formDir_;
            formY_ += 1;
            if (lowestRow() >= REACH_ROW) loseLife(true);
        } else {
            formX_ = nx;
        }
    }

    int lowestRow() const {
        for (int r = FORM_ROWS - 1; r >= 0; --r)
            for (int col = 0; col < FORM_COLS; ++col)
                if (alive_[r][col]) return formY_ + r;
        return formY_;
    }

    void fire() {
        for (int i = 0; i < MAX_BULLETS; ++i)
            if (!bullets_[i].active) { bullets_[i] = {true, shipX_, SHIP_Y - 1}; return; }
    }

    void spawnBomb() {
        int slot = -1;
        for (int i = 0; i < MAX_BOMBS; ++i)
            if (!bombs_[i].active) { slot = i; break; }
        if (slot < 0) return;
        int pick = (int)rng_.below((uint32_t)aliveCount_);
        for (int r = 0; r < FORM_ROWS; ++r)
            for (int col = 0; col < FORM_COLS; ++col)
                if (alive_[r][col] && pick-- == 0) {
                    bombs_[slot] = {true, formX_ + col, formY_ + r + 1};
                    return;
                }
    }

    void resolveHits() {
        for (int i = 0; i < MAX_BULLETS; ++i) {
            if (!bullets_[i].active) continue;
            int r = bullets_[i].y - formY_, col = bullets_[i].x - formX_;
            if (r >= 0 && r < FORM_ROWS && col >= 0 && col < FORM_COLS && alive_[r][col]) {
                alive_[r][col] = false;
                aliveCount_--;
                bullets_[i].active = false;
                score_ += POINTS;
                if (aliveCount_ == 0) { nextWave(); return; }
            }
        }
    }

    void resolveBombs() {
        if (graceMs_ > 0) return;
        for (int i = 0; i < MAX_BOMBS; ++i)
            if (bombs_[i].active && bombs_[i].y == SHIP_Y && bombs_[i].x == shipX_) {
                loseLife(false);
                return;
            }
    }

    void loseLife(bool resetForm) {
        if (lives_ > 0) lives_--;
        clearShots();
        graceMs_ = GRACE_MS;
        shipX_ = console::SCREEN_W / 2;
        if (resetForm) {
            formX_ = FORM_X0;
            formY_ = FORM_Y0;
            formDir_ = 1;
            marchAccum_ = 0;
        }
        if (lives_ <= 0) {
            if (state_ == PLAY) { state_ = GAMEOVER; overClock_ = 0; scrollClock_ = 0; }
            else startAttract();
        }
    }

    void drawInvaders(console::Canvas& c, const console::Theme& t) {
        console::Color hz = t.c(console::ROLE_HAZARD);
        for (int r = 0; r < FORM_ROWS; ++r)
            for (int col = 0; col < FORM_COLS; ++col)
                if (alive_[r][col]) c.pixel(formX_ + col, formY_ + r, hz);
    }

    void drawBombs(console::Canvas& c, const console::Theme& t) {
        console::Color hz = t.c(console::ROLE_HAZARD);
        for (int i = 0; i < MAX_BOMBS; ++i)
            if (bombs_[i].active) c.pixel(bombs_[i].x, bombs_[i].y, hz);
    }

    void drawBullets(console::Canvas& c, const console::Theme& t) {
        console::Color ac = t.c(console::ROLE_ACCENT);
        for (int i = 0; i < MAX_BULLETS; ++i)
            if (bullets_[i].active) c.pixel(bullets_[i].x, bullets_[i].y, ac);
    }

    void drawShip(console::Canvas& c, const console::Theme& t) {
        bool blink = graceMs_ > 0 && (blinkClock_ / BLINK_MS) % 2;
        if (!blink) c.pixel(shipX_, SHIP_Y, t.c(console::ROLE_P1));
    }

    void drawLives(console::Canvas& c, const console::Theme& t) {
        console::Color on = t.c(console::ROLE_P1), off = t.c(console::ROLE_DIM);
        for (int i = 0; i < MAX_LIVES; ++i)
            c.pixel(i, 0, i < lives_ ? on : off);
    }

    void drawGameOver(console::Canvas& c, const console::Theme& t) {
        int digits = 1;
        for (uint32_t n = (uint32_t)score_ / 10; n; n /= 10) ++digits;
        int w = digits * sdk::GLYPH_ADVANCE - 1;
        int cycle = console::SCREEN_W + w + 2;
        int px = (int)((scrollClock_ / SCROLL_PXMS) % (uint32_t)cycle);
        bool on = (blinkClock_ / 250) % 2 == 0;
        sdk::drawNumber(c, console::SCREEN_W - px, 6, (uint32_t)score_,
                        t.c(on ? console::ROLE_ACCENT : console::ROLE_ACCENT2));
    }

    console::GameMeta meta_{"invaders", nullptr, 1};
    sdk::Rng rng_{0x1274DE45u};

    State state_ = ATTRACT;
    int   wave_ = 1, lives_ = MAX_LIVES, score_ = 0;

    bool alive_[FORM_ROWS][FORM_COLS] = {};
    int  aliveCount_ = 0;
    int  formX_ = FORM_X0, formY_ = FORM_Y0, formDir_ = 1;
    uint32_t marchMs_ = MARCH_MS_BASE, marchAccum_ = 0;

    int  shipX_ = console::SCREEN_W / 2;
    Shot bullets_[MAX_BULLETS] = {};
    Shot bombs_[MAX_BOMBS] = {};
    uint32_t bulletAccum_ = 0, bombAccum_ = 0;
    uint32_t bombSpawnAccum_ = 0, bombSpawnMs_ = BOMB_MIN_MS;

    uint32_t graceMs_ = 0, blinkClock_ = 0;
    uint32_t shipAiAccum_ = 0, autofireAccum_ = 0;
    uint32_t overClock_ = 0, scrollClock_ = 0;
};

}  // namespace invaders

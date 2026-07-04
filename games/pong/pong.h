#pragma once
#include <math.h>
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Two-knob local Pong on the 6x18 console. P1 defends the bottom edge
// (knob[0]), P2 the top edge (knob[1]); each knob's .delta slides its paddle
// along its edge. The ball serves from the conceder, bounces off the side walls
// and off the paddles with english set by where it strikes the paddle, and the
// first player to WIN_SCORE takes the match before it resets.
//
// Physics is ported from modules/pong_screen (swept paddle-plane crossing +
// triangle-fold side-wall reflection + contact-point english) but is fully
// self-contained here and redrawn through Canvas using Theme roles only.

namespace pong {

class PongGame : public console::Game {
public:
    const console::GameMeta& meta() const override { return meta_; }

    void init(const console::GameContext& ctx) override {
        rng_ = sdk::Rng(ctx.rngSeed);
        score_[0] = score_[1] = 0;
        padX_[0] = padX_[1] = (W - PADDLE_W) * 0.5f;
        server_ = rng_.chance(0.5f) ? 1 : 0;
        winner_ = -1;
        timeMs_ = 0;
        speed_ = 0;
        enter(ST_SERVE);
        glueBall();
    }

    void update(const console::Input& in, uint32_t dtMs) override {
        timeMs_ += dtMs;
        stateMs_ += dtMs;
        movePaddles(in);
        bool pressed = in.knob[0].justPressed || in.knob[1].justPressed;

        switch (state_) {
            case ST_SERVE:
                glueBall();
                if (pressed || stateMs_ >= SERVE_MS) { launch(server_); enter(ST_PLAY); }
                break;
            case ST_PLAY:
                // fixed-step physics; one 20 ms step per host tick, catch-up safe
                physAcc_ += dtMs;
                while (physAcc_ >= STEP_MS && state_ == ST_PLAY) {
                    physAcc_ -= STEP_MS;
                    step();
                }
                break;
            case ST_POINT:
                if (stateMs_ >= POINT_MS) {
                    if (score_[0] >= WIN_SCORE || score_[1] >= WIN_SCORE) {
                        winner_ = score_[0] >= WIN_SCORE ? 0 : 1;
                        enter(ST_OVER);
                    } else {
                        enter(ST_SERVE);
                    }
                }
                break;
            case ST_OVER:
                if (pressed || stateMs_ >= OVER_MS) {
                    score_[0] = score_[1] = 0;
                    winner_ = -1;
                    server_ = rng_.chance(0.5f) ? 1 : 0;
                    enter(ST_SERVE);
                }
                break;
        }
    }

    void draw(console::Canvas& c, const console::Theme& t) override {
        c.clear(t.c(console::ROLE_BG));
        drawNet(c, t);
        drawPaddle(c, 0, t.c(console::ROLE_P1));
        drawPaddle(c, 1, t.c(console::ROLE_P2));

        if (state_ == ST_OVER) { drawWinner(c, t); return; }

        if (state_ == ST_SERVE || state_ == ST_POINT) drawScores(c, t);

        // ball is live in play, and blinks (4 Hz) while glued at serve
        bool blinkOn = ((timeMs_ / BLINK_MS) & 1) == 0;
        if (state_ == ST_PLAY || (state_ == ST_SERVE && blinkOn))
            drawBall(c, t.c(console::ROLE_BALL));
    }

private:
    // ---- geometry / tuning (self-contained; no module dependency) ----
    static constexpr int   W        = console::SCREEN_W;   // 6
    static constexpr int   H        = console::SCREEN_H;   // 18
    static constexpr int   PADDLE_W = 2;                   // paddle length in px
    static constexpr float PADDLE_GRACE = 0.35f;           // edge-graze still counts as a hit
    static constexpr float PLANE_INSET  = 1.5f;            // ball-center contact plane per edge
    static constexpr float KNOB_STEP    = 1.0f;            // cells per detent

    static constexpr float BALL_SPEED_START = 8.0f;        // cells/s at serve
    static constexpr float SPEEDUP_PER_HIT  = 1.07f;
    static constexpr float BALL_SPEED_MAX   = 20.0f;
    static constexpr float ENGLISH_GAIN     = 0.85f;       // contact offset -> horizontal gain
    static constexpr float VY_MIN           = 0.55f;       // vertical fraction floor (no stalls)
    static constexpr float WALL_INSET       = 0.5f;        // side-wall reflection plane

    static constexpr int      WIN_SCORE = 5;
    static constexpr uint32_t STEP_MS   = 20;              // physics substep == host tick
    static constexpr float    DT        = STEP_MS / 1000.0f;
    static constexpr uint32_t SERVE_MS  = 350;             // glued-ball pause before launch
    static constexpr uint32_t POINT_MS  = 700;             // score pause after a goal
    static constexpr uint32_t OVER_MS   = 2200;            // win celebration before reset
    static constexpr uint32_t BLINK_MS  = 125;             // 4 Hz blink phase
    static constexpr int      NET_ROW   = H / 2;           // mid-field net row

    enum State : uint8_t { ST_SERVE, ST_PLAY, ST_POINT, ST_OVER };

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // triangle-fold x into [0, W]: side walls mirror the crossing coordinate
    static float foldX(float x) {
        const float period = 2.0f * W;
        x = fmodf(x, period);
        if (x < 0) x += period;
        if (x > W) x = period - x;
        return x;
    }

    static int paddleRow(int p) { return p == 0 ? H - 1 : 0; }
    float padCenter(int p) const { return roundf(padX_[p]) + PADDLE_W * 0.5f; }

    void enter(State s) {
        state_ = s;
        stateMs_ = 0;
        physAcc_ = 0;
    }

    void movePaddles(const console::Input& in) {
        for (int p = 0; p < 2; ++p)
            padX_[p] = clampf(padX_[p] + KNOB_STEP * (float)in.knob[p].delta,
                              0.0f, (float)(W - PADDLE_W));
    }

    // ball pinned to the server's paddle center during the serve countdown
    void glueBall() {
        ballX_ = padCenter(server_);
        ballY_ = (server_ == 0) ? H - PLANE_INSET : PLANE_INSET;
        velX_ = velY_ = 0;
    }

    void launch(int p) {
        speed_ = BALL_SPEED_START;
        float pc = padCenter(p);
        ballX_ = pc;
        ballY_ = (p == 0) ? H - PLANE_INSET : PLANE_INSET;
        // aim toward the roomier side; center serve breaks ties randomly
        float sign = pc < W * 0.5f ? 1.0f : (pc > W * 0.5f ? -1.0f : (rng_.chance(0.5f) ? 1.0f : -1.0f));
        static const float SERVE_U[2] = {0.35f, 0.70f};
        float ux = sign * SERVE_U[rng_.below(2)];
        float uy = sqrtf(1.0f - ux * ux);
        velX_ = ux * speed_;
        velY_ = (p == 0 ? -uy : uy) * speed_;   // p0 (bottom) serves up, p1 (top) serves down
    }

    // reflect off paddle p; english from contact offset u in [-1, 1]
    void bounce(int p, float u) {
        speed_ = fminf(speed_ * SPEEDUP_PER_HIT, BALL_SPEED_MAX);
        const float maxUx = sqrtf(1.0f - VY_MIN * VY_MIN);
        float ux = clampf(u * ENGLISH_GAIN, -maxUx, maxUx);
        float uy = sqrtf(1.0f - ux * ux);
        velX_ = ux * speed_;
        velY_ = (p == 0 ? -uy : uy) * speed_;
    }

    // swept crossing of paddle p's contact plane; false => the paddle missed (goal)
    bool tryPaddle(int p, float plane) {
        float t = (plane - ballY_) / (velY_ * DT);
        float xc = foldX(ballX_ + velX_ * DT * t);
        float u = (xc - padCenter(p)) / (PADDLE_W * 0.5f + PADDLE_GRACE);
        if (fabsf(u) > 1.0f) return false;
        bounce(p, clampf(u, -1.0f, 1.0f));
        float rem = (1.0f - t) * DT;
        ballX_ = xc + velX_ * rem;
        ballY_ = plane + velY_ * rem;
        if (ballX_ < WALL_INSET) { ballX_ = 2.0f * WALL_INSET - ballX_; velX_ = -velX_; }
        if (ballX_ > W - WALL_INSET) { ballX_ = 2.0f * (W - WALL_INSET) - ballX_; velX_ = -velX_; }
        return true;
    }

    void goal(int scorer) {
        score_[scorer]++;
        server_ = 1 - scorer;   // the conceder serves next
        enter(ST_POINT);
    }

    void step() {
        float nx = ballX_ + velX_ * DT;
        float ny = ballY_ + velY_ * DT;
        const float planeP1 = H - PLANE_INSET;   // bottom paddle plane
        const float planeP2 = PLANE_INSET;       // top paddle plane
        if (velY_ > 0 && ballY_ <= planeP1 && ny > planeP1) {
            if (tryPaddle(0, planeP1)) return;
        } else if (velY_ < 0 && ballY_ >= planeP2 && ny < planeP2) {
            if (tryPaddle(1, planeP2)) return;
        }
        if (velX_ < 0 && nx < WALL_INSET) { nx = 2.0f * WALL_INSET - nx; velX_ = -velX_; }
        else if (velX_ > 0 && nx > W - WALL_INSET) { nx = 2.0f * (W - WALL_INSET) - nx; velX_ = -velX_; }
        ballX_ = nx;
        ballY_ = ny;
        if (ny < 0.0f) goal(0);            // past P2's edge -> P1 scores
        else if (ny > (float)H) goal(1);   // past P1's edge -> P2 scores
    }

    // ---- rendering (theme roles only, never hex) ----
    void drawNet(console::Canvas& c, const console::Theme& t) {
        for (int x = 0; x < W; x += 2) c.pixel(x, NET_ROW, t.c(console::ROLE_NET));
    }

    void drawPaddle(console::Canvas& c, int p, console::Color col) {
        int x0 = (int)lroundf(padX_[p]);
        int y = paddleRow(p);
        for (int i = 0; i < PADDLE_W; ++i) c.pixel(x0 + i, y, col);
    }

    void drawBall(console::Canvas& c, console::Color col) {
        c.pixel((int)floorf(ballX_), (int)floorf(ballY_), col);
    }

    void drawScores(console::Canvas& c, const console::Theme& t) {
        int x = (W - sdk::FONT_W) / 2;
        sdk::drawNumber(c, x, 2, (uint32_t)score_[1], t.c(console::ROLE_P2));               // top
        sdk::drawNumber(c, x, H - 2 - sdk::FONT_H, (uint32_t)score_[0], t.c(console::ROLE_P1)); // bottom
    }

    void drawWinner(console::Canvas& c, const console::Theme& t) {
        console::Color wc = winner_ == 0 ? t.c(console::ROLE_P1) : t.c(console::ROLE_P2);
        c.hline(0, paddleRow(winner_), W, wc);   // solid bar on the winner's edge
        if (((timeMs_ / 300) & 1) == 0)
            sdk::drawNumber(c, (W - sdk::FONT_W) / 2, (H - sdk::FONT_H) / 2,
                            (uint32_t)score_[winner_], wc);
    }

    console::GameMeta meta_{"pong", nullptr, 2};
    sdk::Rng rng_{1};

    State    state_    = ST_SERVE;
    uint32_t stateMs_  = 0;
    uint32_t physAcc_  = 0;
    uint32_t timeMs_   = 0;
    float    padX_[2]  = {2, 2};
    float    ballX_ = 3, ballY_ = 9, velX_ = 0, velY_ = 0, speed_ = 0;
    uint8_t  score_[2] = {0, 0};
    int      server_ = 0;
    int      winner_ = -1;
};

}  // namespace pong

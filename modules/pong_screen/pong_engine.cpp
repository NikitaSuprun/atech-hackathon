#include <math.h>
#include <string.h>
#include "pong_engine.h"
#include "pong_config.h"
#include "net_config.h"

namespace pong {

// ---- engine-local layout/tuning (implementation detail, not the designer table) ----
static constexpr uint32_t DEFAULT_RNG_SEED = 0xA341316Cu;  // xorshift seed when unseeded
static constexpr uint32_t PF_PULSE1_END   = 120;  // POINT_FLASH pulse windows (ms into state)
static constexpr uint32_t PF_PULSE2_START = 210;
static constexpr uint32_t PF_PULSE2_END   = 330;
static constexpr uint32_t PF_PULSES_END   = 420;  // pulses done -> draw pips
static constexpr int PIP_ROW[2] = {15, 2};        // score-pip row (P1 bottom, P2 top)
static constexpr int NET_ROW   = 9;               // mid-field net dots at columns L/R
static constexpr int NET_COL_L = 1;
static constexpr int NET_COL_R = 4;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// triangle fold into [0, W]: side walls act as mirrors for the crossing x
static float foldX(float x) {
    const float period = 2.0f * W;
    x = fmodf(x, period);
    if (x < 0) x += period;
    if (x > W) x = period - x;
    return x;
}

static Color scaleC(Color c, float s) {
    return {(uint8_t)(c.r * s), (uint8_t)(c.g * s), (uint8_t)(c.b * s)};
}

static int paddleRow(int p) { return p == 0 ? H - 1 : 0; }

uint32_t Engine::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void Engine::reset(uint32_t rngSeed) {
    st_ = EngineStatus();
    st_.state = GS_LINK_WAIT;
    st_.goalBy = PONG_NOBODY;
    st_.winner = PONG_NOBODY;
    st_.servingPlayer = PONG_NOBODY;
    rng_ = rngSeed ? rngSeed : DEFAULT_RNG_SEED;
    timeMs_ = stateMs_ = idleMs_ = staleMs_ = 0;
    ballX_ = ballY_ = velX_ = velY_ = speed_ = 0;
    padX_[0] = padX_[1] = (W - PADDLE_W) * 0.5f;
    lastKnob_[0] = lastKnob_[1] = 0;
    knobInit_ = false;
    holdMs_[0] = holdMs_[1] = 0;
    server_ = PONG_NOBODY;
    prevState_ = GS_LINK_WAIT;
    everLinked_ = false;
    heldNow_[0] = heldNow_[1] = false;
    attract_ = false;
    attractRally_ = 0;
}

void Engine::enter(uint8_t s) {
    st_.state = s;
    stateMs_ = 0;
    switch (s) {
        case GS_READY_CHECK:
            holdMs_[0] = holdMs_[1] = 0;
            st_.readyProgress[0] = st_.readyProgress[1] = 0;
            idleMs_ = 0;
            attract_ = false;
            break;
        case GS_COUNTDOWN:
            holdMs_[0] = holdMs_[1] = 0;
            st_.readyProgress[0] = st_.readyProgress[1] = 0;
            attract_ = false;
            if (server_ == PONG_NOBODY) server_ = (uint8_t)(rnd() & 1);
            st_.servingPlayer = server_;
            speed_ = BALL_SPEED_START;
            glueBall();
            break;
        default:
            break;
    }
}

void Engine::applyKnobs(const EngineInputs& in) {
    if (!knobInit_) {
        lastKnob_[0] = in.knobPos[0];
        lastKnob_[1] = in.knobPos[1];
        knobInit_ = true;
        return;
    }
    for (int p = 0; p < 2; ++p) {
        // twos-complement subtraction = wrap-safe
        int32_t d = in.knobPos[p] - lastKnob_[p];
        lastKnob_[p] = in.knobPos[p];
        if (d == 0) continue;
        idleMs_ = 0;
        if (attract_) continue;
        padX_[p] = clampf(padX_[p] + KNOB_SIGN[p] * (float)d / DETENTS_PER_CELL,
                          0.0f, (float)(W - PADDLE_W));
    }
}

bool Engine::updateHolds(const EngineInputs& in, uint32_t dtMs) {
    for (int p = 0; p < 2; ++p) {
        if (in.held[p]) {
            holdMs_[p] += dtMs;
            if (holdMs_[p] > READY_HOLD_MS) holdMs_[p] = READY_HOLD_MS;
        } else {
            holdMs_[p] = 0;
        }
        uint32_t prog = (uint32_t)READY_PROGRESS_FULL * holdMs_[p] / READY_HOLD_MS;
        st_.readyProgress[p] = prog > READY_PROGRESS_FULL ? READY_PROGRESS_FULL : (uint8_t)prog;
    }
    return holdMs_[0] >= READY_HOLD_MS && holdMs_[1] >= READY_HOLD_MS;
}

void Engine::glueBall() {
    int p = server_;
    ballX_ = (float)lroundf(padX_[p]) + PADDLE_W * 0.5f;
    ballY_ = (p == 0) ? H - PADDLE_PLANE_INSET : PADDLE_PLANE_INSET;
    velX_ = velY_ = 0;
}

void Engine::launchFrom(int p, bool count) {
    speed_ = BALL_SPEED_START;
    float pc = (float)lroundf(padX_[p]) + PADDLE_W * 0.5f;
    ballX_ = pc;
    ballY_ = (p == 0) ? H - PADDLE_PLANE_INSET : PADDLE_PLANE_INSET;
    // aim toward the roomier side
    float sign = pc < W * 0.5f ? 1.0f : (pc > W * 0.5f ? -1.0f : ((rnd() & 1) ? 1.0f : -1.0f));
    float ux = sign * SERVE_U[rnd() & 1];
    float uy = sqrtf(1.0f - ux * ux);
    velX_ = ux * speed_;
    velY_ = (p == 0 ? -uy : uy) * speed_;
    if (count) {
        st_.serveSeq++;
        st_.servingPlayer = (uint8_t)p;
    }
}

void Engine::bounceOffPaddle(int p, float u) {
    speed_ = fminf(speed_ * SPEEDUP_PER_HIT, BALL_SPEED_MAX);
    const float maxUx = sqrtf(1.0f - VY_MIN * VY_MIN);
    float ux = clampf(u * ENGLISH_GAIN, -maxUx, maxUx);
    float uy = sqrtf(1.0f - ux * ux);
    velX_ = ux * speed_;
    velY_ = (p == 0 ? -uy : uy) * speed_;
    if (!attract_) st_.paddleHitSeq++;
}

bool Engine::tryPaddle(int p, float plane) {
    float t = (plane - ballY_) / (velY_ * DT);
    float xc = foldX(ballX_ + velX_ * DT * t);
    float pc = (float)lroundf(padX_[p]) + PADDLE_W * 0.5f;
    float u = (xc - pc) / (PADDLE_W * 0.5f + PADDLE_GRACE);
    if (fabsf(u) > 1.0f) return false;
    bounceOffPaddle(p, clampf(u, -1.0f, 1.0f));
    float rem = (1.0f - t) * DT;
    ballX_ = xc + velX_ * rem;
    ballY_ = plane + velY_ * rem;
    if (ballX_ < WALL_INSET) { ballX_ = 2.0f * WALL_INSET - ballX_; velX_ = -velX_; }
    if (ballX_ > W - WALL_INSET) { ballX_ = 2.0f * (W - WALL_INSET) - ballX_; velX_ = -velX_; }
    return true;
}

void Engine::onGoal(int scorer) {
    if (attract_) {
        attractRally_++;
        launchFrom(1 - scorer, false);
        return;
    }
    st_.goalSeq++;
    st_.goalBy = (uint8_t)scorer;
    st_.score[scorer]++;
    server_ = (uint8_t)(1 - scorer);
    enter(GS_POINT_FLASH);
}

void Engine::stepPhysics() {
    float nx = ballX_ + velX_ * DT;
    float ny = ballY_ + velY_ * DT;
    // swept paddle-plane crossings (fold handles same-tick wall reflection)
    if (velY_ > 0 && ballY_ <= H - PADDLE_PLANE_INSET && ny > H - PADDLE_PLANE_INSET) {
        if (tryPaddle(0, H - PADDLE_PLANE_INSET)) return;
    } else if (velY_ < 0 && ballY_ >= PADDLE_PLANE_INSET && ny < PADDLE_PLANE_INSET) {
        if (tryPaddle(1, PADDLE_PLANE_INSET)) return;
    }
    if (velX_ < 0 && nx < WALL_INSET) {
        nx = 2.0f * WALL_INSET - nx;
        velX_ = -velX_;
        if (!attract_) st_.wallBounceSeq++;
    } else if (velX_ > 0 && nx > W - WALL_INSET) {
        nx = 2.0f * (W - WALL_INSET) - nx;
        velX_ = -velX_;
        if (!attract_) st_.wallBounceSeq++;
    }
    ballX_ = nx;
    ballY_ = ny;
    // goal only once the center is fully past the field
    if (ny < 0.0f) onGoal(0);
    else if (ny > (float)H) onGoal(1);
}

void Engine::stepAttract() {
    int def = velY_ > 0 ? 0 : 1;
    bool forceMiss = (attractRally_ % ATTRACT_FORCE_MISS_EVERY) == (ATTRACT_FORCE_MISS_EVERY - 1);
    for (int p = 0; p < 2; ++p) {
        float target;
        if (p == def) {
            float wobble = ATTRACT_WOBBLE_AMP * sinf(timeMs_ * ATTRACT_WOBBLE_RATE + p * ATTRACT_WOBBLE_PHASE);
            target = ballX_ - PADDLE_W * 0.5f + wobble;
            if (forceMiss)
                target = ballX_ < W * 0.5f ? (float)(W - PADDLE_W) : 0.0f;
        } else {
            target = (W - PADDLE_W) * 0.5f;
        }
        target = clampf(target, 0.0f, (float)(W - PADDLE_W));
        float mv = ATTRACT_PADDLE_SPEED * DT;
        padX_[p] += clampf(target - padX_[p], -mv, mv);
    }
    stepPhysics();
}

void Engine::tick(const EngineInputs& in, uint32_t dtMs) {
    timeMs_ += dtMs;
    heldNow_[0] = in.held[0] && in.controllerLinked;
    heldNow_[1] = in.held[1] && in.controllerLinked;
    st_.heldBits = (uint8_t)((in.held[0] ? PONG_HELD_P1 : 0) | (in.held[1] ? PONG_HELD_P2 : 0));

    if (in.controllerRebooted) {
        lastKnob_[0] = in.knobPos[0];
        lastKnob_[1] = in.knobPos[1];
        knobInit_ = true;
    }

    if (!in.controllerLinked) {
        if (st_.state != GS_LINK_WAIT) {
            prevState_ = st_.state;
            enter(GS_LINK_WAIT);
        }
        stateMs_ += dtMs;
        staleMs_ += dtMs;
        if (staleMs_ >= STALE_HOLD_RESET_MS) {
            holdMs_[0] = holdMs_[1] = 0;
            st_.readyProgress[0] = st_.readyProgress[1] = 0;
        }
        return;
    }
    staleMs_ = 0;
    applyKnobs(in);

    if (st_.state == GS_LINK_WAIT) {
        everLinked_ = true;
        if (prevState_ == GS_GAME_OVER) {
            // don't replay the celebration on relink
            st_.state = GS_GAME_OVER;
            stateMs_ = WIN_CELEBRATION_MS;
        } else {
            enter(GS_READY_CHECK);
        }
        prevState_ = GS_LINK_WAIT;
    }

    stateMs_ += dtMs;

    switch (st_.state) {
        case GS_READY_CHECK: {
            bool both = updateHolds(in, dtMs);
            bool anyHeld = in.held[0] || in.held[1];
            if (anyHeld) {
                idleMs_ = 0;
                attract_ = false;
            } else {
                idleMs_ += dtMs;
            }
            if (both) {
                enter(GS_COUNTDOWN);
                break;
            }
            if (!attract_ && idleMs_ >= IDLE_TO_ATTRACT_MS) {
                attract_ = true;
                attractRally_ = 0;
                launchFrom((int)(rnd() & 1), false);
            }
            if (attract_) stepAttract();
            break;
        }
        case GS_COUNTDOWN:
            glueBall();
            if (stateMs_ >= COUNTDOWN_MS) {
                serveBall();
                enter(GS_PLAYING);
            }
            break;
        case GS_PLAYING:
            stepPhysics();
            break;
        case GS_POINT_FLASH:
            if (stateMs_ >= POINT_FLASH_MS) {
                if (st_.score[0] >= PONG_WIN_SCORE || st_.score[1] >= PONG_WIN_SCORE) {
                    st_.winner = st_.score[0] >= PONG_WIN_SCORE ? 0 : 1;
                    st_.winSeq++;
                    enter(GS_GAME_OVER);
                } else {
                    enter(GS_READY_CHECK);
                }
            }
            break;
        case GS_GAME_OVER:
            if (stateMs_ < WIN_CELEBRATION_MS) break;
            if (updateHolds(in, dtMs)) {
                st_.score[0] = st_.score[1] = 0;
                server_ = PONG_NOBODY;
                enter(GS_COUNTDOWN);
            }
            break;
        default:
            break;
    }
}

// ---------------- rendering ----------------

void Engine::drawPaddle(Frame& f, int p, float s) const {
    int y = paddleRow(p);
    int x0 = (int)lroundf(padX_[p]);
    Color c = scaleC(p == 0 ? COL_P1 : COL_P2, s);
    for (int i = 0; i < PADDLE_W; ++i) {
        int x = x0 + i;
        if (x >= 0 && x < W) f.at(x, y) = c;
    }
}

void Engine::drawBall(Frame& f, float s) const {
    int bx = (int)floorf(ballX_);
    int by = (int)floorf(ballY_);
    if (bx < 0) bx = 0;
    if (bx >= W) bx = W - 1;
    if (by >= 0 && by < H) f.at(bx, by) = scaleC(COL_BALL, s);
}

void Engine::drawPips(Frame& f, float s, bool blinkNewest) const {
    for (int p = 0; p < 2; ++p) {
        int y = PIP_ROW[p];
        Color c = scaleC(p == 0 ? COL_P1 : COL_P2, s);
        for (int i = 0; i < st_.score[p] && i < PONG_WIN_SCORE; ++i) {
            bool newest = blinkNewest && p == st_.goalBy && i == st_.score[p] - 1;
            if (newest && ((timeMs_ / BLINK_MS) & 1)) continue;
            f.at(2 * i, y) = c;
            f.at(2 * i + 1, y) = c;
        }
    }
}

void Engine::drawScene(Frame& f, float s, bool withBall) const {
    drawPaddle(f, 0, s);
    drawPaddle(f, 1, s);
    if (NET_DOTS) {
        int bx = (int)floorf(ballX_), by = (int)floorf(ballY_);
        Color n = scaleC(COL_NET, s);
        if (!(withBall && by == NET_ROW && bx == NET_COL_L)) f.at(NET_COL_L, NET_ROW) = n;
        if (!(withBall && by == NET_ROW && bx == NET_COL_R)) f.at(NET_COL_R, NET_ROW) = n;
    }
    drawPips(f, s, false);
    if (withBall) drawBall(f, s);
}

void Engine::render(Frame& out) const {
    out.clear();
    switch (st_.state) {
        case GS_LINK_WAIT: {
            // never linked: stay black, the glue owns the identify pattern
            if (!everLinked_) return;
            float s = LINKWAIT_BREATHE_BASE + LINKWAIT_BREATHE_AMP * (0.5f + 0.5f * sinf(timeMs_ * LINKWAIT_BREATHE_RATE));
            drawScene(out, s, prevState_ == GS_PLAYING || prevState_ == GS_COUNTDOWN);
            return;
        }
        case GS_READY_CHECK: {
            if (attract_) {
                drawScene(out, ATTRACT_DIM, true);
                return;
            }
            if (NET_DOTS) {
                out.at(NET_COL_L, NET_ROW) = COL_NET;
                out.at(NET_COL_R, NET_ROW) = COL_NET;
            }
            drawPips(out, 1.0f, false);
            for (int p = 0; p < 2; ++p) {
                int y = paddleRow(p);
                Color c = p == 0 ? COL_P1 : COL_P2;
                if (holdMs_[p] >= READY_HOLD_MS) {
                    for (int x = 0; x < W; ++x) out.at(x, y) = c;
                } else if (heldNow_[p]) {
                    // fill left->right with a bright cursor at the edge
                    int cols = (int)(holdMs_[p] * W / READY_HOLD_MS);
                    for (int x = 0; x < W; ++x) {
                        if (x < cols) out.at(x, y) = c;
                        else if (x == cols) out.at(x, y) = COL_BALL;
                        else out.at(x, y) = scaleC(c, DIM_LEVEL / 255.0f);
                    }
                } else {
                    // 0.5 Hz pulse: 2*pi*0.5/1000 rad per ms
                    float lvl = (DIM_LEVEL + (255 - DIM_LEVEL) *
                                 (0.5f + 0.5f * sinf(timeMs_ * READY_PULSE_RATE))) / 255.0f;
                    for (int x = 0; x < W; ++x) out.at(x, y) = scaleC(c, lvl);
                }
            }
            return;
        }
        case GS_COUNTDOWN: {
            drawScene(out, 1.0f, false);
            // glued ball blinks ~4 Hz
            if (((stateMs_ / 125) & 1) == 0) drawBall(out, 1.0f);
            return;
        }
        case GS_PLAYING:
            drawScene(out, 1.0f, true);
            return;
        case GS_POINT_FLASH: {
            // two full-wall pulses in scorer color, then pips (newest blinking)
            Color c = st_.goalBy == 0 ? COL_P1 : COL_P2;
            uint32_t t = stateMs_;
            bool on = t < PF_PULSE1_END || (t >= PF_PULSE2_START && t < PF_PULSE2_END);
            if (t < PF_PULSES_END) {
                if (on)
                    for (int i = 0; i < W * H; ++i) out.px[i] = c;
                return;
            }
            drawPaddle(out, 0, 1.0f);
            drawPaddle(out, 1, 1.0f);
            drawPips(out, 1.0f, true);
            return;
        }
        case GS_GAME_OVER: {
            drawPips(out, 1.0f, false);
            // 3-row band chasing from the winner's edge, ~8 rows/s
            Color c = st_.winner == 0 ? COL_P1 : COL_P2;
            int off = (int)((stateMs_ * GAMEOVER_SWEEP_ROWS_PER_S / 1000) % (uint32_t)(H + GAMEOVER_BAND));
            for (int k = 0; k < GAMEOVER_BAND; ++k) {
                int r = off - k;
                if (r < 0 || r >= H) continue;
                int y = st_.winner == 0 ? H - 1 - r : r;
                for (int x = 0; x < W; ++x) out.at(x, y) = c;
            }
            return;
        }
        default:
            return;
    }
}

}  // namespace pong

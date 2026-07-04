#include "ring_fx.h"
#include "pong_shared.h"
#include <math.h>

namespace {

// per-ring one-shot effect kinds (goalFxKind_)
constexpr uint8_t FX_NONE    = 0;
constexpr uint8_t FX_LOCKPOP = 1;   // ready lock-in brightness pop
constexpr uint8_t FX_CONCEDE = 2;   // 3x 200 ms red flashes (on/off pairs)
constexpr uint8_t FX_SWEEP   = 3;   // scorer "+1" full sweep landing on new dial pos

constexpr uint32_t FX_DUR[4] = {0, 150, 1200, 300};

// every animation period (blink 1000, breathe 2000, rotate 4000, spin 500)
// divides this, so the clock wraps phase-continuously and stays float-safe
constexpr uint32_t ANIM_WRAP_MS = 4000;

}  // namespace

void RingFX::apply(const PongFeedbackPacket& snap, const PongCues& cues,
                   bool linked, uint32_t dtMs) {
    if (!started_) {
        started_ = true;
        for (int i = 0; i < 2; ++i)
            if (k_[i]) k_[i]->enableRing(true);
    }
    animMs_ = (animMs_ + dtMs) % ANIM_WRAP_MS;

    const uint8_t st = snap.gameState;
    if (st != lastState_) {
        lastState_ = st;
        stateMs_ = 0;
        served_ = false;
        goalFxKind_[0] = goalFxKind_[1] = FX_NONE;
    } else {
        stateMs_ += dtMs;
    }

    // tick + expire running one-shots
    for (int i = 0; i < 2; ++i) {
        if (goalFxKind_[i] == FX_NONE) continue;
        goalFxMs_[i] += dtMs;
        if (goalFxMs_[i] >= FX_DUR[goalFxKind_[i]]) goalFxKind_[i] = FX_NONE;
    }

    // start one-shots from this poll's cues
    if (st == GS_READY_CHECK) {
        if (cues.lockP1) { goalFxKind_[0] = FX_LOCKPOP; goalFxMs_[0] = 0; }
        if (cues.lockP2) { goalFxKind_[1] = FX_LOCKPOP; goalFxMs_[1] = 0; }
    }
    if (cues.goal && cues.goalBy < 2 && (st == GS_PLAYING || st == GS_POINT_FLASH)) {
        const int scorer = cues.goalBy;
        goalFxKind_[scorer] = FX_SWEEP;
        goalFxMs_[scorer] = 0;
        goalFxKind_[1 - scorer] = FX_CONCEDE;
        goalFxMs_[1 - scorer] = 0;
    }
    if (cues.serve) served_ = true;

    for (int i = 0; i < 2; ++i) {
        const pong::Color pc = (i == 0) ? COL_P1 : COL_P2;
        uint8_t r = pc.r, g = pc.g, b = pc.b;
        uint8_t bright = 0;
        float pos = 0.0f;

        if (!linked) {
            // slow red blink, dot parked at 12 o'clock
            r = 255; g = 0; b = 0;
            bright = (animMs_ / 500) % 2 ? 60 : 5;
        } else switch (st) {
        case GS_READY_CHECK: {
            const uint8_t rp = snap.readyProgress[i];
            if (rp == 0) {
                // "touch me": slow rotate, 1 rev / 4 s
                bright = 40;
                pos = fmodf(animMs_ * 0.003f, 12.0f);
            } else if (rp < 255) {
                // clock hand sweeping to 12 o'clock with the hold
                bright = 90;
                pos = rp * (12.0f / 255.0f);
            } else {
                // locked: steady at 12 (11.99 avoids the ==0 wrap)
                bright = RING_BRIGHT_MAX;
                pos = 11.99f;
            }
            if (goalFxKind_[i] == FX_LOCKPOP) {
                bright = 255;
                pos = 11.99f;
            }
            break;
        }
        case GS_COUNTDOWN:
            // dot steps 12 -> 8 -> 4 with the 500 ms ticks, snaps to 0 on serve
            bright = 90;
            pos = served_ ? 0.0f
                          : (stateMs_ < 500 ? 11.99f : stateMs_ < 1000 ? 8.0f : 4.0f);
            break;
        case GS_PLAYING:
        case GS_POINT_FLASH:
            // score dial: 0/4/8 for first-to-3
            bright = RING_BRIGHT_GAME;
            pos = snap.score[i] * 4.0f;
            if (goalFxKind_[i] == FX_CONCEDE) {
                r = 255; g = 0; b = 0;
                bright = (goalFxMs_[i] / 200) % 2 ? 8 : RING_BRIGHT_MAX;
            } else if (goalFxKind_[i] == FX_SWEEP) {
                // full revolution plus landing exactly on the new dial pos
                bright = RING_BRIGHT_MAX;
                pos = fmodf((goalFxMs_[i] / 300.0f) * (12.0f + pos), 12.0f);
            }
            break;
        case GS_GAME_OVER:
            if ((int)snap.winner == i) {
                // gold spin, 2 rev/s
                r = 255; g = 180; b = 0;
                bright = RING_BRIGHT_MAX;
                pos = fmodf(animMs_ * 0.024f, 12.0f);
            } else {
                // loser: dim red, static at final score pos
                r = 255; g = 0; b = 0;
                bright = 12;
                pos = snap.score[i] * 4.0f;
            }
            break;
        case GS_LINK_WAIT:
        default:
            // dim white breathe, ~0.5 Hz between 10 and 50
            r = 255; g = 255; b = 255;
            bright = (uint8_t)(30.0f + 20.0f * sinf(animMs_ * (6.2831853f / 2000.0f)));
            break;
        }

        push(i, r, g, b, bright, pos);
    }
}

void RingFX::push(int i, uint8_t r, uint8_t g, uint8_t b, uint8_t bright, float pos) {
    RotaryEncoder* k = k_[i];
    if (!k) return;
    Sent& s = sent_[i];
    if (!s.valid || r != s.r || g != s.g || b != s.b) k->setRingColor(r, g, b);
    if (!s.valid || bright != s.bright) k->setRingBrightness(bright);
    if (!s.valid || pos != s.pos) k->setRingPosition(pos);
    s.r = r; s.g = g; s.b = b;
    s.bright = bright;
    s.pos = pos;
    s.valid = true;
}

#pragma once
#include <stdint.h>
#include <string.h>
#include "pong_proto.h"
#include "net_config.h"

// Pure, header-only. Turns the raw 20 Hz feedback stream into a latest snapshot
// plus EXACTLY-ONCE cues for the presenters (AudioDirector / ScoreDisplay / RingFX).
// Wrapping-counter latch: fires on uint8_t(new - last) > 0, then latches.
// After boot or a feedback gap > FEEDBACK_TIMEOUT_MS it ADOPTS all counters
// without firing — reconnects never replay jingles.

struct PongCues {
    bool    stateChanged;  uint8_t state, prevState;
    bool    scoreChanged;
    bool    lockP1, lockP2;      // readyProgress[i] reached 255 (once per hold)
    bool    paddleHit;
    uint8_t rallyHits;           // paddle hits since the last serve (for pitch creep)
    bool    wallBounce;
    bool    goal;   uint8_t goalBy;
    bool    win;    uint8_t winner;
    bool    serve;  uint8_t servingPlayer;
    bool    linkLost, linkRestored;
};

class FeedbackTracker {
public:
    // Validate + latch one datagram. Returns true if accepted.
    bool feed(const void* buf, int len, uint32_t nowMs) {
        if (len != (int)sizeof(PongFeedbackPacket)) return false;
        PongFeedbackPacket p;
        memcpy(&p, buf, sizeof(p));
        if (p.magic != PONG_MAGIC || p.version != PONG_VERSION || p.type != PKT_FEEDBACK)
            return false;

        const bool gap = !everRx_ || (nowMs - lastRxMs_) > FEEDBACK_TIMEOUT_MS;
        if (!gap && (int32_t)(p.seq - snap_.seq) <= 0) return false;  // stale/duplicate

        if (gap) {
            // Adopt everything silently — no cue replay after boot/reconnect.
            snap_ = p;
            pendingState_ = false;
            pendingScore_ = false;
            hitsSinceServe_ = 0;
            lockFired_[0] = (p.readyProgress[0] == 255);
            lockFired_[1] = (p.readyProgress[1] == 255);
        } else {
            diff(p);
            snap_ = p;
        }
        everRx_ = true;
        lastRxMs_ = nowMs;
        return true;
    }

    // Consume cues accumulated since the last poll (clears them). Also computes
    // link-lost / link-restored transitions from wall-clock silence.
    PongCues poll(uint32_t nowMs) {
        PongCues c = {};
        const bool linkedNow = everRx_ && (nowMs - lastRxMs_) <= FEEDBACK_TIMEOUT_MS;
        if (linkedNow != linked_) {
            (linkedNow ? c.linkRestored : c.linkLost) = true;
            linked_ = linkedNow;
        }

        c.stateChanged = pendingState_;  c.state = snap_.gameState;  c.prevState = prevState_;
        c.scoreChanged = pendingScore_;
        c.lockP1 = pendingLock_[0];
        c.lockP2 = pendingLock_[1];
        c.paddleHit = pendingHit_;       c.rallyHits = hitsSinceServe_;
        c.wallBounce = pendingBounce_;
        c.goal = pendingGoal_;           c.goalBy = snap_.goalBy;
        c.win = pendingWin_;             c.winner = snap_.winner;
        c.serve = pendingServe_;         c.servingPlayer = snap_.servingPlayer;

        pendingState_ = pendingScore_ = pendingHit_ = pendingBounce_ = false;
        pendingGoal_ = pendingWin_ = pendingServe_ = false;
        pendingLock_[0] = pendingLock_[1] = false;
        return c;
    }

    const PongFeedbackPacket& snapshot() const { return snap_; }
    bool linked() const { return linked_; }

private:
    void diff(const PongFeedbackPacket& p) {
        if (p.gameState != snap_.gameState) {
            pendingState_ = true;
            prevState_ = snap_.gameState;
        }
        if (p.score[0] != snap_.score[0] || p.score[1] != snap_.score[1])
            pendingScore_ = true;

        // Ready lock-in: crossing to 255 fires once; release (progress drops) re-arms.
        for (int i = 0; i < 2; ++i) {
            if (p.readyProgress[i] == 255 && !lockFired_[i]) {
                pendingLock_[i] = true;
                lockFired_[i] = true;
            } else if (p.readyProgress[i] < 255) {
                lockFired_[i] = false;
            }
        }

        const uint8_t serveD = (uint8_t)(p.serveSeq - snap_.serveSeq);
        if (serveD > 0) {
            pendingServe_ = true;
            hitsSinceServe_ = 0;
        }
        const uint8_t hitD = (uint8_t)(p.paddleHitSeq - snap_.paddleHitSeq);
        if (hitD > 0) {
            pendingHit_ = true;
            hitsSinceServe_ = (uint8_t)(hitsSinceServe_ + hitD);
        }
        if ((uint8_t)(p.wallBounceSeq - snap_.wallBounceSeq) > 0) pendingBounce_ = true;
        if ((uint8_t)(p.goalSeq - snap_.goalSeq) > 0) pendingGoal_ = true;
        if ((uint8_t)(p.winSeq - snap_.winSeq) > 0) pendingWin_ = true;
    }

    PongFeedbackPacket snap_ = {};
    uint32_t lastRxMs_ = 0;
    bool everRx_ = false;
    bool linked_ = false;

    uint8_t prevState_ = GS_LINK_WAIT;
    bool pendingState_ = false, pendingScore_ = false;
    bool pendingHit_ = false, pendingBounce_ = false;
    bool pendingGoal_ = false, pendingWin_ = false, pendingServe_ = false;
    bool pendingLock_[2] = {false, false};
    bool lockFired_[2] = {false, false};
    uint8_t hitsSinceServe_ = 0;
};

#include "audio_director.h"

// Local mirrors of pong_config.h flags — that header is screen-side only
// (pulls in pong_frame.h) and is not symlinked into this module.
namespace {
constexpr bool WALL_BLIP_ENABLED   = true;
constexpr bool RALLY_PULSE_ENABLED = false;

// suppression windows sized to each motif/jingle (isPlaying() alone misses
// queued playNote chains and the gaps between RTTTL notes)
constexpr uint32_t LOCK_MOTIF_WINDOW_MS = 180;
constexpr uint32_t CHORD_WINDOW_MS      = 350;
constexpr uint32_t GOAL_WINDOW_MS       = 600;
constexpr uint32_t STING_WINDOW_MS      = 600;
constexpr uint32_t WIN_WINDOW_MS        = 2000;
constexpr uint32_t RESTORE_WINDOW_MS    = 120;

constexpr uint32_t COUNTDOWN_TICK_MS    = 500;
constexpr uint32_t STING_DELAY_MS       = 700;
constexpr uint32_t LINK_TONE_GAP_MS     = 200;
constexpr uint32_t PULSE_SUPPRESS_MS    = 150;

constexpr char RTTTL_GOAL_P1[]  = "p1:d=16,o=5,b=200:c,e,g,8c6";
constexpr char RTTTL_GOAL_P2[]  = "p2:d=16,o=5,b=200:e,g,b,8e6";
constexpr char RTTTL_MATCH_PT[] = "mp:d=16,o=5,b=120:8p,g,8g#";
constexpr char RTTTL_CHAMP[]    = "champ:d=8,o=5,b=180:g,c6,e6,4g6,8e6,2g6";
}

void AudioDirector::startJingle(uint32_t lenMs, bool big) {
    jingleActive_ = true;
    jingleMs_ = 0;
    jingleLenMs_ = lenMs;
    bigJingle_ = big;
}

// blips never interrupt anything: gate on the jingle window AND live audio
bool AudioDirector::blipFree() const {
    return !jingleActive_ && !spk_->isPlaying();
}

void AudioDirector::apply(const PongFeedbackPacket& snap, const PongCues& cues, uint32_t dtMs) {
    if (!spk_) return;

    // ---- advance dt-driven timers / fire pending scheduled sounds ----
    if (jingleActive_) {
        jingleMs_ += dtMs;
        if (jingleMs_ >= jingleLenMs_) jingleActive_ = false;
    }
    if (sinceBlipMs_ < 100000u) sinceBlipMs_ += dtMs;

    // second half of the link-lost double tone
    if (linkTonePending_) {
        linkToneMs_ += dtMs;
        if (linkToneMs_ >= LINK_TONE_GAP_MS) {
            linkTonePending_ = false;
            if (!jingleActive_) spk_->playTone(NOTE_C4, 120);
        }
    }

    // match-point sting, 700 ms after the qualifying goal
    if (stingPending_) {
        stingMs_ += dtMs;
        if (stingMs_ >= STING_DELAY_MS) {
            stingPending_ = false;
            if (!jingleActive_) {
                spk_->stop();
                spk_->playRTTTL(RTTTL_MATCH_PT);
                startJingle(STING_WINDOW_MS, true);
            }
        }
    }

    // countdown tick at +500 ms (entry tick played on the state change below)
    if (inCountdown_ && countdownTicksDone_ < 2) {
        countdownMs_ += dtMs;
        if (countdownMs_ >= COUNTDOWN_TICK_MS) {
            countdownTicksDone_ = 2;
            if (!jingleActive_) spk_->playTone(NOTE_G4, 60);
        }
    }

    // ---- link transitions ----
    if (cues.linkLost) {
        inCountdown_ = false;
        stingPending_ = false;
        if (!jingleActive_) spk_->playTone(NOTE_C4, 120);
        linkTonePending_ = true;
        linkToneMs_ = 0;
    }
    if (cues.linkRestored && !jingleActive_) {
        spk_->playNote(NOTE_C5, 60);
        spk_->playNote(NOTE_G5, 60);
        startJingle(RESTORE_WINDOW_MS, false);
    }

    // ---- WIN > POINT: jingles may cut anything lower ----
    if (cues.win) {
        stingPending_ = false;
        inCountdown_ = false;
        spk_->stop();
        spk_->playRTTTL(RTTTL_CHAMP);
        startJingle(WIN_WINDOW_MS, true);
    } else if (cues.goal && cues.goalBy < 2) {
        spk_->stop();
        spk_->playRTTTL(cues.goalBy == 0 ? RTTTL_GOAL_P1 : RTTTL_GOAL_P2);
        startJingle(GOAL_WINDOW_MS, true);
        // this goal put the scorer on match point -> delayed tension sting
        if (snap.score[cues.goalBy] == PONG_WIN_SCORE - 1) {
            stingPending_ = true;
            stingMs_ = 0;
        }
    }

    // ---- ready locks: chord when this lock completes the pair ----
    if (cues.lockP1 || cues.lockP2) {
        const bool both = snap.readyProgress[0] == 255 && snap.readyProgress[1] == 255;
        if (both) {
            // may replace a half-played lock motif, never a goal/win jingle
            if (!(jingleActive_ && bigJingle_)) {
                spk_->stop();
                float chord[3] = {NOTE_C5, NOTE_E5, NOTE_G5};
                spk_->playChord(chord, 3, 350);
                startJingle(CHORD_WINDOW_MS, false);
            }
        } else if (!jingleActive_) {
            if (cues.lockP1) {
                spk_->playNote(NOTE_C5, 70);
                spk_->playNote(NOTE_E5, 110);
            } else {
                spk_->playNote(NOTE_E5, 70);
                spk_->playNote(NOTE_G5, 110);
            }
            startJingle(LOCK_MOTIF_WINDOW_MS, false);
        }
    }

    // ---- countdown entry + serve launch ----
    if (cues.stateChanged) {
        if (cues.state == GS_COUNTDOWN) {
            inCountdown_ = true;
            countdownMs_ = 0;
            countdownTicksDone_ = 1;
            if (!jingleActive_) spk_->playTone(NOTE_G4, 60);
        } else {
            inCountdown_ = false;
        }
    }
    if (cues.serve) {
        inCountdown_ = false;
        if (!jingleActive_) spk_->playTone(NOTE_G5, 140);
    }

    // ---- blips (lowest priority; at most one per call) ----
    bool blipped = false;
    if (cues.paddleHit && blipFree()) {
        hitFlip_ = !hitFlip_;
        const float base = hitFlip_ ? 880.0f : 659.0f;
        const float creep = 15.0f * (cues.rallyHits > 10 ? 10 : cues.rallyHits);
        spk_->playTone(base + creep, 30);
        sinceBlipMs_ = 0;
        blipped = true;
    }
    if (WALL_BLIP_ENABLED && cues.wallBounce && !blipped && blipFree()) {
        spk_->playTone(NOTE_E4, 20);
        sinceBlipMs_ = 0;
        blipped = true;
    }
    if (cues.stateChanged && cues.state == GS_READY_CHECK && !blipped && blipFree()) {
        spk_->playTone(NOTE_C5, 45);
        sinceBlipMs_ = 0;
    }

    // ---- optional rally heartbeat (compile-time off by default) ----
    if (RALLY_PULSE_ENABLED) {
        if (snap.gameState == GS_PLAYING) {
            pulseAccMs_ += dtMs;
            const uint32_t hits = cues.rallyHits > 12 ? 12u : cues.rallyHits;
            const uint32_t interval = 600u - (350u * hits) / 12u;
            if (pulseAccMs_ >= interval) {
                pulseAccMs_ = 0;
                if (sinceBlipMs_ >= PULSE_SUPPRESS_MS && blipFree())
                    spk_->playTone(NOTE_C3, 25);
            }
        } else {
            pulseAccMs_ = 0;
        }
    }
}

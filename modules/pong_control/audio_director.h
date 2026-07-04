#pragma once
#include <stdint.h>
#include "speaker.h"           // lib/atech_speaker (LDF resolves; module is placed)
#include "pong_proto.h"
#include "feedback_tracker.h"

// Presenter: game state/cues -> speaker. PUBLIC API FROZEN (pong_control.cpp calls it).
// Implementation contract (see docs/PLAN.md audio table for the exact motifs):
//   - event motifs only; NEVER play on boot; NEVER unguarded tones per tick
//   - priority WIN > POINT > READY/SERVE cues > blips; stop() before any RTTTL;
//     suppress blips while a jingle plays (isPlaying() + own flag)
//   - local countdown ticks: on entering GS_COUNTDOWN, schedule 3-2-1 ticks by dtMs
//   - volume stays <= 0.5f (bundled setup already set 0.4f)

class AudioDirector {
public:
    void begin(Speaker* spk) { spk_ = spk; }
    void apply(const PongFeedbackPacket& snap, const PongCues& cues, uint32_t dtMs);

    // ---------------- lane D owns everything below ----------------
private:
    Speaker* spk_ = nullptr;
    bool jingleActive_ = false;
    uint32_t jingleMs_ = 0;       // time since jingle start (suppression window)
    uint32_t countdownMs_ = 0;    // local 3-2-1 scheduler inside GS_COUNTDOWN
    int countdownTicksDone_ = 0;
    bool inCountdown_ = false;
    uint32_t pulseAccMs_ = 0;     // optional rally pulse accumulator
};

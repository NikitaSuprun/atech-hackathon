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
//   - volume <= 0.5f; begin() lowers the setup's 0.4f default to 0.2f

class AudioDirector {
public:
    void begin(Speaker* spk) {
        spk_ = spk;
        spk_->setVolume(0.2f);  // quieter than the module setup's default 0.4f
    }
    void apply(const PongFeedbackPacket& snap, const PongCues& cues, uint32_t dtMs);

    // ---------------- lane D owns everything below ----------------
private:
    void startJingle(uint32_t lenMs, bool big);
    bool blipFree() const;

    Speaker* spk_ = nullptr;
    bool jingleActive_ = false;
    uint32_t jingleMs_ = 0;       // time since jingle start (suppression window)
    uint32_t jingleLenMs_ = 0;
    bool bigJingle_ = false;      // goal/sting/win window: also suppresses the ready chord
    uint32_t countdownMs_ = 0;    // local 3-2-1 scheduler inside GS_COUNTDOWN
    int countdownTicksDone_ = 0;
    bool inCountdown_ = false;
    bool stingPending_ = false;   // match-point sting scheduled 700 ms after a goal
    uint32_t stingMs_ = 0;
    bool linkTonePending_ = false;  // second link-lost tone, 200 ms after the first
    uint32_t linkToneMs_ = 0;
    bool hitFlip_ = false;        // alternates hit-blip base 880/659 Hz
    uint32_t sinceBlipMs_ = 1000; // rally-pulse suppression clock
    uint32_t pulseAccMs_ = 0;     // optional rally pulse accumulator
};

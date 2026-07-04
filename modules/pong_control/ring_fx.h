#pragma once
#include <stdint.h>
#include "rotary_encoder.h"    // lib/atech_rotary_encoder (LDF resolves; module is placed)
#include "pong_proto.h"
#include "feedback_tracker.h"

// Presenter: game state -> both knobs' 12-LED rings. PUBLIC API FROZEN.
// The ring is ONE glowing dot with a trail: setRingColor(r,g,b),
// setRingBrightness(0..255), setRingPosition(float 0..12 CCW from 12 o'clock;
// NAN or negative = re-couple to knob rotation). RingFX is the ONLY owner of
// ring calls — keep an explicit override position in every state except
// GS_PLAYING (where re-coupling to the knob is the intended effect).
// Choreography table in docs/PLAN.md: attract breathe, ready hold-sweep
// (pos = readyProgress*12/255), countdown steps 12->8->4->0, score dial
// (pos = score*4) during play, goal flash/spin, winner spin, link-lost red blink.
// Brightness ceiling RING_BRIGHT_MAX (rings sit at eye level).

class RingFX {
public:
    void begin(RotaryEncoder* k1, RotaryEncoder* k2) { k_[0] = k1; k_[1] = k2; }
    void apply(const PongFeedbackPacket& snap, const PongCues& cues,
               bool linked, uint32_t dtMs);

    // ---------------- lane F owns everything below ----------------
private:
    RotaryEncoder* k_[2] = {nullptr, nullptr};
    uint32_t animMs_ = 0;         // global animation clock
    uint32_t goalFxMs_[2] = {0, 0};  // per-ring one-shot effect timers
    uint8_t  goalFxKind_[2] = {0, 0};
};

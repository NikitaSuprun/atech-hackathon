#pragma once
#include <stdint.h>
#include "pong_frame.h"
#include "pong_proto.h"

// Pure C++ game engine — NO Arduino, NO wall-clock, NO heap. The same files
// compile in ESP32 firmware and in the desktop simulator (sim/).
// PUBLIC API BELOW IS FROZEN (glue + sim + tools depend on it).
// Everything under "lane A owns" may be reshaped freely by the engine implementer.

namespace pong {

struct EngineInputs {
    int32_t knobPos[2];       // raw absolute cumulative detents (accel disabled on sender)
    bool    held[2];          // knob push-button levels
    bool    controllerLinked; // false => behave as GS_LINK_WAIT (freeze ball, decay holds)
    bool    controllerRebooted; // one tick true => re-anchor knob baselines, no paddle jump
};

// Mirrors the PongFeedbackPacket payload; glue copies field-by-field into the packet.
struct EngineStatus {
    uint8_t state;            // PongGameState
    uint8_t score[2];
    uint8_t heldBits;         // echo of inputs (bit0 = P1, bit1 = P2)
    uint8_t readyProgress[2]; // 0..255 = holdMs / READY_HOLD_MS, capped & held while held
    uint8_t paddleHitSeq;     // wrapping counters — see pong_proto.h rules
    uint8_t wallBounceSeq;
    uint8_t goalSeq, goalBy;
    uint8_t winSeq, winner;
    uint8_t serveSeq, servingPlayer;
};

class Engine {
public:
    void reset(uint32_t rngSeed);                       // -> GS_LINK_WAIT, scores 0-0
    void tick(const EngineInputs& in, uint32_t dtMs);   // fixed-step; glue calls at TICK_HZ
    void render(Frame& out) const;                      // repaint full frame from state
    EngineStatus status() const { return st_; }

    // ---------------- lane A owns everything below ----------------
private:
    void enter(uint8_t s);
    void applyKnobs(const EngineInputs& in);
    void serveBall();
    void bounceOffPaddle(int p, float u);
    uint32_t rnd();                                     // xorshift32

    EngineStatus st_ = {};
    uint32_t rng_ = 1;
    uint32_t stateMs_ = 0;                              // ms in current state
    uint32_t idleMs_ = 0;                               // READY_CHECK idle -> attract rally
    float ballX_ = 0, ballY_ = 0, velX_ = 0, velY_ = 0, speed_ = 0;
    float padX_[2] = {0, 0};                            // paddle left edge, float
    int32_t lastKnob_[2] = {0, 0};
    bool knobInit_ = false;
    uint32_t holdMs_[2] = {0, 0};
    uint32_t staleMs_ = 0;
    // attract-mode demo AI state
    bool attract_ = false;
    float aiTarget_[2] = {0, 0};
};

}  // namespace pong

#pragma once
#include <stdint.h>
#include "st7735_tft.h"        // lib/atech_st7735_tft (LDF resolves; module is placed)
#include "pong_proto.h"
#include "feedback_tracker.h"

// Presenter: game state -> 160x80 TFT. PUBLIC API FROZEN (pong_control.cpp calls it).
// Implementation contract (see docs/PLAN.md TFT layout table for coordinates):
//   - canvas mode: clear() -> draw -> display(); a full display() costs ~6-10 ms of
//     SPI, so REDRAW ONLY when the quantized model changes: state, scores,
//     heldBits, readyProgress/16 buckets, link status. Never per tick.
//   - begin() draws the boot frame ("PONG / connecting") — drawing on boot is
//     fine (sound is not).
//   - banners derive context: READY_CHECK + 0-0 -> "HOLD TO START";
//     READY_CHECK + scores -> "HOLD FOR NEXT POINT"; GAME_OVER -> "Px WINS —
//     HOLD FOR REMATCH"; link lost -> "SEARCHING WALL...".

class ScoreDisplay {
public:
    void begin(ST7735_TFT* tft);
    void apply(const PongFeedbackPacket& snap, const PongCues& cues, bool linked);

    // ---------------- lane E owns everything below ----------------
private:
    struct Model {               // quantized; operator!= gates the redraw
        uint8_t state = 0xFF, s0 = 0xFF, s1 = 0xFF, held = 0xFF;
        uint8_t hold0q = 0xFF, hold1q = 0xFF;
        uint8_t winner = 0xFF;
        bool linked = false;
        bool operator!=(const Model& o) const {
            return state != o.state || s0 != o.s0 || s1 != o.s1 || held != o.held ||
                   hold0q != o.hold0q || hold1q != o.hold1q || winner != o.winner ||
                   linked != o.linked;
        }
    };
    void draw(const Model& m);
    ST7735_TFT* tft_ = nullptr;
    Model last_;
    // scorer of the current POINT_FLASH (not in the frozen Model)
    uint8_t goalBy_ = PONG_NOBODY;
};

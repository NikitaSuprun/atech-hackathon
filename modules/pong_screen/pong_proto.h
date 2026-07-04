#pragma once
#include <stdint.h>

// CANONICAL wire protocol. The copy in modules/pong_control/ is a SYMLINK to this
// file — edit only here. tools/fake_controller.py mirrors these layouts with
// python struct format '<' (little-endian, packed).
//
// Both ends are ESP32 (little-endian); packed structs go on the wire as-is.
// Rules that make UDP loss/duplication/reorder harmless:
//   - knob positions are ABSOLUTE cumulative detents -> any packet fully
//     re-establishes input state (server applies clamped deltas).
//   - button state travels as HELD LEVELS, re-established every packet.
//   - one-shot events are wrapping uint8 counters; the receiver fires on
//     uint8_t(new - last) > 0 and latches. After a boot or a feedback gap the
//     receiver ADOPTS counters without firing (no replayed jingles).

constexpr uint32_t PONG_MAGIC   = 0x474E4F50u;  // wire bytes: 'P','O','N','G'
constexpr uint8_t  PONG_VERSION = 2;
constexpr uint8_t  PKT_INPUT    = 0x01;
constexpr uint8_t  PKT_FEEDBACK = 0x02;

constexpr uint8_t PONG_MAX_CONTROLLERS = 4;  // controllerId 0..3 (0 = the dual-knob console)
constexpr uint8_t PONG_WIN_SCORE       = 3;  // match = first to 3
constexpr uint8_t PONG_NOBODY          = 0xFF;

// heldBits masks (bit0 = P1, bit1 = P2); readyProgress[] full scale
constexpr uint8_t PONG_HELD_P1         = 0x01;
constexpr uint8_t PONG_HELD_P2         = 0x02;
constexpr uint8_t READY_PROGRESS_FULL  = 255;

enum PongGameState : uint8_t {
    GS_LINK_WAIT   = 0,  // no controller input yet / input silent >= 1 s (ball frozen)
    GS_READY_CHECK = 1,  // both players must hold knob buttons READY_HOLD_MS
    GS_COUNTDOWN   = 2,  // ball glued to server's paddle, 3-2-1 ticks, then launch
    GS_PLAYING     = 3,
    GS_POINT_FLASH = 4,  // goal celebration, auto -> READY_CHECK (or GAME_OVER)
    GS_GAME_OVER   = 5,  // winner set; both-hold -> scores reset -> COUNTDOWN
};

constexpr uint8_t PONG_STATE_COUNT = 6;

#pragma pack(push, 1)

struct PongInputPacket {          // controller -> screen, 50 Hz          off sz
    uint32_t magic;               // PONG_MAGIC                             0  4
    uint8_t  version;             // PONG_VERSION                           4  1
    uint8_t  type;                // PKT_INPUT                              5  1
    uint8_t  controllerId;        // 0 for this board; future boards 1..3   6  1
    uint8_t  heldBits;            // bit0 = P1 button held, bit1 = P2       7  1
    uint32_t seq;                 // per-boot monotonic                     8  4
    uint32_t uptimeMs;            // sender millis(); reboot detection     12  4
    int32_t  knobPos[2];          // ABSOLUTE cumulative detents           16  8
};                                // total 24 bytes
static_assert(sizeof(PongInputPacket) == 24, "input packet layout");

struct PongFeedbackPacket {       // screen -> controller, 20 Hz          off sz
    uint32_t magic;               //                                        0  4
    uint8_t  version;             //                                        4  1
    uint8_t  type;                // PKT_FEEDBACK                           5  1
    uint8_t  targetControllerId;  //                                        6  1
    uint8_t  gameState;           // PongGameState (authoritative)          7  1
    uint32_t seq;                 // reorder guard                          8  4
    uint8_t  score[2];            // authoritative                         12  2
    uint8_t  heldBits;            // echo of current held levels           14  1
    uint8_t  readyProgress[2];    // 0..255 = holdMs / READY_HOLD_MS       15  2
    // ---- wrapping one-shot event counters ----
    uint8_t  paddleHitSeq;        // ++ every paddle contact               17  1
    uint8_t  wallBounceSeq;       // ++ every side-wall bounce             18  1
    uint8_t  goalSeq;             // ++ every point scored                 19  1
    uint8_t  goalBy;              // scorer of goal #goalSeq (0/1/0xFF)    20  1
    uint8_t  winSeq;              // ++ every match win                    21  1
    uint8_t  winner;              // winner of win #winSeq (0/1/0xFF)      22  1
    uint8_t  serveSeq;            // ++ every serve launch                 23  1
    uint8_t  servingPlayer;       // next/current server (0/1/0xFF)        24  1
    uint8_t  pad;                 // = 0                                   25  1
};                                // total 26 bytes
static_assert(sizeof(PongFeedbackPacket) == 26, "feedback packet layout");

#pragma pack(pop)

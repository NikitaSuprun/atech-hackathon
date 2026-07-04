#pragma once
#include <stdint.h>

// CANONICAL cross-board contract (symlinked into modules/pong_control/ — edit only here).
// Holds ONLY what the screen and controller boards must agree on. Arduino-free and free
// of W/H/Frame, so the sim and both firmwares can include it directly.

namespace pong {

struct Color {
    uint8_t r, g, b;
};

}  // namespace pong

// Player identity colors (P1 cyan defends the bottom edge, P2 amber the top).
constexpr pong::Color COL_P1 = {0, 200, 255};
constexpr pong::Color COL_P2 = {255, 120, 0};

// Encoder-ring brightness tiers (rings sit at eye level).
constexpr uint8_t RING_BRIGHT_GAME    = 40;
constexpr uint8_t RING_BRIGHT_ATTRACT = 20;
constexpr uint8_t RING_BRIGHT_MAX     = 120;

// Audio cue toggles shared by both boards.
constexpr bool WALL_BLIP_ENABLED   = true;   // wall-bounce audio blip
constexpr bool RALLY_PULSE_ENABLED = false;  // heartbeat bg "music" (off by default)

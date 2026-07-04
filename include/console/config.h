#pragma once
#include <stdint.h>

// Console-wide geometry & timing. NO Arduino includes — this compiles in
// firmware, the desktop sim, and the WASM build unchanged.
//
// The LOGICAL screen the brain draws to. Physical LED knowledge (serpentine
// wiring, rotation, tile layout, panel count) lives ONLY in the screen adapter,
// never here and never in a game.

namespace console {

constexpr int SCREEN_W  = 6;
constexpr int SCREEN_H  = 18;
constexpr int SCREEN_PX = SCREEN_W * SCREEN_H;   // 108

// Fixed game/OS tick. The OS owns the loop; games get a dtMs each update.
constexpr int      TICK_HZ = 50;
constexpr uint32_t TICK_MS = 1000 / TICK_HZ;     // 20

}  // namespace console

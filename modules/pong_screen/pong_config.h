#pragma once
#include <stdint.h>
#include "pong_frame.h"

// THE tuning table — one file = the whole game feel. Pure (sim-safe).
// Hardware-day edits happen HERE (TILE_MAP, KNOB_SIGN, WALL_BRIGHTNESS) and in
// net_config.h (NET_CHANNEL); everything else is taste, tuned in the sim.

// ---------------- timing ----------------
constexpr int      TICK_HZ = 50;
constexpr uint32_t TICK_MS = 1000 / TICK_HZ;      // 20 ms fixed step
constexpr float    DT      = 1.0f / TICK_HZ;

// ---------------- paddles & knobs ----------------
constexpr int   PADDLE_W         = 2;             // px on the 6-wide edge (3 = casual mode)
constexpr float PADDLE_GRACE     = 0.35f;         // extra half-width; edge-graze = max english
constexpr float DETENTS_PER_CELL = 2.0f;          // full 4-cell sweep = 8 detents ~= 160 deg
constexpr int   KNOB_SIGN[2]     = {+1, +1};      // flip per side on hardware day
// Engine input contract: knob acceleration is DISABLED on the controller
// (setAcceleration(false,1) in the module setup template) — never undo that.

// ---------------- ball ----------------
constexpr float BALL_SPEED_START = 7.0f;          // cells/s (first crossing ~2.1 s on 18-tall)
constexpr float SPEEDUP_PER_HIT  = 1.07f;
constexpr float BALL_SPEED_MAX   = 22.0f;         // ~0.44 cells/tick — no tunneling
constexpr float ENGLISH_GAIN     = 0.85f;         // u -> horizontal component gain
constexpr float VY_MIN           = 0.55f;         // vertical fraction floor (no sideways stalls)
constexpr float SERVE_U[2]       = {0.35f, 0.70f}; // |serve english|, pick + sign randomized

// ---------------- match & ceremony (ms) ----------------
// WIN_SCORE lives in pong_proto.h (PONG_WIN_SCORE = 3) — it is wire-visible.
constexpr uint32_t COUNTDOWN_MS         = 1500;   // 3 ticks at 500 ms, launch on the last
constexpr uint32_t POINT_FLASH_MS       = 1200;
constexpr uint32_t WIN_CELEBRATION_MS   = 3000;   // then GAME_OVER waits for both-hold
constexpr uint32_t IDLE_TO_ATTRACT_MS   = 30000;  // READY_CHECK idle -> demo rally
constexpr uint32_t STALE_HOLD_RESET_MS  = 1000;   // stale link: freeze holds, then zero them

// ---------------- palette ----------------
// Wall brightness is hard-clamped to 51/255 (~20%) by the NeoPixel driver: effective
// PWM = value/5, so anything < ~25 quantizes to mud. Two tiers only:
// accents (saturated, >= 200) and dims (70-90). Dim = dim PALETTE ENTRY, never
// setBrightness (that would dim the ball too).
constexpr pong::Color COL_P1     = {0, 200, 255};   // cyan, defends bottom (y = H-1)
constexpr pong::Color COL_P2     = {255, 120, 0};   // amber, defends top (y = 0)
constexpr pong::Color COL_BALL   = {255, 255, 255};
constexpr pong::Color COL_NET    = {70, 70, 70};    // dots at mid-field, cols 1 & 4
constexpr pong::Color COL_LOSE   = {255, 0, 0};
constexpr uint8_t     DIM_LEVEL  = 80;
constexpr float       ATTRACT_DIM = 0.45f;

// ---------------- wall / compositor ----------------
constexpr int NUM_TILES = 12;
constexpr int TILE_DIM      = 3;                   // 3x3 LEDs per physical tile
constexpr int LEDS_PER_TILE = TILE_DIM * TILE_DIM; // 9 chips per tile
constexpr int TILE_BYTES    = LEDS_PER_TILE * (int)sizeof(pong::Color);  // 27 = packed RGB per tile
constexpr uint8_t WALL_BRIGHTNESS = 40;           // 0..51 (driver clamps at 51)
constexpr float HEARTBEAT_REPAINT_S = 2.0f;       // periodic full repaint heals WS2812 glitches

struct TileCfg {
    uint8_t tileRow;   // 0..5 (0 = top, P2's end)
    uint8_t tileCol;   // 0..1 (0 = left, facing the wall)
    uint8_t rot;       // quarter-turns CW the tile is physically mounted: 0/1/2/3
};

// Index order = WIRING order = port order 1,2,3,4,5,6,7,9,10,11,13,14
// (matches the pointer array in modules/pong_screen/module.yaml setup template).
// DEFAULT from known build: ports 1-6 = left column top->bottom, rot 0;
// ports 7,9,10,11,13,14 = right column top->bottom, mounted flipped = rot 2 (180 deg).
// CONFIRM/EDIT ON HARDWARE DAY via calibration mode (see docs/PLAN.md):
//   tileRow/tileCol = where the tile's identity color sits on the wall;
//   rot from the WHITE corner pixel: TL=0, TR=1, BR=2, BL=3.
// CALIBRATED on hardware 2026-07-04 ("cyan" variant of the convention finder):
// positions = ports 1-6 left col top->bottom, 7..14 right col top->bottom;
// rots = base reading +1 quarter-turn; chip chain serpentine (compositor.cpp).
constexpr TileCfg TILE_MAP[NUM_TILES] = {
    {0, 0, 2},  // port 1
    {1, 0, 2},  // port 2
    {2, 0, 2},  // port 3
    {3, 0, 2},  // port 4
    {4, 0, 2},  // port 5
    {5, 0, 2},  // port 6
    {0, 1, 0},  // port 7
    {1, 1, 0},  // port 9
    {2, 1, 0},  // port 10
    {3, 1, 0},  // port 11
    {4, 1, 0},  // port 13
    {5, 1, 0},  // port 14 (hosted by the pong_screen module itself)
};

// ---------------- rings ----------------
constexpr uint8_t RING_BRIGHT_GAME    = 40;
constexpr uint8_t RING_BRIGHT_ATTRACT = 20;
constexpr uint8_t RING_BRIGHT_MAX     = 120;      // rings sit at eye level

// ---------------- feature flags ----------------
constexpr bool NET_DOTS              = false;     // user: no dots mid-field
constexpr bool WALL_BLIP_ENABLED     = true;      // wall-bounce audio blip
constexpr bool RALLY_PULSE_ENABLED   = false;     // heartbeat bg "music" (off by default)
constexpr bool ATTRACT_CHIRP_ENABLED = false;
constexpr bool SPEED_TINT_BALL       = false;     // polish: white -> warm as speed rises

// #define PONG_CALIBRATION_MODE 1   // identify/verify patterns instead of the game
#define PONG_DEBUG 1              // 1 Hz status line on Serial (visible in atech monitor)

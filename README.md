# Atech Arcade

**An extensible retro game *console* — and an ambient pixel display — built from stock
[atech.dev](https://atech.dev) ESP32-S3 modules. No soldering.** Two boards click
together: a **brain** (two rotary knobs + speaker + color TFT) runs an on-device OS and a
library of games, and streams pixel frames to a **screen** (a 6×18 = 108-pixel NeoPixel
matrix) that is a dumb, glowing renderer. When nobody's playing, the same matrix runs
generative ambient scenes. Grew out of a two-player Pong; now it's a platform.

<table>
  <tr>
    <td align="center" valign="top">
      <img src="assets/pong.gif" width="220" alt="The real 108-pixel matrix rendering a live game">
    </td>
    <td align="center" valign="top">
      <img src="assets/controller.gif" width="440" alt="The console: two click-in knobs, a color scoreboard, and a speaker">
    </td>
  </tr>
</table>

*The left GIF is the real firmware engine rendering itself onto the 6×18 matrix.*

## The two gadgets

- **The brain (console).** A 14-port Atech Motherboard with **two click-in rotary knobs**
  (each with a push-button + a 12-LED ring), an **I2S speaker**, and a **160×80 ST7735
  color TFT** scoreboard. It runs the OS + every game and draws to an abstract 6×18
  `console::Canvas`.
- **The screen (display).** A second Motherboard driving **twelve 3×3 "Light Grid"
  NeoPixel tiles** = one **6×18, 108-pixel** RGB matrix. It knows nothing about games: it
  decodes pixel frames off the link, eases them through a glow engine, and lights LEDs.

## The game menu

Ten games register in the on-device menu, in this order (the OS adds a trailing
**SETTINGS** row). Every game is one `console::Game` subclass under `games/<name>/`.

| # | Game | What it is | Players | Controls |
|---|------|-----------|:---:|----------|
| 1 | **eggcatch** | "Nu, pogodi!" flagship: 4 chutes, a wolf's basket, 3-penalty game-over | 1 | knob L = move basket left/right · knob R = up/down · press L/R = start Game A (3 chutes) / B (4) |
| 2 | **snake** | Classic snake on a 6×18 field | 1 | knob L = relative turn (CW→right, CCW→left) · press = pause / restart |
| 3 | **pong** | Two-knob local Pong with contact-point english | 2 | knob L = bottom paddle · knob R = top paddle · press = serve |
| 4 | **racing** | Brick-Game vertical car-dodge; *you* own the scroll speed | 1 | knob L = steer lane, press = brake · knob R = throttle |
| 5 | **flappy** | Gravity/flap through scrolling pipe gaps | 1 | either press = flap |
| 6 | **doodlejump** | Auto-bounce climber with edge-wrap, springs, a shootable monster | 1 | knob L = steer (wraps), press = shoot |
| 7 | **invaders** | 4×3 formation shooter that marches down and speeds up per wave | 1 | knob L = move ship, press = fire |
| 8 | **jukebox** | Audio+lights showpiece: RTTTL song player + VU visualizer, and a note instrument | 1 | knob L = browse/play, press = play/stop · knob R press = instrument mode |
| 9 | **ambient** | Zero-player "desk object": Life, fire, plasma, matrix rain, clock (auto-cycle) | 1 | knob L = pick effect, press = reseed |
| 10 | **demo** | Smallest bouncing-ball reference game | 1 | knob L = horizontal dir, press = flip vertical |

*Only Pong is truly two-player; eggcatch, racing and jukebox use **both** knobs but are
single-player. "Players" is each game's own `meta().players`.*

## Quick start (no hardware)

Everything runs on the desktop — the games are dependency-free C++14 and compile for the
host unchanged.

```bash
# Play any game in an interactive ANSI-truecolor terminal (6×18):
make -C sim gamerun GAME=snake
#   a/d = left knob   j/l = right knob   s/k = buttons   t = theme   r = reset   q = quit

# The determinism CI gate for a game (runs it twice, asserts bit-identical):
make -C sim gameselftest GAME=eggcatch

# The module test suites:
make -C modules/console_os     test    # OS: loop, menu, theme switch, settings persistence
make -C modules/screen_render  test    # dumb renderer: glow engine + compositor mapping
make -C modules/link           test    # frame encode → COBS → decode round-trip
make -C modules/console_e2e    test    # end-to-end: brain → wire → screen reproduces the frame

# Aesthetic eval + visual-regression render (glowing PNG/GIF per game, no hardware):
PYTHONPATH=tools uv run --group dev python tools/eval/run.py

# PC mirror over Web-Serial (Chrome/Edge; "Mock" mode needs no board):
open host/dashboard/index.html
```

Valid `GAME=` values: `pong snake eggcatch racing flappy doodlejump invaders jukebox ambient demo`.

## Architecture at a glance

Ports-and-adapters. Games draw only to a `Canvas` using **theme tokens** (never hex, never
LED wiring). The brain composes a frame and ships pixels; the screen just glows.

```
        BRAIN board (owns logic)                         SCREEN board (dumb renderer)
 ┌───────────────────────────────────┐            ┌────────────────────────────────────┐
 │ BrainOS   fixed 50 Hz loop         │            │ ScreenRenderBoard                  │
 │  menu · settings · active game     │            │  drain link → frameDecodeInto      │
 │  draws → Canvas 6×18  Color[108]   │            │  → persistent logical canvas       │
 │  + active Theme + LightProfile     │            │ ScreenRenderer                     │
 │                                    │  COBS-     │  LightEngine  (decay + bloom)      │
 │ LinkFrameSink                      │  framed    │  Compositor   (serpentine + rot)   │
 │  frameEncode → MSG_FRAME (RGB565,  │  frames    │  → 12 × NeoTile (WS2812)           │
 │   full or dirty-rect)              │ ─────────► │  → 6×18 = 108-px matrix @ 20% cap   │
 │  MSG_SET_LIGHT_PROFILE on restyle  │ (PongLink) │                                    │
 └───────────────────────────────────┘            └────────────────────────────────────┘
   2 knobs · speaker · 160×80 TFT                    108 NeoPixels (12 snap-in 3×3 tiles)
```

Games are **deterministic** (all randomness from `ctx.rngSeed`, all motion from `dtMs`,
`init()` is a full reset), so a fixed-seed replay is bit-for-bit identical — that is the CI
gate. A theme is a design-token bundle that restyles the whole console (menu, every game,
knob rings, TFT, and the glow itself) at runtime.

## Docs

| Doc | What's inside |
|-----|---------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Contracts, the frame path, the light engine, the design-token system, determinism, and the design decisions + rationale |
| [docs/GAMES.md](docs/GAMES.md) | Every game (mechanics + controls) and a **"how to add a game"** recipe |
| [docs/THEMES.md](docs/THEMES.md) | The 5 themes, the token taxonomy, and **"how to add a theme"** |
| [docs/HARDWARE.md](docs/HARDWARE.md) | The two boards, pin map, flash flow, power/brownout, and the board-to-board link |
| [docs/HANDOVER.md](docs/HANDOVER.md) | Developer onboarding: repo layout, build/test/run, key modules, invariants, gotchas |
| [docs/STATE.md](docs/STATE.md) | Current state: what's verified, what's on hardware, what's roadmap |

Product/business framing lives in [`gtm/`](gtm/README.md). Built on atech boards and the
atech SDK. **Powered by Atech.**

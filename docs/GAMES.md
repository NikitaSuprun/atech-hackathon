# Games

Every game is one `console::Game` subclass under `games/<name>/`, header-only logic in
`<name>.h` plus a 3-line `<name>.cpp` that registers it for the desktop host. Games draw
only to the 6×18 `Canvas` using theme tokens, read only `Input`, and are deterministic
(see [ARCHITECTURE.md](ARCHITECTURE.md) §4). The OS lists them in registration order
followed by a SETTINGS row.

Run any of them with no hardware:

```bash
make -C sim gamerun      GAME=<name>   # interactive terminal (a/d,j/l knobs; s,k buttons; t theme; r reset; q quit)
make -C sim gameselftest GAME=<name>   # deterministic twice-run diff (CI gate)
make -C sim gamedump     GAME=<name>   # headless F-format frame dump
```

## The library

Geometry is a **portrait 6-wide × 18-tall** grid. Controls below: **L/R** = the two knobs
(`knob[0]`/`knob[1]`), *rotate* = a detent, *press* = the push-button.

### 1. eggcatch — the flagship ("Nu, pogodi!" / Elektronika IM-02)
Four chutes feed two left + two right catch points; the wolf's basket auto-catches an egg
the instant it rests on that landing cell. A dropped egg is a penalty (2 "halves", or 1
while the blinking hare bonus is up); **3 penalties** (6 halves) end the game. Speed ramps
with catches and eases briefly each 100 points; score wraps at 999. Idle → an attract demo
self-plays.
**Controls:** L rotate = basket left/right · R rotate = up/down · L press = start Game A (3
chutes) · R press = start Game B (4 chutes).

### 2. snake
Classic snake. The knob is a **relative rudder**: a CW detent turns right of the current
heading, CCW turns left (the exact reverse of the committed heading is refused, so a fast
spin can't fold the snake onto its neck). Eating food grows the snake, respawns food at a
random free cell, and nudges the step rate faster. Wall/self hit flashes HAZARD, then resets.
**Controls:** L rotate = turn · L press = pause (alive) / restart (dead).

### 3. pong — 2 players
Two-knob local Pong. P1 defends the bottom edge (`knob[0]`), P2 the top (`knob[1]`); each
`.delta` slides a 2-px paddle. The ball serves from the conceder, gains **english** from
where it strikes the paddle (swept plane-crossing + triangle-fold side-wall reflection), and
speeds up 7% per hit up to a cap. **First to 5** takes the match. Physics is ported from the
original `modules/pong_screen` but is fully self-contained here and drawn through theme roles.
**Controls:** L = bottom paddle · R = top paddle · either press = serve / dismiss the win.

### 4. racing
A vertical car-dodge in the Brick-Game tradition. Cols 0 & 5 are grass verges; the road
(cols 1–4) is two 2-wide lanes. Your 2×2 car holds the bottom rows and hops lanes; 2×2 cars
fall from the top. The authentic quirk: **you own the scroll speed** via the throttle knob —
the road only rushes as fast as you dare, and bolder throttle scores faster. Touch a car →
crash flash → restart.
**Controls:** L rotate = steer lane · L press = brake (slowest scroll) · R rotate = throttle.

### 5. flappy
Flappy in the classic orientation: the bird holds a fixed near-left column, gravity governs
its vertical position in the tall (18-row) axis, and 1-column pipe barriers with a fixed gap
scroll in from the right. Sub-cell fixed point (1/256 cell) keeps 50 Hz gravity smooth.
Score ticks per pipe cleared; the ceiling only bonks, the ground kills; crash → instant
restart (a tap skips the flash).
**Controls:** either press = flap.

### 6. doodlejump
An auto-bounce climber. You **only** catch a platform while falling (you pass through them
rising) and bounce automatically on landing. Steering **edge-wraps** (leave one side,
reappear on the other), pairing with platforms that drift toward the walls. The camera never
descends — it pins you to the top third and scrolls the world down; drop below the bottom and
you die. SPRING platforms (ACCENT) fling ~2× higher; a rare HAZARD monster kills on touch but
pops if you shoot it. Score = greatest height reached.
**Controls:** L rotate = steer (wraps) · L press = shoot pellet.

### 7. invaders
A compact **4×3** invader block marches sideways, drops a row and reverses at each edge, and
so descends toward your ship on the bottom row. Clear a wave → the next spawns faster.
Invaders rain bombs you must dodge; a bomb hit or invaders reaching the ship costs one of **3
lives**. A few bullets may be in flight at once. Idle → an attract pilot auto-plays.
**Controls:** L rotate = move ship · L press = fire (also restarts from game-over).

### 8. jukebox — the audio + lights showpiece
Two apps on one wall. **TRACK mode:** rotate to scroll a library of 8 famous public-domain
RTTTL melodies (Super Mario, Zelda, Tetris, Nokia, Star Wars, Für Elise, Pac-Man, Axel F);
press plays it via `audio->melody(rtttl)`, press again stops. While playing, a spectrum
visualizer runs — six VU bars with peak-hold dots, a beat pulse, and falling sparkles, all
timed from the track's RTTTL tempo and colored from `theme.ramp[]`/`theme.cat[]`. **PLAY
mode** (R press toggles): rotate to pick a note across three octaves, press to sound it via
`audio->note(midi, ms)` and light the matrix in the note color. Idle → a silent attract demo
keeps the visualizer alive.
**Controls:** L rotate = browse / pick note · L press = play/stop / sound note · R press =
switch mode.

### 9. ambient — the zero-player desk object
A pack of generative effects auto-cycles every 15 s, each painted only from the theme's
`ramp[]`/`cat[]`/roles: **Conway's Life** (wrapping, reseeds on stall), **fire** (hot bottom
source, cools + drifts up), **plasma** (summed sines, ramp-mapped), **matrix rain** (falling
streaks in cat colors), and a **pixel clock**. Stays alive with no input, so it doubles as
the idle/ambient screen.
**Controls:** L rotate = hand-pick an effect · L press = reseed the current one.

### 10. demo — the reference
The smallest end-to-end `console::Game`: one ball bounces around the field, BALL role on BG
role. Proves the runner, terminal sim, and headless dump end-to-end; the template to copy.
**Controls:** L rotate = set horizontal direction · either press = flip vertical.

## How to add a game

Say the new game is `spark`.

**1. Write the logic — `games/spark/spark.h`.** Subclass `console::Game`. Keep it
header-only, C++14, `<stdint.h>`-only, no Arduino. Include the SDK for rng/font/helpers:

```cpp
#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"                       // sdk::Rng, sdk::clampi/signi, 3×5 font

namespace spark {
class SparkGame : public console::Game {
public:
  const console::GameMeta& meta() const override { return meta_; }

  void init(const console::GameContext& ctx) override {
    rng_ = sdk::Rng(ctx.rngSeed);      // ALL randomness from the seed
    // ...full reset of every field here...
  }
  void update(const console::Input& in, uint32_t dtMs) override {
    // read in.knob[0]/[1]; integrate motion from dtMs (never wall-clock)
  }
  void draw(console::Canvas& c, const console::Theme& t) override {
    c.clear(t.c(console::ROLE_BG));
    c.pixel(x_, y_, t.c(console::ROLE_ACCENT));   // theme tokens ONLY, never hex
  }
private:
  console::GameMeta meta_{"spark", nullptr, 1};   // name · icon · players (1 or 2)
  sdk::Rng rng_{1};
  int x_ = 3, y_ = 9;
};
}  // namespace spark
```

Rules that keep it correct: **draw only via `Canvas` + theme tokens** (`t.c(ROLE_*)`,
`t.cat[i]`, `t.ramp[i]`); **all rng from `ctx.rngSeed`**; **all motion from `dtMs`**;
`init()` is a **full reset**. These are what make the selftest pass.

**2. Register it for the sim — `games/spark/spark.cpp`:**

```cpp
#include "spark.h"
#include "registry.h"
REGISTER_GAME(spark::SparkGame)
```

`REGISTER_GAME` defines the single `host::createGame()` the desktop host links against (one
game per host binary). `sim/Makefile` picks the game up from `GAME=spark` automatically
(`-I ../games/spark`), so this already works:

```bash
make -C sim gamerun GAME=spark
make -C sim gameselftest GAME=spark      # must print GAME SELFTEST OK
```

**3. Add it to the on-device menu — `modules/console_os/builtin_games.cpp`:**

```cpp
#include "spark.h"                        // with the others at the top
// ...
console::Game* makeSpark() { static spark::SparkGame g; return &g; }
// ...in registerBuiltinGames():
reg.add(makeSpark()->meta().name, &makeSpark);   // registration order == menu order
```

Each factory returns one function-local-static instance; the OS calls `init()` to re-arm it
per launch. The menu label is pulled from `meta().name`, so it never drifts.

**4. Let the OS test build see the header — `modules/console_os/Makefile`:** add
`-I../../games/spark` to `INCLUDES`. Then:

```bash
make -C modules/console_os test          # registry now lists spark; BRAIN OS TEST OK
make -C modules/console_e2e test          # end-to-end still green
```

That's the whole path: one header, one 3-line `.cpp`, one `reg.add()`, one `-I`.

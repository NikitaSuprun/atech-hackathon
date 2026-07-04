# PONG WALL — Current State & Developer Guide

> Read this first. It's the operational handover: what the system is, where things
> stand, how to build/flash/test, and the gotchas that will bite you if you don't know
> them. The original hackathon spec is `docs/PLAN.md`; the original handover is
> `docs/HANDOVER.md`. This file supersedes them for *current* state.

Last updated: 2026-07-04. `origin/main` @ `523f035`. Working tree clean.

---

## 1. What it is

A two-board ESP32 Pong game on a physical LED wall.

- **Screen board** (`modules/pong_screen/`) — runs the authoritative game engine and drives
  a **6 px × 18 px wall** made of **12 NeoPixel 3×3 "Light Grid" tiles** (2 tile-cols × 6
  tile-rows). Also hosts a WiFi SoftAP.
- **Controller board** (`modules/pong_control/`) — 2 rotary-encoder **knobs** (P1/P2),
  a **speaker**, and a small **TFT scoreboard**. Reads inputs, plays audio, shows score.
- **Transport** — the two boards talk over **USB serial**, relayed by a host script
  (`tools/serial_bridge.py`). A UDP/WiFi transport also exists in-tree but serial is active.
- **Desktop sim** (`sim/`) — terminal Pong for engine work with no hardware.
- **Render tooling** (`tools/gifgen/`) — Python package that renders the README assets
  (`assets/pong-wall.gif`, `controller.gif`, `system-diagram.png`) from the real engine.

Colors/sides: **P1 = cyan, defends the bottom edge (y = H−1)**; **P2 = amber, defends the
top edge (y = 0)**.

---

## 2. Where things stand (flashed & confirmed on hardware)

- **Screen firmware:** `KNOB_SIGN = {-1, -1}` (both knobs reversed — see §5), **wall score
  pips OFF** (`WALL_SCORE_PIPS = false`).
- **Controller firmware:** speaker volume lowered to **`0.2f`** (from the module default `0.4f`).
- Everything is committed and pushed to `main`. The prior "cleanup" work (named constants,
  types, dedup, `pong_shared.h`, drift guard, tooling) is also on `main`.

---

## 3. Repo layout (the parts that matter)

| Path | What |
|---|---|
| `modules/pong_screen/` | Screen firmware: `pong_engine.cpp` (engine), `compositor.cpp` (tile mapping), `pong_config.h` (the tuning table), `pong_screen.cpp`. **Canonical home of the shared headers.** |
| `modules/pong_control/` | Controller firmware: `pong_control.cpp`, `audio_director.{h,cpp}`, `ring_fx.cpp`, `score_display.cpp`. |
| `modules/pong_screen/pong_shared.h` | Arduino-free header both boards agree on: `pong::Color`, `COL_P1/P2`, `RING_BRIGHT_*`, audio toggles. |
| `sim/` | Desktop simulator (`main.cpp` + the engine). |
| `tools/gifgen/` | The `gifgen` Python package: render scripts + `game.py` (the C++-contract mirror) + `check_cpp_constants.py` (drift guard). |
| `tools/serial_bridge.py` | Host USB relay between the two boards. |
| `tools/ci.sh` | CI harness (symlinks, both firmware builds, sim selftest, python stages). |
| `screen/`, `controller/` | atech PlatformIO projects. `*/build/` is **generated** (gitignored). |
| `docs/PLAN.md`, `docs/HANDOVER.md` | Original hackathon spec + handover. |

---

## 4. Architecture

**Engine** (`pong_engine.cpp`) — a fixed-timestep (50 Hz, `TICK_MS = 20`) state machine over
a logical 6×18 `Frame`. States: `LINK_WAIT → READY_CHECK → COUNTDOWN → PLAYING → POINT_FLASH
→ (READY_CHECK | GAME_OVER)`. Paddles move in **x** (0..W−PADDLE_W) driven by knob deltas.
The engine is the only thing that touches `padX_` — inputs come in as **absolute cumulative
knob detents** (`knobPos`), and `padX += KNOB_SIGN[p] * delta / DETENTS_PER_CELL`.

**Compositor** (`compositor.cpp`) — maps the logical frame onto the 12 physical tiles via
`TILE_MAP[12] = {tileRow, tileCol, rot}` + a serpentine chip order. **ALL wall orientation
lives here** (engine/`pushFrame`/driver are orientation-transparent). `rot` encodes pure
rotations only (0/1/2/3 quarter-turns), **no mirroring**.
- Calibrated build: **left column (ports 1–6) = `rot 2` (mounted 180°)**, right column
  (ports 7,9,10,11,13,14) = `rot 0` (upright). This matches the real hardware.
- The wall GIF renders the **logical** frame directly and does **not** go through `TILE_MAP`.

**Transport** — controller sends `PongInputPacket`s (`PKT:<hex>` lines over USB serial),
screen replies with `PongFeedbackPacket`s. `serial_bridge.py` relays both directions. Wire
layout is frozen in `pong_proto.h`.

---

## 5. Key facts & calibrations

- **`KNOB_SIGN = {-1, -1}`** (`pong_config.h`). This is a **UX/feel calibration** — which way
  each knob drives its paddle so it feels natural — confirmed live on hardware. It is *not* a
  wiring claim. The dev harness (`dump_frames.cpp`, `sim/main.cpp`) multiplies its knob output
  by `KNOB_SIGN[p]` so the demo/tests stay identical for any sign (only the physical knobs
  change). If a future build feels reversed on **one** side, flip that side's sign.
- **`WALL_SCORE_PIPS = false`** — the wall no longer shows score pips (they read as confusing
  "health bars"); score lives on the controller's TFT scoreboard. Guarded in
  `Engine::drawPips()`.
- **Speaker volume** — the speaker module's setup template sets `setVolume(0.4f)`;
  `AudioDirector::begin()` runs after it (via `pongc.begin`) and overrides to `0.2f`.

---

## 6. Build / test / verify

```bash
bash tools/ci.sh          # symlinks · atech build screen+controller · sim build+selftest · py format/lint/types/drift
make -C sim && ./sim/pong_sim --selftest    # -> SELFTEST OK
```

**Golden oracle (byte-exact engine check).** `dump_frames.cpp` is deterministic (fixed
`SEED`, no wall-clock). Use it to prove an engine edit is behavior-preserving (empty diff) or
to see *exactly* what changed:
```bash
g++ -std=c++14 -O2 -I modules/pong_screen tools/gifgen/dump_frames.cpp \
    modules/pong_screen/pong_engine.cpp -o /tmp/dump && /tmp/dump 2 > golden.txt
# ...make the change, rebuild, diff. Frame = "F" then 18 rows × 6 hex tokens (row 0 = top).
```

**Render assets** (reproducible byte-identically in the pinned env — numpy 2.4.6 / pillow 12.3.0):
```bash
PYTHONPATH=tools uv run --group dev python -m gifgen.render_wall        # assets/pong-wall.gif
PYTHONPATH=tools uv run --group dev python -m gifgen.render_controller  # assets/controller.gif
PYTHONPATH=tools uv run --group dev python -m gifgen.render_wiring       # assets/system-diagram.png
```

**Drift guard** — `game.py` mirrors the C++ contract (W, H, NUM_TILES, TICK_HZ, COLORS,
TILE_MAP, WIN_SCORE). `check_cpp_constants.py` (the `py drift` CI stage) re-parses the four
headers and fails on any mismatch. Change a mirrored constant in **both** places.

---

## 7. Flashing hardware ⚠️ (this is where you'll get burned)

**`atech upload` reuses an existing `firmware.bin` and skips the rebuild → it will flash a
STALE binary that doesn't contain your change.** Always build first, verify, then upload:

```bash
uv run atech build screen                 # force a real rebuild (re-syncs modules/ -> build/lib)
grep <your-change> screen/build/lib/atech_pong_screen/...   # CONFIRM it re-synced
uv run atech upload screen --port <port>  # now flashes the fresh binary
```

**Which board is which port.** Both boards enumerate with **identical** USB descriptors
(`USB JTAG/serial debug unit`, vid 0x303a). Ports (`/dev/cu.usbmodem11xx/12xx`) can change on
reconnect. **Re-identify by serial output**, not by port number:
- **Screen** emits `{"module":"pong_screen","score":[..],"linked":..,"ap":1,...}` JSON.
- **Controller** emits `PKT:504F4E47...` (504F4E47 = "PONG" magic).

Sample a port read-only for ~3 s with pyserial to tell them apart (see how the session did it
if unsure). As of last session: screen = `usbmodem1101`, controller = `usbmodem1201`.

**Which board to flash for a given change:** engine/wall/`KNOB_SIGN`/pips → **screen**;
audio/rings/scoreboard/volume → **controller**. (`pong_config.h` is shared, but `KNOB_SIGN`
is only *used* by the screen engine.)

**To play** (link the two boards), run the bridge — controller port first:
```bash
uv run --group dev python -u tools/serial_bridge.py <controller_port> <screen_port>
# e.g. .../usbmodem1201 .../usbmodem1101
```
It must be **stopped before flashing** (it holds both ports): `pkill -f serial_bridge`.
When linked you'll see `"linked":1` and the game advancing (`state` 1→2→3...).

---

## 8. Hard rules / conventions (don't trip)

- **Edit only canonical sources:** `modules/pong_screen/*`, the **non-symlink**
  `modules/pong_control/*`, `sim/`, `tools/`. **Never** edit `*/build/`, `*/.pio/`, `*/.venv/`
  (generated / vendored).
- **Symlinks:** `modules/pong_control/{pong_proto.h, pong_link.h, net_config.h, link_serial.h,
  pong_shared.h}` are symlinks to the `pong_screen` originals. Edit the original once.
  `tools/ci.sh` has a symlink-integrity check.
- **C++14 / Arduino-free** for sim-compiled headers (`pong_config.h`, `pong_frame.h`,
  `pong_proto.h`, `net_config.h`, `pong_shared.h`): plain `constexpr` only; no `IPAddress`
  in headers (build it at runtime from the `uint8_t[4]` octet arrays).
- **Value-preserving C++ edits** when refactoring: same type & value, no expression
  reassociation; let the golden oracle be the gate.
- **Python style (user preference):** parametrized `Final[X]` (never bare `Final`), fully
  specified `dict[...]`/`tuple[...]`, **absolute imports only**, **no `sys.path` hacks**
  (it's a real package — run via `python -m gifgen.*` / `PYTHONPATH=tools`).
- **Comments minimal** — no duplication, on the line above, not verbose scaffolding notes.
- **Commits:** no Claude co-author trailer/watermark.

---

## 9. Feature flags (quick reference)

`pong_config.h`: `WALL_SCORE_PIPS=false` · `NET_DOTS=false` · `KNOB_SIGN={-1,-1}` ·
`ATTRACT_CHIRP_ENABLED=false` · `SPEED_TINT_BALL=false`.
`pong_shared.h`: `WALL_BLIP_ENABLED=true` · `RALLY_PULSE_ENABLED=false`.
Speaker volume override: `AudioDirector::begin()` → `setVolume(0.2f)`.

---

## 10. Open follow-ups

- **GIF sync** — `pong-wall.gif` (real engine match, ~49 s / 554 frames) and `controller.gif`
  (separate *stylized* animation, ~12 s / 240 frames, its own scripted story) are independent
  renders, so they drift when looped side-by-side. Real fix = drive the controller render from
  the **same engine match** (export per-frame paddle-x / score / state from `dump_frames`,
  feed it into `render_controller`). Meaningful re-architecture; **not done**.
- **Full both-board reflash** — do screen + controller together from committed firmware when
  wanted (both are current now).
- **Attract mode** isn't exercised by the golden oracle or sim selftest — verify attract
  constants by review + interactive `./sim/pong_sim` (idle in READY) or on hardware.

---

## 11. History (what got done)

- **Cleanup pass** (10 commits): named the magic constants across C++ (`pong_config.h`,
  `pong_engine.cpp`, transport/tile/IP) and Python; introduced `pong_shared.h` to kill the
  hand-synced cross-board mirrors; built the `gifgen` package with the `game.py` contract
  mirror + `check_cpp_constants.py` drift guard; added ruff + basedpyright tooling and the
  `py *` CI stages; fixed a latent `speaker_fx` note-origin bug. All behavior-preserving
  (golden byte-identical, assets byte-identical) except the reviewed `speaker_fx` regen.
- **This session** (6 commits): `KNOB_SIGN → {-1,-1}` (control feel, hardware-verified);
  corrected the wiring diagram (left column is the rotated one, derived from `TILE_MAP`) +
  stale docs; untracked `.DS_Store`; lowered speaker volume to `0.2f`; turned off wall score
  pips. All flashed and confirmed on the physical wall.

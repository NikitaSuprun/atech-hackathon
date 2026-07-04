# HANDOVER — Atech Pong (two-board WiFi Pong)

> Last updated 2026-07-04 (hackathon day). **Status: plan finalized & approved; no game code written yet.**
> Full design: [PLAN.md](PLAN.md). Read this file first; it's everything you need to start or resume work without re-exploring.

## What we're building

Two-player Pong on two **Atech 14-Port boards** (ESP32-S3):

| Board | Modules (port) | Role |
|---|---|---|
| **Screen** | 12× `neopixel` Light Grid: ports 1-6 direct + 7,9,10,11,13,14 mounted **flipped 180°** → 6×18 px wall | Game engine (C++ state machine), WiFi **SoftAP** `atech-pong`, UDP server :47420 |
| **Controller** | knob_p1 (1,2) · knob_p2 (9,10)¹ · speaker (4,5) · TFT 160×80 (13,14) · virtual `pong_control` (7) | WiFi STA, sends inputs 50 Hz, receives feedback 20 Hz → drives speaker jingles, TFT score, knob rings |

¹ User remembered "8,9" — impossible (port 8 = USB-C, reserved). Default (9,10); if wrong on hardware day it's one line in `controller/project.yaml` ([10,11] is the other valid option).

**Rules locked with Nikita:** first to **3** · paddle 2 px, ball 1 px · paddles on the short (6-px) edges · **no standalone button** — to start, to continue after EVERY point, and to rematch, **both players hold their knob push-buttons 0.5 s** (server-timed) · music is state/event-driven · TFT shows the score.

## Current state

- Repo scaffolded (uv, python 3.11), `atech==1.0.0a3` in `.venv`. **No `modules/`, `screen/`, `controller/`, `sim/`, `tools/` yet** — next step is Phase 0 (freeze contract headers + yamls), then 7 parallel lanes (see PLAN.md §workstreams).
- PlatformIO ESP32-S3 toolchain already cached in `~/.platformio`. **Adafruit NeoPixel/GFX/ST7735 download on the FIRST build → run `uv run atech build` for both projects once while on good WiFi.**
- Plan approved incl. committing these docs.

## Commands you'll actually use

```bash
cd /Users/nick/src/atech-hackathon
uv run atech list-boards / list-modules        # catalog
uv run atech validate screen                   # placement check (instant)
uv run atech build screen                      # codegen + PlatformIO compile → screen/build/
uv run atech upload screen --port /dev/cu.usbmodemXXX
uv run atech ports                             # find board serial ports (plug one at a time, label!)
uv run atech monitor --port ...                # live JSON events (knob detents/presses print for free)
uv run atech check --port ...                  # reboot + module health + reset reason (BROWNOUT watch)
uv run atech free-port --port ...              # kill whatever is holding a stuck serial port
make -C sim && ./sim/pong_sim                  # desktop game sim (once built)
tools/ci.sh                                    # validate+build both + sim + symlink checks
```

## The 8 gotchas (each cost us research — don't rediscover)

1. **SDK has ZERO networking.** We hand-roll SoftAP + WiFiUDP inside our custom modules (`WiFi.h`/`WiFiUdp.h` are in the arduino-espressif32 core; no lib_deps needed).
2. **User C++ can't add #includes** (`code:` blocks are spliced inside functions). ALL our code ships as two custom modules under `modules/` (`modules_path: ../modules` in project.yaml); files are copied **FLAT** (no subfolders!) into `lib/atech_<id>/` in the generated tree, and the declared header is auto-#included in main.cpp.
3. **Bundled knob loop template eats `wasPressed()` edges** before our loop code runs → only ever read **levels**: `getPosition()`, `isPressed()`. (Bonus: those bundled templates print JSON events = free diagnostics in `atech monitor`.)
4. **Knob acceleration is baked into `getPosition()` in the ISR** — can't be undone later → our setup template calls `setAcceleration(false, 1)` on both knobs. Non-negotiable for paddle feel.
5. **Knob ring = one glowing dot** (`setRingPosition(float 0..12)`, NAN = re-follow knob), not an addressable ring. Choreography = (color, brightness, dot position) motions only.
6. **`atech send` / module actions are DEAD in this alpha** (codegen generates no serial dispatcher). Don't build anything on it; all runtime control is our UDP.
7. **All 12 screen ports are grids** → no spare slot for a virtual engine module → `pong_screen` module IS the 12th grid (its template instantiates `NeoPixelGrid {{instance}}_grid` on port 14 AND the engine; engine gets pointers to all 12).
8. **Never hand-wire pins.** Codegen resolves size-2 modules' pin order incl. the left-column 180° swap automatically.

Where verified: SDK source at `.venv/lib/python3.11/site-packages/atech/` — `codegen.py` (main.cpp template), `project.py` (Project/catalog merge — same-id external module OVERRIDES bundled = our neopixel patch path), `placement.py` (validation rules), `catalog/loader.py` (flat copy, follows symlinks), `catalog/data/boards/14port.yaml` (ports/GPIOs/pairs/reserved 8+12), `catalog/data/modules/{neopixel,rotary_encoder,speaker,st7735_tft}/` (driver APIs + templates). Web: atech.dev/docs + PyPI README confirm no board-to-board API exists.

## Architecture in 10 lines

- Screen board is authoritative: pure-C++ `pong::Engine` (no Arduino deps — same files compile in the desktop sim), states `LINK_WAIT → READY_CHECK → COUNTDOWN → PLAYING → POINT_FLASH → (READY_CHECK | GAME_OVER)`.
- Controller sends 24-B INPUT @50 Hz: **absolute** knob int32s + button **held bits** + seq + uptimeMs. Server applies **clamped deltas** (no drift, no dead-knob at walls; reboot detected via uptime regression).
- Screen sends 24-B FEEDBACK @20 Hz: state, scores, readyProgress[2], and **wrapping u8 event counters** (paddleHit, goal+goalBy, win+winner, serve+servingPlayer). Controller's `FeedbackTracker` latches counters → each jingle/flash/redraw fires **exactly once** despite UDP loss/dup; after reconnect it adopts counters **without firing** (no jingle replay).
- Three controller presenters consume (snapshot, Cues): **AudioDirector** (speaker motifs/RTTTL — table in PLAN.md), **ScoreDisplay** (TFT, redraw only on quantized state change — full display() costs ~6-10 ms), **RingFX** (hold-progress dot sweep, score dial, goal flash/spin).
- WiFi hygiene both sides: `WiFi.persistent(false)`, `WiFi.setSleep(false)`, nothing blocks loop() ever (watchdog).
- `PongLink` (begin/poll/sendRaw/recvRaw/isUp) isolates transport → ESP-NOW is a 2-file swap if venue WiFi is hostile (same packets). UDP primary because a laptop can join the SoftAP and run `tools/fake_controller.py` = test the whole game with no controller board.

## Work lanes (parallel, zero file overlap — fan out to agents)

0. **Contracts first (blocks all, ~1 h):** `pong_frame.h`, `pong_engine.h`, `pong_proto.h`, `pong_link.h`, `net_config.h`, `feedback_tracker.h` (Cues), both `project.yaml` + `module.yaml`.
1. A: engine (`modules/pong_screen/pong_engine.cpp`) — with G's sim for tuning.
2. B: screen glue (`pong_screen.*`, `link_udp_server.*`, `compositor.*`, identify/verify modes) — stub engine first so `atech build screen` is green day-0.
3. C: controller core (`pong_control.*`, `link_udp_client.*`, tracker, symlinks) — presenter stubs so build is green day-0.
4. D/E/F: audio_director / score_display / ring_fx — independent file-pair drop-ins.
5. G: `sim/` + `tools/fake_controller.py` + `tools/ci.sh` + `.gitignore`.

## Hardware-day checklist (in order — kills the biggest unknowns first)

1. Flash **screen** → identify pattern (auto in LINK_WAIT before first link): each tile = solid hue + WHITE corner pixel (rotation: TL=0° TR=90° BR=180° BL=270°) + gray neighbor pixel (misread guard); white blinks tile-index+1 times. **This is also the RMT go/no-go for 24 strip objects** (12 tiles × 2 lines). Take ONE photo = the calibration record.
2. Fill `TILE_MAP[12]` in `pong_config.h` (expected: ports 1-6 col 0 rot 0; ports 7,9,10,11,13,14 col 1 rot 180) → rebuild → verify glyph ("F" + arrow) → attract rally shows any residual error as a discontinuity.
3. `uv run atech check` on screen → 13 modules ok (SoftAP proven via health check).
4. Laptop joins `atech-pong` (pass `pong4242`) → `python tools/fake_controller.py` → play with keyboard: full server proven with zero controller hardware.
5. Flash **controller** → TFT "SEARCHING" + red-blink rings immediately; `atech monitor` shows knob detents/presses (fixes knob-2 port if needed); `atech check` (speaker/TFT init).
6. End-to-end match; then robustness (kill controller mid-rally → LINK_WAIT ≤1 s → resume via READY_CHECK; button spam does nothing outside the hold gate) and power (`atech check` reset reason ≠ BROWNOUT; solid 5V/2A+ per board).
7. On-site tune: WiFi channel (least busy of 1/6/11 → `net_config.h`), `KNOB_SIGN`, `DETENTS_PER_CELL`, ball speeds, speaker volume (≤0.5), `WALL_BRIGHTNESS`.

## Open items

- [ ] Phase 0 contracts → then lanes A–G (fan out).
- [ ] One full `atech build` of both projects on good WiFi (lib cache).
- [ ] Hardware day: TILE_MAP truth, knob-2 pair, TFT `setRotation`, venue channel, volume/brightness taste.
- [ ] Stretch (protocol-ready): 2nd controller board = `controllerId 1`, one knob, zero protocol change.

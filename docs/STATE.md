# Current State

> Snapshot of where the **Atech Arcade** platform stands. Branch: `console-platform`.
> Last updated: 2026-07-05. This supersedes the Pong-era state; the original hackathon spec
> and handover are `docs/PLAN.md` / (historic) — kept for reference.

## TL;DR

The platform is **host/sim-verified end to end AND running on hardware**: 10 games, an
on-device OS, a dumb glowing screen renderer, the brain→wire→screen frame path, and 5 runtime
themes — all building + passing tests off-hardware, and **both ESP32-S3 boards are flashed
with the console firmware**. The brain is hardware-verified streaming ~59 fps of valid RGB565
frames; the screen board is flashed and fed those frames over a USB bridge. CI (`tools/ci.sh`)
runs the whole host suite + a GitHub workflow.

## What's verified (green off-hardware)

Every one of these was run and passes:

| Area | Check | Result |
|------|-------|--------|
| Games | `make -C sim gameselftest GAME=<g>` for all 10 | deterministic twice-run diff — **OK** |
| OS | `make -C modules/console_os test` | loop, menu, theme switch, settings round-trip — **BRAIN OS TEST OK** |
| Screen renderer | `make -C modules/screen_render test` | glow engine + compositor mapping + dirty cache + ESP-NOW-stub inertness — **CONSOLE TESTS OK** |
| Link | `make -C modules/link test` | frame encode → COBS → decode + light-profile round-trip — **LINK ADAPTER TEST OK** |
| End-to-end | `make -C modules/console_e2e test` | real brain → wire → real screen mirrors the framebuffer across menu, game launch, theme switch — **E2E TEST OK** |

## The library & shell

- **10 games**, registered in menu order: `eggcatch · snake · pong · racing · flappy ·
  doodlejump · invaders · jukebox · ambient · demo` (+ a trailing SETTINGS row). Details in
  [GAMES.md](GAMES.md).
- **5 themes**: Warm Analog · Neon Coin-op · Mono Glow · DMG Green · Aurora. Details in
  [THEMES.md](THEMES.md).
- **OS** (`BrainOS`): fixed 50 Hz loop, one framebuffer, menu ↔ game with a both-knobs pause
  overlay, settings (theme / volume / brightness) persisted via the `SettingsStore` seam
  (file/RAM on host, NVS on hardware).
- **Dumb renderer** (`screen_render`): light engine (attack/decay + bloom) + reused pong
  compositor (serpentine + rotation + dirty cache), driving WS2812 through the abstract
  `TileSink`.
- **Transport** (`link`): `LinkCobsSerial` (COBS over USB-CDC) is the primary path;
  `LinkLoopback` backs the tests; `LinkEspNow` is an inert in-tree stub.

## On hardware vs. roadmap

| ✅ Real / verified now | 🛣️ Roadmap (not yet) |
|---|---|
| Whole platform builds + passes tests on the desktop (host/sim/E2E) | Wired-UART board-to-board link + **ESP-NOW** failover (stub in-tree) — removes the USB bridge |
| **Both boards flashed** with the console firmware (`firmware/brain`, `firmware/screen`) | Palette wire-codec, telemetry/passive twin, render pipeline (RMT5/dual-core) |
| Brain hardware-verified emitting ~59 fps of valid RGB565 frames on serial | Slim firmware (`-Os`/LTO/`--gc-sections`) via a post-generate override |
| Unified `tools/ci.sh` + GitHub CI workflow running all selftests + module/e2e tests + eval | The in-console **game market** (signed OTA → sandboxed cartridges) — see `gtm/roadmap.md` |
| Board-to-board runs through the laptop `tools/console_bridge.py` USB relay (bring-up) | Eyeball/photo confirmation of the physical LED output (no camera this session) |

## Firmware & hardware

The console firmware lives in **`firmware/brain/`** and **`firmware/screen/`** — hand-authored
PlatformIO projects (the atech codegen copies module files flat and can't express the console's
cross-module C++/`console/*` include layout). The brain runs `BrainOS` + all 10 games, reads
the two knobs, drives the speaker, and streams COBS frames; the screen decodes + renders them
to the 12 WS2812 tiles. Both are flashed on the two ESP32-S3 boards and the brain is verified
emitting frames; board-to-board runs through `tools/console_bridge.py` (a USB relay) until the
wired-UART/ESP-NOW failover link lands. The old `screen/` + `controller/` atech projects (old
Pong) are retained pending the git archive.

## Known cleanups (non-blocking)

- Two `Color` / `TICK_MS` definitions coexist (`console::` and the Pong global scope);
  qualify at the call site — a de-dup pass is planned.
- `docs/PLAN.md` is the original Pong plan, intentionally left as history.

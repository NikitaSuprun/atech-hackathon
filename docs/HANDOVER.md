# Developer Handover

Onboarding for the **Atech Arcade** platform — an extensible ESP32-S3 game console (a
"brain" board that runs the OS + games and streams pixel frames to a dumb, glowing 6×18
"screen" board). Read [ARCHITECTURE.md](ARCHITECTURE.md) for the *why*; this file is the
*how*. Grew out of the two-player Pong that still lives under `modules/pong_*`.

> The repo-root `HANDOVER.md` is the program-level, multi-stage handover (what's done, what
> remains). This file is the code-level onboarding.

## Repo layout

| Path | What |
|------|------|
| `include/console/` | **The contracts.** Pure C++14, no Arduino/heap: `color · config · canvas · input · audio · theme · game · frame_proto · host_proto` + `themes.{h,cpp}`. The whole platform depends on these. |
| `games/<name>/` | The 10 games — each a header-only `console::Game` + a 3-line `.cpp` that `REGISTER_GAME`s it. |
| `games/_sdk/` | Shared game SDK: `sdk.h` (rng + font + `clampi`/`signi`), `rng.h`, `font3x5.h`, `registry.h` (`REGISTER_GAME`), `runner.h` (host `GameRunner`/`InputSampler`/`NullAudio`). |
| `modules/console_os/` | **Brain OS**: `brain_os.*`, `menu.h`, `overlay.h`, `settings.h`, `settings_view.h`, `game_registry.h`, `frame_sink.h`, `builtin_games.cpp`, `os_gfx.h`. Host test: `test_os.cpp`. |
| `modules/screen_render/` | **The dumb renderer**: `screen_render.h` (compositor glue), `light_engine.h` (glow), `neo_tile.h` (WS2812 sink), `screen_render_board.h` (transport glue). Host test: `test_console.cpp`. |
| `modules/link/` | Transport adapters over the `PongLink` seam: `link_frame.h` (COBS de-framer + MTU), `link_cobs_serial.h` (primary), `link_frame_sink.h` (OS→wire), `link_loopback.h` (tests), `link_espnow.h` (stub). Host test: `test_link.cpp`. |
| `modules/console_e2e/` | End-to-end test: real brain → wire → real screen. `test_e2e.cpp`. |
| `firmware/brain/`, `firmware/screen/` | **The shipped, flashed console firmware** — hand-authored PlatformIO projects. Brain = `BrainOS` + all 10 games + knobs + speaker + the ST7735 TFT dashboard; screen = the SK6812/line-B `screen_render` stack. Both ESP32-S3 boards run these. |
| `modules/pong_screen/`, `modules/pong_control/` | The **original Pong** firmware. `screen_render` reuses its `Compositor` + `pong_config.h` (`TILE_MAP`, tile geometry). Still the code the `screen/`/`controller/` projects build today. |
| `sim/` | Desktop host: `game_main.cpp` (interactive terminal / `--dump` / `--selftest` for any game) + `Makefile`. `pong_sim` is the legacy Pong sim. |
| `tools/` | `console_bridge.py` (board↔board USB relay for the console), `twin_server.py` (one-command deploy: bridge + live browser twin over SSE), `serial_bridge.py` (legacy Pong `PKT:` relay), `eval/run.py` (aesthetic-eval + visual-regression), `gifgen/` (README asset renderers), `ci.sh`. |
| `host/dashboard/` | The **browser twin** (`index.html`): runs the real `TftDashboard` in WASM; live over SSE when served by `tools/twin_server.py`, or standalone via Web-Serial / Mock (no board). |
| `web/` | WASM build of the twin: `main_wasm.cpp` (the real `BrainOS` + `TftDashboard`) + `build.sh`. |
| `screen/`, `controller/` | atech PlatformIO projects (`project.yaml` + generated `build/`, gitignored). |
| `docs/`, `gtm/` | These docs; product/business framing. `docs/PLAN.md` is the original Pong spec (kept for history). |

## Build / test / run (no hardware)

```bash
# Any game, interactively (ANSI truecolor 6×18):
make -C sim gamerun GAME=<pong|snake|eggcatch|racing|flappy|doodlejump|invaders|jukebox|ambient|demo>
#   a/d = L knob   j/l = R knob   s/k = buttons   t = theme   r = reset   q = quit

# Per-game determinism gate (twice-run diff):
make -C sim gameselftest GAME=snake            # -> GAME SELFTEST OK

# Module suites (each self-contained; run all four before a commit):
make -C modules/console_os    test             # BRAIN OS TEST OK  (loop, menu, theme, settings)
make -C modules/screen_render test             # CONSOLE TESTS OK  (glow + compositor + dirty cache)
make -C modules/link          test             # LINK ADAPTER TEST OK (encode→COBS→decode)
make -C modules/console_e2e   test             # E2E TEST OK       (brain→wire→screen mirror)

# Aesthetic eval + visual regression (glowing PNG/GIF per game + a manifest; no LLM, no board):
PYTHONPATH=tools uv run --group dev python tools/eval/run.py

# PC mirror:
open host/dashboard/index.html                  # Chrome/Edge; Web-Serial (Mock mode = no board)
```

For the on-hardware flash flow, board identification, power/brownout, and the link, see
[HARDWARE.md](HARDWARE.md).

## Key modules (what to read first)

1. `include/console/game.h` + `canvas.h` + `theme.h` — the surface every game codes against.
2. `games/demo/demo.h` — the smallest complete game; copy it to start a new one.
3. `modules/console_os/brain_os.h` — the loop, framebuffer, lifecycle, and the `FrameSink` seam.
4. `include/console/frame_proto.h` — the entire brain→screen wire protocol in one header.
5. `modules/screen_render/screen_render.h` + `light_engine.h` — how pixels become glow.
6. `modules/console_e2e/test_e2e.cpp` — the clearest tour of the full path, end to end.

## Design invariants (don't break these)

- **Games draw only via `Canvas` + theme tokens.** No hex literals, no LED/serpentine/panel
  knowledge, no direct hardware. `t.c(ROLE_*)`, `t.cat[i]`, `t.ramp[i]` only.
- **Determinism.** All randomness from `ctx.rngSeed` (a seeded `sdk::Rng`), all motion from
  `dtMs` (never wall-clock), and `init()` is a full reset. Fixed seed ⇒ bit-identical replay;
  the selftest enforces it.
- **Contracts stay pure.** `include/console/*` and the sim-compiled headers are C++14,
  `<stdint.h>`-only, no Arduino, no heap. They must compile for firmware, host, and WASM
  unchanged.
- **The screen is dumb.** It knows pixel frames + a light profile, nothing about games. Keep
  game logic on the brain.
- **Append, never insert, in the `Role` enum and `THEMES[]` layout** — `THEMES[]` is
  positional, so an insert silently recolors every theme.
- **One transport seam.** New links implement `PongLink`; nothing above it knows the wire.
- **The registry is the menu.** Add a game with one `reg.add()` in `builtin_games.cpp`; never
  hand-edit menu drawing.

## Gotchas

- **Two boards, identical USB descriptors.** Re-identify by serial output, not port number,
  and stop `console_bridge.py` before any reflash (it holds both ports).
- **`atech upload` reuses a stale `firmware.bin`** if you skip `atech build` — always build,
  confirm the change synced into `build/lib/…`, then upload.
- **20% brightness clamp is real.** The NeoPixel driver caps at 51/255, so anything below
  ~25 quantizes to mud — keep palettes in the accent (saturated, bright) / dim (~70–90) tiers.
- **`TICK_MS` is defined twice** — in `console::` (`config.h`) and the global pong scope
  (`pong_config.h`). Qualify it (`console::TICK_MS`) in code that includes both (the E2E test
  does). De-duping the two Pong-era `Color`/`TICK_MS` overlaps is a known cleanup item.
- **`screen/` and `controller/` still build the old Pong.** The shipped console firmware lives in
  its own PlatformIO projects `firmware/brain/` + `firmware/screen/` (both boards are flashed);
  the legacy atech `screen/`/`controller/` projects are retained but still build the old Pong.
  See [STATE.md](STATE.md).
- **Conventions:** comments minimal and on the line above (not inline); Python is a real
  package (`PYTHONPATH=tools`, absolute imports, parametrized `Final[X]`, no `sys.path`
  hacks); small self-contained commits, no AI co-author trailer.

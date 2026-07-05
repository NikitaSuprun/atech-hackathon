# Atech Arcade — Handover (start here)

**What this is:** an extensible retro game **console** platform built on Atech's
ESP32-S3 kit — two boards (a "brain" controller + a 6×18 glowing NeoPixel "screen"),
a ports-and-adapters core (brain owns logic + draws to an abstract `Canvas`; screen is
a dumb glowing renderer), a runtime **theme** system, an on-device **OS**
(menu/settings/theme-picker/overlay), **10 games**, a headless **sim + aesthetic-eval**
harness, and a Web-Serial **PC dashboard**. Grew out of the original two-player Pong.

**Where it lives:** branch **`console-platform`** (≈28 commits ahead of `main`, 0
behind). `main` = the old Pong (to be archived). Everything is **committed + host/sim-
verified; nothing is on hardware yet.**

**The full multi-stage plan + all findings, decisions, run commands, and the business
pitch live in:** `~/.claude/plans/i-am-thinking-that-serialized-mochi.md` — read it to
continue (it's the authoritative handover; this file is the in-repo pointer).

## Remaining work — 7 stages
1. **Integration** ⭐ *(do first; needs hardware for the flash)* — the `FrameSink` seam
   (OS → `console::frameEncode` → link), a `project.yaml` that builds `console_os` +
   `screen_render` instead of old Pong, register **all 10 games** in `builtin_games.cpp`,
   flash both boards, add the end-to-end test.
2. **CI** — unified `tools/ci.sh` (all 10 game selftests + module tests + eval + E2E) +
   `.github/workflows/ci.yml`.
3. **Docs** — full set: README (+ games menu) · ARCHITECTURE · GAMES · THEMES · HARDWARE
   · HANDOVER · STATE.
4. **Pitch deck** — self-contained **HTML** slide deck (smooth-glow, print-to-PDF), lead
   = *"the flagship that sells Atech's kit"*; brainstorm/verify with `codex`.
5. **Optimization** — palette wire-codec · wired+ESP-NOW failover link · digital-twin
   telemetry (passive dashboard) · firmware slim · render pipeline (RMT5/dual-core).
6. **Git promotion** — tag `v1.0-pong`, archive the old Pong as a separate repo,
   fast-forward `main`.
7. **Dead-code sweep + dedup + modularity** — the final best-practices pass.

## Business one-liner
*"A $229 Playdate plays alone; a $199 Tidbyt just sits there — Atech Arcade is $249 and
does both. Powered by Atech."* Every unit pulls through 2 Motherboards + 12 Light Grids
+ 2 Knobs + Screen + Speaker — the flagship that sells the kit. (Full economics: plan §9
+ `gtm/`.)

## Run it (no hardware)
`make -C sim gameselftest GAME=<pong|snake|eggcatch|racing|flappy|doodlejump|invaders|jukebox|ambient|demo>`
· `make -C modules/console_os test` · `make -C modules/screen_render test` ·
`PYTHONPATH=tools uv run --group dev python tools/eval/run.py` · open
`host/dashboard/index.html` in Chrome/Edge. Full cheatsheet: plan §6.

## How to execute
**Parallelize** — fan out independent agents per stage (plan §10). The only serialized
gate is Stage 1's FrameSink seam. Small self-contained commits, no AI co-author trailer.

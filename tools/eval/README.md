# tools/eval — aesthetic-eval + visual-regression harness

Renders every `console::Game` **headless** through the same photoreal glow pipeline
the product GIFs use, so you can review how a game looks *without* flashing it to the
LED wall. It builds each game's desktop host, dumps N framebuffers, and packages them
as PNGs + one GIF per game, plus a `manifest.json` an orchestrator hands to a vision
grader. It also hashes framebuffers and diffs them against a baseline to catch visual
regressions.

This tool produces assets only — it never calls an LLM.

## Usage

Run from the repo root (`PYTHONPATH=tools` makes `gifgen` importable; `--group dev`
pulls in numpy + Pillow):

```sh
PYTHONPATH=tools uv run --group dev python tools/eval/run.py
```

Options:

| flag | meaning |
| --- | --- |
| `-n, --frames N` | frames to dump per game (default `60`) |
| `--stride S` | engine ticks per dumped frame (default: host default of `2` → 25 fps) |
| `--games a,b,c` | only these games (default: all discovered under `games/`) |
| `--no-gif` | skip the per-game animated GIF (faster) |
| `--update-baseline` | (re)write regression baselines instead of only diffing |
| `--out DIR` | output root (default `tools/eval/out`) |

Under the hood, per game it runs:

```sh
make -C sim gamebin GAME=<name>          # build ./sim/gamehost_<name>
./sim/gamehost_<name> --dump <N>         # F-format frame dump (F + 18 rows of 6 hex)
```

The dump is deterministic (fixed seed `0x1234`, neutral input, each game
self-animates), so re-runs are byte-identical unless the game code changes — which is
what makes the hash-based regression check meaningful.

## Output layout

```
tools/eval/
  out/                     # regenerable, gitignored
    manifest.json          # every game: paths + stats + regression status
    <game>/
      frames/frame_0000.png … frame_00NN.png   # one glowing PNG per frame
      <game>.gif                               # animated preview
      dump.txt                                 # raw F-format dump (repro)
  baseline/<game>.json     # committed per-frame framebuffer hashes (regression ref)
```

### manifest.json

Top level records `repo_root`, `frames_requested`, `stride`, the render `theme`, and
`canvas` dimensions. Per game under `games.<name>`:

- `status` — `ok` / `build_failed` / `no_frames`
- `frame_count`, `unique_frame_count`
- `gif`, `frames_dir`, `png_frames[]`, `dump` — **paths relative to `repo_root`**
- `stats` — `non_bg_coverage_mean/max` (fraction of the 108 LEDs lit above the modal
  background), `distinct_colors_max` (busiest single frame), `distinct_colors_total`
  (palette across the whole run), `background` (modal color hex)
- `regression` — `unchanged` / `changed` (with `changed_frames[]`) / `new` / `updated`

An orchestrator joins `repo_root` with each relative path, then grades the PNGs/GIF
against a rubric (readability at 6×18, contrast under the brightness clamp, motion
clarity, "is it recognizably `<game>`"). The stats pre-filter obvious problems: near-
zero coverage = blank screen, `distinct_colors_total == 1` = nothing drawn.

## Visual regression

`baseline/<game>.json` stores per-frame `blake2b` hashes of the raw framebuffers plus
a `combined` hash. On a normal run the harness diffs the fresh dump against the
baseline and reports `unchanged` / `changed` (listing which frame indices differ). A
missing baseline is auto-seeded (`new`); `--update-baseline` force-refreshes after an
intentional visual change. Commit `baseline/` so regressions are caught in review.

## Extending to the theme × game matrix

Today every game is dumped under stub theme **0** (`neon`; see `host::stubThemes` in
`sim/stub_theme.h`, which also defines `warm`). To render the full **theme × game**
matrix:

1. Add a `--theme <idx|name>` arg to the dump host (`sim/game_main.cpp`, `runDump`)
   so it can select `themes[i]` instead of always `themes[0]`.
2. In `run.py`, loop themes per game and key the output dir, `manifest` entries, and
   `baseline/` files by `(game, theme)` — e.g. `out/<game>/<theme>/…` and
   `baseline/<game>__<theme>.json`.

The render pipeline itself is theme-agnostic (it composites whatever RGB the host
emits), so no changes are needed in `gifgen.ledfx`.

"""Headless aesthetic-eval + visual-regression harness for console::Game.

Renders every game under games/ through the ledfx glow pipeline so you can review
how each looks WITHOUT flashing it to hardware. For each game it builds the desktop
host, dumps N framebuffers headless, writes a glowing PNG per frame plus one animated
GIF, records a manifest.json for a downstream vision grader, and diffs per-frame
framebuffer hashes against a stored baseline to flag visual regressions.

It never calls an LLM: it only produces the assets + manifest an orchestrator hands
to a vision grader (readability at 6x18, contrast under the brightness clamp, motion
clarity, "is it recognizably <game>").

Usage (from the repo root; PYTHONPATH=tools makes gifgen importable):
    PYTHONPATH=tools uv run --group dev python tools/eval/run.py [options]

    -n/--frames N       frames to dump per game (default 60)
    --stride S          engine ticks per dumped frame (default: host default of 2)
    --games a,b,c       only these games (default: all discovered under games/)
    --no-gif            skip the per-game animated GIF
    --update-baseline   (re)write baseline hashes instead of only diffing them
    --out DIR           output root (default tools/eval/out)
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Final

import numpy as np
from PIL import Image

from gifgen import game, ledfx, paths
from gifgen.arrays import Frame
from gifgen.render_matrix import Renderer, chunk_scenes, dedup

EVAL_DIR: Final[Path] = Path(__file__).resolve().parent
DEFAULT_OUT: Final[Path] = EVAL_DIR / "out"
BASELINE_DIR: Final[Path] = EVAL_DIR / "baseline"
GAMES_DIR: Final[Path] = paths.REPO / "games"
SIM_DIR: Final[Path] = paths.REPO / "sim"

DEFAULT_FRAMES: Final[int] = 60
# the game host dumps every Nth 20 ms tick; N defaults to 2 when omitted
HOST_DEFAULT_STRIDE: Final[int] = 2
GRID_CELLS: Final[int] = ledfx.GRID_W * ledfx.GRID_H
HASH_BYTES: Final[int] = 8
COMBINED_HASH_BYTES: Final[int] = 16
QUANTIZE_COLORS: Final[int] = 256
# theme index 0 the dump host renders with (host::stubThemes in sim/stub_theme.h)
DUMP_THEME: Final[str] = "neon"


def discover_games() -> list[str]:
    """Every buildable game dir under games/, skipping _sdk and dotfiles."""
    names: list[str] = []
    for d in sorted(GAMES_DIR.iterdir()):
        if not d.is_dir() or d.name.startswith(("_", ".")):
            continue
        if (d / f"{d.name}.cpp").exists():
            names.append(d.name)
    return names


def build_game(name: str) -> subprocess.CompletedProcess[str]:
    """make -C sim gamebin GAME=<name> — our own Makefile + g++, trusted local build."""
    cmd = ["make", "-C", str(SIM_DIR), "gamebin", f"GAME={name}"]
    return subprocess.run(cmd, capture_output=True, text=True)


def dump_frames(name: str, frames: int, stride: int | None) -> tuple[list[Frame], str]:
    """Run ./sim/gamehost_<name> --dump <frames> [stride] and parse its F-format."""
    cmd = [str(SIM_DIR / f"gamehost_{name}"), "--dump", str(frames)]
    if stride is not None:
        cmd.append(str(stride))
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return parse_dump(proc.stdout), proc.stdout


def parse_dump(text: str) -> list[Frame]:
    """F-format: a line "F" then GRID_H rows of GRID_W hex RRGGBB tokens -> frames."""
    lines = text.splitlines()
    out: list[Frame] = []
    i = 0
    while i < len(lines):
        if lines[i] == "F":
            rows = [
                [bytes.fromhex(tok) for tok in lines[i + 1 + r].split()]
                for r in range(ledfx.GRID_H)
            ]
            out.append(np.array([[list(px) for px in row] for row in rows], np.uint8))
            i += 1 + ledfx.GRID_H
        else:
            i += 1
    return out


def frame_stats(f: Frame) -> tuple[float, int, str, set[tuple[int, int, int]]]:
    """Per-frame: non-bg coverage, distinct color count, bg hex, and the palette set.

    Background is the modal color (the theme fills the whole 6x18 with ROLE_BG)."""
    colors, counts = np.unique(f.reshape(-1, 3), axis=0, return_counts=True)
    bg_idx = int(counts.argmax())
    coverage = (GRID_CELLS - int(counts[bg_idx])) / GRID_CELLS
    bg_hex = "".join(f"{int(v):02x}" for v in colors[bg_idx])
    palette = {(int(c[0]), int(c[1]), int(c[2])) for c in colors}
    return coverage, len(colors), bg_hex, palette


def aggregate_stats(frames: list[Frame]) -> dict[str, object]:
    """Roll per-frame stats into the compact block a vision grader keys off."""
    covs: list[float] = []
    distincts: list[int] = []
    all_colors: set[tuple[int, int, int]] = set()
    bg_counts: dict[str, int] = {}
    for f in frames:
        cov, dist, bg, palette = frame_stats(f)
        covs.append(cov)
        distincts.append(dist)
        all_colors |= palette
        bg_counts[bg] = bg_counts.get(bg, 0) + 1
    return {
        "non_bg_coverage_mean": round(float(np.mean(covs)), 4),
        "non_bg_coverage_max": round(float(np.max(covs)), 4),
        "distinct_colors_max": int(max(distincts)),
        "distinct_colors_total": len(all_colors),
        "background": max(bg_counts, key=lambda k: bg_counts[k]),
    }


def frame_hashes(frames: list[Frame]) -> tuple[list[str], str]:
    """Per-frame framebuffer hash + one combined hash (deterministic: fixed seed)."""
    per = [
        hashlib.blake2b(f.tobytes(), digest_size=HASH_BYTES).hexdigest() for f in frames
    ]
    combined = hashlib.blake2b(
        "|".join(per).encode(), digest_size=COMBINED_HASH_BYTES
    ).hexdigest()
    return per, combined


def _write_baseline(path: Path, name: str, per: list[str], combined: str) -> None:
    BASELINE_DIR.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "game": name,
                "frame_count": len(per),
                "combined": combined,
                "frames": per,
            },
            indent=2,
        )
        + "\n"
    )


def check_regression(
    name: str, per: list[str], combined: str, update: bool
) -> dict[str, object]:
    """Diff per-frame hashes against baseline/<name>.json; auto-seed when missing."""
    bl_path = BASELINE_DIR / f"{name}.json"
    existed = bl_path.exists()
    if existed and not update:
        base = json.loads(bl_path.read_text())
        if base.get("combined") == combined:
            return {"status": "unchanged", "combined": combined}
        base_frames: list[str] = base.get("frames", [])
        span = max(len(base_frames), len(per))
        changed = [
            i
            for i in range(span)
            if i >= len(base_frames) or i >= len(per) or base_frames[i] != per[i]
        ]
        return {
            "status": "changed",
            "combined": combined,
            "baseline_combined": base.get("combined"),
            "frame_count": len(per),
            "baseline_frame_count": base.get("frame_count"),
            "changed_frames": changed,
        }
    _write_baseline(bl_path, name, per, combined)
    return {
        "status": "updated" if existed else "new",
        "combined": combined,
    }


def save_pngs(frames: list[Frame], renderer: Renderer, frames_dir: Path) -> list[Path]:
    """One glowing PNG per frame (1:1 index) so a grader can inspect motion."""
    frames_dir.mkdir(parents=True, exist_ok=True)
    out: list[Path] = []
    for idx, f in enumerate(frames):
        p = frames_dir / f"frame_{idx:04d}.png"
        Image.fromarray(renderer.render(f)).save(p)
        out.append(p)
    return out


def write_gif(
    frames: list[Frame], frame_ms: int, renderer: Renderer, path: Path
) -> int:
    """Per-scene adaptive 256-color GIF (reuses the render_matrix chunking)."""
    images: list[Image.Image] = []
    durations: list[int] = []
    for ch in chunk_scenes(dedup(frames, frame_ms)):
        mosaic = np.concatenate([renderer.render(f) for f, _ in ch[::2]], axis=0)
        palette = Image.fromarray(mosaic).quantize(
            colors=QUANTIZE_COLORS, method=Image.Quantize.MEDIANCUT
        )
        for f, ms in ch:
            images.append(
                Image.fromarray(renderer.render(f)).quantize(
                    palette=palette, dither=Image.Dither.NONE
                )
            )
            durations.append(ms)
    path.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(
        path,
        save_all=True,
        append_images=images[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    return len(images)


def _rel(p: Path) -> str:
    return str(p.relative_to(paths.REPO))


def process_game(
    name: str, args: argparse.Namespace, renderer: Renderer
) -> dict[str, object]:
    """Build, dump, render and hash one game; returns its manifest entry."""
    print(f"[{name}] build...", flush=True)
    build = build_game(name)
    if build.returncode != 0:
        print(f"[{name}] BUILD FAILED")
        return {"status": "build_failed", "error": build.stderr.strip()[-800:]}

    print(f"[{name}] dump {args.frames} frames...", flush=True)
    frames, raw = dump_frames(name, args.frames, args.stride)
    if not frames:
        return {"status": "no_frames"}

    out_game = args.out / name
    frames_dir = out_game / "frames"
    dump_path = out_game / "dump.txt"
    out_game.mkdir(parents=True, exist_ok=True)
    dump_path.write_text(raw)

    print(f"[{name}] render {len(frames)} PNGs...", flush=True)
    png_paths = save_pngs(frames, renderer, frames_dir)

    gif_rel: str | None = None
    if not args.no_gif:
        gif_path = out_game / f"{name}.gif"
        stride = args.stride if args.stride is not None else HOST_DEFAULT_STRIDE
        n = write_gif(frames, stride * game.TICK_MS, renderer, gif_path)
        gif_rel = _rel(gif_path)
        print(f"[{name}] gif {n} frames -> {gif_rel}", flush=True)

    per, combined = frame_hashes(frames)
    regression = check_regression(name, per, combined, args.update_baseline)
    renderer.cache.clear()

    return {
        "status": "ok",
        "frame_count": len(frames),
        "unique_frame_count": len(set(per)),
        "gif": gif_rel,
        "frames_dir": _rel(frames_dir),
        "png_frames": [_rel(p) for p in png_paths],
        "dump": _rel(dump_path),
        "stats": aggregate_stats(frames),
        "regression": regression,
    }


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-n", "--frames", type=int, default=DEFAULT_FRAMES)
    ap.add_argument("--stride", type=int, default=None)
    ap.add_argument("--games", type=str, default=None, help="comma-separated subset")
    ap.add_argument("--no-gif", action="store_true")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    return ap.parse_args()


def print_summary(manifest: dict[str, object]) -> None:
    """Console table an orchestrator can eyeball; the manifest is the machine copy."""
    games: dict[str, dict[str, object]] = manifest["games"]  # type: ignore[assignment]
    print("\n=== eval summary ===")
    header = (
        f"{'game':10} {'status':7} {'frames':>6} {'uniq':>4} "
        f"{'cov_mean':>8} {'cov_max':>7} {'colors':>6} {'regression':>12}"
    )
    print(header)
    print("-" * len(header))
    for name, e in games.items():
        if e.get("status") != "ok":
            print(f"{name:10} {str(e.get('status')):7}")
            continue
        st: dict[str, object] = e["stats"]  # type: ignore[assignment]
        rg: dict[str, object] = e["regression"]  # type: ignore[assignment]
        reg = str(rg.get("status"))
        if reg == "changed":
            reg += f"({len(rg.get('changed_frames', []))})"  # type: ignore[arg-type]
        print(
            f"{name:10} {'ok':7} {e['frame_count']:>6} {e['unique_frame_count']:>4} "
            f"{st['non_bg_coverage_mean']:>8} {st['non_bg_coverage_max']:>7} "
            f"{st['distinct_colors_total']:>6} {reg:>12}"
        )
    print(f"\nmanifest: {manifest['manifest_path']}")


def main() -> None:
    args = parse_args()
    names = (
        [g.strip() for g in args.games.split(",") if g.strip()]
        if args.games
        else discover_games()
    )
    print(f"games: {', '.join(names)}")

    args.out.mkdir(parents=True, exist_ok=True)
    renderer = Renderer()
    entries: dict[str, object] = {}
    for name in names:
        entries[name] = process_game(name, args, renderer)

    manifest: dict[str, object] = {
        "generated_at": datetime.now(UTC).isoformat(),
        "repo_root": str(paths.REPO),
        "frames_requested": args.frames,
        "stride": args.stride,
        "theme": DUMP_THEME,
        "canvas": {
            "w": ledfx.CANVAS_W,
            "h": ledfx.CANVAS_H,
            "grid_w": ledfx.GRID_W,
            "grid_h": ledfx.GRID_H,
        },
        "games": entries,
    }
    manifest_path = args.out / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    manifest["manifest_path"] = str(manifest_path)
    print_summary(manifest)

    # Make eval a real CI gate: exit non-zero on any visual regression or render
    # error. --update-baseline (re)writes baselines, so it never fails here.
    if not args.update_baseline:
        bad: list[str] = []
        for name, e in entries.items():
            ent: dict[str, object] = e  # type: ignore[assignment]
            reg: dict[str, object] = ent.get("regression") or {}  # type: ignore[assignment]
            if ent.get("status") != "ok" or reg.get("status") == "changed":
                bad.append(name)
        if bad:
            raise SystemExit(f"\nFAIL: visual regression in: {', '.join(bad)}")


if __name__ == "__main__":
    main()

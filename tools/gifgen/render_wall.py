"""Renders the PONG WALL product GIF from the real C++ engine.

Usage (from the repo root):
    PYTHONPATH=tools uv run --group dev python -m gifgen.render_wall [--stills-only]

Compiles tools/gifgen/dump_frames.cpp if needed, replays the scripted match,
composites every framebuffer with the ledfx glow pipeline, writes five preview
stills to tools/gifgen/preview/ and the final animation to assets/pong-wall.gif.
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path
from typing import Any, Final

import numpy as np
from PIL import Image

from gifgen import game, ledfx, paths
from gifgen.arrays import F32, U8, Frame

BINARY: Final[Path] = Path("/tmp/dump_frames")
GIF_PATH: Final[Path] = paths.ASSETS / "pong-wall.gif"
CXX: Final[str] = "g++"
CXX_FLAGS: Final[list[str]] = ["-std=c++14", "-O2"]
MAX_BYTES: Final[int] = 8 * 1024 * 1024
MAX_STRIDE: Final[int] = 3
CHUNK_MAX: Final[int] = 25
SCENE_CUT_CELLS: Final[int] = 20
QUANTIZE_COLORS: Final[int] = 256

STILLS: Final[dict[str, str]] = {
    "ready_pulse": "still_ready.png",
    "countdown": "still_countdown.png",
    "mid_rally": "still_rally.png",
    "goal_flash": "still_flash.png",
    "win_sweep": "still_win.png",
}


def ensure_binary() -> None:
    sources = [
        paths.REPO / "tools" / "gifgen" / "dump_frames.cpp",
        paths.REPO / "modules" / "pong_screen" / "pong_engine.cpp",
    ]
    headers = list((paths.REPO / "modules" / "pong_screen").glob("*.h"))
    newest = max(p.stat().st_mtime for p in sources + headers)
    if BINARY.exists() and BINARY.stat().st_mtime >= newest:
        return
    cmd = [
        CXX,
        *CXX_FLAGS,
        "-I",
        str(paths.REPO / "modules" / "pong_screen"),
        str(sources[0]),
        str(sources[1]),
        "-o",
        str(BINARY),
    ]
    print("building:", " ".join(cmd))
    subprocess.run(cmd, check=True)


def run_dump(stride: int) -> tuple[list[Frame], dict[str, int]]:
    proc = subprocess.run(
        [str(BINARY), str(stride)], capture_output=True, text=True, check=True
    )
    frames: list[Frame] = []
    marks: dict[str, int] = {}
    lines = proc.stdout.splitlines()
    i = 0
    while i < len(lines):
        if lines[i] == "F":
            rows = [
                [bytes.fromhex(tok) for tok in lines[i + 1 + r].split()]
                for r in range(ledfx.GRID_H)
            ]
            frames.append(
                np.array([[list(px) for px in row] for row in rows], np.uint8)
            )
            i += 1 + ledfx.GRID_H
        else:
            i += 1
    for line in proc.stderr.splitlines():
        parts = line.split()
        if parts and parts[0] == "MARK":
            marks[parts[2]] = int(parts[1])
        else:
            print(" ", line)
    # drop the initial all-black LINK_WAIT frame (would blink at the loop seam)
    while frames and not frames[0].any():
        frames.pop(0)
        marks = {k: v - 1 for k, v in marks.items()}
    return frames, marks


class Renderer:
    def __init__(self) -> None:
        self.background: F32 = ledfx.make_background()
        self.cache: dict[bytes, U8] = {}

    def render(self, frame: Frame) -> U8:
        key = frame.tobytes()
        out = self.cache.get(key)
        if out is None:
            out = ledfx.render_frame(frame, self.background)
            self.cache[key] = out
        return out


def dedup(frames: list[Frame], frame_ms: int) -> list[list[Any]]:
    """Merge runs of identical framebuffers into (frame, duration) pairs."""
    merged: list[list[Any]] = []
    for f in frames:
        if merged and np.array_equal(merged[-1][0], f):
            merged[-1][1] += frame_ms
        else:
            merged.append([f, frame_ms])
    return merged


def chunk_scenes(merged: list[list[Any]]) -> list[list[tuple[Frame, int]]]:
    """Split into runs of <= CHUNK_MAX frames, cutting on large scene changes.
    Each run shares one adaptive palette so Pillow delta-encodes within it."""
    chunks: list[list[tuple[Frame, int]]] = []
    cur: list[tuple[Frame, int]] = []
    prev = None
    for f, ms in merged:
        cut = (
            prev is not None and int(np.any(f != prev, axis=2).sum()) > SCENE_CUT_CELLS
        )
        if cur and (cut or len(cur) == CHUNK_MAX):
            chunks.append(cur)
            cur = []
        cur.append((f, ms))
        prev = f
    if cur:
        chunks.append(cur)
    return chunks


def write_gif(
    frames: list[Frame], frame_ms: int, renderer: Renderer
) -> tuple[int, int]:
    # per-run adaptive 256-color palettes; dithering is deliberately off — the
    # adaptive palettes show no banding and Floyd-Steinberg speckle costs ~25%
    images: list[Image.Image] = []
    durations: list[int] = []
    for ch in chunk_scenes(dedup(frames, frame_ms)):
        mosaic = np.concatenate([renderer.render(f) for f, _ in ch[::2]], axis=0)
        palette = Image.fromarray(mosaic).quantize(
            colors=QUANTIZE_COLORS, method=Image.Quantize.MEDIANCUT
        )
        for f, ms in ch:
            img = Image.fromarray(renderer.render(f))
            images.append(img.quantize(palette=palette, dither=Image.Dither.NONE))
            durations.append(ms)
    GIF_PATH.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(
        GIF_PATH,
        save_all=True,
        append_images=images[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    return len(images), sum(durations)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stills-only", action="store_true")
    ap.add_argument(
        "--stride", type=int, default=2, help="engine ticks per dumped frame"
    )
    args = ap.parse_args()

    ensure_binary()
    frames, marks = run_dump(args.stride)
    print(f"parsed {len(frames)} frames, marks: {marks}")
    renderer = Renderer()

    paths.ensure(paths.PREVIEW)
    for label, name in STILLS.items():
        if label not in marks:
            print(f"WARN no mark for {label}")
            continue
        Image.fromarray(renderer.render(frames[marks[label]])).save(
            paths.PREVIEW / name
        )
        print(f"still: {paths.PREVIEW / name}")
    if args.stills_only:
        return

    stride = args.stride
    while True:
        n, total_ms = write_gif(frames, stride * game.TICK_MS, renderer)
        size = GIF_PATH.stat().st_size
        print(
            f"gif: {GIF_PATH}  {size / 1e6:.2f} MB, {n} frames "
            f"({len(frames)} source), {total_ms / 1000:.1f}s"
        )
        if size <= MAX_BYTES or stride >= MAX_STRIDE:
            break
        stride += 1
        print(f"over {MAX_BYTES / 1e6:.0f} MB, retrying at stride {stride}")
        frames, marks = run_dump(stride)
        renderer.cache.clear()


if __name__ == "__main__":
    main()

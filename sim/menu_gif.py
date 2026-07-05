#!/usr/bin/env python3
"""Turn a menu_dump frame stream into a looping assets/menu.gif.

Reads the binary produced by sim/menu_dump (header "MD1\\n", u32 w, h, count, then
count * w*h*3 RGB888 bytes), subsamples for playback rate, nearest-neighbour scales
each frame, drops it into a subtle dark console bezel, and lets ffmpeg build the GIF
with a per-run 256-colour palette (palettegen stats_mode=diff / paletteuse dither=none).

    uv run --group dev python menu_gif.py <frames.bin> <out.gif> [--stride N] [--scale N]
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Final

from PIL import Image, ImageDraw

TICK_MS: Final[int] = 20  # sim/menu_dump advances nowMs by console::TICK_MS per frame

# Dark bezel that frames the panel — matches the repo's moody gallery look.
BEZEL_MARGIN: Final[int] = 18
BEZEL_RGB: Final[tuple[int, int, int]] = (10, 10, 12)
FRAME_RGB: Final[tuple[int, int, int]] = (38, 38, 44)  # thin edge highlight
CORNER_R: Final[int] = 10


def find_ffmpeg() -> str:
    for cand in ("/opt/homebrew/bin/ffmpeg", "ffmpeg"):
        p = shutil.which(cand) or (cand if Path(cand).exists() else None)
        if p:
            return p
    sys.exit("menu_gif: ffmpeg not found")


def read_frames(path: Path) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    if data[:4] != b"MD1\n":
        sys.exit(f"menu_gif: {path} is not an MD1 frame dump")
    w, h, n = struct.unpack_from("<III", data, 4)
    fb = w * h * 3
    base = 16
    frames = [data[base + i * fb : base + (i + 1) * fb] for i in range(n)]
    return w, h, frames


def bezeled(img: Image.Image) -> Image.Image:
    """Round the panel corners and drop it into a dark bezel with a thin edge."""
    w, h = img.size
    # rounded-corner mask so the panel reads like a screen, not a raw rectangle
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], CORNER_R, fill=255)

    ow, oh = w + 2 * BEZEL_MARGIN, h + 2 * BEZEL_MARGIN
    canvas = Image.new("RGB", (ow, oh), BEZEL_RGB)
    # faint edge highlight just outside the panel
    ImageDraw.Draw(canvas).rounded_rectangle(
        [BEZEL_MARGIN - 2, BEZEL_MARGIN - 2, BEZEL_MARGIN + w + 1, BEZEL_MARGIN + h + 1],
        CORNER_R + 2,
        outline=FRAME_RGB,
        width=2,
    )
    canvas.paste(img, (BEZEL_MARGIN, BEZEL_MARGIN), mask)
    return canvas


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("frames", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--stride", type=int, default=2, help="keep every Nth dumped frame")
    ap.add_argument("--scale", type=int, default=5, help="nearest-neighbour zoom")
    ap.add_argument("--colors", type=int, default=64, help="palette size (2..256)")
    ap.add_argument("--no-bezel", action="store_true")
    args = ap.parse_args()

    w, h, frames = read_frames(args.frames)
    kept = frames[:: args.stride]
    fps = round(1000.0 / (TICK_MS * args.stride), 3)
    print(f"menu_gif: {len(frames)} frames -> {len(kept)} @ stride {args.stride}, {fps} fps")

    ffmpeg = find_ffmpeg()
    tmp = Path(tempfile.mkdtemp(prefix="menu_gif_"))
    try:
        for i, fr in enumerate(kept):
            im = Image.frombytes("RGB", (w, h), fr).resize(
                (w * args.scale, h * args.scale), Image.NEAREST
            )
            if not args.no_bezel:
                im = bezeled(im)
            im.save(tmp / f"f{i:04d}.png")

        palette = tmp / "palette.png"
        pattern = str(tmp / "f%04d.png")
        colors = max(2, min(256, args.colors))
        subprocess.run(
            [ffmpeg, "-y", "-v", "error", "-framerate", str(fps), "-i", pattern,
             "-vf", f"palettegen=max_colors={colors}:stats_mode=diff", str(palette)],
            check=True,
        )
        args.out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [ffmpeg, "-y", "-v", "error", "-framerate", str(fps), "-i", pattern,
             "-i", str(palette), "-lavfi",
             "paletteuse=dither=none:diff_mode=rectangle", "-loop", "0", str(args.out)],
            check=True,
        )
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    kb = args.out.stat().st_size / 1024
    print(f"menu_gif: wrote {args.out} ({kb:.0f} KB, {len(kept)} frames, "
          f"{(w*args.scale)}x{(h*args.scale)} panel)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

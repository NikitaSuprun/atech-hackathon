"""Fail if the Python game contract (gifgen.game) drifts from the C++ headers.

Run: PYTHONPATH=tools python -m gifgen.check_cpp_constants
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Final

from gifgen import game
from gifgen.paths import REPO

HEADERS: Final[tuple[Path, ...]] = (
    REPO / "modules" / "pong_screen" / "pong_frame.h",
    REPO / "modules" / "pong_screen" / "pong_config.h",
    REPO / "modules" / "pong_screen" / "pong_proto.h",
    REPO / "modules" / "pong_screen" / "pong_shared.h",
)


def _blob() -> str:
    return "\n".join(p.read_text() for p in HEADERS)


def _int(blob: str, name: str, ctype: str = "int") -> int:
    m = re.search(rf"constexpr\s+{ctype}\s+{name}\b\s*=\s*(\d+)", blob)
    if not m:
        raise SystemExit(f"drift: {name} not found in the C++ headers")
    return int(m.group(1))


def _color(blob: str, name: str) -> tuple[int, int, int]:
    m = re.search(
        rf"constexpr\s+pong::Color\s+{name}\b\s*=\s*\{{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}}",
        blob,
    )
    if not m:
        raise SystemExit(f"drift: {name} not found in the C++ headers")
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)))


def _tile_map(blob: str) -> tuple[tuple[int, int, int], ...]:
    m = re.search(r"TILE_MAP\[[^\]]*\]\s*=\s*\{(.*?)\};", blob, re.S)
    if not m:
        raise SystemExit("drift: TILE_MAP not found in the C++ headers")
    triples = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", m.group(1))
    return tuple((int(a), int(b), int(c)) for a, b, c in triples)


def main() -> int:
    blob = _blob()
    errs: list[str] = []

    def eq(label: str, cpp: object, py: object) -> None:
        if cpp != py:
            errs.append(f"{label}: C++={cpp!r} != game.py={py!r}")

    eq("W", _int(blob, "W"), game.W)
    eq("H", _int(blob, "H"), game.H)
    eq("NUM_TILES", _int(blob, "NUM_TILES"), game.NUM_TILES)
    eq("TICK_HZ", _int(blob, "TICK_HZ"), game.TICK_HZ)
    eq("TICK_MS", 1000 // game.TICK_HZ, game.TICK_MS)
    eq("WIN_SCORE", _int(blob, "PONG_WIN_SCORE", "uint8_t"), game.WIN_SCORE)
    for cname, key in (
        ("COL_P1", "P1"),
        ("COL_P2", "P2"),
        ("COL_BALL", "BALL"),
        ("COL_NET", "NET"),
        ("COL_LOSE", "LOSE"),
    ):
        eq(cname, _color(blob, cname), game.COLORS[key])
    eq("TILE_MAP", _tile_map(blob), game.TILE_MAP)

    if errs:
        print("C++ <-> Python constant drift detected:", file=sys.stderr)
        for e in errs:
            print("  " + e, file=sys.stderr)
        return 1
    print("gifgen.game matches the C++ headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

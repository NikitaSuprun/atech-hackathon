"""The C++ game contract, mirrored once for the Python render tooling.

The C++ headers are the source of truth; check_cpp_constants.py fails CI on drift:
  modules/pong_screen/pong_frame.h   -> W, H
  modules/pong_screen/pong_config.h  -> NUM_TILES, TICK_HZ, COL_BALL/NET/LOSE, TILE_MAP
  modules/pong_screen/pong_shared.h  -> COL_P1, COL_P2
  modules/pong_screen/pong_proto.h   -> PONG_WIN_SCORE
"""

from __future__ import annotations

from typing import Final

# --- wall geometry ---
W: Final[int] = 6
H: Final[int] = 18
NUM_TILES: Final[int] = 12
TILE_COLS: Final[int] = 2
TILE_ROWS: Final[int] = 6
TILE_DIM: Final[int] = 3  # 3x3 LEDs per tile (C++ TILE_DIM; total LEDS_PER_TILE = 9)

# --- timing / match ---
TICK_HZ: Final[int] = 50
TICK_MS: Final[int] = 1000 // TICK_HZ  # 20 ms fixed step
WIN_SCORE: Final[int] = 3  # PONG_WIN_SCORE

# --- palette (8-bit RGB, keyed by role) ---
COLORS: Final[dict[str, tuple[int, int, int]]] = {
    "P1": (0, 200, 255),
    "P2": (255, 120, 0),
    "BALL": (255, 255, 255),
    "NET": (70, 70, 70),
    "LOSE": (255, 0, 0),
}

# --- physical tile map: (tileRow, tileCol, rot) in wiring/port order ---
TILE_MAP: Final[tuple[tuple[int, int, int], ...]] = (
    (0, 0, 2),
    (1, 0, 2),
    (2, 0, 2),
    (3, 0, 2),
    (4, 0, 2),
    (5, 0, 2),
    (0, 1, 0),
    (1, 1, 0),
    (2, 1, 0),
    (3, 1, 0),
    (4, 1, 0),
    (5, 1, 0),
)

# board port number per tile index (skips reserved 8 = USB-C, 12 = Reset)
PORT_ORDER: Final[tuple[int, ...]] = (1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 13, 14)

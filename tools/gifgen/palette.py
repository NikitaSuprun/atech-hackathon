"""Render-space color representations derived once from game.COLORS.

Render-only colors (gold, red flashes, diagram grays) stay local to their renderer.
"""

from __future__ import annotations

from typing import Final

import numpy as np

from gifgen import game
from gifgen.arrays import F32

# 8-bit RGB tuples, keyed like game.COLORS (P1, P2, BALL, NET, LOSE)
RGB8: Final[dict[str, tuple[int, int, int]]] = dict(game.COLORS)

# normalized float32 [0, 1] RGB for the additive compositor
NORM: Final[dict[str, F32]] = {
    name: (np.array(rgb, np.float32) / 255.0).astype(np.float32)
    for name, rgb in game.COLORS.items()
}

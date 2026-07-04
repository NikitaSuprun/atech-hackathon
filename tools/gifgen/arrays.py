"""numpy array type aliases shared by the render scripts."""

from __future__ import annotations

from typing import Any, TypeAlias

import numpy as np
from numpy.typing import NDArray

U8: TypeAlias = NDArray[np.uint8]
F32: TypeAlias = NDArray[np.float32]
F64: TypeAlias = NDArray[np.float64]
FloatArr: TypeAlias = NDArray[np.floating[Any]]
Frame: TypeAlias = NDArray[np.uint8]  # (H, W, 3) engine framebuffer

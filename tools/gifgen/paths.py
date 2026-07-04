"""Repo-relative paths — the single source of the repo root for the render tooling."""

from __future__ import annotations

from pathlib import Path
from typing import Final

REPO: Final[Path] = Path(__file__).resolve().parents[2]
ASSETS: Final[Path] = REPO / "assets"
PREVIEW: Final[Path] = REPO / "tools" / "gifgen" / "preview"
ATECH: Final[Path] = ASSETS / "atech"


def ensure(directory: Path) -> Path:
    """Create `directory` (and parents) if needed and return it."""
    directory.mkdir(parents=True, exist_ok=True)
    return directory

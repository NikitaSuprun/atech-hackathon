#!/usr/bin/env python3
"""Product-shot animation of the PONG controller board -> assets/controller.gif."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Final, TypedDict

import numpy as np
from PIL import Image

from gifgen import game, paths
from gifgen.arrays import F32, F64, U8

GIF_PATH: Final[Path] = paths.ASSETS / "controller.gif"

# canvas / timing
W: Final[int] = 440
H: Final[int] = 560
FRAMES: Final[int] = 240
DUR_MS: Final[int] = 50

# story timeline (frame indices)
HOLD_END: Final[int] = 45
COUNT_END: Final[int] = 75
RALLY1_END: Final[int] = 115
POINT1_END: Final[int] = 135
RALLY2_END: Final[int] = 175
POINT2_END: Final[int] = 195
WIN_START: Final[int] = 215
LOOP_FADE: Final[int] = 230
POP_FRAMES: Final[tuple[int, int]] = (HOLD_END, HOLD_END + 1)
PREVIEWS: Final[dict[int, str]] = {
    28: "ctrl_hold",
    96: "ctrl_rally",
    121: "ctrl_point",
    226: "ctrl_win",
}

# animation tuning
COUNT_STEP: Final[int] = 10
RALLY_PERIOD: Final[float] = 26.0
RALLY_SWING: Final[float] = 55.0
POINT_SWEEP: Final[float] = 720.0
WIN_SPIN_STEP: Final[float] = 33.0
TRAIL_DECAY: Final[float] = 0.60
SWEEP_DECAY: Final[float] = 0.74
NOTE_LIFE: Final[int] = 20
PULSE_LIFE: Final[int] = 16

# palette / encoding
PAL_SAMPLES: Final[tuple[int, ...]] = (
    0,
    20,
    47,
    55,
    67,
    90,
    110,
    118,
    124,
    150,
    160,
    178,
    186,
    190,
    205,
    216,
    220,
    228,
    235,
)
PAL_BRIGHT: Final[int] = 96
PAL_DARK: Final[int] = 159
PAL_SPLIT_LUM: Final[int] = 60
TRANSPARENT: Final[int] = 255

# P1/P2 identity colors come from the shared game contract (float64 for byte-exact
# compositing); render-only accents stay local.
WHITE: Final[F64] = np.array([1.0, 1.0, 1.0])
CYAN: Final[F64] = np.array(game.COLORS["P1"], np.float64) / 255.0
AMBER: Final[F64] = np.array(game.COLORS["P2"], np.float64) / 255.0
GOLD: Final[F64] = np.array([1.0, 180 / 255, 0.0])
RED: Final[F64] = np.array([1.0, 45 / 255, 30 / 255])

# board geometry
K1: Final[tuple[float, float]] = (128.0, 168.0)
K2: Final[tuple[float, float]] = (312.0, 168.0)
CAP_R: Final[float] = 46.0
RING_R: Final[float] = 64.0
RING_LEDS: Final[int] = 12
LED_STEP: Final[float] = 360.0 / RING_LEDS

BX0: Final[int] = 108
BY0: Final[int] = 292
BX1: Final[int] = 332
BY1: Final[int] = 412
GX0: Final[int] = 120
GY0: Final[int] = 304
GX1: Final[int] = 320
GY1: Final[int] = 400
GW: Final[int] = GX1 - GX0
GH: Final[int] = GY1 - GY0
PAD: Final[int] = 14
SPK: Final[tuple[float, float]] = (92.0, 476.0)
SPK_PITCH: Final[int] = 11

# comparison thresholds lifted out of the drawing math
SHRINK_MIN_SCALE: Final[int] = 3
SCREW_SLOT_HW: Final[float] = 1.2
COUNT_FLASH_FRAMES: Final[int] = 2
MIN_PULSE_W: Final[float] = 0.02
MIN_RING_A: Final[float] = 0.03


class KnobFrame(TypedDict):
    press: float
    rot1: float
    rot2: float
    m1: float
    m2: float
    c1: F64
    c2: F64
    g1: float
    g2: float
    acc1: F64
    acc2: F64
    pop: bool
    ov1: tuple[F64, F64] | None
    ov2: tuple[F64, F64] | None
    ring1: F64
    ring2: F64


def inv_tone(hexs: str) -> F64:
    v = np.array([int(hexs[i : i + 2], 16) for i in (1, 3, 5)]) / 255.0
    u = np.clip(v, 0.0, 0.995) ** (1 / 0.9)
    return u / (1 - u)


BG_C: Final[F64] = inv_tone("#0b0d10")
PCB_C: Final[F64] = inv_tone("#161a21")
BEZ_C: Final[F64] = inv_tone("#0c0f14")
GLASS_C: Final[F64] = inv_tone("#05070b")
HOLE_C: Final[F64] = inv_tone("#06080b")
M_EDGE: Final[F64] = inv_tone("#1a1e25")
M_FACE: Final[F64] = inv_tone("#566070")

FONT: Final[dict[str, tuple[str, ...]]] = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "C": ("01110", "10001", "10000", "10000", "10000", "10001", "01110"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "G": ("01110", "10001", "10000", "10011", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11111", "00010", "00100", "00010", "00001", "10001", "01110"),
    "!": ("00100", "00100", "00100", "00100", "00100", "00000", "00100"),
    ":": ("00000", "00100", "00100", "00000", "00100", "00100", "00000"),
    ".": ("00000", "00000", "00000", "00000", "00000", "00000", "00100"),
    " ": ("00000",) * 7,
}
NOTE: Final[tuple[str, ...]] = (
    "00100",
    "00110",
    "00101",
    "00100",
    "00100",
    "01100",
    "11100",
)


def glow(
    buf: F64, x: float, y: float, color: F64, inten: float = 1.0, scale: float = 1.0
) -> None:
    core = color + (WHITE - color) * 0.7
    for sig, wgt, col in (
        (14 * scale, 0.30, color),
        (6 * scale, 0.75, color),
        (2.5 * scale, 1.6, core),
    ):
        r = max(2, int(sig * 3.0))
        x0, x1 = max(0, int(x) - r), min(W, int(x) + r + 1)
        y0, y1 = max(0, int(y) - r), min(H, int(y) + r + 1)
        if x0 >= x1 or y0 >= y1:
            continue
        dx = np.arange(x0, x1) - x
        dy = np.arange(y0, y1) - y
        g = np.exp(-(dy[:, None] ** 2 + dx[None, :] ** 2) / (2 * sig * sig)) * (
            wgt * inten
        )
        buf[y0:y1, x0:x1] += g[..., None] * col


def box1d(a: F64, r: int, axis: int) -> F64:
    if r <= 0:
        return a
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis)
    n = a.shape[axis]
    i0 = [slice(None)] * a.ndim
    i1 = [slice(None)] * a.ndim
    i0[axis] = slice(2 * r + 1, 2 * r + 1 + n)
    i1[axis] = slice(0, n)
    return (c[tuple(i0)] - c[tuple(i1)]) / (2 * r + 1)


def blur(img: F64, sigma: float) -> F64:
    r = max(1, int(sigma * 0.7))
    out = img
    for _ in range(3):
        out = box1d(box1d(out, r, 0), r, 1)
    return out


def rrect_d(
    xx: F64, yy: F64, x0: float, y0: float, x1: float, y1: float, r: float
) -> F64:
    cx = np.clip(xx, x0 + r, x1 - r)
    cy = np.clip(yy, y0 + r, y1 - r)
    return np.hypot(xx - cx, yy - cy) - r


def splat_line(acc: F64, x0: float, y0: float, x1: float, y1: float) -> None:
    n = int(math.hypot(x1 - x0, y1 - y0) * 2) + 2
    ts = np.linspace(0.0, 1.0, n)
    xs = x0 + (x1 - x0) * ts
    ys = y0 + (y1 - y0) * ts
    ix, iy = np.floor(xs).astype(int), np.floor(ys).astype(int)
    fx, fy = xs - ix, ys - iy
    for dx in (0, 1):
        for dy in (0, 1):
            wx = fx if dx else 1 - fx
            wy = fy if dy else 1 - fy
            np.add.at(acc, (iy + dy, ix + dx), wx * wy)


def text_w(s: str, scale: int) -> int:
    return len(s) * 6 * scale - scale


def draw_text(
    img: F64,
    s: str,
    cx: float,
    cy: float,
    scale: int,
    colors: F64 | list[F64],
    inten: float = 1.0,
) -> None:
    x0 = int(round(cx - text_w(s, scale) / 2))
    y0 = int(round(cy - 3.5 * scale))
    sz = max(1, scale - 1) if scale >= SHRINK_MIN_SCALE else scale
    for i, ch in enumerate(s):
        col = colors[i] if isinstance(colors, list) else colors
        for ry, row in enumerate(FONT[ch]):
            for rx, c in enumerate(row):
                if c == "1":
                    px, py = x0 + (i * 6 + rx) * scale, y0 + ry * scale
                    img[py : py + sz, px : px + sz] += col * inten


def ring_pos(center: tuple[float, float], k: int) -> tuple[float, float]:
    a = math.radians(-90 + k * LED_STEP)
    return center[0] + RING_R * math.cos(a), center[1] + RING_R * math.sin(a)


def build_static() -> tuple[F64, F64, F64, F64]:
    rng = np.random.default_rng(7)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)

    light = 0.55 + 0.75 * np.exp(-((xx - 220) ** 2 + (yy - 235) ** 2) / (2 * 250**2))
    S = BG_C * light[..., None]

    d = rrect_d(xx, yy, 26, 26, 414, 534, 22)
    mask = np.clip(0.5 - d, 0, 1)
    shadow = blur(np.roll(mask, 9, axis=0), 7)
    S *= (1 - 0.55 * shadow * (1 - mask))[..., None]

    grad = 1.16 - 0.30 * (yy - 26) / 508
    board = PCB_C * grad[..., None]
    board += (rng.random((H, W, 1)) - 0.5) * 0.006
    topw = np.clip(1.15 - 1.1 * (yy - 26) / 508, 0.2, 1.0)
    rim = np.exp(-((d + 1.6) ** 2) / 2.5) * topw
    board += rim[..., None] * 0.10 * np.array([0.85, 0.95, 1.1])
    m3 = mask[..., None]
    S = S * (1 - m3) + board * m3

    # routing traces from the knobs down to the display
    acc = np.zeros((H, W))
    for kx, sgn in ((K1[0], 1), (K2[0], -1)):
        for off in (-14, 0, 14):
            x = kx + off
            splat_line(acc, x, 246, x, 268)
            splat_line(acc, x, 268, x + sgn * 16, 284)
            splat_line(acc, x + sgn * 16, 284, x + sgn * 16, 293)
    S += np.clip(acc, 0, 1)[..., None] * 0.035 * np.array([0.8, 0.9, 1.05])

    # TFT bezel + glass
    d2 = rrect_d(xx, yy, BX0, BY0, BX1, BY1, 12)
    bm = np.clip(0.5 - d2, 0, 1)[..., None]
    bez = BEZ_C * (1.06 - 0.2 * (yy - BY0) / (BY1 - BY0))[..., None]
    bez += (np.exp(-((d2 + 2.2) ** 2) / 2.0) * np.clip(1 - (yy - BY0) / 30, 0, 1))[
        ..., None
    ] * 0.05
    S = S * (1 - bm) + bez * bm
    d3 = rrect_d(xx, yy, GX0, GY0, GX1, GY1, 7)
    gm = np.clip(0.5 - d3, 0, 1)
    glass = GLASS_C * (1 + 0.35 * np.clip(1 - (yy - GY0) / 40, 0, 1))[..., None]
    streak = np.exp(
        -(((xx - GX0) - (yy - GY0) * 1.7 - 46) ** 2) / (2 * 13**2)
    ) * np.clip(1 - (yy - GY0) / 70, 0, 1)
    glass += streak[..., None] * 0.035 * np.array([0.9, 0.95, 1.05])
    glass *= (1 - 0.35 * np.exp(-((d3 + 1.5) ** 2) / 2.0))[..., None]
    gm3 = gm[..., None]
    S = S * (1 - gm3) + glass * gm3
    spill = 0.30 + 0.70 * gm[GY0 - PAD : GY1 + PAD, GX0 - PAD : GX1 + PAD, None]

    def hole(cx: float, cy: float, r: float, depth: float = 0.85) -> None:
        R = int(r + 4)
        hx0, hy0 = int(cx) - R, int(cy) - R
        hdx = np.arange(hx0, hx0 + 2 * R + 1) - cx
        hdy = np.arange(hy0, hy0 + 2 * R + 1) - cy
        rr = np.hypot(hdy[:, None], hdx[None, :])
        a = np.clip(r + 0.5 - rr, 0, 1)[..., None]
        reg = S[hy0 : hy0 + 2 * R + 1, hx0 : hx0 + 2 * R + 1]
        reg[:] = reg * (1 - a * depth) + HOLE_C * a * depth
        rimh = (
            np.exp(-((rr - r - 0.8) ** 2) / 1.0)
            * np.clip(hdy[:, None] / r, 0, 1)
            * 0.04
        )
        reg += rimh[..., None]

    # LED ring wells with a faint idle tint
    for center, accent in ((K1, CYAN), (K2, AMBER)):
        for k in range(RING_LEDS):
            x, y = ring_pos(center, k)
            hole(x, y, 3.3)
            glow(S, x, y, accent, 0.075, 0.42)

    for i in range(5):
        for j in range(5):
            hole(SPK[0] + (i - 2) * SPK_PITCH, SPK[1] + (j - 2) * SPK_PITCH, 2.6, 0.9)
    d4 = rrect_d(xx, yy, SPK[0] - 30, SPK[1] - 30, SPK[0] + 30, SPK[1] + 30, 12)
    S += (np.exp(-(d4**2) / 1.4) * 0.022)[..., None]

    def screw(cx: float, cy: float) -> None:
        r = 7.5
        R = 12
        sx0, sy0 = int(cx) - R, int(cy) - R
        sdx = np.arange(sx0, sx0 + 2 * R + 1) - cx
        sdy = np.arange(sy0, sy0 + 2 * R + 1) - cy
        rr = np.hypot(sdy[:, None], sdx[None, :])
        reg = S[sy0 : sy0 + 2 * R + 1, sx0 : sx0 + 2 * R + 1]
        sink = np.clip(r + 2.5 - rr, 0, 1) * 0.5
        reg *= (1 - sink)[..., None]
        a = np.clip(r + 0.5 - rr, 0, 1)[..., None]
        met = (
            inv_tone("#2a3038")
            * np.clip(1 - 0.5 * (sdy[:, None] + 0.4 * sdx[None, :]) / r, 0.5, 1.5)[
                ..., None
            ]
        )
        u = (sdx[None, :] + sdy[:, None]) / math.sqrt(2)
        v = (sdx[None, :] - sdy[:, None]) / math.sqrt(2)
        on_slot = (np.abs(u) < SCREW_SLOT_HW) | (np.abs(v) < SCREW_SLOT_HW)
        slot = on_slot & (rr < r - 1)
        met = np.where(slot[..., None], met * 0.35, met)
        reg[:] = reg * (1 - a) + met * a

    for sx, sy in ((52, 52), (388, 52), (52, 508), (388, 508)):
        screw(sx, sy)

    def smd(x: int, y: int, w: int, h: int) -> None:
        body = inv_tone("#171b22")
        pad = inv_tone("#252b34")
        S[y : y + h, x : x + w] = S[y : y + h, x : x + w] * 0.35 + body
        S[y : y + h, x : x + 2] = pad
        S[y : y + h, x + w - 2 : x + w] = pad
        S[y + h : y + h + 1, x : x + w] *= 0.8

    for sx, sy, sw, sh in (
        (346, 444, 13, 5),
        (346, 456, 13, 5),
        (346, 468, 13, 5),
        (374, 448, 5, 13),
        (374, 466, 5, 13),
        (386, 448, 5, 13),
    ):
        smd(sx, sy, sw, sh)

    silk = np.array([0.9, 1.0, 1.1])
    draw_text(S, "PONG", 220, 54, 2, silk * 0.060)
    draw_text(S, "CONSOLE", 220, 73, 1, silk * 0.042)
    draw_text(S, "P1", K1[0], 250, 1, CYAN * 0.16)
    draw_text(S, "P2", K2[0], 250, 1, AMBER * 0.16)
    draw_text(S, "SN 001", 362, 520, 1, silk * 0.022)

    rn = np.hypot((xx - 220) / 280, (yy - 252) / 330)
    vig = np.clip(1 - 0.42 * rn**2.6, 0, 1)[..., None]
    dith = (rng.random((H, W, 3)) - 0.5) * 2.4
    return S, spill, vig, dith


def draw_knob(
    buf: F64,
    center: tuple[float, float],
    rot: float,
    press: float,
    accent: F64,
    mglow: float = 1.0,
) -> None:
    cx, cy = center
    rc = CAP_R * (1 - 0.045 * press)
    R = int(CAP_R + 12)
    x0, y0 = int(cx) - R, int(cy) - R
    dx = np.arange(x0, x0 + 2 * R + 1) - cx
    dy = np.arange(y0, y0 + 2 * R + 1) - cy
    XX, YY = dx[None, :], dy[:, None]
    rr = np.hypot(YY, XX)
    th = np.arctan2(YY, XX)
    reg = buf[y0 : y0 + 2 * R + 1, x0 : x0 + 2 * R + 1]

    sh = np.clip((rc + 7 - np.hypot(YY - 3, XX)) / 7, 0, 1)
    reg *= (1 - 0.55 * sh)[..., None]

    a = np.clip(rc + 0.5 - rr, 0, 1)[..., None]
    band = np.clip((rr - 0.78 * rc) / 2.5, 0, 1)
    knurl = 0.60 + 0.40 * np.sin(th * 28 + math.radians(rot)) ** 2
    facet = np.clip(1 - rr / (0.80 * rc), 0, 1) ** 0.9
    face = M_EDGE + (M_FACE - M_EDGE) * facet[..., None] + accent * 0.012
    bandc = M_EDGE * (0.55 + 0.75 * knurl)[..., None]
    metal = face * (1 - band[..., None]) + bandc * band[..., None]
    light = np.clip(1 - 0.42 * (YY + 0.25 * XX) / rc, 0.55, 1.6)
    metal *= light[..., None]
    hub = np.clip((0.30 * rc - rr) / 1.5 + 0.5, 0, 1)
    metal *= (1 - 0.18 * hub)[..., None]
    metal += (
        np.exp(-((rr - 0.30 * rc) ** 2) / 2.0) * np.clip(0.5 - YY / rc, 0, 1) * 0.05
    )[..., None]
    spec = np.exp(
        -((XX + 0.36 * rc) ** 2 + (YY + 0.36 * rc) ** 2) / (2 * (0.30 * rc) ** 2)
    )
    metal += (spec * 0.16 * (1 - band))[..., None]
    if press > 0:
        metal *= 1 - 0.10 * press
        metal *= (1 - np.exp(-((rr - rc) ** 2) / 18.0) * 0.45 * press)[..., None]
    reg[:] = reg * (1 - a) + metal * a

    ar = math.radians(rot)
    ca, sa = math.cos(ar), math.sin(ar)
    for t in np.linspace(0.34, 0.90, 11):
        glow(
            buf,
            cx + ca * t * rc,
            cy + sa * t * rc,
            accent,
            (0.04 + 0.34 * t) * mglow,
            0.24,
        )
    glow(buf, cx + ca * 0.97 * rc, cy + sa * 0.97 * rc, accent, 0.85 * mglow, 0.40)


class State:
    def __init__(self) -> None:
        self.r1: F64 = np.zeros(RING_LEDS)
        self.r2: F64 = np.zeros(RING_LEDS)
        self.prev1: float = -90.0
        self.prev2: float = -90.0


def mark_sweep(ring: F64, prev: float, rot: float) -> None:
    d = ((rot - prev + 180) % 360) - 180
    steps = max(1, int(abs(d) / (LED_STEP / 2)) + 1)
    for i in range(steps + 1):
        a = prev + d * i / steps
        ring[int(round((a + 90) / LED_STEP)) % RING_LEDS] = 1.0


def rally_rot(f: int, t0: int, sign: int) -> float:
    return -90 + sign * RALLY_SWING * math.sin(2 * math.pi * (f - t0) / RALLY_PERIOD)


def in_rally(f: int) -> bool:
    return f < RALLY1_END or POINT1_END <= f < RALLY2_END or POINT2_END <= f < WIN_START


def smoothstep(u: float) -> float:
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


def update(f: int, st: State) -> KnobFrame:
    p: KnobFrame = {
        "press": 0.0,
        "rot1": -90.0,
        "rot2": -90.0,
        "m1": 1.0,
        "m2": 1.0,
        "c1": CYAN,
        "c2": AMBER,
        "g1": 1.0,
        "g2": 1.0,
        "acc1": CYAN,
        "acc2": AMBER,
        "pop": f in POP_FRAMES,
        "ov1": None,
        "ov2": None,
        "ring1": np.zeros(RING_LEDS),
        "ring2": np.zeros(RING_LEDS),
    }
    if f < HOLD_END:
        p["press"] = 1.0
        p["m1"] = p["m2"] = 0.5
        fill = RING_LEDS * f / HOLD_END
        n = int(fill)
        a = np.zeros(RING_LEDS)
        a[:n] = 0.65
        if n < RING_LEDS:
            a[n] = 0.3 + 0.7 * (fill - n)
        st.r1[:] = a
        st.r2[:] = a
    elif f < COUNT_END:
        if f == POP_FRAMES[0]:
            p["press"] = -0.3
        elif f == POP_FRAMES[1]:
            p["press"] = -0.12
        lit = int(round(RING_LEDS * (COUNT_END - f) / (COUNT_END - HOLD_END)))
        a = np.zeros(RING_LEDS)
        a[:lit] = 0.55
        if 0 < lit <= RING_LEDS:
            a[lit - 1] = 0.9
        st.r1[:] = a
        st.r2[:] = a
    elif in_rally(f):
        t0 = (
            COUNT_END
            if f < RALLY1_END
            else (POINT1_END if f < RALLY2_END else POINT2_END)
        )
        p["rot1"] = rally_rot(f, t0, 1)
        p["rot2"] = rally_rot(f, t0, -1)
        st.r1 *= TRAIL_DECAY
        st.r2 *= TRAIL_DECAY
        mark_sweep(st.r1, st.prev1, p["rot1"])
        mark_sweep(st.r2, st.prev2, p["rot2"])
    elif f < POINT1_END:
        e = smoothstep((f - RALLY1_END) / (POINT1_END - RALLY1_END))
        p["rot1"] = -90 + POINT_SWEEP * e
        base = rally_rot(RALLY1_END, COUNT_END, -1)
        p["rot2"] = base + (-90 - base) * e
        st.r1 *= SWEEP_DECAY
        st.r2[:] = 0.0
        mark_sweep(st.r1, st.prev1, p["rot1"])
        p["g1"], p["m1"] = 1.3, 1.3
        t = f - RALLY1_END
        fl = abs(math.sin(math.pi * t / 10)) * max(0.0, 1 - t / 24)
        p["ov2"] = (np.full(RING_LEDS, 0.12 + 0.88 * fl), RED)
    elif f < POINT2_END:
        e = smoothstep((f - RALLY2_END) / (POINT2_END - RALLY2_END))
        p["rot2"] = -90 + POINT_SWEEP * e
        base = rally_rot(RALLY2_END, POINT1_END, 1)
        p["rot1"] = base + (-90 - base) * e
        st.r2 *= SWEEP_DECAY
        st.r1[:] = 0.0
        mark_sweep(st.r2, st.prev2, p["rot2"])
        p["g2"], p["m2"] = 1.3, 1.3
        t = f - RALLY2_END
        fl = abs(math.sin(math.pi * t / 10)) * max(0.0, 1 - t / 24)
        p["ov1"] = (np.full(RING_LEDS, 0.12 + 0.88 * fl), RED)
    else:
        p["rot1"] = -90 + (f - WIN_START) * WIN_SPIN_STEP
        st.r1 *= SWEEP_DECAY
        mark_sweep(st.r1, st.prev1, p["rot1"])
        st.r1[:] = np.maximum(st.r1, 0.12)
        st.r2[:] = 0.10
        p["c1"] = p["acc1"] = GOLD
        p["g1"], p["m1"] = 1.25, 1.3
        p["m2"] = 0.5
    st.prev1, st.prev2 = p["rot1"], p["rot2"]
    p["ring1"] = st.r1.copy()
    p["ring2"] = st.r2.copy()
    return p


def tft_layer(f: int) -> F64:
    t = np.zeros((GH + 2 * PAD, GW + 2 * PAD, 3))
    cx, cy = PAD + GW / 2, PAD + GH / 2
    if f < HOLD_END:
        inten = 1.7 + 0.7 * math.sin(2 * math.pi * f / 22)
        draw_text(t, "HOLD TO", cx, cy - 14, 3, WHITE * 0.95, inten)
        draw_text(t, "START", cx, cy + 13, 3, WHITE * 0.95, inten)
    elif f < COUNT_END:
        age = (f - HOLD_END) % COUNT_STEP
        inten = 4.2 if age < COUNT_FLASH_FRAMES else 2.6
        draw_text(t, "321"[(f - HOLD_END) // COUNT_STEP], cx, cy, 10, WHITE, inten)
    elif in_rally(f):
        s = "0:0" if f < RALLY1_END else ("1:0" if f < RALLY2_END else "2:1")
        draw_text(t, s, cx, cy - 12, 5, [CYAN, WHITE * 0.7, AMBER], 2.6)
        draw_text(
            t, "RALLY", cx, cy + 26, 2, WHITE * 0.85, 1.2 + 0.3 * math.sin(f * 0.5)
        )
    elif f < POINT2_END:
        t0, tag, col = (
            (RALLY1_END, "P1!", CYAN) if f < POINT1_END else (RALLY2_END, "P2!", AMBER)
        )
        inten = 3.4 if ((f - t0) // 3) % 2 == 0 else 1.4
        draw_text(t, "POINT", cx, cy - 15, 4, WHITE, inten)
        draw_text(t, tag, cx, cy + 17, 4, col, inten)
    else:
        inten = 2.6 + 0.8 * math.sin((f - WIN_START) * 0.55)
        draw_text(t, "P1 WINS", cx, cy - 16, 3, GOLD, inten)
        draw_text(t, "3:1", cx, cy + 18, 5, [CYAN, WHITE * 0.7, AMBER], 2.4)
    return t


def add_tft(buf: F64, t: F64, spill: F64) -> None:
    out = t * 1.1 + blur(t, 1.6) * 0.8 + blur(t, 5.0) * 0.55
    out[::2] *= 0.93
    buf[GY0 - PAD : GY1 + PAD, GX0 - PAD : GX1 + PAD] += out * spill


EVENTS: Final[tuple[tuple[int, F64], ...]] = (
    (RALLY1_END, CYAN),
    (RALLY2_END, AMBER),
    (WIN_START, GOLD),
    (WIN_START + 9, GOLD),
)
NOTES: Final[tuple[tuple[int, F64], ...]] = (
    (RALLY1_END + 1, CYAN),
    (RALLY1_END + 7, CYAN),
    (RALLY2_END + 1, AMBER),
    (RALLY2_END + 7, AMBER),
    (WIN_START + 1, GOLD),
    (WIN_START + 7, GOLD),
    (WIN_START + 13, GOLD),
)


def draw_note(buf: F64, x: float, y: float, color: F64, alpha: float) -> None:
    for ry, row in enumerate(NOTE):
        for rx, c in enumerate(row):
            if c == "1":
                glow(buf, x + rx * 2, y + ry * 2, color, 0.30 * alpha, 0.22)


def speaker_fx(buf: F64, f: int) -> None:
    for t0, col in EVENTS:
        a = f - t0
        if 0 <= a <= PULSE_LIFE:
            env = 1 - a / PULSE_LIFE
            c = col + (WHITE - col) * 0.35
            for i in range(5):
                for j in range(5):
                    d = math.hypot(i - 2, j - 2)
                    w = math.exp(-((a * 0.55 - d) ** 2) / 1.4) * env
                    if w > MIN_PULSE_W:
                        glow(
                            buf,
                            SPK[0] + (i - 2) * SPK_PITCH,
                            SPK[1] + (j - 2) * SPK_PITCH,
                            c,
                            0.5 * w,
                            0.28,
                        )
    for t0, col in NOTES:
        a = f - t0
        if 0 <= a < NOTE_LIFE:
            alpha = min(1.0, a / 3) * (1 - a / NOTE_LIFE)
            draw_note(
                buf,
                SPK[0] + 5 * math.sin(a * 0.45 + t0),
                SPK[1] - a * 2.6,
                col,
                alpha,
            )


def draw_all(f: int, p: KnobFrame, static: F64, spill: F64) -> F64:
    buf = static.copy()
    draw_knob(buf, K1, p["rot1"], p["press"], p["acc1"], p["m1"])
    draw_knob(buf, K2, p["rot2"], p["press"], p["acc2"], p["m2"])
    for center, ring, col0, ov, gain0 in (
        (K1, p["ring1"], p["c1"], p["ov1"], p["g1"]),
        (K2, p["ring2"], p["c2"], p["ov2"], p["g2"]),
    ):
        arr, col, gain = ring, col0, gain0
        if p["pop"]:
            arr, col, gain = np.full(RING_LEDS, 1.1), WHITE, 1.0
        elif ov is not None:
            arr, col = ov
            gain = 1.0
        for k in range(RING_LEDS):
            if arr[k] > MIN_RING_A:
                x, y = ring_pos(center, k)
                glow(buf, x, y, col, arr[k] * gain * 0.95, 0.55)
    add_tft(buf, tft_layer(f), spill)
    speaker_fx(buf, f)
    return buf


def tonemap(buf: F64, vig: F64, dith: F64) -> U8:
    x = buf * vig
    t = x / (1 + x)
    v = np.clip(t, 0, 1) ** 0.9 * 255 + dith
    return np.clip(v, 0, 255).astype(np.uint8)


def remap_exact(frames: list[U8], pal_rgb: F32) -> list[U8]:
    packed = [
        (fr[..., 0].astype(np.uint32) << 16)
        | (fr[..., 1].astype(np.uint32) << 8)
        | fr[..., 2]
        for fr in frames
    ]
    uniq = np.unique(np.concatenate([p.ravel() for p in packed]))
    cols = np.stack([(uniq >> 16) & 255, (uniq >> 8) & 255, uniq & 255], axis=1).astype(
        np.float32
    )
    near = np.empty(len(uniq), np.uint8)
    for i in range(0, len(uniq), 65536):
        d = ((cols[i : i + 65536, None, :] - pal_rgb[None, :, :]) ** 2).sum(axis=2)
        near[i : i + 65536] = d.argmin(axis=1).astype(np.uint8)
    return [near[np.searchsorted(uniq, p.ravel())].reshape(p.shape) for p in packed]


def pal_from(pixels: U8, n: int) -> F32:
    w = 2048
    h = (len(pixels) + w - 1) // w
    img = np.zeros((h * w, 3), np.uint8)
    img[: len(pixels)] = pixels
    img[len(pixels) :] = pixels[0]
    q = Image.fromarray(img.reshape(h, w, 3)).quantize(
        n, method=Image.Quantize.MEDIANCUT
    )
    pal = q.getpalette() or []
    return np.array(pal[: n * 3], np.float32).reshape(n, 3)


def write_gif(frames: list[U8]) -> int:
    px = np.concatenate([frames[i].reshape(-1, 3) for i in PAL_SAMPLES])
    lum = px.mean(axis=1)
    # split the palette so glow ramps and whites keep enough entries
    pal_rgb = np.vstack(
        [
            pal_from(px[lum > PAL_SPLIT_LUM], PAL_BRIGHT),
            pal_from(px[lum <= PAL_SPLIT_LUM], PAL_DARK),
        ]
    )
    idx_frames = remap_exact(frames, pal_rgb)
    pal = pal_rgb.astype(np.uint8).ravel().tolist() + [255, 0, 255]
    imgs: list[Image.Image] = []
    prev = None
    for idx in idx_frames:
        d = idx if prev is None else np.where(idx == prev, np.uint8(TRANSPARENT), idx)
        im = Image.fromarray(d.astype(np.uint8), mode="P")
        im.putpalette(pal)
        imgs.append(im)
        prev = idx
    imgs[0].save(
        GIF_PATH,
        save_all=True,
        append_images=imgs[1:],
        duration=DUR_MS,
        loop=0,
        transparency=TRANSPARENT,
        disposal=1,
        optimize=False,
    )
    return GIF_PATH.stat().st_size


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--previews", action="store_true", help="render preview stills only"
    )
    args = ap.parse_args()

    paths.ensure(paths.PREVIEW)
    paths.ensure(GIF_PATH.parent)
    static, spill, vig, dith = build_static()

    st = State()
    frames: list[U8] = []
    first_lin = None
    for f in range(FRAMES):
        p = update(f, st)
        if args.previews and f not in PREVIEWS:
            continue
        buf = draw_all(f, p, static, spill)
        if f == 0:
            first_lin = buf.copy()
        if f > LOOP_FADE and first_lin is not None:
            s = smoothstep((f - LOOP_FADE) / (FRAMES - LOOP_FADE))
            buf = buf * (1 - s) + first_lin * s
        u8 = tonemap(buf, vig, dith)
        if f in PREVIEWS:
            Image.fromarray(u8).save(paths.PREVIEW / f"{PREVIEWS[f]}.png")
            print(f"preview {PREVIEWS[f]}.png  (frame {f})")
        if not args.previews:
            frames.append(u8)

    if not args.previews:
        size = write_gif(frames)
        print(f"{GIF_PATH}  {len(frames)} frames  {size / 1e6:.2f} MB")


if __name__ == "__main__":
    main()

"""Glowing-LED wall renderer: static background plate, precomputed gaussian
light sprites, additive float32 compositing, filmic tonemap."""

import numpy as np

# LED geometry (px)
PITCH = 18
GRID_W, GRID_H = 6, 18
TILE_LEDS = 3
TILE_COLS, TILE_ROWS = 2, 6
TILE_PAD = 6
SEAM = 7
MARGIN = 24
TILE_PX = TILE_LEDS * PITCH + 2 * TILE_PAD
CANVAS_W = 2 * MARGIN + TILE_COLS * TILE_PX + (TILE_COLS - 1) * SEAM
CANVAS_H = 2 * MARGIN + TILE_ROWS * TILE_PX + (TILE_ROWS - 1) * SEAM

# tonemap
EXPOSURE = 2.2
WHITE_PT = 3.0
GAMMA = 0.9

# light sprites: wide halo + mid glow + hot core (overdriven-WS2812 look)
HALO_SIGMA, HALO_W = 1.00 * PITCH, 0.30
MID_SIGMA, MID_W = 0.42 * PITCH, 0.75
CORE_SIGMA, CORE_W = 0.16 * PITCH, 1.60
WHITE_LERP = 0.6

# background palette (display space)
BEZEL = "#0b0d10"
TILE_FACE = "#08090c"
TILE_EDGE = "#14161b"
PKG = "#1c1f24"
PKG_DOT = "#101216"
CORNER_R = 14
VIGNETTE = 0.08
PKG_SIZE = 9
DOT_SIZE = 3


def _hex(c):
    return np.array([int(c[i : i + 2], 16) / 255.0 for i in (1, 3, 5)], np.float32)


def _lin(c):
    # invert gamma+exposure so background colors land near their display values
    return (_hex(c) ** (1.0 / GAMMA)) / EXPOSURE


def _gauss(sigma):
    r = int(np.ceil(2.6 * sigma))
    ax = np.arange(-r, r + 1, dtype=np.float32)
    g = np.exp(-(ax[:, None] ** 2 + ax[None, :] ** 2) / (2.0 * sigma * sigma))
    edge = np.exp(-(r * r) / (2.0 * sigma * sigma))
    return np.clip((g - edge) / (1.0 - edge), 0.0, 1.0).astype(np.float32)


_SPRITES = [(_gauss(HALO_SIGMA), HALO_W), (_gauss(MID_SIGMA), MID_W)]
_CORE = _gauss(CORE_SIGMA)


def led_centers():
    c = np.zeros((GRID_H, GRID_W, 2), np.int32)
    for gy in range(GRID_H):
        tr, j = divmod(gy, TILE_LEDS)
        for gx in range(GRID_W):
            tc, i = divmod(gx, TILE_LEDS)
            c[gy, gx, 0] = (
                MARGIN + tc * (TILE_PX + SEAM) + TILE_PAD + i * PITCH + PITCH // 2
            )
            c[gy, gx, 1] = (
                MARGIN + tr * (TILE_PX + SEAM) + TILE_PAD + j * PITCH + PITCH // 2
            )
    return c


_CENTERS = led_centers()


def make_background():
    albedo = np.zeros((CANVAS_H, CANVAS_W, 3), np.float32)
    albedo[:] = _lin(BEZEL)

    face, edge, pkg, dot = _lin(TILE_FACE), _lin(TILE_EDGE), _lin(PKG), _lin(PKG_DOT)
    for tr in range(TILE_ROWS):
        for tc in range(TILE_COLS):
            x0 = MARGIN + tc * (TILE_PX + SEAM)
            y0 = MARGIN + tr * (TILE_PX + SEAM)
            albedo[y0 : y0 + TILE_PX, x0 : x0 + TILE_PX] = face
            albedo[y0, x0 : x0 + TILE_PX] = edge

    hp, hd = PKG_SIZE // 2, DOT_SIZE // 2
    for gy in range(GRID_H):
        for gx in range(GRID_W):
            cx, cy = _CENTERS[gy, gx]
            albedo[cy - hp : cy + hp + 1, cx - hp : cx + hp + 1] = pkg
            albedo[cy - hd : cy + hd + 1, cx - hd : cx + hd + 1] = dot

    yy = np.arange(CANVAS_H, dtype=np.float32)[:, None]
    xx = np.arange(CANVAS_W, dtype=np.float32)[None, :]

    # subtle top light + radial vignette
    light = 1.06 - 0.08 * (yy / (CANVAS_H - 1)) * np.ones_like(xx)
    dx = (xx - CANVAS_W / 2) / (CANVAS_W / 2)
    dy = (yy - CANVAS_H / 2) / (CANVAS_H / 2)
    light = light * (1.0 - VIGNETTE * (dx * dx + dy * dy) / 2.0)

    # rounded-corner bezel mask, antialiased
    cxx = np.clip(xx, CORNER_R, CANVAS_W - 1 - CORNER_R)
    cyy = np.clip(yy, CORNER_R, CANVAS_H - 1 - CORNER_R)
    d = np.sqrt((xx - cxx) ** 2 + (yy - cyy) ** 2)
    mask = np.clip(CORNER_R + 0.5 - d, 0.0, 1.0)

    return (albedo * (light * mask)[:, :, None]).astype(np.float32)


def _add_sprite(buf, kernel, cx, cy, color):
    r = kernel.shape[0] // 2
    x0, x1 = cx - r, cx + r + 1
    y0, y1 = cy - r, cy + r + 1
    kx0, ky0 = max(0, -x0), max(0, -y0)
    x0, y0 = max(x0, 0), max(y0, 0)
    x1, y1 = min(x1, CANVAS_W), min(y1, CANVAS_H)
    buf[y0:y1, x0:x1] += kernel[ky0 : ky0 + y1 - y0, kx0 : kx0 + x1 - x0, None] * color


def render_frame(frame, background):
    """frame: (18, 6, 3) uint8 engine framebuffer -> (H, W, 3) uint8 image."""
    buf = background.copy()
    for gy, gx in np.argwhere(frame.any(axis=2)):
        c = frame[gy, gx].astype(np.float32) / 255.0
        cx, cy = _CENTERS[gy, gx]
        for kernel, w in _SPRITES:
            _add_sprite(buf, kernel, cx, cy, c * w)
        # hot core overdrives toward white with LED brightness
        core = c + (1.0 - c) * (WHITE_LERP * c.max())
        _add_sprite(buf, _CORE, cx, cy, core * CORE_W)

    x = buf * EXPOSURE
    t = x * (1.0 + x / (WHITE_PT * WHITE_PT)) / (1.0 + x)
    t = np.clip(t, 0.0, 1.0) ** GAMMA
    return (t * 255.0 + 0.5).astype(np.uint8)

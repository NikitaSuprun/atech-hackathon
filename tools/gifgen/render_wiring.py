"""Render the PONG full-system hardware diagram (assets/system-diagram.png)."""

from __future__ import annotations

import math
from pathlib import Path
from typing import Final

import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

from gifgen import game, paths
from gifgen.palette import RGB8

ATECH: Final[Path] = paths.ATECH
PREVIEW: Final[Path] = paths.PREVIEW
FINAL: Final[Path] = paths.ASSETS / "system-diagram.png"

# canvas (all layout in 1x coordinates, drawn at SS supersample)
W: Final[int] = 1600
H: Final[int] = 900
SS: Final[int] = 2

BG: Final[tuple[int, ...]] = (11, 13, 16)
INK: Final[tuple[int, ...]] = (232, 235, 240)
CAPTION: Final[tuple[int, ...]] = (207, 212, 220)
MUTED: Final[tuple[int, ...]] = (139, 147, 160)
FAINT: Final[tuple[int, ...]] = (90, 98, 112)
CYAN: Final[tuple[int, ...]] = RGB8["P1"]
AMBER: Final[tuple[int, ...]] = RGB8["P2"]
CABLE: Final[tuple[int, ...]] = (183, 189, 198)
STUB: Final[tuple[int, ...]] = (150, 158, 168)
USB_C: Final[tuple[int, ...]] = (216, 220, 226)
GRAY_LEAD: Final[tuple[int, ...]] = (168, 175, 186)
CASING: Final[tuple[int, ...]] = (4, 6, 9, 215)
CARD: Final[tuple[int, ...]] = (216, 219, 224)
CARD_TEXT: Final[tuple[int, ...]] = (32, 36, 44)
CARD_SUB: Final[tuple[int, ...]] = (96, 104, 118)

# motherboard port geometry, measured from 14port-motherboard-V2.png (222x445)
PORT_ROW_Y: Final[list[float]] = [0.078, 0.247, 0.415, 0.583, 0.752, 0.919]
SLOT_LX: Final[tuple[float, float]] = (0.131, 0.333)
SLOT_RX: Final[tuple[float, float]] = (0.667, 0.865)
SLOT_HY: Final[float] = 0.062
P7_RECT: Final[tuple[float, float, float, float]] = (0.428, 0.058, 0.586, 0.152)
P8_RECT: Final[tuple[float, float, float, float]] = (0.428, 0.849, 0.586, 0.940)

BOARD_W: Final[int] = 210
BOARD_H: Final[int] = 420

# screen zone
TILE: Final[int] = 70
SEAM: Final[int] = 3
GRID_X: Final[int] = 64
GRID_Y: Final[int] = 168
GRID_R: Final[int] = GRID_X + 2 * TILE + SEAM
WALL_BX: Final[int] = 330
WALL_BY: Final[float] = (GRID_Y + (GRID_Y + 6 * TILE + 5 * SEAM)) / 2 - BOARD_H / 2
ROW_C: Final[list[float]] = [GRID_Y + i * (TILE + SEAM) + TILE / 2 for i in range(6)]

# screen harness: right tiles -> ports, planar nested routing
ROW_PORT: Final[list[int]] = [13, 11, 10, 9, 7, 14]
TRUNK_X: Final[list[int]] = [222, 227, 232, 237, 242, 247]
LANE_Y: Final[dict[int, int]] = {13: 80, 11: 95, 10: 110, 9: 125, 7: 146}
DROP_X: Final[dict[int, int]] = {13: 620, 11: 605, 10: 590, 9: 575}
HOOK_X: Final[int] = 547
BOTTOM_LANE_Y: Final[int] = 658
BOTTOM_UP_X: Final[int] = 598

# console zone
CONS_BX: Final[int] = 1150
CONS_BY: Final[float] = WALL_BY
CARD_W: Final[int] = 150
CARD_H: Final[int] = 168
CARD_LX: Final[int] = 968
CARD_RX: Final[int] = 1392
CARD_TY: Final[int] = 190
CARD_BY2: Final[int] = 402

# center zone
LAP_CX: Final[int] = 804
LID_W: Final[int] = 190
LID_H: Final[int] = 120
LID_X: Final[float] = LAP_CX - LID_W / 2
LID_Y: Final[int] = 348

CAPTION_Y: Final[int] = 686
NOTE_Y: Final[int] = 710
FOOTER_LINE_Y: Final[int] = 750
BOM_Y: Final[int] = 778

FONT_BOLD: Final[str] = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
FONT_REG: Final[str] = "/System/Library/Fonts/Supplemental/Arial.ttf"
_fonts: dict[tuple[float, bool], ImageFont.FreeTypeFont] = {}


def F(size: float, bold: bool = False) -> ImageFont.FreeTypeFont:
    key = (size, bold)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(
            FONT_BOLD if bold else FONT_REG, int(round(size * SS))
        )
    return _fonts[key]


def sc(*pts: float) -> list[float]:
    return [p * SS for p in pts]


def load(name: str) -> Image.Image:
    im = Image.open(ATECH / name).convert("RGBA")
    return im.crop(im.getbbox())


def make_background() -> Image.Image:
    yy, xx = np.mgrid[0 : H * SS, 0 : W * SS].astype(np.float32)
    nx = (xx / (W * SS) - 0.5) * 2
    ny = (yy / (H * SS) - 0.5) * 2
    r = np.sqrt(nx * nx * 1.15 + ny * ny)
    fade = np.clip(1.0 - 0.34 * np.clip(r - 0.35, 0, None) ** 1.6, 0, 1)
    lift = 1.0 + 0.05 * np.clip(0.55 - r, 0, None)
    arr = np.zeros((H * SS, W * SS, 3), np.float32)
    for i, c in enumerate(BG):
        arr[..., i] = c * fade * lift
    return Image.fromarray(arr.clip(0, 255).astype(np.uint8), "RGB").convert("RGBA")


def new_layer(canvas: Image.Image) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    return layer, ImageDraw.Draw(layer)


def paste_shadowed(
    canvas: Image.Image,
    img: Image.Image,
    x: float,
    y: float,
    blur: float = 11,
    dy: float = 7,
    alpha: int = 150,
) -> None:
    def _fade(a: int) -> int:
        return a * alpha // 255

    sil = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sil.putalpha(img.getchannel("A").point(_fade))
    pad = int(blur * SS * 3)
    sh = Image.new("RGBA", (img.width + 2 * pad, img.height + 2 * pad), (0, 0, 0, 0))
    sh.paste(sil, (pad, pad))
    sh = sh.filter(ImageFilter.GaussianBlur(blur * SS / 2.2))
    canvas.alpha_composite(sh, (int(x * SS - pad), int(y * SS - pad + dy * SS)))
    canvas.alpha_composite(img, (int(x * SS), int(y * SS)))


def polyline(
    d: ImageDraw.ImageDraw,
    pts: list[tuple[float, float]],
    w: float,
    fill: tuple[int, ...],
) -> None:
    spts = [(x * SS, y * SS) for x, y in pts]
    lw = max(1, int(round(w * SS)))
    d.line(spts, fill=fill, width=lw, joint="curve")
    r = lw / 2
    for p in (spts[0], spts[-1]):
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=fill)


def rounded_path(
    pts: list[tuple[float, float]], r: float = 12, seg: int = 14
) -> list[tuple[float, float]]:
    # orthogonal-ish polyline with quadratic fillets at interior corners
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        p0, p1, p2 = pts[i - 1], pts[i], pts[i + 1]
        d0 = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
        d1 = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        rr = min(r, d0 / 2, d1 / 2)
        a = (p1[0] - (p1[0] - p0[0]) / d0 * rr, p1[1] - (p1[1] - p0[1]) / d0 * rr)
        b = (p1[0] + (p2[0] - p1[0]) / d1 * rr, p1[1] + (p2[1] - p1[1]) / d1 * rr)
        out.append(a)
        for s in range(1, seg):
            t = s / seg
            u = 1 - t
            out.append(
                (
                    u * u * a[0] + 2 * u * t * p1[0] + t * t * b[0],
                    u * u * a[1] + 2 * u * t * p1[1] + t * t * b[1],
                )
            )
        out.append(b)
    out.append(pts[-1])
    return out


def bez(
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    p3: tuple[float, float],
    n: int = 90,
) -> list[tuple[float, float]]:
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append(
            (
                u**3 * p0[0]
                + 3 * u * u * t * p1[0]
                + 3 * u * t * t * p2[0]
                + t**3 * p3[0],
                u**3 * p0[1]
                + 3 * u * u * t * p1[1]
                + 3 * u * t * t * p2[1]
                + t**3 * p3[1],
            )
        )
    return out


def cable(
    d: ImageDraw.ImageDraw,
    pts: list[tuple[float, float]],
    w: float = 3.0,
    color: tuple[int, ...] = CABLE,
) -> None:
    polyline(d, pts, w + 3.0, CASING)
    polyline(d, pts, w, color)


def dot(
    d: ImageDraw.ImageDraw, x: float, y: float, r: float, fill: tuple[int, ...]
) -> None:
    d.ellipse(sc(x - r, y - r, x + r, y + r), fill=fill)


def text(
    d: ImageDraw.ImageDraw,
    x: float,
    y: float,
    s: str,
    size: float,
    fill: tuple[int, ...],
    bold: bool = False,
    anchor: str = "la",
) -> float:
    d.text((x * SS, y * SS), s, font=F(size, bold), fill=fill, anchor=anchor)
    return d.textlength(s, font=F(size, bold)) / SS


def round_rect(
    d: ImageDraw.ImageDraw,
    rect: tuple[float, float, float, float],
    radius: float,
    fill: tuple[int, ...] | None = None,
    outline: tuple[int, ...] | None = None,
    width: float = 2,
) -> None:
    d.rounded_rectangle(
        sc(*rect),
        radius=radius * SS,
        fill=fill,
        outline=outline,
        width=max(1, int(round(width * SS / 2))),
    )


# ---- port geometry ---------------------------------------------------------


def slot_rect(bx: float, by: float, port: int) -> tuple[float, float, float, float]:
    if port in (7, 8):
        f = P7_RECT if port == 7 else P8_RECT
        return (
            bx + f[0] * BOARD_W,
            by + f[1] * BOARD_H,
            bx + f[2] * BOARD_W,
            by + f[3] * BOARD_H,
        )
    col = SLOT_LX if port <= 6 else SLOT_RX
    yc = by + PORT_ROW_Y[(port - 1) if port <= 6 else (port - 9)] * BOARD_H
    return (
        bx + col[0] * BOARD_W,
        yc - SLOT_HY * BOARD_H,
        bx + col[1] * BOARD_W,
        yc + SLOT_HY * BOARD_H,
    )


def port_y(by: float, port: int) -> float:
    return by + PORT_ROW_Y[(port - 1) if port <= 6 else (port - 9)] * BOARD_H


def highlight_ports(
    d: ImageDraw.ImageDraw,
    bx: float,
    by: float,
    ports: tuple[int, ...],
    color: tuple[int, ...],
    pad: float = 4,
    fill_a: int = 26,
    line_a: int = 215,
) -> tuple[float, float, float, float]:
    rects = [slot_rect(bx, by, p) for p in ports]
    x0 = min(r[0] for r in rects) - pad
    y0 = min(r[1] for r in rects) - pad
    x1 = max(r[2] for r in rects) + pad
    y1 = max(r[3] for r in rects) + pad
    round_rect(
        d,
        (x0, y0, x1, y1),
        6,
        fill=color + (fill_a,),
        outline=color + (line_a,),
        width=3,
    )
    return (x0, y0, x1, y1)


def dim_port(d: ImageDraw.ImageDraw, bx: float, by: float, port: int) -> None:
    round_rect(d, slot_rect(bx, by, port), 4, fill=BG + (120,))


def rotate_icon(
    d: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    r: float = 7,
    color: tuple[int, ...] = (40, 45, 54, 220),
) -> None:
    lw = max(1, int(round(1.9 * SS)))
    d.arc(sc(cx - r, cy - r, cx + r, cy + r), start=210, end=330, fill=color, width=lw)
    d.arc(sc(cx - r, cy - r, cx + r, cy + r), start=30, end=150, fill=color, width=lw)
    for a_deg in (330, 150):
        a = math.radians(a_deg)
        tip = (cx + r * math.cos(a), cy + r * math.sin(a))
        ta = a + math.pi / 2
        tri = [
            tip,
            (tip[0] + 4.4 * math.cos(ta + 0.5), tip[1] + 4.4 * math.sin(ta + 0.5)),
            (tip[0] + 4.4 * math.cos(ta - 0.5), tip[1] + 4.4 * math.sin(ta - 0.5)),
        ]
        d.polygon([q for p in tri for q in sc(*p)], fill=color)


# ---- zones -----------------------------------------------------------------


def draw_wall(
    canvas: Image.Image,
    d_over: ImageDraw.ImageDraw,
    tile_img: Image.Image,
    board_img: Image.Image,
) -> None:
    bx, by = WALL_BX, WALL_BY
    wires, d = new_layer(canvas)

    # direct-plug stubs: left tiles -> ports 1-6 (hidden behind the right tiles)
    stub_pts: list[tuple[tuple[float, float], tuple[float, float]]] = []
    for i in range(6):
        y0, y1 = ROW_C[i], port_y(by, i + 1)
        stub_pts.append(((GRID_X + TILE - 6, y0), (bx - 4, y1)))

    # harness: right tiles -> ports via nested top lanes + one bottom run
    for row, port in enumerate(ROW_PORT):
        ry = ROW_C[row] + 16
        tx = TRUNK_X[row]
        pts: list[tuple[float, float]]
        if port == 14:
            pts = [
                (GRID_R - 4, ry),
                (tx, ry),
                (tx, BOTTOM_LANE_Y),
                (BOTTOM_UP_X, BOTTOM_LANE_Y),
                (BOTTOM_UP_X, port_y(by, port)),
                (HOOK_X, port_y(by, port)),
            ]
        elif port == 7:
            r7 = slot_rect(bx, by, 7)
            p7x = (r7[0] + r7[2]) / 2
            pts = [
                (GRID_R - 4, ry),
                (tx, ry),
                (tx, LANE_Y[port]),
                (p7x, LANE_Y[port]),
                (p7x, by - 13),
            ]
        else:
            pts = [
                (GRID_R - 4, ry),
                (tx, ry),
                (tx, LANE_Y[port]),
                (DROP_X[port], LANE_Y[port]),
                (DROP_X[port], port_y(by, port)),
                (HOOK_X, port_y(by, port)),
            ]
        path = rounded_path(pts, r=13)
        cable(d, path, w=2.8)
        dot(d, *pts[0], 2.9, CABLE)
        dot(d, *pts[-1], 3.2, CABLE)

    # stubs drawn last so they read as passing over the harness trunk
    for a, b in stub_pts:
        polyline(d, [a, b], 5.4, CASING)
        polyline(d, [a, b], 2.6, STUB)
        dot(d, *b, 3.0, STUB)

    canvas.alpha_composite(wires)

    # tiles: rotate each per its TILE_MAP entry (rot 2 = mounted 180°); the board
    # then covers the hidden wire runs
    rot_at = {(r, c): rot for r, c, rot in game.TILE_MAP}
    for row in range(6):
        for col in range(2):
            x = GRID_X + col * (TILE + SEAM)
            y = GRID_Y + row * (TILE + SEAM)
            img = tile_img.rotate(180) if rot_at[(row, col)] == 2 else tile_img
            paste_shadowed(canvas, img, x, y, blur=7, dy=4, alpha=120)
    paste_shadowed(canvas, board_img, bx, by)

    # seam connectors where the direct plugs leave the left tiles
    for i in range(6):
        round_rect(
            d_over,
            (GRID_X + TILE - 5, ROW_C[i] - 5, GRID_X + TILE + 8, ROW_C[i] + 5),
            2.5,
            fill=(58, 64, 74, 255),
            outline=(150, 158, 168, 230),
            width=1.6,
        )

    # rotate icon on each rotated tile, plus a label above the column
    rot_col = next((c for _, c, rot in game.TILE_MAP if rot == 2), 1)
    col_cx = GRID_X + rot_col * (TILE + SEAM) + TILE / 2  # rotated column's center x
    for row in range(6):
        rotate_icon(d_over, col_cx, GRID_Y + row * (TILE + SEAM) + TILE / 2)
    label_y = GRID_Y - 30  # label sits in the margin above the screen
    text(d_over, col_cx, label_y, "rotated 180°", 13.5, MUTED, anchor="mm")
    # leader down from just under the label to just above the tiles
    polyline(d_over, [(col_cx, label_y + 9), (col_cx, GRID_Y - 3)], 1.4, FAINT + (210,))

    # port treatments
    for p in game.PORT_ORDER:
        r = slot_rect(bx, by, p)
        round_rect(
            d_over,
            (r[0] - 2, r[1] - 2, r[2] + 2, r[3] + 2),
            5,
            fill=(255, 255, 255, 13),
            outline=(255, 255, 255, 64),
            width=2,
        )
    dim_port(d_over, bx, by, 8)
    dim_port(d_over, bx, by, 12)

    cx = (GRID_X + bx + BOARD_W) / 2
    text(
        d_over,
        cx,
        CAPTION_Y,
        "Screen board — 12× Light Grid = one 6×18 screen",
        17,
        CAPTION,
        bold=True,
        anchor="mm",
    )
    text(d_over, cx, NOTE_Y, "the game engine runs here", 13.5, MUTED, anchor="mm")


def draw_laptop(canvas: Image.Image, d_over: ImageDraw.ImageDraw) -> None:
    layer, d = new_layer(canvas)
    lx, ly = LID_X, LID_Y

    sh = Image.new("RGBA", (int((LID_W + 80) * SS), int(60 * SS)), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse(
        [20 * SS, 14 * SS, (LID_W + 60) * SS, 46 * SS], fill=(0, 0, 0, 130)
    )
    sh = sh.filter(ImageFilter.GaussianBlur(9 * SS))
    canvas.alpha_composite(sh, (int((lx - 40) * SS), int((ly + LID_H - 8) * SS)))

    # usb cables first so the chassis sits on top of their ends
    base_y = ly + LID_H + 2
    wall_p8x = WALL_BX + (P8_RECT[0] + P8_RECT[2]) / 2 * BOARD_W
    cons_p8x = CONS_BX + (P8_RECT[0] + P8_RECT[2]) / 2 * BOARD_W
    wall_end = (wall_p8x, WALL_BY + BOARD_H + 13)
    cons_end = (cons_p8x, CONS_BY + BOARD_H + 13)
    cable(
        d,
        bez((lx - 16, base_y + 10), (600, 610), (wall_p8x + 44, 626), wall_end, n=110),
        w=3.8,
        color=USB_C,
    )
    cable(
        d,
        bez(
            (lx + LID_W + 16, base_y + 10),
            (1008, 610),
            (cons_p8x - 44, 626),
            cons_end,
            n=110,
        ),
        w=3.8,
        color=USB_C,
    )
    for ex, ey in (wall_end, cons_end):
        round_rect(
            d,
            (ex - 6, ey - 15, ex + 6, ey),
            2.5,
            fill=(58, 64, 74, 255),
            outline=USB_C + (255,),
            width=2,
        )

    round_rect(
        d,
        (lx, ly, lx + LID_W, ly + LID_H),
        9,
        fill=(22, 26, 32, 255),
        outline=(74, 81, 94, 255),
        width=2.6,
    )
    sx0, sy0, sx1, sy1 = lx + 11, ly + 9, lx + LID_W - 11, ly + LID_H - 11
    round_rect(d, (sx0, sy0, sx1, sy1), 4, fill=(13, 21, 28, 255))

    glow = Image.new(
        "RGBA", (int((sx1 - sx0) * SS), int((sy1 - sy0) * SS)), (0, 0, 0, 0)
    )
    gd = ImageDraw.Draw(glow)
    gw, gh = glow.size
    gd.ellipse([-gw * 0.3, -gh * 0.55, gw * 1.3, gh * 1.1], fill=(0, 200, 255, 26))
    glow = glow.filter(ImageFilter.GaussianBlur(9 * SS))
    layer.alpha_composite(glow, (int(sx0 * SS), int(sy0 * SS)))

    # tiny pong scene
    mx = (sx0 + sx1) / 2
    for i in range(4):
        yy = sy0 + 12 + i * 22
        polyline(d, [(mx, yy), (mx, yy + 10)], 1.6, (120, 132, 146, 170))
    d.rectangle(sc(sx0 + 14, sy0 + 30, sx0 + 18, sy0 + 56), fill=CYAN + (235,))
    d.rectangle(sc(sx1 - 18, sy0 + 44, sx1 - 14, sy0 + 70), fill=AMBER + (235,))
    dot(d, mx + 22, sy0 + 38, 3, (255, 255, 255, 245))

    bx0, bx1 = lx - 20, lx + LID_W + 20
    round_rect(
        d,
        (bx0, base_y, bx1, base_y + 14),
        6,
        fill=(35, 40, 48, 255),
        outline=(80, 87, 100, 255),
        width=2,
    )
    d.rounded_rectangle(
        sc(LAP_CX - 14, base_y, LAP_CX + 14, base_y + 4.5),
        radius=2 * SS,
        fill=(20, 24, 30, 255),
    )
    canvas.alpha_composite(layer)

    text(
        d_over, LAP_CX, 518, "USB serial bridge", 16.5, CAPTION, bold=True, anchor="mm"
    )
    text(
        d_over, LAP_CX, 542, "WiFi hotspot mode also built in", 13.5, MUTED, anchor="mm"
    )


def card(
    canvas: Image.Image,
    d_over: ImageDraw.ImageDraw,
    x: float,
    y: float,
    img: Image.Image,
    label: str,
    sub: str | None = None,
    badge: str | None = None,
    badge_color: tuple[int, ...] | None = None,
    ring: tuple[float, float, float, tuple[int, ...]] | None = None,
) -> None:
    layer, d = new_layer(canvas)
    sh = Image.new(
        "RGBA", (int((CARD_W + 60) * SS), int((CARD_H + 60) * SS)), (0, 0, 0, 0)
    )
    ImageDraw.Draw(sh).rounded_rectangle(
        [30 * SS, 34 * SS, (CARD_W + 30) * SS, (CARD_H + 36) * SS],
        radius=14 * SS,
        fill=(0, 0, 0, 165),
    )
    sh = sh.filter(ImageFilter.GaussianBlur(6.5 * SS))
    canvas.alpha_composite(sh, (int((x - 30) * SS), int((y - 30) * SS)))

    round_rect(d, (x, y, x + CARD_W, y + CARD_H), 13, fill=CARD + (255,))
    round_rect(
        d, (x, y, x + CARD_W, y + CARD_H), 13, outline=(255, 255, 255, 90), width=2
    )
    canvas.alpha_composite(layer)

    iw, ih = img.size
    scale = min(96 * SS / iw, 96 * SS / ih)
    img2 = img.resize((int(iw * scale), int(ih * scale)), Image.Resampling.LANCZOS)
    ix = int(x * SS + (CARD_W * SS - img2.width) / 2)
    iy = int(y * SS + 12 * SS + (96 * SS - img2.height) / 2)
    canvas.alpha_composite(img2, (ix, iy))

    if ring:
        rcx = ix / SS + (img2.width / SS) * ring[0]
        rcy = iy / SS + (img2.height / SS) * ring[1]
        rr = (img2.width / SS) * ring[2]
        d_over.ellipse(
            sc(rcx - rr, rcy - rr, rcx + rr, rcy + rr),
            outline=ring[3] + (235,),
            width=int(2.6 * SS),
        )
    if badge and badge_color is not None:
        round_rect(d_over, (x + 9, y + 9, x + 43, y + 29), 6, fill=badge_color + (255,))
        text(
            d_over, x + 26, y + 19.5, badge, 13, (255, 255, 255), bold=True, anchor="mm"
        )
    ty = y + CARD_H - (30 if sub else 24)
    text(d_over, x + CARD_W / 2, ty, label, 15, CARD_TEXT, bold=True, anchor="mm")
    if sub:
        text(d_over, x + CARD_W / 2, ty + 19, sub, 13, CARD_SUB, anchor="mm")


def draw_console(
    canvas: Image.Image,
    d_over: ImageDraw.ImageDraw,
    board_img: Image.Image,
    knob_img: Image.Image,
    speaker_img: Image.Image,
    screen_img: Image.Image,
) -> None:
    bx, by = CONS_BX, CONS_BY
    paste_shadowed(canvas, board_img, bx, by)

    knob = ImageEnhance.Brightness(knob_img).enhance(1.45)
    card(
        canvas,
        d_over,
        CARD_LX,
        CARD_TY,
        knob,
        "Knob",
        sub="paddle control",
        badge="P1",
        badge_color=CYAN,
        ring=(0.26, 0.47, 0.22, CYAN),
    )
    card(
        canvas, d_over, CARD_LX, CARD_BY2, speaker_img, "Speaker", sub="bleeps & buzzes"
    )
    card(
        canvas,
        d_over,
        CARD_RX,
        CARD_TY,
        knob,
        "Knob",
        sub="paddle control",
        badge="P2",
        badge_color=AMBER,
        ring=(0.26, 0.47, 0.22, AMBER),
    )
    card(
        canvas, d_over, CARD_RX, CARD_BY2, screen_img, "Scoreboard", sub="Screen module"
    )

    def leader(
        p_from: tuple[float, float],
        p_to: tuple[float, float],
        color: tuple[int, ...],
        w: float = 2.4,
    ) -> None:
        polyline(d_over, [p_from, p_to], w, color + (235,))
        dot(d_over, *p_from, 2.9, color + (255,))
        dot(d_over, *p_to, 3.3, color + (255,))

    r12 = highlight_ports(d_over, bx, by, (1, 2), CYAN)
    leader((CARD_LX + CARD_W + 3, CARD_TY + 74), (bx - 5, (r12[1] + r12[3]) / 2), CYAN)
    r34 = highlight_ports(
        d_over, bx, by, (3, 4), (152, 160, 172), fill_a=36, line_a=235
    )
    leader(
        (CARD_LX + CARD_W + 3, CARD_BY2 + 74),
        (bx - 5, (r34[1] + r34[3]) / 2),
        GRAY_LEAD,
    )
    r910 = highlight_ports(d_over, bx, by, (9, 10), AMBER)
    leader(
        (CARD_RX - 3, CARD_TY + 74), (bx + BOARD_W + 5, (r910[1] + r910[3]) / 2), AMBER
    )
    r1314 = highlight_ports(
        d_over, bx, by, (13, 14), (152, 160, 172), fill_a=36, line_a=235
    )
    leader(
        (CARD_RX - 3, CARD_BY2 + 74),
        (bx + BOARD_W + 5, (r1314[1] + r1314[3]) / 2),
        GRAY_LEAD,
    )

    r7 = highlight_ports(
        d_over, bx, by, (7,), (152, 160, 172), pad=3, fill_a=36, line_a=235
    )
    tag_x = (r7[0] + r7[2]) / 2 - 14
    text(d_over, tag_x, 146, "game uplink (code)", 13.5, MUTED, anchor="mm")
    polyline(d_over, [(tag_x, 157), (tag_x, r7[1] - 4)], 1.6, FAINT + (225,))
    dot(d_over, tag_x, r7[1] - 4, 2.4, GRAY_LEAD + (255,))

    for p in (5, 6, 8, 11, 12):
        dim_port(d_over, bx, by, p)

    text(
        d_over,
        (CARD_LX + CARD_RX + CARD_W) / 2,
        CAPTION_Y,
        "Console board — knobs, scoreboard, speaker",
        17,
        CAPTION,
        bold=True,
        anchor="mm",
    )


def draw_footer(
    canvas: Image.Image, d_over: ImageDraw.ImageDraw, imgs: dict[str, Image.Image]
) -> None:
    layer, d = new_layer(canvas)
    d.line(
        sc(64, FOOTER_LINE_Y, W - 64, FOOTER_LINE_Y), fill=(255, 255, 255, 20), width=SS
    )
    items = [
        (imgs["board"], "2×", "Motherboard"),
        (imgs["tile"], "12×", "Light Grid"),
        (imgs["knob"], "2×", "Knob"),
        (imgs["screen"], "1×", "Screen"),
        (imgs["speaker"], "1×", "Speaker"),
    ]
    chip = 52
    widths = []
    for _img, count, name in items:
        tw = F(15, True).getlength(count) / SS + 4 + F(15).getlength(name) / SS
        widths.append(chip + 10 + tw)
    gap = 46
    x = (W - (sum(widths) + gap * (len(items) - 1))) / 2
    cy = BOM_Y + chip / 2
    positions = []
    for (img, count, name), iw in zip(items, widths, strict=True):
        round_rect(
            d,
            (x, BOM_Y, x + chip, BOM_Y + chip),
            10,
            fill=CARD + (255,),
            outline=(255, 255, 255, 70),
            width=1.6,
        )
        positions.append((x, img, count, name))
        x += iw + gap
    canvas.alpha_composite(layer)
    for x, img, count, name in positions:
        s = min((chip - 10) * SS / img.width, (chip - 10) * SS / img.height)
        img2 = img.resize(
            (max(1, int(img.width * s)), max(1, int(img.height * s))),
            Image.Resampling.LANCZOS,
        )
        canvas.alpha_composite(
            img2,
            (
                int(x * SS + (chip * SS - img2.width) / 2),
                int(BOM_Y * SS + (chip * SS - img2.height) / 2),
            ),
        )
        tx = x + chip + 10
        tx += text(d_over, tx, cy, count, 15, INK, bold=True, anchor="lm") + 4
        text(d_over, tx, cy, name, 15, MUTED, anchor="lm")
    text(d_over, W - 64, H - 26, "component renders: atech.dev", 13, FAINT, anchor="rm")


def draw_title(d_over: ImageDraw.ImageDraw) -> None:
    a = "PONG"
    b = "  ·  full-system hardware"
    wa = F(24, True).getlength(a) / SS
    wb = F(19).getlength(b) / SS
    x = (W - wa - wb) / 2
    text(d_over, x, 44, a, 24, INK, bold=True, anchor="lm")
    text(d_over, x + wa, 46, b, 19, MUTED, anchor="lm")


def main() -> None:
    paths.ensure(PREVIEW)

    board_src = load("14port-motherboard-V2.png")
    tile_src = load("Light.png")
    knob_src = load("Knob.png")
    screen_src = load("Screen.png")
    speaker_src = load("Speaker.png")

    board = board_src.resize((BOARD_W * SS, BOARD_H * SS), Image.Resampling.LANCZOS)
    tile = tile_src.resize((TILE * SS, TILE * SS), Image.Resampling.LANCZOS)

    canvas = make_background()
    over, d_over = new_layer(canvas)

    draw_title(d_over)
    draw_wall(canvas, d_over, tile, board)
    draw_laptop(canvas, d_over)
    draw_console(canvas, d_over, board, knob_src, speaker_src, screen_src)
    draw_footer(
        canvas,
        d_over,
        {
            "board": board_src,
            "tile": tile_src,
            "knob": ImageEnhance.Brightness(knob_src).enhance(1.45),
            "screen": screen_src,
            "speaker": speaker_src,
        },
    )
    canvas.alpha_composite(over)

    final = canvas.convert("RGB").resize((W, H), Image.Resampling.LANCZOS)
    final.save(PREVIEW / "wiring_full.png", optimize=True)
    for name, box in (
        ("wall", (30, 40, 700, 740)),
        ("center", (560, 300, 1040, 720)),
        ("console", (930, 40, 1600, 740)),
        ("footer", (0, 730, 1600, 900)),
    ):
        final.crop(box).save(PREVIEW / f"wiring_{name}.png")
    final.save(FINAL, optimize=True)
    print("saved", FINAL, FINAL.stat().st_size, "bytes")


if __name__ == "__main__":
    main()

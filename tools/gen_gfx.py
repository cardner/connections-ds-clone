#!/usr/bin/env python3
"""Generate Connections DS grit source PNGs into gfx/.

Renders the Info / How-to-play screens and the toolbar icon set to magenta-keyed
PNGs (FF00FF = transparent) that grit converts to 16bpp GRF bitmaps. Text is
rasterized from the same Rodin NFTR faces the game uses, so baked type matches
the in-game font. Icon geometry mirrors source/ui_bottom.cpp.

Usage:
  python3 tools/gen_gfx.py            # regenerate all gfx/*.png
No third-party deps (pure stdlib: zlib + struct).
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
GFX = ROOT / "gfx"

MAGENTA = (255, 0, 255)  # grit transparent key

# --- Palette (mirrors include/gfx.hpp gfx::pal) ---
WHITE = (255, 255, 255)
INK = (18, 18, 18)
DIM = (120, 120, 120)
TILE_TEXT = (0x28, 0x27, 0x27)
SELECTED = (0x28, 0x27, 0x27)
DISABLED = (228, 228, 228)
BORDER = (120, 120, 120)
DIS_TEXT = (120, 120, 120)
SUBMIT = (0xA0, 0xC3, 0x5A)
TOOL_ICON = (28, 28, 28)
CATEGORY = [
    (0xF9, 0xDF, 0x6D),
    (0xA0, 0xC3, 0x5A),
    (0xB0, 0xC4, 0xEF),
    (0xBA, 0x81, 0xC5),
]


# --------------------------------------------------------------------------
# NFTR font (parse + rasterize), matching source/font.cpp
# --------------------------------------------------------------------------
class Face:
    def __init__(self, path: Path):
        b = path.read_bytes()
        self.data = b
        nftr_size = struct.unpack_from("<I", b, 8)[0]
        gp = 0x14 + b[0x14]
        chunk = struct.unpack_from("<I", b, gp)[0]
        self.tile_w = b[gp + 4]
        self.tile_h = b[gp + 5]
        self.tile_size = struct.unpack_from("<H", b, gp + 6)[0]
        self.count = (chunk - 0x10) // self.tile_size
        self.tiles = gp + 12
        locw = struct.unpack_from("<I", b, 0x24)[0]
        self.widths = locw - 4 + 12
        self.cmap = {}
        loc = struct.unpack_from("<I", b, 0x28)[0]
        seen = set()
        while loc and loc < nftr_size and loc not in seen:
            seen.add(loc)
            first, last = struct.unpack_from("<HH", b, loc)
            mtype, nxt = struct.unpack_from("<II", b, loc + 4)
            p = loc + 12
            if mtype == 0:
                tile = struct.unpack_from("<H", b, p)[0]
                for c in range(first, last + 1):
                    self.cmap[c] = tile + (c - first)
            elif mtype == 1:
                for c in range(first, last + 1):
                    t = struct.unpack_from("<H", b, p)[0]
                    p += 2
                    if t != 0xFFFF:
                        self.cmap[c] = t
            elif mtype == 2:
                n = struct.unpack_from("<H", b, p)[0]
                p += 2
                for _ in range(n):
                    c, t = struct.unpack_from("<HH", b, p)
                    p += 4
                    self.cmap[c] = t
            loc = nxt

    def _idx(self, ch: str) -> int:
        return self.cmap.get(ord(ch), self.cmap.get(ord("?"), 0))

    def advance(self, ch: str) -> int:
        return self.data[self.widths + self._idx(ch) * 3 + 2]

    def left(self, ch: str) -> int:
        return self.data[self.widths + self._idx(ch) * 3 + 0]

    def text_width(self, s: str) -> int:
        return sum(self.advance(c) for c in s)

    def coverage(self, ch: str, col: int, row: int) -> float:
        idx = self._idx(ch)
        glyph = self.tiles + idx * self.tile_size
        bit = row * self.tile_w + col
        v = (self.data[glyph + bit // 4] >> ((3 - (bit % 4)) * 2)) & 3
        return v / 3.0


# --------------------------------------------------------------------------
# Canvas: float RGB over white with a coverage mask for transparency
# --------------------------------------------------------------------------
class Canvas:
    def __init__(self, w: int, h: int, bg=WHITE, opaque=True):
        self.w = w
        self.h = h
        self.col = [list(bg) for _ in range(w * h)]
        # coverage: 1.0 where a pixel is opaque, 0.0 = transparent (magenta out)
        self.cov = [1.0 if opaque else 0.0 for _ in range(w * h)]

    def blend(self, x: int, y: int, rgb, a: float):
        if a <= 0 or x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        if a > 1:
            a = 1.0
        i = y * self.w + x
        c = self.col[i]
        c[0] = int(round(rgb[0] * a + c[0] * (1 - a)))
        c[1] = int(round(rgb[1] * a + c[1] * (1 - a)))
        c[2] = int(round(rgb[2] * a + c[2] * (1 - a)))
        if a > self.cov[i]:
            self.cov[i] = a

    def save(self, path: Path):
        rows = bytearray()
        for y in range(self.h):
            rows.append(0)
            for x in range(self.w):
                i = y * self.w + x
                if self.cov[i] <= 0.004:
                    rows += bytes(MAGENTA)
                else:
                    rows += bytes(self.col[i])
        raw = zlib.compress(bytes(rows), 9)

        def chunk(t, d):
            c = t + d
            return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", raw)
        png += chunk(b"IEND", b"")
        path.write_bytes(png)


# --- coverage curve matching gfx.cpp coverageFromSdf (band +/-0.4px) ---
def cov_sdf(sdf: float) -> float:
    if sdf <= -0.4:
        return 1.0
    if sdf >= 0.4:
        return 0.0
    return (0.4 - sdf) / 0.8


def fill_disk(cv: Canvas, cx: float, cy: float, r: float, color):
    x0 = int(math.floor(cx - r - 1))
    x1 = int(math.ceil(cx + r + 1))
    y0 = int(math.floor(cy - r - 1))
    y1 = int(math.ceil(cy + r + 1))
    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            d = math.hypot(px + 0.5 - cx, py + 0.5 - cy)
            cv.blend(px, py, color, cov_sdf(d - r))


def stroke_line(cv: Canvas, x0, y0, x1, y1, pen, color):
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    if length < 1e-3:
        fill_disk(cv, x0, y0, pen, color)
        return
    ux, uy = dx / length, dy / length
    pad = int(math.ceil(pen + 1))
    minx = int(math.floor(min(x0, x1))) - pad
    maxx = int(math.ceil(max(x0, x1))) + pad
    miny = int(math.floor(min(y0, y1))) - pad
    maxy = int(math.ceil(max(y0, y1))) + pad
    for py in range(miny, maxy + 1):
        for px in range(minx, maxx + 1):
            fx, fy = px + 0.5 - x0, py + 0.5 - y0
            t = max(0.0, min(length, fx * ux + fy * uy))
            cxp, cyp = x0 + ux * t, y0 + uy * t
            d = math.hypot(px + 0.5 - cxp, py + 0.5 - cyp)
            cv.blend(px, py, color, cov_sdf(d - pen))


def stroke_arc(cv: Canvas, cx, cy, r, a0, a1, pen, color):
    while a1 < a0:
        a1 += 2 * math.pi
    pad = pen + 2
    x0 = int(math.floor(cx - r - pad))
    x1 = int(math.ceil(cx + r + pad))
    y0 = int(math.floor(cy - r - pad))
    y1 = int(math.ceil(cy + r + pad))
    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            fx, fy = px + 0.5 - cx, py + 0.5 - cy
            d = math.hypot(fx, fy)
            ring = abs(d - r)
            ang = math.atan2(-fy, fx)
            if ang < 0:
                ang += 2 * math.pi
            a = ang
            while a < a0:
                a += 2 * math.pi
            while a > a0 + 2 * math.pi:
                a -= 2 * math.pi
            if a > a1 + 0.05:
                continue
            cv.blend(px, py, color, cov_sdf(ring - pen))


def fill_ring(cv: Canvas, cx, cy, outer, inner, color):
    x0 = int(math.floor(cx - outer - 1))
    x1 = int(math.ceil(cx + outer + 1))
    y0 = int(math.floor(cy - outer - 1))
    y1 = int(math.ceil(cy + outer + 1))
    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            d = math.hypot(px + 0.5 - cx, py + 0.5 - cy)
            a = cov_sdf(d - outer) - cov_sdf(d - inner)
            cv.blend(px, py, color, a)


def fill_tri(cv: Canvas, pts, color):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, x1 = int(math.floor(min(xs))), int(math.ceil(max(xs)))
    y0, y1 = int(math.floor(min(ys))), int(math.ceil(max(ys)))

    def edge(ax, ay, bx, by, px, py):
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax)

    (ax, ay), (bx, by), (cx, cy) = pts
    area = edge(ax, ay, bx, by, cx, cy)
    if area == 0:
        return
    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            sx, sy = px + 0.5, py + 0.5
            w0 = edge(bx, by, cx, cy, sx, sy)
            w1 = edge(cx, cy, ax, ay, sx, sy)
            w2 = edge(ax, ay, bx, by, sx, sy)
            if area > 0:
                inside = w0 >= 0 and w1 >= 0 and w2 >= 0
            else:
                inside = w0 <= 0 and w1 <= 0 and w2 <= 0
            if inside:
                cv.blend(px, py, color, 1.0)


def fill_round_rect(cv: Canvas, x, y, w, h, r, color):
    for py in range(y, y + h):
        for px in range(x, x + w):
            # signed distance to rounded rect
            dx = max(x + r - (px + 0.5), (px + 0.5) - (x + w - r), 0.0)
            dy = max(y + r - (py + 0.5), (py + 0.5) - (y + h - r), 0.0)
            d = math.hypot(dx, dy) - r
            cv.blend(px, py, color, cov_sdf(d))


def round_rect_border(cv: Canvas, x, y, w, h, r, color, t):
    for py in range(y - 1, y + h + 1):
        for px in range(x - 1, x + w + 1):
            dx = max(x + r - (px + 0.5), (px + 0.5) - (x + w - r), 0.0)
            dy = max(y + r - (py + 0.5), (py + 0.5) - (y + h - r), 0.0)
            d = math.hypot(dx, dy) - r
            a = cov_sdf(d) - cov_sdf(d + t)
            cv.blend(px, py, color, a)


def draw_text(cv: Canvas, face: Face, x: int, y: int, s: str, color, alpha_map=(0, 85, 170, 255)):
    cx = x
    for ch in s:
        xoff = face.left(ch)
        idx = face._idx(ch)
        glyph = face.tiles + idx * face.tile_size
        for row in range(face.tile_h):
            for col in range(face.tile_w):
                bit = row * face.tile_w + col
                v = (face.data[glyph + bit // 4] >> ((3 - (bit % 4)) * 2)) & 3
                if v:
                    cv.blend(cx + xoff + col, y + row, color, alpha_map[v] / 255.0)
        cx += face.advance(ch)
    return cx


def draw_text_center(cv, face, cx, y, s, color):
    w = face.text_width(s)
    draw_text(cv, face, cx - w // 2, y, s, color)


# --------------------------------------------------------------------------
# Icon glyph drawing (mirrors source/ui_bottom.cpp geometry) at cell center
# --------------------------------------------------------------------------
def icon_info(cv, cx, cy, color, r=8):
    fill_ring(cv, cx + 0.5, cy + 0.5, r, r - 3, color)
    stem_h = max(5, r - 1)
    fill_round_rect(cv, cx - 1, cy - stem_h // 2, 2, stem_h, 0, color)


def icon_sync(cv, cx, cy, color, r=7):
    d = math.pi / 180.0
    stroke_arc(cv, cx, cy, r, 20 * d, 160 * d, 1.2, color)
    stroke_arc(cv, cx, cy, r, 200 * d, 340 * d, 1.2, color)
    fill_tri(cv, [(cx + r + 1, cy - r + 2), (cx + r - 4, cy - 1), (cx + r + 2, cy - 1)], color)
    fill_tri(cv, [(cx - r - 1, cy + r - 2), (cx - r + 4, cy + 1), (cx - r - 2, cy + 1)], color)


def icon_shuffle(cv, cx, cy, color, r=7):
    lx, rx = cx - r, cx + r - 2
    ty, by = cy - r + 1, cy + r - 1
    stroke_line(cv, lx, ty, rx - 2, by - 1, 1.2, color)
    stroke_line(cv, lx, by, rx - 2, ty + 1, 1.2, color)
    ax = cx + r - 1
    fill_tri(cv, [(ax - 5, cy - r), (ax - 5, cy - r + 6), (ax + 2, cy - r + 3)], color)
    fill_tri(cv, [(ax - 5, cy + r - 6), (ax - 5, cy + r), (ax + 2, cy + r - 3)], color)


def icon_cross(cv, cx, cy, color, r=4):
    stroke_line(cv, cx - r, cy - r, cx + r, cy + r, 1.2, color)
    stroke_line(cv, cx - r, cy + r, cx + r, cy - r, 1.2, color)


def icon_check(cv, cx, cy, color, r=5):
    stroke_line(cv, cx - r, cy, cx - r // 3, cy + r, 1.2, color)
    stroke_line(cv, cx - r // 3, cy + r, cx + r, cy - r, 1.2, color)


# --------------------------------------------------------------------------
# Asset composition
# --------------------------------------------------------------------------
def bare_icon(name, draw, color, size=20):
    # The bare toolbar icons (Info / Sync / Shuffle) may be hand-authored as
    # RGBA PNGs; never clobber an existing file. Delete it first to regenerate.
    if (GFX / name).exists():
        print("skip (exists):", name)
        return
    cv = Canvas(size, size, opaque=False)
    draw(cv, size // 2, size // 2, color)
    cv.save(GFX / name)


def framed_icon(name, draw, glyph_color, *, fill=None, border=None, border_t=2, size=20):
    cv = Canvas(size, size, opaque=False)
    r = 3
    if fill is not None and border is not None and fill == border:
        fill_round_rect(cv, 0, 0, size, size, r, fill)
    elif fill is not None:
        fill_round_rect(cv, 0, 0, size, size, r, fill)
        if border is not None:
            round_rect_border(cv, 0, 0, size, size, r, border, border_t)
    elif border is not None:
        round_rect_border(cv, 0, 0, size, size, r, border, border_t)
    draw(cv, size // 2, size // 2, glyph_color)
    cv.save(GFX / name)


def gen_icons():
    bare_icon("toolInfo.png", icon_info, TOOL_ICON)
    bare_icon("toolSyncOn.png", icon_sync, TOOL_ICON)
    bare_icon("toolSyncOff.png", icon_sync, DIS_TEXT)
    bare_icon("toolShuffleOn.png", icon_shuffle, TOOL_ICON)
    bare_icon("toolShuffleOff.png", icon_shuffle, DIS_TEXT)

    # Deselect (X): active = hollow dark frame + dark X; disabled = gray chip.
    framed_icon("deselectActive.png", icon_cross, SELECTED, border=SELECTED, border_t=3)
    framed_icon("deselectDisabled.png", icon_cross, DIS_TEXT, fill=DISABLED, border=BORDER, border_t=2)
    # Submit (check): active = green chip + white check; disabled = gray chip.
    framed_icon("submitActive.png", icon_check, WHITE, fill=SUBMIT, border=SUBMIT)
    framed_icon("submitDisabled.png", icon_check, DIS_TEXT, fill=DISABLED, border=BORDER, border_t=2)


def gen_help(regular: Face, small: Face):
    lh = small.tile_h + 2  # help line height

    # --- Top: title + rules ---
    top = Canvas(256, 192)
    draw_text_center(top, regular, 128, 6, "How to play", INK)
    y = 28
    for s, col in [
        ("Find groups of four items that", INK),
        ("share something in common.", INK),
    ]:
        draw_text(top, small, 10, y, s, col)
        y += lh
    y += 4
    for s in ["- Select four items and tap Submit",
              "  to check if your guess is correct.",
              "- Find the groups without making",
              "  4 mistakes!"]:
        draw_text(top, small, 10, y, s, INK)
        y += lh
    y += 6
    for s in ["Categories are more specific than",
              "\"5-LETTER WORDS,\" \"NAMES\" or \"VERBS.\""]:
        draw_text(top, small, 10, y, s, DIM)
        y += lh
    y += 8
    draw_text(top, small, 10, y, "Tap X or press B to close.", DIM)
    top.save(GFX / "howtoTop.png")

    # --- Bottom: examples + color legend + close X ---
    bot = Canvas(256, 192)
    # close affordance (top-right), matches ui::helpCloseRect center (240,16)
    icon_cross(bot, 240, 16, INK, r=6)
    y = 8
    draw_text(bot, small, 10, y, "Category Examples", INK)
    y += lh + 4
    draw_text(bot, small, 10, y, "FISH: Bass, Flounder, Salmon, Trout", INK)
    y += lh + 2
    draw_text(bot, small, 10, y, "FIRE ___: Ant, Drill, Island, Opal", INK)
    y += lh + 8
    draw_text(bot, small, 10, y, "Each group is assigned a color,", INK)
    y += lh
    draw_text(bot, small, 10, y, "revealed as you solve:", INK)
    y += lh + 6
    sw = sh = 14
    sx = 16
    sy = y
    for i in range(4):
        fill_round_rect(bot, sx, sy + i * (sh + 4), sw, sh, 3, CATEGORY[i])
    draw_text(bot, small, sx + sw + 10, sy + 1, "Straightforward", INK)
    ax = sx + sw + 18
    ay0 = sy + sh + 2
    ay1 = sy + 3 * (sh + 4) - 2
    stroke_line(bot, ax, ay0, ax, ay1, 1.2, DIM)
    fill_tri(bot, [(ax - 3, ay1 - 4), (ax + 3, ay1 - 4), (ax, ay1 + 1)], DIM)
    draw_text(bot, small, sx + sw + 10, sy + 3 * (sh + 4) + 1, "Tricky", INK)
    bot.save(GFX / "howtoBottom.png")


def write_grit_files():
    icon_names = [
        "toolInfo", "toolSyncOn", "toolSyncOff", "toolShuffleOn", "toolShuffleOff",
        "deselectActive", "deselectDisabled", "submitActive", "submitDisabled",
    ]
    # Icons: transparent (magenta) key, 16bpp bitmap, uncompressed.
    for n in icon_names:
        (GFX / f"{n}.grit").write_text("-gb -gB16 -gz! -gTFF00FF\n")
    # Help screens: opaque 16bpp bitmap (no transparency), uncompressed.
    for n in ["howtoTop", "howtoBottom"]:
        (GFX / f"{n}.grit").write_text("-gb -gB16 -gz! -gT! \n")


def main():
    GFX.mkdir(exist_ok=True)
    regular = Face(DATA / "rodin.bin")
    small = Face(DATA / "rodin_small.bin")
    gen_icons()
    gen_help(regular, small)
    write_grit_files()
    print("Generated PNG + .grit assets in", GFX)


if __name__ == "__main__":
    main()

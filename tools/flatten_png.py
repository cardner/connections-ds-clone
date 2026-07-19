#!/usr/bin/env python3
"""Flatten a PNG into a magenta-keyed RGB PNG that grit can convert.

grit ignores the PNG alpha channel, so RGBA icons (transparent background) would
otherwise convert to opaque black boxes. This composites any alpha over the
white toolbar page and turns fully transparent pixels into the grit transparent
key (FF00FF). RGB inputs (already magenta-keyed) pass through unchanged.

Usage: python3 tools/flatten_png.py <in.png> <out.png>
Pure stdlib (zlib + struct); no Pillow required.
"""

import struct
import sys
import zlib

MAGENTA = (255, 0, 255)
WHITE = (255, 255, 255)
ALPHA_CUTOFF = 8  # alpha at/below this becomes fully transparent (magenta)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode(path):
    b = open(path, "rb").read()
    if b[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    p, w, h, depth, ct = 8, 0, 0, 0, 0
    idat = bytearray()
    plte = None
    trns = None
    while p < len(b):
        ln = struct.unpack_from(">I", b, p)[0]
        typ = b[p + 4:p + 8]
        data = b[p + 8:p + 8 + ln]
        p += 12 + ln
        if typ == b"IHDR":
            w, h, depth, ct = struct.unpack_from(">IIBB", data, 0)
        elif typ == b"IDAT":
            idat += data
        elif typ == b"PLTE":
            plte = data
        elif typ == b"tRNS":
            trns = data
        elif typ == b"IEND":
            break
    if depth != 8:
        raise SystemExit(f"{path}: only 8-bit depth supported (got {depth})")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ct)
    if channels is None:
        raise SystemExit(f"{path}: unsupported color type {ct}")
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    rows = []
    prev = bytearray(stride)
    off = 0
    for _ in range(h):
        f = raw[off]
        off += 1
        line = bytearray(raw[off:off + stride])
        off += stride
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 255
        rows.append(line)
        prev = line
    return w, h, ct, channels, plte, trns, rows


def sample(ct, channels, plte, trns, line, x):
    """Return (r, g, b, a) for pixel x in a decoded row."""
    if ct == 6:  # RGBA
        r, g, b, a = line[x * 4:x * 4 + 4]
    elif ct == 2:  # RGB
        r, g, b = line[x * 3:x * 3 + 3]
        a = 255
    elif ct == 0:  # grayscale
        r = g = b = line[x]
        a = 255
    elif ct == 4:  # grayscale + alpha
        r = g = b = line[x * 2]
        a = line[x * 2 + 1]
    elif ct == 3:  # indexed
        idx = line[x]
        r, g, b = plte[idx * 3:idx * 3 + 3]
        a = trns[idx] if (trns and idx < len(trns)) else 255
    else:
        r = g = b = 0
        a = 255
    return r, g, b, a


def flatten(inp, outp):
    w, h, ct, channels, plte, trns, rows = decode(inp)
    out = bytearray()
    for y in range(h):
        out.append(0)  # filter: none
        line = rows[y]
        for x in range(w):
            r, g, b, a = sample(ct, channels, plte, trns, line, x)
            if a <= ALPHA_CUTOFF:
                out += bytes(MAGENTA)
            else:
                if a < 255:
                    r = (r * a + WHITE[0] * (255 - a)) // 255
                    g = (g * a + WHITE[1] * (255 - a)) // 255
                    b = (b * a + WHITE[2] * (255 - a)) // 255
                if (r, g, b) == MAGENTA:
                    b = 254  # avoid an accidental transparent key
                out += bytes((r, g, b))

    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(out), 9))
    png += chunk(b"IEND", b"")
    open(outp, "wb").write(png)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: flatten_png.py <in.png> <out.png>")
    flatten(sys.argv[1], sys.argv[2])

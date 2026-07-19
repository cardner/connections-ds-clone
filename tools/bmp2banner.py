#!/usr/bin/env python3
"""Convert a 32x32 8-bit BMP into a 16-color 4-bit BMP for ndstool -b.

devkitPro's grit→GRF path currently embeds a 4-byte size word that ndstool
treats as icon pixels, which corrupts the ROM banner. A plain 4bpp BMP works.
"""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path
from struct import pack, unpack


def read_bmp8(path: Path):
	data = path.read_bytes()
	if data[:2] != b"BM":
		raise SystemExit(f"{path}: not a BMP")
	off = unpack("<I", data[10:14])[0]
	hdr = unpack("<I", data[14:18])[0]
	w, h = unpack("<ii", data[18:26])
	bpp = unpack("<H", data[28:30])[0]
	comp = unpack("<I", data[30:34])[0]
	if bpp != 8 or comp != 0 or abs(w) != 32 or abs(h) != 32:
		raise SystemExit(f"{path}: need uncompressed 32x32 8-bit BMP (got {w}x{h} {bpp}bpp)")
	top_down = h < 0
	h = abs(h)
	pal = data[14 + hdr : off]
	bits = data[off:]
	row_bytes = (w + 3) & ~3

	def rgb(idx: int):
		b, g, r, _a = pal[idx * 4 : idx * 4 + 4]
		return r, g, b

	pixels = []
	for y in range(h):
		src_row = y if top_down else (h - 1 - y)
		row = []
		for x in range(w):
			row.append(rgb(bits[src_row * row_bytes + x]))
		pixels.append(row)
	return pixels


def quantize(pixels):
	freq = Counter(c for row in pixels for c in row)
	colors = [(255, 255, 255)]  # index 0 = white / transparent-ish
	for c, _n in freq.most_common():
		if c == (255, 255, 255):
			continue
		if c not in colors:
			colors.append(c)
		if len(colors) >= 16:
			break
	while len(colors) < 16:
		colors.append((0, 0, 0))

	def nearest(c):
		if c == (255, 255, 255):
			return 0
		best, best_d = 1, 10**9
		for i in range(1, 16):
			r, g, b = colors[i]
			d = (r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2
			if d < best_d:
				best, best_d = i, d
		return best

	indexed = [[nearest(c) for c in row] for row in pixels]
	return colors, indexed


def write_bmp4(path: Path, colors, indexed):
	# 4bpp, bottom-up, 32x32 → 16 bytes/row (DWORD-aligned).
	row_bytes = 16
	img = bytearray(row_bytes * 32)
	for y in range(32):
		src_y = 31 - y
		for x in range(0, 32, 2):
			hi = indexed[src_y][x] & 0xF
			lo = indexed[src_y][x + 1] & 0xF
			img[y * row_bytes + x // 2] = (hi << 4) | lo

	pal = bytearray()
	for r, g, b in colors:
		pal += bytes((b, g, r, 0))

	hdr_size = 40
	data_off = 14 + hdr_size + 16 * 4
	file_size = data_off + len(img)
	out = bytearray()
	out += b"BM"
	out += pack("<IHHI", file_size, 0, 0, data_off)
	out += pack("<IiiHHIIiiII", hdr_size, 32, 32, 1, 4, 0, len(img), 2835, 2835, 16, 0)
	out += pal
	out += img
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(out)


def main():
	if len(sys.argv) != 3:
		print(f"usage: {sys.argv[0]} icon.bmp out_banner.bmp", file=sys.stderr)
		return 2
	src = Path(sys.argv[1])
	dst = Path(sys.argv[2])
	colors, indexed = quantize(read_bmp8(src))
	write_bmp4(dst, colors, indexed)
	print(f"wrote {dst} ({dst.stat().st_size} bytes, 16-color 4bpp)")
	return 0


if __name__ == "__main__":
	sys.exit(main())

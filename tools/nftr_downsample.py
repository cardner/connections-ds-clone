#!/usr/bin/env python3
"""Downsample an NFTR (2bpp) font to a smaller cell size via area averaging.

Produces a valid NFTR that the Connections DS font loader can parse.
Usage:
  python3 tools/nftr_downsample.py data/rodin.bin data/rodin_small.bin --height 12
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def read_u16(b: bytes, o: int) -> int:
	return struct.unpack_from("<H", b, o)[0]


def read_u32(b: bytes, o: int) -> int:
	return struct.unpack_from("<I", b, o)[0]


def glyph_pixel(glyph: bytes, tile_w: int, col: int, row: int) -> int:
	bit_idx = row * tile_w + col
	return (glyph[bit_idx // 4] >> ((3 - (bit_idx % 4)) * 2)) & 3


def pack_glyph(levels: list[int], tile_w: int, tile_h: int, tile_size: int) -> bytes:
	out = bytearray(tile_size)
	for row in range(tile_h):
		for col in range(tile_w):
			bit_idx = row * tile_w + col
			shift = (3 - (bit_idx % 4)) * 2
			out[bit_idx // 4] |= (levels[row * tile_w + col] & 3) << shift
	return bytes(out)


def downsample_glyph(
	src: bytes, src_w: int, src_h: int, dst_w: int, dst_h: int
) -> list[int]:
	"""Area-average 2bpp coverage into dst_w x dst_h levels 0..3."""
	cov = [0, 85, 170, 255]
	levels: list[int] = []
	for dy in range(dst_h):
		y0 = dy * src_h / dst_h
		y1 = (dy + 1) * src_h / dst_h
		for dx in range(dst_w):
			x0 = dx * src_w / dst_w
			x1 = (dx + 1) * src_w / dst_w
			acc = 0.0
			area = 0.0
			for sy in range(int(y0), min(src_h, int(y1) + 1)):
				for sx in range(int(x0), min(src_w, int(x1) + 1)):
					ox0 = max(x0, float(sx))
					ox1 = min(x1, float(sx + 1))
					oy0 = max(y0, float(sy))
					oy1 = min(y1, float(sy + 1))
					a = max(0.0, ox1 - ox0) * max(0.0, oy1 - oy0)
					if a <= 0:
						continue
					acc += cov[glyph_pixel(src, src_w, sx, sy)] * a
					area += a
			avg = acc / area if area > 0 else 0.0
			if avg < 32:
				levels.append(0)
			elif avg < 112:
				levels.append(1)
			elif avg < 192:
				levels.append(2)
			else:
				levels.append(3)
	return levels


def scale_width(v: int, num: int, den: int) -> int:
	if den <= 0:
		return v
	out = (v * num + den // 2) // den
	return out if out > 0 or v == 0 else 1


def build_small_nftr(src: bytes, dst_h: int) -> bytes:
	if src[0:4] != b"RTFN":
		raise ValueError("not an NFTR (expected RTFN magic)")

	# Loader skips FINF with `0x14 + data[0x14]`, landing on the CGLP size field.
	# Nintendo fourccs are often byte-reversed ("PLGC" == CGLP).
	gpos = 0x14 + src[0x14]
	cglp_magic = gpos - 4
	tag = src[cglp_magic : cglp_magic + 4]
	if tag not in (b"CGLP", b"PLGC"):
		raise ValueError(f"CGLP chunk not found (tag={tag!r})")
	chunk_size = read_u32(src, gpos)
	src_w = src[gpos + 4]
	src_h = src[gpos + 5]
	tile_size = read_u16(src, gpos + 6)
	tile_amount = (chunk_size - 0x10) // tile_size
	tiles_off = gpos + 12
	unknown = src[gpos + 8 : gpos + 12]

	dst_w = max(1, (src_w * dst_h + src_h // 2) // src_h)
	dst_tile_bits = dst_w * dst_h
	dst_tile_size = (dst_tile_bits + 3) // 4
	if dst_tile_size % 2:
		dst_tile_size += 1

	new_tiles = bytearray()
	for i in range(tile_amount):
		glyph = src[tiles_off + i * tile_size : tiles_off + (i + 1) * tile_size]
		levels = downsample_glyph(glyph, src_w, src_h, dst_w, dst_h)
		new_tiles += pack_glyph(levels, dst_w, dst_h, dst_tile_size)

	loc_hdwc = read_u32(src, 0x24)
	# Loader: ptr = base + locHDWC - 4 → chunk size field.
	wpos = loc_hdwc - 4
	wchunk_hdr = src[wpos + 4 : wpos + 12]
	widths_off = wpos + 12
	widths = bytearray(src[widths_off : widths_off + tile_amount * 3])
	for i in range(tile_amount):
		widths[i * 3] = scale_width(widths[i * 3], dst_h, src_h)
		widths[i * 3 + 1] = scale_width(widths[i * 3 + 1], dst_h, src_h)
		widths[i * 3 + 2] = scale_width(widths[i * 3 + 2], dst_h, src_h)

	loc_pamc = read_u32(src, 0x28)
	pamc_blob = src[loc_pamc:]

	head = bytearray(src[:cglp_magic])
	if read_u32(head, 0x18) == src_h:
		struct.pack_into("<I", head, 0x18, dst_h)
	if head[0x1C] == src_w:
		head[0x1C] = dst_w
		head[0x1D] = dst_w

	new_chunk_size = 0x10 + tile_amount * dst_tile_size
	cglp = bytearray()
	cglp += tag  # preserve original fourcc endianness
	cglp += struct.pack("<I", new_chunk_size)
	cglp += bytes((dst_w, dst_h))
	cglp += struct.pack("<H", dst_tile_size)
	cglp += unknown
	cglp += new_tiles

	new_wchunk = 0x10 + tile_amount * 3
	cwdh_out = bytearray()
	cwdh_out += struct.pack("<I", new_wchunk)
	cwdh_out += wchunk_hdr
	cwdh_out += widths

	out = bytearray()
	out += head
	out += cglp
	cwdh_size_off = len(out)
	out += cwdh_out
	pamc_off = len(out)
	out += pamc_blob

	struct.pack_into("<I", out, 0x24, cwdh_size_off + 4)
	struct.pack_into("<I", out, 0x28, pamc_off)
	struct.pack_into("<I", out, 8, len(out))

	assert (new_chunk_size - 0x10) // dst_tile_size == tile_amount
	return bytes(out)


def main() -> None:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("src", type=Path)
	ap.add_argument("dst", type=Path)
	ap.add_argument("--height", type=int, default=12)
	args = ap.parse_args()
	out = build_small_nftr(args.src.read_bytes(), args.height)
	args.dst.write_bytes(out)
	print(f"wrote {args.dst} ({len(out)} bytes, height={args.height})")


if __name__ == "__main__":
	main()

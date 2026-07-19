#include "font.hpp"
#include "gfx.hpp"

#include <string.h>

namespace {

struct FaceData {
	const u8 *tiles = nullptr;
	const u8 *widths = nullptr;
	u16 map[128];
	int tileAmount = 0;
	u8 tileW = 0, tileH = 0;
	u16 tileSize = 0;
	u16 questionMark = 0;
};

FaceData gFaces[2];

// Retuned mid-levels for clearer stems on light/dark fills (was 0,96,176,256).
static const int kAlpha[4] = {0, 85, 170, 256};

inline void read32(const u8 *src, u32 *dst) {
	memcpy(dst, src, 4);
}

u16 charIndex(const FaceData &f, char16_t c) {
	int left = 0, right = f.tileAmount - 1;
	while (left <= right) {
		int mid = left + ((right - left) / 2);
		if (f.map[mid] == (u16)c) return (u16)mid;
		if (f.map[mid] < (u16)c) left = mid + 1;
		else right = mid - 1;
	}
	return f.questionMark;
}

inline u8 glyphPixel(const FaceData &f, const u8 *glyph, int col, int row) {
	int bitIdx = row * f.tileW + col;
	return (glyph[bitIdx / 4] >> ((3 - (bitIdx % 4)) * 2)) & 3;
}

void loadFace(FaceData &f, const u8 *nftr, size_t len) {
	(void)len;
	const u8 *base = nftr;
	const u8 *ptr = base;

	u32 nftrSize;
	read32(ptr + 8, &nftrSize);

	ptr += 0x14 + ptr[0x14];

	u32 chunkSize;
	read32(ptr, &chunkSize);
	ptr += 4;
	f.tileW = ptr[0];
	f.tileH = ptr[1];
	memcpy(&f.tileSize, ptr + 2, 2);
	ptr += 4;

	f.tileAmount = (int)((chunkSize - 0x10) / f.tileSize);
	ptr += 4;
	f.tiles = ptr;

	ptr = base + 0x24;
	u32 locHDWC;
	read32(ptr, &locHDWC);
	ptr = base + locHDWC - 4;
	read32(ptr, &chunkSize);
	ptr += 4 + 8;
	f.widths = ptr;

	memset(f.map, 0, sizeof(f.map));
	ptr = base + 0x28;
	u32 locPAMC, mapType;
	read32(ptr, &locPAMC);

	while (locPAMC < nftrSize && locPAMC != 0) {
		u16 firstChar, lastChar;
		ptr = base + locPAMC;
		memcpy(&firstChar, ptr, 2);
		ptr += 2;
		memcpy(&lastChar, ptr, 2);
		ptr += 2;
		read32(ptr, &mapType);
		ptr += 4;
		read32(ptr, &locPAMC);
		ptr += 4;

		switch (mapType) {
			case 0: {
				u16 firstTile;
				memcpy(&firstTile, ptr, 2);
				ptr += 2;
				for (u16 i = firstChar; i <= lastChar; i++)
					f.map[firstTile + (i - firstChar)] = i;
				break;
			}
			case 1: {
				for (int i = firstChar; i <= lastChar; i++) {
					u16 tile;
					memcpy(&tile, ptr, 2);
					ptr += 2;
					f.map[tile] = (u16)i;
				}
				break;
			}
			case 2: {
				u16 groupAmount;
				memcpy(&groupAmount, ptr, 2);
				ptr += 2;
				for (int i = 0; i < groupAmount; i++) {
					u16 charNo, tileNo;
					memcpy(&charNo, ptr, 2);
					ptr += 2;
					memcpy(&tileNo, ptr, 2);
					ptr += 2;
					f.map[tileNo] = charNo;
				}
				break;
			}
		}
	}

	f.questionMark = charIndex(f, 0xFFFD);
	if (f.questionMark == 0) f.questionMark = charIndex(f, '?');
}

FaceData &faceData(font::Face face) {
	int i = (face == font::Face::Small) ? 1 : 0;
	return gFaces[i];
}

void drawGlyph(bool top, const FaceData &f, int x, int y, u16 index, u16 c, int scale) {
	const u8 *glyph = f.tiles + index * f.tileSize;
	for (int row = 0; row < f.tileH; row++) {
		for (int col = 0; col < f.tileW; col++) {
			int alpha = kAlpha[glyphPixel(f, glyph, col, row)];
			if (!alpha) continue;
			if (scale == 1) {
				gfx::plotAlpha(top, x + col, y + row, c, alpha);
			} else {
				for (int sy = 0; sy < scale; sy++)
					for (int sx = 0; sx < scale; sx++)
						gfx::plotAlpha(top, x + col * scale + sx, y + row * scale + sy, c, alpha);
			}
		}
	}
}

// Box-filter resample of a glyph to (native * num / den).
void drawGlyphFrac(bool top, const FaceData &f, int x, int y, u16 index, u16 c, int num, int den) {
	if (num <= 0 || den <= 0) return;
	if (num == den) {
		drawGlyph(top, f, x, y, index, c, 1);
		return;
	}
	const u8 *glyph = f.tiles + index * f.tileSize;
	int dstW = (f.tileW * num + den - 1) / den;
	int dstH = (f.tileH * num + den - 1) / den;
	if (dstW < 1) dstW = 1;
	if (dstH < 1) dstH = 1;

	static const int cov[4] = {0, 85, 170, 255};

	for (int row = 0; row < dstH; row++) {
		float y0 = (float)row * (float)f.tileH / (float)dstH;
		float y1 = (float)(row + 1) * (float)f.tileH / (float)dstH;
		for (int col = 0; col < dstW; col++) {
			float x0 = (float)col * (float)f.tileW / (float)dstW;
			float x1 = (float)(col + 1) * (float)f.tileW / (float)dstW;
			float acc = 0.f, area = 0.f;
			int sy0 = (int)y0;
			int sy1 = (int)y1 + 1;
			if (sy1 > f.tileH) sy1 = f.tileH;
			int sx0 = (int)x0;
			int sx1 = (int)x1 + 1;
			if (sx1 > f.tileW) sx1 = f.tileW;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					float ox0 = x0 > (float)sx ? x0 : (float)sx;
					float ox1 = x1 < (float)(sx + 1) ? x1 : (float)(sx + 1);
					float oy0 = y0 > (float)sy ? y0 : (float)sy;
					float oy1 = y1 < (float)(sy + 1) ? y1 : (float)(sy + 1);
					float a = (ox1 - ox0) * (oy1 - oy0);
					if (a <= 0.f) continue;
					acc += (float)cov[glyphPixel(f, glyph, sx, sy)] * a;
					area += a;
				}
			}
			if (area <= 0.f) continue;
			int avg = (int)(acc / area + 0.5f);
			// Map 0..255 coverage onto plotAlpha 0..256.
			int alpha = (avg * 256 + 127) / 255;
			if (alpha > 0) gfx::plotAlpha(top, x + col, y + row, c, alpha);
		}
	}
}

} // namespace

void font::init(const u8 *regular, size_t regularLen, const u8 *small, size_t smallLen) {
	loadFace(gFaces[0], regular, regularLen);
	loadFace(gFaces[1], small, smallLen);
}

int font::height(Face face) { return faceData(face).tileH; }

int font::heightFrac(Face face, int num, int den) {
	if (den <= 0) return height(face);
	return (faceData(face).tileH * num) / den;
}

int font::charAdvance(Face face, char c) {
	const FaceData &f = faceData(face);
	u16 idx = charIndex(f, (unsigned char)c);
	return f.widths[idx * 3 + 2];
}

int font::textWidth(Face face, const char *s) {
	int w = 0;
	for (const char *p = s; *p; p++)
		w += charAdvance(face, *p);
	return w;
}

int font::textWidthFrac(Face face, const char *s, int num, int den) {
	if (den <= 0) return textWidth(face, s);
	// Fixed-point accumulate to reduce gap jitter, then snap.
	int acc = 0; // 256ths of a pixel
	for (const char *p = s; *p; p++)
		acc += (charAdvance(face, *p) * num * 256) / den;
	return (acc + 128) / 256;
}

void font::drawChar(bool top, int x, int y, char ch, u16 c, Face face, int scale, bool bold) {
	(void)bold;
	const FaceData &f = faceData(face);
	u16 idx = charIndex(f, (unsigned char)ch);
	int xoff = f.widths[idx * 3] * scale;
	drawGlyph(top, f, x + xoff, y, idx, c, scale);
}

void font::drawCharFrac(bool top, int x, int y, char ch, u16 c, Face face, int num, int den,
                        bool bold) {
	(void)bold;
	const FaceData &f = faceData(face);
	u16 idx = charIndex(f, (unsigned char)ch);
	int xoff = (f.widths[idx * 3] * num) / den;
	drawGlyphFrac(top, f, x + xoff, y, idx, c, num, den);
}

int font::drawText(bool top, int x, int y, const char *s, u16 c, Face face, bool bold) {
	(void)bold;
	int cx = x;
	for (const char *p = s; *p; p++) {
		drawChar(top, cx, y, *p, c, face, 1, false);
		cx += charAdvance(face, *p);
	}
	return cx;
}

int font::drawTextFrac(bool top, int x, int y, const char *s, u16 c, Face face, int num, int den,
                       bool bold) {
	(void)bold;
	if (num == den)
		return drawText(top, x, y, s, c, face, false);

	int cx256 = x * 256;
	for (const char *p = s; *p; p++) {
		int px = (cx256 + 128) / 256;
		drawCharFrac(top, px, y, *p, c, face, num, den, false);
		cx256 += (charAdvance(face, *p) * num * 256) / den;
	}
	return (cx256 + 128) / 256;
}

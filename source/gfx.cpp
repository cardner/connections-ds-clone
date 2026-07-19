#include "gfx.hpp"
#include "font.hpp"
#include "rodin_bin.h"
#include "rodin_small_bin.h"

#include <string.h>
#include <math.h>

namespace {

// Draw into RAM back-buffers, then DMA to VRAM on flip. Writing the live
// framebuffer caused visible flashing once AA made a full redraw take longer
// than a frame.
u16 backTop[gfx::SCREEN_W * gfx::SCREEN_H];
u16 backBottom[gfx::SCREEN_W * gfx::SCREEN_H];
u16 *vramTop = nullptr;
u16 *vramBottom = nullptr;
u16 *fbTop = backTop;
u16 *fbBottom = backBottom;

inline u16 *fb(bool top) { return top ? fbTop : fbBottom; }

inline void clampRect(int &x, int &y, int &w, int &h) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > gfx::SCREEN_W) w = gfx::SCREEN_W - x;
	if (y + h > gfx::SCREEN_H) h = gfx::SCREEN_H - y;
}

// Slightly tighter AA band than ±0.5px for crisper glyphs/icons on RGB555.
static int coverageFromSdf(float sdf) {
	if (sdf <= -0.4f) return 256;
	if (sdf >= 0.4f) return 0;
	return (int)((0.4f - sdf) * (256.f / 0.8f));
}

// Anti-aliased quarter-disk for rounded-rect corners (only r² pixels).
static void fillCornerAA(bool top, int cx, int cy, int r, u16 c, int sx, int sy) {
	for (int j = 0; j <= r; j++) {
		for (int i = 0; i <= r; i++) {
			float d = sqrtf((i + 0.5f) * (i + 0.5f) + (j + 0.5f) * (j + 0.5f));
			int a = coverageFromSdf(d - (float)r);
			if (a > 0) gfx::plotAlpha(top, cx + sx * i, cy + sy * j, c, a);
		}
	}
}

// Anti-aliased quarter-ring for rounded-rect borders.
static void borderCornerAA(bool top, int cx, int cy, int outerR, int innerR, u16 c, int sx, int sy) {
	int lim = outerR + 1;
	for (int j = 0; j <= lim; j++) {
		for (int i = 0; i <= lim; i++) {
			float d = sqrtf((i + 0.5f) * (i + 0.5f) + (j + 0.5f) * (j + 0.5f));
			int a = coverageFromSdf(d - (float)outerR) - coverageFromSdf(d - (float)innerR);
			if (a > 0) gfx::plotAlpha(top, cx + sx * i, cy + sy * j, c, a);
		}
	}
}

static void plotDiskAA(bool top, float cx, float cy, float radius, u16 c) {
	int r0 = (int)ceilf(radius + 0.5f);
	int ix = (int)floorf(cx);
	int iy = (int)floorf(cy);
	for (int dy = -r0; dy <= r0; dy++) {
		for (int dx = -r0; dx <= r0; dx++) {
			float dist = sqrtf((ix + dx + 0.5f - cx) * (ix + dx + 0.5f - cx) +
			                   (iy + dy + 0.5f - cy) * (iy + dy + 0.5f - cy));
			int a = coverageFromSdf(dist - radius);
			if (a > 0) gfx::plotAlpha(top, ix + dx, iy + dy, c, a);
		}
	}
}

} // namespace

u16 gfx::blend(u16 fg, u16 bg, int t) {
	if (t <= 0) return bg;
	if (t >= 256) return fg;
	auto ch = [](u16 c, int shift) { return (c >> shift) & 0x1f; };
	int r = (ch(fg, 0) * t + ch(bg, 0) * (256 - t)) / 256;
	int g = (ch(fg, 5) * t + ch(bg, 5) * (256 - t)) / 256;
	int b = (ch(fg, 10) * t + ch(bg, 10) * (256 - t)) / 256;
	return ARGB16(1, r, g, b);
}

void gfx::init() {
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);

	int bgMain = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
	int bgSub = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);

	vramTop = bgGetGfxPtr(bgMain);
	vramBottom = bgGetGfxPtr(bgSub);
	fbTop = backTop;
	fbBottom = backBottom;

	font::init(rodin_bin, rodin_bin_size, rodin_small_bin, rodin_small_bin_size);

	clear(true, pal::bg);
	clear(false, pal::bg);
	flip();
}

void gfx::flip() {
	DC_FlushRange(backTop, sizeof(backTop));
	DC_FlushRange(backBottom, sizeof(backBottom));
	dmaCopyWords(3, backTop, vramTop, sizeof(backTop));
	dmaCopyWords(2, backBottom, vramBottom, sizeof(backBottom));
}

void gfx::copyRectTo(bool top, int x, int y, int w, int h, u16 *dst, int dstStride) {
	const u16 *src = fb(top);
	if (!src || !dst) return;
	for (int row = 0; row < h; row++) {
		int sy = y + row;
		for (int col = 0; col < w; col++) {
			int sx = x + col;
			u16 c = 0;
			if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H)
				c = src[sy * SCREEN_W + sx];
			dst[row * dstStride + col] = c;
		}
	}
}

void gfx::clear(bool top, u16 c) {
	u16 *p = fb(top);
	if (!p) return;
	dmaFillHalfWords(c, p, SCREEN_W * SCREEN_H * sizeof(u16));
}

void gfx::fillRect(bool top, int x, int y, int w, int h, u16 c) {
	u16 *p = fb(top);
	if (!p) return;
	clampRect(x, y, w, h);
	if (w <= 0 || h <= 0) return;
	for (int row = 0; row < h; row++) {
		u16 *dst = &p[(y + row) * SCREEN_W + x];
		for (int col = 0; col < w; col++)
			dst[col] = c;
	}
}

void gfx::rectBorder(bool top, int x, int y, int w, int h, u16 c, int thickness) {
	if (w <= 0 || h <= 0) return;
	fillRect(top, x, y, w, thickness, c);
	fillRect(top, x, y + h - thickness, w, thickness, c);
	fillRect(top, x, y, thickness, h, c);
	fillRect(top, x + w - thickness, y, thickness, h, c);
}

void gfx::panel(bool top, int x, int y, int w, int h, u16 fill, u16 border, int thickness) {
	fillRect(top, x, y, w, h, fill);
	rectBorder(top, x, y, w, h, border, thickness);
}

void gfx::plot(bool top, int x, int y, u16 c) {
	if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
	u16 *p = fb(top);
	if (!p) return;
	p[y * SCREEN_W + x] = c;
}

u16 gfx::readPixel(bool top, int x, int y) {
	if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return pal::bg;
	u16 *p = fb(top);
	if (!p) return pal::bg;
	return p[y * SCREEN_W + x];
}

void gfx::plotDisk(bool top, float cx, float cy, float radius, u16 c) {
	plotDiskAA(top, cx, cy, radius, c);
}

void gfx::strokeLine(bool top, float x0, float y0, float x1, float y1, float pen, u16 c) {
	// Snap near-integer endpoints for sharper icon geometry.
	auto snap = [](float v) {
		float r = floorf(v + 0.5f);
		return fabsf(v - r) < 0.01f ? r : v;
	};
	x0 = snap(x0); y0 = snap(y0); x1 = snap(x1); y1 = snap(y1);

	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.01f) {
		plotDiskAA(top, x0, y0, pen, c);
		return;
	}
	// Single-pass capsule SDF along the segment (avoids overlapping stamp blur).
	float inv = 1.f / len;
	float ux = dx * inv, uy = dy * inv;
	int pad = (int)ceilf(pen + 1.f);
	int minX = (int)floorf((x0 < x1 ? x0 : x1) - pad);
	int maxX = (int)ceilf((x0 > x1 ? x0 : x1) + pad);
	int minY = (int)floorf((y0 < y1 ? y0 : y1) - pad);
	int maxY = (int)ceilf((y0 > y1 ? y0 : y1) + pad);
	if (minX < 0) minX = 0;
	if (minY < 0) minY = 0;
	if (maxX >= SCREEN_W) maxX = SCREEN_W - 1;
	if (maxY >= SCREEN_H) maxY = SCREEN_H - 1;
	for (int py = minY; py <= maxY; py++) {
		for (int px = minX; px <= maxX; px++) {
			float fx = (float)px + 0.5f - x0;
			float fy = (float)py + 0.5f - y0;
			float t = fx * ux + fy * uy;
			if (t < 0.f) t = 0.f;
			if (t > len) t = len;
			float cx = x0 + ux * t;
			float cy = y0 + uy * t;
			float dist = sqrtf((px + 0.5f - cx) * (px + 0.5f - cx) +
			                   (py + 0.5f - cy) * (py + 0.5f - cy));
			int a = coverageFromSdf(dist - pen);
			if (a > 0) plotAlpha(top, px, py, c, a);
		}
	}
}

void gfx::strokeArc(bool top, float cx, float cy, float radius, float a0, float a1, float pen,
                    u16 c) {
	// Normalize sweep so a0..a1 is CCW in radians.
	while (a1 < a0) a1 += 6.2831853f;
	float pad = pen + 1.5f;
	int minX = (int)floorf(cx - radius - pad);
	int maxX = (int)ceilf(cx + radius + pad);
	int minY = (int)floorf(cy - radius - pad);
	int maxY = (int)ceilf(cy + radius + pad);
	if (minX < 0) minX = 0;
	if (minY < 0) minY = 0;
	if (maxX >= SCREEN_W) maxX = SCREEN_W - 1;
	if (maxY >= SCREEN_H) maxY = SCREEN_H - 1;

	for (int py = minY; py <= maxY; py++) {
		for (int px = minX; px <= maxX; px++) {
			float fx = (float)px + 0.5f - cx;
			float fy = (float)py + 0.5f - cy;
			float dist = sqrtf(fx * fx + fy * fy);
			float ring = fabsf(dist - radius);
			// Angular test with a small feather at the tips.
			float ang = atan2f(-fy, fx); // screen y-down → flip for math CCW
			if (ang < 0.f) ang += 6.2831853f;
			float a = ang;
			// Shift into [a0, a0+2π) then compare to sweep.
			while (a < a0) a += 6.2831853f;
			while (a > a0 + 6.2831853f) a -= 6.2831853f;
			if (a > a1 + 0.05f) continue;
			int cov = coverageFromSdf(ring - pen);
			if (cov > 0) plotAlpha(top, px, py, c, cov);
		}
	}
}

void gfx::plotAlpha(bool top, int x, int y, u16 fg, int alpha) {
	if (alpha <= 0) return;
	if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
	if (alpha >= 256) {
		plot(top, x, y, fg);
		return;
	}
	u16 bg = readPixel(top, x, y);
	plot(top, x, y, blend(fg, bg, alpha));
}

void gfx::fillRoundRect(bool top, int x, int y, int w, int h, int r, u16 c) {
	if (w <= 0 || h <= 0) return;
	if (r < 0) r = 0;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r == 0) {
		fillRect(top, x, y, w, h, c);
		return;
	}

	// Opaque body (fast paths); only the four corners need AA.
	fillRect(top, x + r, y, w - 2 * r, h, c);
	fillRect(top, x, y + r, r, h - 2 * r, c);
	fillRect(top, x + w - r, y + r, r, h - 2 * r, c);

	fillCornerAA(top, x + r, y + r, r, c, -1, -1);
	fillCornerAA(top, x + w - 1 - r, y + r, r, c, 1, -1);
	fillCornerAA(top, x + r, y + h - 1 - r, r, c, -1, 1);
	fillCornerAA(top, x + w - 1 - r, y + h - 1 - r, r, c, 1, 1);
}

void gfx::roundPanel(bool top, int x, int y, int w, int h, int r, u16 fill, u16 border, int thickness) {
	fillRoundRect(top, x, y, w, h, r, border);
	if (thickness > 0) {
		int ir = r - thickness;
		if (ir < 0) ir = 0;
		fillRoundRect(top, x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness, ir, fill);
	}
}

void gfx::roundRectBorder(bool top, int x, int y, int w, int h, int r, u16 c, int thickness) {
	if (w <= 0 || h <= 0 || thickness <= 0) return;
	if (r < 0) r = 0;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r == 0) {
		rectBorder(top, x, y, w, h, c, thickness);
		return;
	}

	int t = thickness;
	if (t > r) t = r;
	int inner = r - t;

	fillRect(top, x + r, y, w - 2 * r, t, c);
	fillRect(top, x + r, y + h - t, w - 2 * r, t, c);
	fillRect(top, x, y + r, t, h - 2 * r, c);
	fillRect(top, x + w - t, y + r, t, h - 2 * r, c);

	borderCornerAA(top, x + r, y + r, r, inner, c, -1, -1);
	borderCornerAA(top, x + w - 1 - r, y + r, r, inner, c, 1, -1);
	borderCornerAA(top, x + r, y + h - 1 - r, r, inner, c, -1, 1);
	borderCornerAA(top, x + w - 1 - r, y + h - 1 - r, r, inner, c, 1, 1);
}

void gfx::roundRectDottedBorder(bool top, int x, int y, int w, int h, int r, u16 c,
                                int dash, int gap) {
	if (w <= 0 || h <= 0) return;
	if (dash < 1) dash = 1;
	if (gap < 1) gap = 1;
	if (r < 0) r = 0;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;

	const int period = dash + gap;
	auto onDash = [&](int d) { return (d % period) < dash; };

	int dist = 0;
	auto plotDash = [&](int px, int py) {
		if (onDash(dist)) plot(top, px, py, c);
		dist++;
	};

	const float pi2 = 1.5707963f;
	auto corner = [&](int cx, int cy, float a0, float a1) {
		if (r <= 0) return;
		int steps = (int)(pi2 * (float)r + 0.5f);
		if (steps < 1) steps = 1;
		for (int i = 0; i <= steps; i++) {
			float t = (float)i / (float)steps;
			float a = a0 + (a1 - a0) * t;
			int px = cx + (int)floorf((float)r * cosf(a) + 0.5f);
			int py = cy + (int)floorf((float)r * sinf(a) + 0.5f);
			plotDash(px, py);
		}
	};

	// Clockwise: top → TR → right → BR → bottom → BL → left → TL.
	for (int i = 0; i < w - 2 * r; i++)
		plotDash(x + r + i, y);
	corner(x + w - 1 - r, y + r, -pi2, 0.f);
	for (int i = 0; i < h - 2 * r; i++)
		plotDash(x + w - 1, y + r + i);
	corner(x + w - 1 - r, y + h - 1 - r, 0.f, pi2);
	for (int i = 0; i < w - 2 * r; i++)
		plotDash(x + w - 1 - r - i, y + h - 1);
	corner(x + r, y + h - 1 - r, pi2, 2.f * pi2);
	for (int i = 0; i < h - 2 * r; i++)
		plotDash(x, y + h - 1 - r - i);
	corner(x + r, y + r, 2.f * pi2, 3.f * pi2);
}

void gfx::fillCircle(bool top, int cx, int cy, int r, u16 c) {
	if (r <= 0) {
		plot(top, cx, cy, c);
		return;
	}
	plotDiskAA(top, (float)cx, (float)cy, (float)r, c);
}

void gfx::fillRing(bool top, int cx, int cy, int outerR, int innerR, u16 c) {
	if (outerR <= innerR) return;
	// Treat (cx, cy) as a pixel center (cx+0.5, cy+0.5), matching plotDiskAA.
	const float fcx = (float)cx + 0.5f;
	const float fcy = (float)cy + 0.5f;
	int r0 = outerR + 1;
	for (int dy = -r0; dy <= r0; dy++) {
		for (int dx = -r0; dx <= r0; dx++) {
			float px = (float)(cx + dx) + 0.5f;
			float py = (float)(cy + dy) + 0.5f;
			float dist = sqrtf((px - fcx) * (px - fcx) + (py - fcy) * (py - fcy));
			int a = coverageFromSdf(dist - (float)outerR) - coverageFromSdf(dist - (float)innerR);
			if (a > 0) plotAlpha(top, cx + dx, cy + dy, c, a);
		}
	}
}

void gfx::fillTriangle(bool top, int x0, int y0, int x1, int y1, int x2, int y2, u16 c) {
	if (y0 > y1) { int t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
	if (y0 > y2) { int t; t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
	if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
	int totalH = y2 - y0;
	if (totalH == 0) return;
	for (int i = 0; i <= totalH; i++) {
		bool second = (i > y1 - y0) || (y1 == y0);
		int segH = second ? (y2 - y1) : (y1 - y0);
		if (segH == 0) segH = 1;
		int ax = x0 + (x2 - x0) * i / totalH;
		int bx = second ? x1 + (x2 - x1) * (i - (y1 - y0)) / segH
		                : x0 + (x1 - x0) * i / segH;
		if (ax > bx) { int t = ax; ax = bx; bx = t; }
		fillRect(top, ax, y0 + i, bx - ax + 1, 1, c);
	}
}

void gfx::drawChar(bool top, int x, int y, char ch, u16 c, int scale, bool bold) {
	font::drawChar(top, x, y, ch, c, font::Face::Regular, scale, bold);
}

int gfx::drawText(bool top, int x, int y, const char *s, u16 c, int scale, bool bold) {
	int cx = x;
	const int lineH = font::height(font::Face::Regular) * scale + scale;
	for (const char *p = s; *p; p++) {
		if (*p == '\n') { cx = x; y += lineH; continue; }
		drawChar(top, cx, y, *p, c, scale, bold);
		cx += font::charAdvance(font::Face::Regular, *p) * scale;
	}
	return cx;
}

int gfx::drawTextAdv(bool top, int x, int y, const char *s, u16 c, int advance, bool bold) {
	int cx = x;
	for (const char *p = s; *p; p++) {
		drawChar(top, cx, y, *p, c, 1, bold);
		cx += advance;
	}
	return cx;
}

int gfx::textWidth(const char *s, int scale) {
	return font::textWidth(font::Face::Regular, s) * scale;
}

void gfx::drawTextCentered(bool top, int cx, int y, const char *s, u16 c, int scale, bool bold) {
	int w = textWidth(s, scale);
	drawText(top, cx - w / 2, y, s, c, scale, bold);
}

namespace {

bool parseBmp8(const u8 *bmp, u32 len, int *outW, int *outH, bool *outTopDown,
               const u8 **outPal, const u8 **outBits, int *outRowBytes) {
	if (!bmp || len < 54 || bmp[0] != 'B' || bmp[1] != 'M')
		return false;

	u32 dataOff, hdrSize;
	memcpy(&dataOff, bmp + 10, 4);
	memcpy(&hdrSize, bmp + 14, 4);
	if (dataOff >= len || hdrSize < 40)
		return false;

	s32 bw, bh;
	memcpy(&bw, bmp + 18, 4);
	memcpy(&bh, bmp + 22, 4);
	u16 bpp;
	memcpy(&bpp, bmp + 28, 2);
	u32 compression;
	memcpy(&compression, bmp + 30, 4);
	if (bpp != 8 || compression != 0 || bw <= 0)
		return false;

	bool topDown = bh < 0;
	int h = topDown ? -bh : bh;
	int w = bw;
	int rowBytes = (w + 3) & ~3;
	if (h <= 0 || dataOff + (u32)(rowBytes * h) > len)
		return false;

	*outW = w;
	*outH = h;
	*outTopDown = topDown;
	*outPal = bmp + 14 + hdrSize;
	*outBits = bmp + dataOff;
	*outRowBytes = rowBytes;
	return true;
}

} // namespace

bool gfx::drawBmp(bool top, int x, int y, const u8 *bmp, u32 len, int scale) {
	if (scale < 1) return false;
	int w, h, rowBytes;
	bool topDown;
	const u8 *pal, *bits;
	if (!parseBmp8(bmp, len, &w, &h, &topDown, &pal, &bits, &rowBytes))
		return false;

	for (int row = 0; row < h; row++) {
		int srcRow = topDown ? row : (h - 1 - row);
		const u8 *src = bits + srcRow * rowBytes;
		for (int col = 0; col < w; col++) {
			u8 idx = src[col];
			u8 b = pal[idx * 4 + 0];
			u8 g = pal[idx * 4 + 1];
			u8 r = pal[idx * 4 + 2];
			if (r == 255 && g == 255 && b == 255)
				continue;
			u16 c = color(r, g, b);
			if (scale == 1)
				plot(top, x + col, y + row, c);
			else
				fillRect(top, x + col * scale, y + row * scale, scale, scale, c);
		}
	}
	return true;
}

bool gfx::drawBmpSized(bool top, int x, int y, const u8 *bmp, u32 len, int destW, int destH) {
	if (destW <= 0 || destH <= 0) return false;
	int w, h, rowBytes;
	bool topDown;
	const u8 *pal, *bits;
	if (!parseBmp8(bmp, len, &w, &h, &topDown, &pal, &bits, &rowBytes))
		return false;

	for (int row = 0; row < destH; row++) {
		int srcY = row * h / destH;
		int srcRow = topDown ? srcY : (h - 1 - srcY);
		const u8 *src = bits + srcRow * rowBytes;
		for (int col = 0; col < destW; col++) {
			int srcX = col * w / destW;
			u8 idx = src[srcX];
			u8 b = pal[idx * 4 + 0];
			u8 g = pal[idx * 4 + 1];
			u8 r = pal[idx * 4 + 2];
			if (r == 255 && g == 255 && b == 255)
				continue;
			plot(top, x + col, y + row, color(r, g, b));
		}
	}
	return true;
}

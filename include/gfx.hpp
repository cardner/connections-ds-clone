// Minimal dual-screen drawing layer for Connections DS.
//
// Both screens are driven as 16-bit (RGB555) bitmap backgrounds and everything
// is drawn as filled rectangles plus an embedded 8x8 bitmap font. This keeps
// rendering fully self-contained (no external graphics assets, no console /
// palette / VRAM-base juggling).
#pragma once

#include <nds.h>

namespace gfx {

// Screen dimensions (visible area).
constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 192;

// Opaque RGB555 color (bit 15 must be set for a bitmap pixel to be visible).
constexpr u16 color(u8 r, u8 g, u8 b) {
	return ARGB16(1, r >> 3, g >> 3, b >> 3);
}

// Palette used across the UI (NYT Connections light theme).
namespace pal {
	constexpr u16 bg        = gfx::color(255, 255, 255); // white page
	constexpr u16 panel     = gfx::color(233, 233, 228); // empty slot / placeholder
	constexpr u16 tile      = gfx::color(0xE5, 0xE5, 0xE5); // default word tile (#E5E5E5)
	constexpr u16 tileText  = gfx::color(0x28, 0x27, 0x27); // default tile text (#282727)
	constexpr u16 selected  = gfx::color(0x28, 0x27, 0x27); // selected word tile (#282727)
	constexpr u16 selText   = gfx::color(0xE5, 0xE5, 0xE5); // selected tile text (#E5E5E5)
	constexpr u16 white     = gfx::color(255, 255, 255);
	constexpr u16 ink       = gfx::color(18, 18, 18);    // primary text on light bg
	constexpr u16 dim       = gfx::color(120, 120, 120);
	constexpr u16 disabled  = gfx::color(228, 228, 228); // framed toolbar fill (#E4E4E4)
	constexpr u16 disText   = gfx::color(120, 120, 120); // framed toolbar icon/border (#787878)
	constexpr u16 button    = gfx::color(255, 255, 255); // secondary button fill
	constexpr u16 buttonHot = gfx::color(18, 18, 18);    // primary (active) button fill
	constexpr u16 submit    = gfx::color(0xA0, 0xC3, 0x5A); // enabled Submit (NYT green)
	constexpr u16 border    = gfx::color(120, 120, 120); // outline / framed toolbar stroke
	constexpr u16 toolIcon  = gfx::color(28, 28, 28);    // bare toolbar glyph (#1C1C1C)
	constexpr u16 focus     = gfx::color(0x62, 0x90, 0xE8); // word-tile focus border (#6290E8)
	constexpr u16 black     = gfx::color(0, 0, 0);
	constexpr u16 qrLight   = gfx::color(255, 255, 255);
	constexpr u16 qrDark    = gfx::color(0, 0, 0);

	// NYT category difficulty colors: 0 yellow, 1 green, 2 blue, 3 purple.
	constexpr u16 category[4] = {
		gfx::color(0xF9, 0xDF, 0x6D),
		gfx::color(0xA0, 0xC3, 0x5A),
		gfx::color(0xB0, 0xC4, 0xEF),
		gfx::color(0xBA, 0x81, 0xC5),
	};
}

// Blend @p fg toward @p bg. @p t is 0 (all bg) .. 256 (all fg).
u16 blend(u16 fg, u16 bg, int t);

void init();

// Present the RAM back-buffers to VRAM (call once per finished frame).
void flip();

// Copy a w×h rect of the back-buffer (source screen @p top) into @p dst, a
// row-major ARGB16 buffer with the given stride (pixels). Out-of-bounds source
// pixels are written transparent (0). Used to capture a freshly-rendered element
// into a hardware sprite's graphics for animation.
void copyRectTo(bool top, int x, int y, int w, int h, u16 *dst, int dstStride);

// Fill the whole given screen with a color.
void clear(bool top, u16 c);

// Filled rectangle (clipped to the screen).
void fillRect(bool top, int x, int y, int w, int h, u16 c);

// Rectangle outline of the given thickness.
void rectBorder(bool top, int x, int y, int w, int h, u16 c, int thickness = 1);

// A filled rectangle with a border (a "card").
void panel(bool top, int x, int y, int w, int h, u16 fill, u16 border, int thickness = 1);

// Filled rounded rectangle (radius is clamped to half the smaller side).
void fillRoundRect(bool top, int x, int y, int w, int h, int radius, u16 c);

// A filled rounded rectangle with a border (a rounded "card"/pill).
void roundPanel(bool top, int x, int y, int w, int h, int radius, u16 fill, u16 border, int thickness = 1);

// Rounded rectangle outline only (used for the focus ring).
void roundRectBorder(bool top, int x, int y, int w, int h, int radius, u16 c, int thickness = 2);

// Rounded rectangle with a dotted/dashed outline (empty selection slots).
void roundRectDottedBorder(bool top, int x, int y, int w, int h, int radius, u16 c,
                           int dash = 2, int gap = 2);

// Filled circle centered at (cx, cy) (used for the mistake dots).
void fillCircle(bool top, int cx, int cy, int radius, u16 c);

// Filled ring (anti-aliased annulus) centered at (cx, cy).
void fillRing(bool top, int cx, int cy, int outerR, int innerR, u16 c);

// Filled triangle through three points (used for the refresh arrowheads).
void fillTriangle(bool top, int x0, int y0, int x1, int y1, int x2, int y2, u16 c);

// Plot a single pixel (used by the QR renderer). Clipped.
void plot(bool top, int x, int y, u16 c);

// Plot a filled anti-aliased disk (used for smooth icon strokes).
void plotDisk(bool top, float cx, float cy, float radius, u16 c);

// Stroke a line with a round, anti-aliased pen.
void strokeLine(bool top, float x0, float y0, float x1, float y1, float penRadius, u16 c);

// Stroke a circular arc (radians, CCW from +x) with a round AA pen — single-pass SDF.
void strokeArc(bool top, float cx, float cy, float radius, float a0, float a1, float penRadius,
               u16 c);

// Sample the framebuffer (returns bg if out of bounds).
u16 readPixel(bool top, int x, int y);

// Plot with alpha blending (0..256) against the pixel already in the framebuffer.
void plotAlpha(bool top, int x, int y, u16 fg, int alpha);

// Draw one glyph scaled by `scale` (integer). Transparent background.
// `bold` is accepted for compatibility but ignored.
void drawChar(bool top, int x, int y, char ch, u16 c, int scale = 1, bool bold = false);

// Draw a string. Returns the x position just past the text.
int drawText(bool top, int x, int y, const char *s, u16 c, int scale = 1, bool bold = false);

// Draw a string at 1x with a custom per-glyph advance (for condensing long
// words). Returns the x position just past the text.
int drawTextAdv(bool top, int x, int y, const char *s, u16 c, int advance, bool bold = false);

// Pixel width of a string at the given scale.
int textWidth(const char *s, int scale = 1);

// Draw a string horizontally centered around cx.
void drawTextCentered(bool top, int cx, int y, const char *s, u16 c, int scale = 1, bool bold = false);

// Draw an 8-bit Windows BMP (BI_RGB). Pure white pixels are skipped (transparent).
// @p scale is an integer zoom (1 = native). Returns false if the buffer is not a
// supported BMP.
bool drawBmp(bool top, int x, int y, const u8 *bmp, u32 len, int scale = 1);

// Like drawBmp, but nearest-neighbor scaled to an exact destination size
// (keeps aspect if you pass proportional destW/destH).
bool drawBmpSized(bool top, int x, int y, const u8 *bmp, u32 len, int destW, int destH);

} // namespace gfx

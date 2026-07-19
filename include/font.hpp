// Rodin NTLG (DSi system font) loaded from embedded NFTR files.
#pragma once

#include <nds.h>

namespace font {

// Designed sizes: Regular = data/rodin.bin (~18px), Small = rodin_small.bin (~12px).
enum class Face {
	Regular = 0,
	Small = 1,
};

// Parse and load Regular + Small NFTR blobs (called once from gfx::init).
void init(const u8 *regular, size_t regularLen, const u8 *small, size_t smallLen);

// Glyph cell / line metrics for a face.
int height(Face face);
int charAdvance(Face face, char c);
int textWidth(Face face, const char *s);

// Fractional metrics (overflow fallback when even Small is too wide).
int heightFrac(Face face, int num, int den);
int textWidthFrac(Face face, const char *s, int num, int den);

// Legacy helpers (Regular face, scale relative to Regular cell).
inline int height() { return height(Face::Regular); }
inline int charAdvance(char c) { return charAdvance(Face::Regular, c); }
inline int textWidth(const char *s, int scale = 1) {
	return textWidth(Face::Regular, s) * scale;
}
inline int heightFrac(int num, int den) { return heightFrac(Face::Regular, num, den); }
inline int textWidthFrac(const char *s, int num, int den) {
	return textWidthFrac(Face::Regular, s, num, den);
}

// Draw one glyph. `bold` is ignored (kept for call-site compatibility).
void drawChar(bool top, int x, int y, char ch, u16 c, Face face, int scale = 1,
              bool bold = false);
void drawCharFrac(bool top, int x, int y, char ch, u16 c, Face face, int num, int den,
                  bool bold = false);

// Draw a string. Returns x just past the text.
int drawText(bool top, int x, int y, const char *s, u16 c, Face face, bool bold = false);
int drawTextFrac(bool top, int x, int y, const char *s, u16 c, Face face, int num, int den,
                 bool bold = false);

// Legacy Regular-face 1× draw.
inline int drawText(bool top, int x, int y, const char *s, u16 c, bool bold = false) {
	return drawText(top, x, y, s, c, Face::Regular, bold);
}

// Legacy Regular-face frac draw (num/den against Regular cell).
inline int drawTextFrac(bool top, int x, int y, const char *s, u16 c, int num, int den,
                        bool bold = false) {
	return drawTextFrac(top, x, y, s, c, Face::Regular, num, den, bold);
}

} // namespace font

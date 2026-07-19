#include "share.hpp"
#include "config.hpp"
#include "gfx.hpp"

#include "qrcodegen.h"

#include <stdio.h>
#include <string.h>

namespace {

// UTF-8 colored squares matching NYT category difficulty colors:
// 0 yellow, 1 green, 2 blue, 3 purple.
const char *SQUARE[NUM_CATEGORIES] = {
	"\xF0\x9F\x9F\xA8", // U+1F7E8 yellow square
	"\xF0\x9F\x9F\xA9", // U+1F7E9 green square
	"\xF0\x9F\x9F\xA6", // U+1F7E6 blue square
	"\xF0\x9F\x9F\xAA", // U+1F7EA purple square
};

} // namespace

void share::buildText(const Game &g, char *buf, int cap) {
	int n = 0;
	n += snprintf(buf + n, cap - n, "NoYT Connections\n");
	if (g.puzzle.id > 0)
		n += snprintf(buf + n, cap - n, "Puzzle #%d\n", g.puzzle.id);
	else if (g.puzzle.date[0])
		n += snprintf(buf + n, cap - n, "%s\n", g.puzzle.date);

	int guesses = g.guessCount;
	int mistakes = g.mistakes;
	n += snprintf(buf + n, cap - n, "%s (%d/%d)\n",
	              g.status == GameStatus::Won ? "Solved" : "Missed",
	              guesses - mistakes, guesses);

	for (int r = 0; r < g.guessCount && n < cap - 24; r++) {
		for (int i = 0; i < CARDS_PER_CATEGORY; i++) {
			int cat = g.guessRows[r][i];
			if (cat >= 0 && cat < NUM_CATEGORIES)
				n += snprintf(buf + n, cap - n, "%s", SQUARE[cat]);
		}
		n += snprintf(buf + n, cap - n, "\n");
	}
	if (n >= cap) n = cap - 1;
	buf[n] = 0;
}

bool share::renderQR(const Game &g) {
	static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
	static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];

	char text[600];
	buildText(g, text, sizeof(text));

	bool ok = qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW,
	                               qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
	                               qrcodegen_Mask_AUTO, true);

	gfx::clear(false, gfx::pal::qrLight);
	if (!ok) {
		gfx::drawTextCentered(false, 128, 90, "QR encode failed", gfx::pal::qrDark, 1);
		return false;
	}

	int size = qrcodegen_getSize(qr);
	const int quiet = 4;
	int total = size + quiet * 2;
	int scale = 168 / total;
	if (scale < 1) scale = 1;
	int dim = total * scale;
	int ox = (gfx::SCREEN_W - dim) / 2;
	int oy = (gfx::SCREEN_H - dim) / 2;

	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			if (qrcodegen_getModule(qr, x, y)) {
				int px = ox + (x + quiet) * scale;
				int py = oy + (y + quiet) * scale;
				gfx::fillRect(false, px, py, scale, scale, gfx::pal::qrDark);
			}
		}
	}

	gfx::drawTextCentered(false, 128, 3, "Scan to share your result", gfx::pal::qrDark, 1);
	gfx::drawTextCentered(false, 128, 183, "B: back", gfx::pal::qrDark, 1);
	return true;
}

bool share::writeFile(const Game &g) {
	char text[600];
	buildText(g, text, sizeof(text));
	FILE *f = fopen(SHARE_PATH, "w");
	if (!f) return false;
	fputs(text, f);
	fclose(f);
	return true;
}

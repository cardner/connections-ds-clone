#include "ui.hpp"
#include "font.hpp"
#include "image.hpp"
#include "anim.hpp"
#include "sprite.hpp"
#include "header_grf.h"

#include <string.h>
#include <stdio.h>

namespace {

// Top-screen layout from Figma / data/top-screen-default.bmp (256×192).
//   Title → solved bars → Selection + slots → Mistakes (bottom).
constexpr int TITLE_Y = 8;
constexpr int SEL_LABEL_Y = 33; // default when no solved bars yet
constexpr int SEL_SLOT_W = 56;
constexpr int SEL_SLOT_H = 32;
constexpr int SEL_SLOT_R = 4;
constexpr int SEL_SLOT_GAP = 6;
constexpr int SEL_SLOT_MX = 9;
constexpr int MARGIN_X = 6;
constexpr int BAR_X = 6;
constexpr int BAR_W = 244;
constexpr int BAR_H = 28;
constexpr int BAR_GAP = 3;
constexpr int BAR_R = 4;
constexpr int MISTAKES_Y = 177; // 3px above prior bottom placement

// Build "WORD1, WORD2, WORD3, WORD4" for a category (in board position order).
void categoryWords(const Puzzle &p, int cat, char *out, int cap) {
	out[0] = 0;
	int written = 0;
	bool first = true;
	for (int pos = 0; pos < NUM_CARDS; pos++) {
		if (p.cardCategory[pos] != cat) continue;
		const char *sep = first ? "" : ", ";
		int n = snprintf(out + written, cap - written, "%s%s", sep, p.words[pos]);
		if (n < 0) break;
		written += n;
		if (written >= cap - 1) break;
		first = false;
	}
}

void drawMistakes(const Game &g, int y) {
	const char *label = "Mistakes Remaining:";
	constexpr font::Face face = font::Face::Small;
	const int textH = font::height(face);
	font::drawText(true, MARGIN_X, y, label, gfx::pal::dim, face, false);

	int remaining = MAX_MISTAKES - g.mistakes;
	const int outerR = 3;
	int x = MARGIN_X + font::textWidth(face, label) + 8;
	int cy = y + textH / 2;
	// Medium-gray dots matching the top-screen BMP (#999).
	const u16 dot = gfx::color(153, 153, 153);
	for (int i = 0; i < MAX_MISTAKES; i++) {
		int cx = x + i * (outerR * 2 + 4);
		if (i < remaining)
			gfx::fillCircle(true, cx, cy, outerR, dot);
		else
			gfx::fillRing(true, cx, cy, outerR, outerR - 1, gfx::pal::border);
	}
}

// Connections header (logo + wordmark) as a single grit GRF, centered.
// Returns the y just below the header row.
int drawTitle(int y) {
	int w = 0, h = 0;
	img::grfSize(header_grf, header_grf_size, &w, &h);
	int x = (gfx::SCREEN_W - w) / 2;
	if (x < 0) x = 0;
	img::blitGrf(true, x, y, header_grf, header_grf_size);
	return y + h;
}

// Selection chips pop in when a tile is picked and shrink out when removed. The
// pop is a hardware affine-scaled bitmap sprite: while a slot animates, the
// background shows the dotted placeholder and the sprite carries the chip image,
// scaled about its center (0.6->1.0 on pop-in, 1.0->0.6 on pop-out). The last
// word is retained so a removed chip can finish shrinking out.
static bool s_chipInit = false;
static bool s_chipLast[CARDS_PER_CATEGORY]; // target: filled?
static u32  s_chipStart[CARDS_PER_CATEGORY];
static char s_chipWord[CARDS_PER_CATEGORY][MAX_WORD_LEN];
static int  s_chipSpr[CARDS_PER_CATEGORY];  // top-engine sprite handle or -1
static int  s_chipCx[CARDS_PER_CATEGORY], s_chipCy[CARDS_PER_CATEGORY];
static bool s_chipGrow[CARDS_PER_CATEGORY]; // pop-in (true) vs pop-out (false)
static int  s_slotTextNum = 1, s_slotTextDen = 1;

// A slot is mid-pop while within CHIP_MS of a (nonzero) start timestamp.
static bool chipAnimating(int i) {
	return s_chipStart[i] != 0 &&
	       (int)(anim::now() - s_chipStart[i]) < anim::CHIP_MS;
}

// Paint a full-size chip (dark pill + light word) into the back-buffer at r.
void drawChip(const Rect &r, const char *word, int textNum, int textDen) {
	gfx::fillRoundRect(true, r.x, r.y, r.w, r.h, SEL_SLOT_R, gfx::pal::selected);
	ui::drawWordFitted(true, r, word, gfx::pal::selText, textNum, textDen, false);
}

void drawPlaceholder(const Rect &r) {
	gfx::roundRectDottedBorder(true, r.x, r.y, r.w, r.h, SEL_SLOT_R,
	                          gfx::pal::tileText, 4, 4);
}

// Draw the Selection label + slots starting at @p labelY. Returns y below the slots.
int drawSelection(const Game &g, int labelY) {
	constexpr font::Face face = font::Face::Small;
	font::drawText(true, MARGIN_X, labelY, "Selection", gfx::pal::dim, face, false);
	const int slotY = labelY + font::height(face) + 6;

	const char *sel[CARDS_PER_CATEGORY] = {nullptr, nullptr, nullptr, nullptr};
	int nsel = 0;
	for (int i = 0; i < g.boardCount && nsel < CARDS_PER_CATEGORY; i++) {
		int pos = g.board[i];
		if (g.selected[pos]) sel[nsel++] = g.puzzle.words[pos];
	}
	int textNum = 1, textDen = 1;
	ui::pickTileTextScale(g, &textNum, &textDen);
	s_slotTextNum = textNum;
	s_slotTextDen = textDen;

	if (!s_chipInit) {
		for (int i = 0; i < CARDS_PER_CATEGORY; i++) {
			s_chipLast[i] = false; s_chipStart[i] = 0; s_chipWord[i][0] = 0;
			s_chipSpr[i] = -1;
		}
		s_chipInit = true;
	}
	// While the solve transition owns the selected tiles, settle chips instantly
	// (they don't get a separate shrink-out - the tiles fly on the bottom screen).
	const bool solveActive = anim::solve().active;

	for (int i = 0; i < CARDS_PER_CATEGORY; i++) {
		Rect r = {SEL_SLOT_MX + i * (SEL_SLOT_W + SEL_SLOT_GAP), slotY,
		          SEL_SLOT_W, SEL_SLOT_H};
		bool filled = (sel[i] != nullptr);
		if (filled != s_chipLast[i]) {
			s_chipLast[i] = filled;
			s_chipStart[i] = solveActive ? 0 : anim::now();
			if (!solveActive) anim::keepAwake(anim::now() + anim::CHIP_MS);
		}
		if (filled) {
			strncpy(s_chipWord[i], sel[i], MAX_WORD_LEN - 1);
			s_chipWord[i][MAX_WORD_LEN - 1] = 0;
		}

		if (chipAnimating(i) && s_chipWord[i][0]) {
			// Background shows the placeholder; a scaled sprite owns the chip.
			u16 *gp; int st, aw, ah;
			int h = spr::create(true, r.w, r.h, &gp, &st, &aw, &ah);
			if (h >= 0) {
				int xoff = (aw - r.w) / 2, yoff = (ah - r.h) / 2;
				drawChip(r, s_chipWord[i], textNum, textDen); // render for capture
				gfx::copyRectTo(true, r.x, r.y, r.w, r.h, gp + yoff * st + xoff, st);
				s_chipSpr[i] = h;
				s_chipCx[i] = r.x + r.w / 2;
				s_chipCy[i] = r.y + r.h / 2;
				s_chipGrow[i] = filled;
			}
			drawPlaceholder(r); // restore bg under the sprite
		} else if (filled) {
			drawChip(r, s_chipWord[i], textNum, textDen);
		} else {
			drawPlaceholder(r);
		}
	}
	return slotY + SEL_SLOT_H;
}

int drawSolvedBars(const Game &g, int y, int yMax) {
	if (g.solvedCount <= 0) return y;

	int barH = BAR_H;
	int gap = BAR_GAP;
	int need = g.solvedCount * barH + (g.solvedCount - 1) * gap;
	int avail = yMax - y;
	if (avail < need && g.solvedCount > 0) {
		// Shrink bars so the stack still fits above the mistakes row.
		int fit = (avail - (g.solvedCount - 1) * 2) / g.solvedCount;
		if (fit < 18) fit = 18;
		barH = fit;
		gap = 2;
	}

	for (int k = 0; k < g.solvedCount; k++) {
		int cat = g.solvedOrder[k];

		// The newest bar fades in as the solve transition's flying tiles "arrive"
		// (phase 2); every settled bar is fully opaque.
		int a256 = 256;
		if (anim::solve().active && k == g.solvedCount - 1) {
			const anim::Solve &s = anim::solve();
			int el = (int)(anim::now() - s.start);
			a256 = (int)(anim::clamp01(anim::smooth((float)(el - anim::SOLVE_P1) /
			                                        (float)anim::SOLVE_P2)) * 256.0f);
		}
		u16 fill = gfx::blend(gfx::pal::category[cat], gfx::pal::bg, a256);
		u16 ink = gfx::blend(gfx::pal::black, gfx::pal::bg, a256);
		gfx::fillRoundRect(true, BAR_X, y, BAR_W, barH, BAR_R, fill);

		// Category title at Small face (native 12px); no fake bold.
		constexpr font::Face titleFace = font::Face::Small;
		const char *title = g.puzzle.categories[cat];
		int titleW = font::textWidth(titleFace, title);
		int titleY = y + 2;
		font::drawText(true, (gfx::SCREEN_W - titleW) / 2, titleY, title,
		               ink, titleFace, false);

		char words[128];
		categoryWords(g.puzzle, cat, words, sizeof(words));
		Rect wordsR = {BAR_X + 2, y + barH / 2 + 1, BAR_W - 4, barH / 2 - 2};
		if (wordsR.h < 8) wordsR.h = 8;
		// Word list renders smaller than the category title (3/4 of Small,
		// laddering down further only if the list still overflows).
		ui::drawLineFitted(true, wordsR, words, ink, 3, 4);
		y += barH + gap;
	}
	return y;
}

} // namespace

void ui::drawTop(const Game &g, const char *statusMsg, u16 statusColor) {
	gfx::clear(true, gfx::pal::bg);

	// Rebuild this engine's animation sprites from scratch for this full frame.
	spr::reset(true);
	if (s_chipInit)
		for (int i = 0; i < CARDS_PER_CATEGORY; i++) s_chipSpr[i] = -1;

	int y = drawTitle(TITLE_Y) + 6;

	// Completed rows sit above the Selection slots.
	const bool showSelection = (g.solvedCount < NUM_CATEGORIES);
	int selBudget = showSelection
		? (font::height(font::Face::Small) + 6 + SEL_SLOT_H + 8)
		: 0;
	y = drawSolvedBars(g, y, MISTAKES_Y - 6 - selBudget);

	if (showSelection) {
		int labelY = (g.solvedCount > 0) ? (y + 4) : SEL_LABEL_Y;
		drawSelection(g, labelY);
	}

	drawMistakes(g, MISTAKES_Y);

	if (statusMsg && statusMsg[0])
		gfx::drawTextCentered(true, gfx::SCREEN_W / 2, 168, statusMsg, statusColor, 1, true);
}

// Per-frame update of the top-screen animation sprites: scale each selection
// chip about its center (no background repaint).
void ui::animTop(const Game &g) {
	(void)g;
	if (!s_chipInit) return;
	for (int i = 0; i < CARDS_PER_CATEGORY; i++) {
		int h = s_chipSpr[i];
		if (h < 0) continue;
		float t = anim::smoothProg(s_chipStart[i], anim::CHIP_MS);
		if (t >= 1.0f) {
			if (s_chipGrow[i]) {
				// Hold the full chip until the settle render paints it on the bg
				// (the bg currently shows only the placeholder under this slot).
				spr::placeScaled(true, h, s_chipCx[i], s_chipCy[i], 100, 100, 15);
			} else {
				spr::hide(true, h); // pop-out done: bg placeholder is correct
				s_chipSpr[i] = -1;
			}
			continue;
		}
		// Pop-in grows 0.6->1.0; pop-out shrinks 1.0->0.6 and fades.
		float sc = s_chipGrow[i] ? (0.6f + 0.4f * t) : (1.0f - 0.4f * t);
		int alpha = s_chipGrow[i] ? 15 : (int)((1.0f - t) * 15.0f + 0.5f);
		if (alpha < 1) alpha = 1;
		spr::placeScaled(true, h, s_chipCx[i], s_chipCy[i],
		                 (int)(sc * 100.0f), 100, alpha);
	}
}

#include "ui.hpp"
#include "font.hpp"
#include "image.hpp"
#include "anim.hpp"
#include "sprite.hpp"

#include "toolInfo_grf.h"
#include "settings_grf.h"
#include "toolSyncOn_grf.h"
#include "toolSyncOff_grf.h"
#include "toolShuffleOn_grf.h"
#include "toolShuffleOff_grf.h"
#include "deselectActive_grf.h"
#include "deselectDisabled_grf.h"
#include "submitActive_grf.h"
#include "submitDisabled_grf.h"
#include "howtoTop_grf.h"
#include "howtoBottom_grf.h"
#include "stats_grf.h"
#include "share_grf.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

using gfx::pal::category;

namespace {

// --- Bottom-screen grid layout ---
// Word tiles match data/word-tile-{default,selected,focus}.bmp (55×32).
constexpr int GRID_MX = 12;  // centered: 4*55 + 3*GAP
constexpr int GRID_MY = 6;   // top margin
constexpr int TILE_W = 55;
constexpr int TILE_H = 32;
constexpr int GAP = 4;

// Target shape the four solved tiles merge into during the "correct guess"
// transition (matches the top-screen completed-row bar width so the composition
// reads as the same row moving up between the screens).
constexpr int BAR_BOTTOM_W = 244;
constexpr int BAR_BOTTOM_H = 28;

int lerpI(int a, int b, float t) { return a + (int)lroundf((float)(b - a) * t); }

// --- Toolbar (Info, Settings, Sync, Shuffle, Deselect, Submit) ---
// Bare glyphs on the left, framed chips on the right (Figma strip node 8:207,
// re-spaced for the sixth control).
constexpr int BTN_H = 32;      // touch hit height
constexpr int BTN_W = 40;      // touch hit width
constexpr int BTN_Y = 158;
constexpr int BTN_CX[ui::BTN_COUNT] = {22, 62, 102, 142, 188, 232};

// Stats/finished toolbar centers (Info, Settings, Sync, Stats, Share). The
// three left glyphs stay put relative to the play toolbar; Stats + Share sit on
// the right so the row reads as data/toolbar-stats.png.
constexpr int STAT_CX[ui::STAT_COUNT] = {22, 62, 102, 188, 232};
// Deselect/Submit chips match data/{deselect,submit}-*.bmp (20×20).
constexpr int FRAME_W = 20;
constexpr int FRAME_H = 20;
constexpr int FRAME_R = 3;
constexpr int FRAME_BORDER = 2;
constexpr int DESELECT_ACTIVE_BORDER = 3;

// Corner radii used across the touch screen (tiles ≈ r=3 from BMP silhouette).
constexpr int TILE_R = 3;
constexpr int BTN_R = 8;

// Horizontal padding inside a tile / selection slot. Tightened so the fitted
// tile text can render a touch larger (see kFracs / TILE_TRACK below).
constexpr int TEXT_PAD_X = 4;

// Extra tracking (px) inserted between tile letters for a slightly airier word.
constexpr int TILE_TRACK = 1;

// Overflow ladder against Face::Small. {9,8} lets short words render a little
// above native; the rest ladder down when a word would overflow the tile.
constexpr int kFracs[][2] = {
	{9, 8}, {1, 1}, {5, 6}, {4, 5}, {3, 4}, {2, 3}, {1, 2},
};
constexpr font::Face kTileFace = font::Face::Small;

int glyphCount(const char *s) {
	int n = 0;
	for (const char *p = s; *p; p++) n++;
	return n;
}

// Fitted tile-text width including inter-letter tracking (n-1 gaps).
int tileTextWidth(const char *s, int num, int den) {
	int w = font::textWidthFrac(kTileFace, s, num, den);
	int n = glyphCount(s);
	if (n > 1) w += TILE_TRACK * (n - 1);
	return w;
}

bool lineFits(const char *s, int inner, int num, int den) {
	return tileTextWidth(s, num, den) <= inner;
}

bool wordFits(const char *word, int inner, int num, int den) {
	const char *sp = strchr(word, ' ');
	if (sp && *(sp + 1)) {
		char a[MAX_WORD_LEN];
		int alen = (int)(sp - word);
		if (alen > MAX_WORD_LEN - 1) alen = MAX_WORD_LEN - 1;
		memcpy(a, word, alen);
		a[alen] = 0;
		return lineFits(a, inner, num, den) && lineFits(sp + 1, inner, num, den);
	}
	return lineFits(word, inner, num, den);
}

void chooseScale(const char *s, int inner, int prefNum, int prefDen, int *num, int *den) {
	// Try the caller's preferred scale first, then the standard ladder.
	if (lineFits(s, inner, prefNum, prefDen)) {
		*num = prefNum;
		*den = prefDen;
		return;
	}
	for (unsigned i = 0; i < sizeof(kFracs) / sizeof(kFracs[0]); i++) {
		int n = kFracs[i][0], d = kFracs[i][1];
		if (n * prefDen > prefNum * d) continue; // only same size or smaller
		if (lineFits(s, inner, n, d)) {
			*num = n;
			*den = d;
			return;
		}
	}
	*num = 1;
	*den = 2;
}

void drawLineAt(bool top, const Rect &r, const char *s, u16 color, int y, int num, int den) {
	if (!s || !s[0]) return;
	int tw = tileTextWidth(s, num, den);
	int x = r.x + (r.w - tw) / 2;
	// Draw per glyph so the extra tracking is applied uniformly (fixed-point
	// x accumulator matches font::drawTextFrac to avoid gap jitter).
	int cx256 = x * 256;
	for (const char *p = s; *p; p++) {
		int px = (cx256 + 128) / 256;
		if (num == den)
			font::drawChar(top, px, y, *p, color, kTileFace, 1, false);
		else
			font::drawCharFrac(top, px, y, *p, color, kTileFace, num, den, false);
		cx256 += (font::charAdvance(kTileFace, *p) * num * 256) / den + TILE_TRACK * 256;
	}
}

} // namespace

Rect ui::tileRect(int boardIndex) {
	int row = boardIndex / 4;
	int col = boardIndex % 4;
	Rect r;
	r.x = GRID_MX + col * (TILE_W + GAP);
	r.y = GRID_MY + row * (TILE_H + GAP);
	r.w = TILE_W;
	r.h = TILE_H;
	return r;
}

Rect ui::buttonRect(Btn b) {
	if (b < 0 || b >= BTN_COUNT) return {0, 0, 0, 0};
	int cx = BTN_CX[(int)b];
	return {cx - BTN_W / 2, BTN_Y, BTN_W, BTN_H};
}

const char *ui::buttonLabel(Btn b) {
	switch (b) {
		case BTN_INFO:     return "Info";
		case BTN_SETTINGS: return "Settings";
		case BTN_SYNC:     return "Sync";
		case BTN_SHUFFLE:  return "Shuffle";
		case BTN_DESELECT: return "Deselect";
		case BTN_SUBMIT:   return "Submit";
		default:           return "";
	}
}

void ui::drawLineFitted(bool top, const Rect &r, const char *s, u16 color, int scaleNum, int scaleDen,
                        bool bold) {
	(void)bold;
	int inner = r.w - 2 * TEXT_PAD_X;
	if (inner < 8) inner = r.w - 4;
	int num = scaleNum, den = scaleDen;
	chooseScale(s, inner, scaleNum, scaleDen, &num, &den);
	int th = font::heightFrac(kTileFace, num, den);
	drawLineAt(top, r, s, color, r.y + (r.h - th) / 2, num, den);
}

void ui::drawWordFitted(bool top, const Rect &r, const char *word, u16 color, int scaleNum, int scaleDen,
                        bool bold) {
	(void)bold;
	int inner = r.w - 2 * TEXT_PAD_X;
	if (inner < 8) inner = r.w - 4;
	int num = scaleNum, den = scaleDen;

	const char *sp = strchr(word, ' ');
	if (sp && *(sp + 1)) {
		char a[MAX_WORD_LEN];
		int alen = (int)(sp - word);
		if (alen > MAX_WORD_LEN - 1) alen = MAX_WORD_LEN - 1;
		memcpy(a, word, alen);
		a[alen] = 0;
		bool found = false;
		if (wordFits(word, inner, scaleNum, scaleDen)) {
			num = scaleNum;
			den = scaleDen;
			found = true;
		} else {
			for (unsigned i = 0; i < sizeof(kFracs) / sizeof(kFracs[0]); i++) {
				int n = kFracs[i][0], d = kFracs[i][1];
				if (n * scaleDen > scaleNum * d) continue;
				if (wordFits(word, inner, n, d)) {
					num = n;
					den = d;
					found = true;
					break;
				}
			}
		}
		if (!found) { num = 1; den = 2; }

		int th = font::heightFrac(kTileFace, num, den);
		int y0 = r.y + (r.h - (th * 2 + 1)) / 2;
		drawLineAt(top, r, a, color, y0, num, den);
		drawLineAt(top, r, sp + 1, color, y0 + th + 1, num, den);
	} else {
		chooseScale(word, inner, scaleNum, scaleDen, &num, &den);
		int th = font::heightFrac(kTileFace, num, den);
		drawLineAt(top, r, word, color, r.y + (r.h - th) / 2, num, den);
	}
}

int ui::tileCornerRadius() { return TILE_R; }

void ui::pickTileTextScale(const Game &g, int *scaleNum, int *scaleDen) {
	const int inner = TILE_W - 2 * TEXT_PAD_X;
	for (unsigned i = 0; i < sizeof(kFracs) / sizeof(kFracs[0]); i++) {
		int n = kFracs[i][0], d = kFracs[i][1];
		bool ok = true;
		for (int b = 0; b < g.boardCount; b++) {
			if (!wordFits(g.puzzle.words[g.board[b]], inner, n, d)) {
				ok = false;
				break;
			}
		}
		if (ok) {
			*scaleNum = n;
			*scaleDen = d;
			return;
		}
	}
	*scaleNum = 1;
	*scaleDen = 2;
}

int ui::hitTile(const Game &g, int px, int py) {
	for (int i = 0; i < g.boardCount; i++) {
		if (tileRect(i).contains(px, py))
			return i;
	}
	return -1;
}

int ui::hitButton(int px, int py) {
	for (int b = 0; b < BTN_COUNT; b++) {
		if (buttonRect((Btn)b).contains(px, py))
			return b;
	}
	return -1;
}

// --- Toolbar icons (grit assets blitted into the RGB555 back-buffer) ---
// Each control is a pre-composited GRF bitmap authored by tools/gen_gfx.py and
// converted with grit. Blitting keeps toolbar chrome on the same single Bmp16
// compositor as gameplay, so gfx::flip and lid-close sleep/wake are unchanged.
static void blitTool(const Rect &hit, const void *grf, u32 len) {
	img::blitGrfCentered(false, hit.x + hit.w / 2, hit.y + hit.h / 2, grf, len);
}

// Contrast-safe tile text: pick dark or light ink for the (possibly mid-morph)
// fill so the word never washes out at the crossfade midpoint.
static u16 tileTextFor(u16 fill) {
	int r = fill & 31, g = (fill >> 5) & 31, b = (fill >> 10) & 31;
	int lum = (r * 77 + g * 151 + b * 28) >> 8; // 0..31
	return lum > 15 ? gfx::pal::tileText : gfx::pal::selText;
}

// Per-position select/deselect morph state (keyed by board position 0..15 so it
// survives shuffles). The transition is a hardware crossfade: the background
// holds the *end* state, and a bitmap sprite carrying the *start* image is faded
// out over it via OAM alpha (see anim / sprite modules). s_tileSpr[pos] is the
// bottom-engine sprite handle for a tile currently mid-crossfade (-1 = none).
static bool s_selInit = false;
static bool s_selLast[NUM_CARDS];
static u32  s_selStart[NUM_CARDS];
static int  s_tileSpr[NUM_CARDS];
static Rect s_tileSprRect[NUM_CARDS];

// Solve-transition sprites (bottom engine): each solved tile is an A sprite (dark
// tile + word) crossfading into a B sprite (category-colored bar segment); the
// remaining tiles slide from their old slots to the collapsed layout.
static int s_solvedA[CARDS_PER_CATEGORY];
static int s_solvedB[CARDS_PER_CATEGORY];
static int s_remainSpr[NUM_CARDS];

// Toolbar button crossfade: when a control changes state (submit enabling,
// deselect arming, shuffle/sync toggling) the previous icon is captured into a
// sprite and faded out over the new one on the background.
static bool        s_btnInit = false;
static const void *s_btnLastGrf[ui::BTN_COUNT];
static u32         s_btnLastLen[ui::BTN_COUNT];
static int         s_btnSpr[ui::BTN_COUNT];
static u32         s_btnStart[ui::BTN_COUNT];
static Rect        s_btnRect[ui::BTN_COUNT];

static int captureSprite(const Rect &r, int w, int h);

static void ensureTileCache() {
	if (s_selInit) return;
	for (int i = 0; i < NUM_CARDS; i++) {
		s_selLast[i] = false; s_selStart[i] = 0; s_tileSpr[i] = -1;
	}
	s_selInit = true;
}

// Paint one tile face (fill + contrast-safe fitted word) into the back-buffer.
static void paintTile(const Rect &r, const char *word, u16 fill, int num, int den) {
	gfx::fillRoundRect(false, r.x, r.y, r.w, r.h, TILE_R, fill);
	ui::drawWordFitted(false, r, word, tileTextFor(fill), num, den, false);
}

// Draw a single board tile in its settled (end) state. If the tile just changed
// selection, first render the previous (start) state, capture it into a bitmap
// sprite, and start a crossfade so the change animates on hardware.
static void drawTileAt(const Game &g, int i, int textNum, int textDen) {
	Rect r = ui::tileRect(i);
	int pos = g.board[i];
	bool sel = g.selected[pos];
	const char *word = g.puzzle.words[pos];
	u16 endFill = sel ? gfx::pal::selected : gfx::pal::tile;

	if (sel != s_selLast[pos]) {
		// Capture the outgoing (start) look into a sprite, then start the fade.
		u16 startFill = sel ? gfx::pal::tile : gfx::pal::selected;
		paintTile(r, word, startFill, textNum, textDen);
		u16 *gp; int st, aw, ah;
		int h = spr::create(false, r.w, r.h, &gp, &st, &aw, &ah);
		if (h >= 0) {
			gfx::copyRectTo(false, r.x, r.y, r.w, r.h, gp, st);
			s_tileSpr[pos] = h;
			s_tileSprRect[pos] = r;
			spr::place(false, h, r.x, r.y, 15);
		}
		s_selLast[pos] = sel;
		s_selStart[pos] = anim::now();
		anim::keepAwake(anim::now() + anim::TILE_MS);
	}

	paintTile(r, word, endFill, textNum, textDen);
}

static void drawTilesNormal(const Game &g, int textNum, int textDen) {
	for (int i = 0; i < g.boardCount; i++)
		drawTileAt(g, i, textNum, textDen);
}

// Per-frame update of the tile crossfade sprites (no background repaint). Fades
// each active start-image sprite out; the settled tile already sits on the bg.
static void animSolveBottom();

void ui::animBottom(const Game &g) {
	(void)g;
	if (anim::solve().active) {
		animSolveBottom();
		return;
	}
	for (int pos = 0; pos < NUM_CARDS; pos++) {
		int h = s_tileSpr[pos];
		if (h < 0) continue;
		float t = anim::smoothProg(s_selStart[pos], anim::TILE_MS);
		int a = (int)((1.0f - t) * 15.0f + 0.5f);
		if (a <= 0) {
			spr::hide(false, h);
			s_tileSpr[pos] = -1;
		} else {
			spr::place(false, h, s_tileSprRect[pos].x, s_tileSprRect[pos].y, a);
		}
	}
	for (int b = 0; b < BTN_COUNT; b++) {
		int h = s_btnSpr[b];
		if (h < 0) continue;
		float t = anim::smoothProg(s_btnStart[b], anim::BTN_MS);
		int a = (int)((1.0f - t) * 15.0f + 0.5f);
		if (a <= 0) {
			spr::hide(false, h);
			s_btnSpr[b] = -1;
		} else {
			spr::place(false, h, s_btnRect[b].x, s_btnRect[b].y, a);
		}
	}
}

// Current GRF (and its size) for a toolbar control given game state.
static void toolbarGrf(const Game &g, ui::Btn b, bool syncing, const void **grf, u32 *len) {
	bool playing = (g.status == GameStatus::Playing);
	bool canDeselect = playing && g.selectedCount > 0;
	bool canSubmit = playing && g.selectedCount == CARDS_PER_CATEGORY;
	switch (b) {
		case ui::BTN_INFO:  *grf = toolInfo_grf;  *len = toolInfo_grf_size;  break;
		case ui::BTN_SETTINGS: *grf = settings_grf; *len = settings_grf_size; break;
		case ui::BTN_SYNC:
			*grf = syncing ? toolSyncOff_grf : toolSyncOn_grf;
			*len = syncing ? toolSyncOff_grf_size : toolSyncOn_grf_size;
			break;
		case ui::BTN_SHUFFLE:
			*grf = playing ? toolShuffleOn_grf : toolShuffleOff_grf;
			*len = playing ? toolShuffleOn_grf_size : toolShuffleOff_grf_size;
			break;
		case ui::BTN_DESELECT:
			*grf = canDeselect ? deselectActive_grf : deselectDisabled_grf;
			*len = canDeselect ? deselectActive_grf_size : deselectDisabled_grf_size;
			break;
		case ui::BTN_SUBMIT:
			*grf = canSubmit ? submitActive_grf : submitDisabled_grf;
			*len = canSubmit ? submitActive_grf_size : submitDisabled_grf_size;
			break;
		default: *grf = nullptr; *len = 0; break;
	}
}

// Bar-segment geometry the four solved tiles gather into.
static int solveSeg() { return BAR_BOTTOM_W / CARDS_PER_CATEGORY; }
static int solveBarX() { return (gfx::SCREEN_W - BAR_BOTTOM_W) / 2; }

// Capture a just-rendered w×h region of the back-buffer into a fresh bottom-engine
// bitmap sprite, then erase that region back to the page. Returns the handle.
static int captureSprite(const Rect &r, int w, int h) {
	u16 *gp; int st, aw, ah;
	int handle = spr::create(false, w, h, &gp, &st, &aw, &ah);
	if (handle >= 0) gfx::copyRectTo(false, r.x, r.y, w, h, gp, st);
	gfx::fillRect(false, r.x, r.y, w, h, gfx::pal::bg);
	return handle;
}

// "Correct guess" transition (bottom): build the hardware sprites once. The four
// solved tiles become an A (dark tile + word) and B (category bar segment) sprite
// pair that crossfade; the remaining tiles each become a sprite that slides to its
// collapsed slot. The background grid stays blank while these animate.
static void buildSolveSprites(const Game &g) {
	const anim::Solve &s = anim::solve();
	int num = 1, den = 1;
	ui::pickTileTextScale(g, &num, &den);
	const int seg = solveSeg();
	const u16 cat = gfx::pal::category[s.cat];

	for (int k = 0; k < CARDS_PER_CATEGORY; k++) {
		Rect ra = {s.fromX[k], s.fromY[k], TILE_W, TILE_H};
		paintTile(ra, s.word[k], gfx::pal::selected, num, den);
		s_solvedA[k] = captureSprite(ra, TILE_W, TILE_H);

		Rect rb = {s.fromX[k], s.fromY[k], seg, BAR_BOTTOM_H};
		gfx::fillRoundRect(false, rb.x, rb.y, seg, BAR_BOTTOM_H, TILE_R, cat);
		s_solvedB[k] = captureSprite(rb, seg, BAR_BOTTOM_H);
	}
	for (int m = 0; m < s.nRemain; m++) {
		Rect r = {s.remFromX[m], s.remFromY[m], TILE_W, TILE_H};
		paintTile(r, s.remWord[m], gfx::pal::tile, num, den);
		s_remainSpr[m] = captureSprite(r, TILE_W, TILE_H);
	}
}

// Per-frame placement of the solve sprites (no background repaint).
static void animSolveBottom() {
	const anim::Solve &s = anim::solve();
	int el = (int)(anim::now() - s.start);
	float e1 = anim::clamp01(anim::smooth((float)el / (float)anim::SOLVE_P1));
	float e2 = anim::clamp01(anim::smooth((float)(el - anim::SOLVE_P1) / (float)anim::SOLVE_P2));

	const int seg = solveSeg();
	const int barX = solveBarX();
	const int barCy = GRID_MY + BAR_BOTTOM_H / 2;
	const int rise = (int)(e2 * 72.0f);

	for (int k = 0; k < CARDS_PER_CATEGORY; k++) {
		int barCx = barX + k * seg + seg / 2;
		int slotCx = s.fromX[k] + TILE_W / 2;
		int slotCy = s.fromY[k] + TILE_H / 2;
		int cx = lerpI(slotCx, barCx, e1);
		int cy = lerpI(slotCy, barCy, e1) - rise;

		int aA = (int)((1.0f - e1) * 15.0f + 0.5f);
		if (s_solvedA[k] >= 0) {
			if (aA >= 1) spr::place(false, s_solvedA[k], cx - TILE_W / 2, cy - TILE_H / 2, aA);
			else spr::hide(false, s_solvedA[k]);
		}
		float fB = (e1 < 1.0f) ? e1 : (1.0f - e2);
		int aB = (int)(fB * 15.0f + 0.5f);
		if (s_solvedB[k] >= 0) {
			if (aB >= 1) spr::place(false, s_solvedB[k], cx - seg / 2, cy - BAR_BOTTOM_H / 2, aB);
			else spr::hide(false, s_solvedB[k]);
		}
	}
	for (int m = 0; m < s.nRemain; m++) {
		if (s_remainSpr[m] < 0) continue;
		int x = lerpI(s.remFromX[m], s.remToX[m], e2);
		int y = lerpI(s.remFromY[m], s.remToY[m], e2);
		spr::place(false, s_remainSpr[m], x, y, 15);
	}
}

void ui::drawToast(bool top, int centerY, const char *msg) {
	const int h = 24;
	int w = gfx::textWidth(msg, 1) + 20;
	if (w > 240) w = 240;
	int x = (gfx::SCREEN_W - w) / 2;
	int y = centerY - h / 2;
	gfx::roundPanel(top, x, y, w, h, 12, gfx::pal::ink, gfx::pal::ink, 1);
	// Vertically center the glyphs in the pill (drawTextCentered's y is the top).
	int textH = font::height(font::Face::Regular);
	gfx::drawTextCentered(top, gfx::SCREEN_W / 2, y + (h - textH) / 2, msg,
	                      gfx::pal::white, 1, true);
}

void ui::drawBottom(const Game &g, const char *toast, bool syncing) {
	gfx::clear(false, gfx::pal::bg);
	ensureTileCache();

	// Rebuild this engine's animation sprites from scratch for this full frame.
	spr::reset(false);
	for (int i = 0; i < NUM_CARDS; i++) { s_tileSpr[i] = -1; s_remainSpr[i] = -1; }
	for (int k = 0; k < CARDS_PER_CATEGORY; k++) { s_solvedA[k] = -1; s_solvedB[k] = -1; }
	for (int b = 0; b < BTN_COUNT; b++) s_btnSpr[b] = -1;

	if (anim::solve().active) {
		// Grid stays blank; the solved/remaining tiles are all hardware sprites.
		buildSolveSprites(g);
		animSolveBottom(); // place at the current instant so frame 0 isn't blank
		// Keep the select cache in sync with the (now cleared) selection so the
		// grid doesn't animate a phantom deselect once it returns to normal.
		for (int i = 0; i < NUM_CARDS; i++) s_selLast[i] = g.selected[i];
	} else {
		// One Small-face size for every tile (native 12px, frac only if needed).
		int textNum = 1, textDen = 1;
		pickTileTextScale(g, &textNum, &textDen);
		drawTilesNormal(g, textNum, textDen);
	}

	for (int b = 0; b < BTN_COUNT; b++) {
		const void *grf = nullptr;
		u32 len = 0;
		toolbarGrf(g, (Btn)b, syncing, &grf, &len);
		Rect hit = buttonRect((Btn)b);
		if (s_btnInit && grf != s_btnLastGrf[b] && s_btnLastGrf[b] &&
		    !anim::solve().active) {
			// Capture the outgoing icon, then fade it out over the new one.
			blitTool(hit, s_btnLastGrf[b], s_btnLastLen[b]);
			s_btnSpr[b] = captureSprite(hit, hit.w, hit.h);
			s_btnRect[b] = hit;
			s_btnStart[b] = anim::now();
			anim::keepAwake(anim::now() + anim::BTN_MS);
		}
		blitTool(hit, grf, len);
		s_btnLastGrf[b] = grf;
		s_btnLastLen[b] = len;
	}
	s_btnInit = true;

	// Transient game toasts now appear on the top screen (see render()); the
	// bottom screen only shows the modal "Syncing..." indicator.
	if (syncing)
		ui::drawToast(false, 102, "Syncing...");
	(void)toast;
}

// --- How to play ---

Rect ui::helpCloseRect() {
	return {228, 4, 24, 24};
}

bool ui::hitHelpClose(int px, int py) {
	return helpCloseRect().contains(px, py);
}

// How-to-play is a pair of pre-rendered grit screens (see tools/gen_gfx.py):
// howtoTop bakes the title + rules, howtoBottom bakes the examples, color legend
// and the close X. Both are blitted into the Bmp16 back-buffers so the overlay
// rides the same compositor / flip / sleep path as gameplay. The close hit
// target stays ui::helpCloseRect (its glyph is baked into howtoBottom).
void ui::drawHelp() {
	gfx::clear(true, gfx::pal::bg);
	gfx::clear(false, gfx::pal::bg);
	img::blitGrf(true, 0, 0, howtoTop_grf, howtoTop_grf_size);
	img::blitGrf(false, 0, 0, howtoBottom_grf, howtoBottom_grf_size);
}

// --- Settings panel ---

namespace {

constexpr int SET_ROW_X = 10;
constexpr int SET_ROW_W = 236;
constexpr int SET_ROW_H = 22;
constexpr int SET_ROW_Y0 = 34;
constexpr int SET_ROW_PITCH = 25;

const char *settingsLabel(int i) {
	switch (i) {
		case ui::SET_PLAY_PREV:  return "Play a previous puzzle";
		case ui::SET_BACK_TODAY: return "Back to today's puzzle";
		case ui::SET_VIEW_STATS: return "View statistics";
		case ui::SET_AUTOSYNC:   return "Auto-sync on launch";
		case ui::SET_CONFIRM:    return "Confirm before submit";
		case ui::SET_RESET:      return "Reset statistics";
		default:                 return "";
	}
}

void drawTogglePill(const Rect &row, bool on) {
	const int pw = 40, ph = 16;
	int px = row.x + row.w - pw - 6;
	int py = row.y + (row.h - ph) / 2;
	u16 fill = on ? gfx::pal::submit : gfx::pal::disabled;
	u16 tcol = on ? gfx::pal::white : gfx::pal::dim;
	gfx::fillRoundRect(false, px, py, pw, ph, 8, fill);
	int th = font::height(font::Face::Regular);
	gfx::drawTextCentered(false, px + pw / 2, py + (ph - th) / 2, on ? "On" : "Off", tcol, 1, true);
}

} // namespace

Rect ui::settingsCloseRect() { return {228, 4, 24, 24}; }
bool ui::hitSettingsClose(int px, int py) { return settingsCloseRect().contains(px, py); }

Rect ui::settingsRowRect(int i) {
	return {SET_ROW_X, SET_ROW_Y0 + i * SET_ROW_PITCH, SET_ROW_W, SET_ROW_H};
}

int ui::hitSettingsRow(int px, int py) {
	for (int i = 0; i < SET_COUNT; i++)
		if (settingsRowRect(i).contains(px, py)) return i;
	return -1;
}

void ui::drawSettings(const SettingsView &v, int focus) {
	// Top screen: hero title + which puzzle is loaded.
	gfx::clear(true, gfx::pal::bg);
	gfx::drawTextCentered(true, gfx::SCREEN_W / 2, 66, "Settings", gfx::pal::ink, 2, true);
	if (v.modeLabel && v.modeLabel[0])
		gfx::drawTextCentered(true, gfx::SCREEN_W / 2, 104, v.modeLabel, gfx::pal::dim, 1, false);

	// Bottom screen: option list + close affordance.
	gfx::clear(false, gfx::pal::bg);
	Rect cx = settingsCloseRect();
	gfx::strokeLine(false, cx.x + 7, cx.y + 7, cx.x + cx.w - 7, cx.y + cx.h - 7, 1.3f, gfx::pal::ink);
	gfx::strokeLine(false, cx.x + 7, cx.y + cx.h - 7, cx.x + cx.w - 7, cx.y + 7, 1.3f, gfx::pal::ink);

	int th = font::height(font::Face::Regular);
	for (int i = 0; i < SET_COUNT; i++) {
		Rect r = settingsRowRect(i);
		bool disabled = (i == SET_BACK_TODAY && !v.archiveActive);
		u16 col = disabled ? gfx::pal::disText : gfx::pal::ink;
		gfx::drawText(false, r.x + 8, r.y + (r.h - th) / 2, settingsLabel(i), col, 1, false);
		if (i == SET_AUTOSYNC) drawTogglePill(r, v.autoSync);
		else if (i == SET_CONFIRM) drawTogglePill(r, v.confirmSubmit);
	}

	if (focus >= 0 && focus < SET_COUNT) {
		Rect r = settingsRowRect(focus);
		gfx::roundRectBorder(false, r.x, r.y, r.w, r.h, 6, gfx::pal::focus, 2);
	}
}

// --- End-of-game stats screen ---

Rect ui::statButtonRect(StatBtn b) {
	if (b < 0 || b >= STAT_COUNT) return {0, 0, 0, 0};
	int cx = STAT_CX[(int)b];
	return {cx - BTN_W / 2, BTN_Y, BTN_W, BTN_H};
}

const char *ui::statButtonLabel(StatBtn b) {
	switch (b) {
		case STAT_INFO:     return "Info";
		case STAT_SETTINGS: return "Settings";
		case STAT_SYNC:     return "Sync";
		case STAT_STATS:    return "Stats";
		case STAT_SHARE:    return "Share";
		default:            return "";
	}
}

static void drawStatCol(int cx, int value, const char *label, bool percent) {
	char num[8];
	if (percent) snprintf(num, sizeof(num), "%d%%", value);
	else         snprintf(num, sizeof(num), "%d", value);
	// Numbers at 1×; labels stay full size.
	gfx::drawTextCentered(false, cx, 36, num, gfx::pal::ink, 1, true);
	gfx::drawTextCentered(false, cx, 56, label, gfx::pal::dim, 1);
}

// Recap of every counted guess as a grid of category-colored squares — the same
// layout encoded in the share text / QR (g.guessRows[row][col] = category 0..3).
// Fitted into the band between the stat columns and the toolbar row.
static void drawGuessGrid(const Game &g, int top, int bottom) {
	int rows = g.guessCount;
	if (rows <= 0) return;
	const int cols = CARDS_PER_CATEGORY;
	const int gap = 4;
	int availH = bottom - top;
	int sqW = (150 - (cols - 1) * gap) / cols;   // width-limited
	int sqH = (availH - (rows - 1) * gap) / rows; // height-limited
	int sq = sqW < sqH ? sqW : sqH;
	if (sq > 22) sq = 22;
	if (sq < 6) sq = 6;

	int gridW = cols * sq + (cols - 1) * gap;
	int gridH = rows * sq + (rows - 1) * gap;
	int ox = (gfx::SCREEN_W - gridW) / 2;
	int oy = top + (availH - gridH) / 2;
	int r = 1;

	for (int row = 0; row < rows; row++) {
		for (int col = 0; col < cols; col++) {
			int cat = g.guessRows[row][col];
			u16 c = (cat >= 0 && cat < NUM_CATEGORIES) ? gfx::pal::category[cat]
			                                           : gfx::pal::panel;
			gfx::fillRoundRect(false, ox + col * (sq + gap), oy + row * (sq + gap),
			                   sq, sq, r, c);
		}
	}
}

void ui::drawStats(const Game &g, const Stats &s, int focus, bool archive) {
	gfx::clear(false, gfx::pal::bg);

	bool won = (g.status == GameStatus::Won);
	const char *title = (g.status == GameStatus::Playing) ? "Statistics"
	                    : (won ? "Solved!" : "Next time!");
	gfx::drawTextCentered(false, gfx::SCREEN_W / 2, 8, title, gfx::pal::ink, 1, true);

	if (archive) {
		// Practice puzzles never touch lifetime stats; show a clear note instead
		// of the counters so the numbers can't be misread as "this counted".
		gfx::drawTextCentered(false, gfx::SCREEN_W / 2, 34, "Practice - not counted",
		                      gfx::pal::dim, 1, false);
	} else {
		int winPct = s.played > 0 ? (s.wins * 100 + s.played / 2) / s.played : 0;
		drawStatCol(32, s.played, "Played", false);
		drawStatCol(96, winPct, "Win", true);
		drawStatCol(160, s.streak, "Streak", false);
		drawStatCol(224, s.maxStreak, "Best", false);
	}

	// Guess recap grid between the stats and the toolbar row (BTN_Y = 158).
	drawGuessGrid(g, 72, BTN_Y - 8);

	// Toolbar row (Info, Settings, Sync, Stats, Share) — all bare glyphs.
	blitTool(statButtonRect(STAT_INFO), toolInfo_grf, toolInfo_grf_size);
	blitTool(statButtonRect(STAT_SETTINGS), settings_grf, settings_grf_size);
	blitTool(statButtonRect(STAT_SYNC), toolSyncOn_grf, toolSyncOn_grf_size);
	blitTool(statButtonRect(STAT_STATS), stats_grf, stats_grf_size);
	blitTool(statButtonRect(STAT_SHARE), share_grf, share_grf_size);

	if (focus >= 0 && focus < STAT_COUNT) {
		Rect r = statButtonRect((StatBtn)focus);
		// Toolbar-style halo around the bare glyph hit target.
		gfx::roundRectBorder(false, r.x + 4, r.y + 2, r.w - 8, r.h - 4, 8,
		                     gfx::pal::focus, 2);
	}
}

int ui::hitStatButton(int px, int py) {
	for (int b = 0; b < STAT_COUNT; b++) {
		if (statButtonRect((StatBtn)b).contains(px, py))
			return b;
	}
	return -1;
}

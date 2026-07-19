// Dual-screen UI: layout, drawing, and touch hit-testing.
#pragma once

#include "game.hpp"
#include "gfx.hpp"
#include "storage.hpp"

struct Rect {
	int x, y, w, h;
	bool contains(int px, int py) const {
		return px >= x && px < x + w && py >= y && py < y + h;
	}
};

namespace ui {

// Buttons available during play, left-to-right (Share lives on the stats
// screen instead). Rendered as a single row of icon buttons.
enum Btn {
	BTN_INFO = 0,
	BTN_SYNC,
	BTN_STATS,
	BTN_SHUFFLE,
	BTN_DESELECT,
	BTN_SUBMIT,
	BTN_COUNT
};

// Buttons on the end-of-game stats screen (enum order = left-to-right focus/nav
// order). Info + Sync sit on the left of the toolbar row; Share on the right.
enum StatBtn {
	STAT_INFO = 0,
	STAT_SYNC,
	STAT_SHARE,
	STAT_COUNT
};

// Bottom-screen layout.
Rect tileRect(int boardIndex);
Rect buttonRect(Btn b);
const char *buttonLabel(Btn b);

// A single line fitted in a rect using Face::Small (1/1 native, then frac).
void drawLineFitted(bool top, const Rect &r, const char *s, u16 color,
                    int scaleNum = 1, int scaleDen = 1, bool bold = false);

// Word text fitted (centered, wrapped on a space) using Face::Small.
void drawWordFitted(bool top, const Rect &r, const char *word, u16 color,
                    int scaleNum = 1, int scaleDen = 1, bool bold = false);

// Largest scale (≤ preferred) that fits every board word in a tile.
void pickTileTextScale(const Game &g, int *scaleNum, int *scaleDen);

// Corner radius of bottom-screen word tiles (for focus ring matching).
int tileCornerRadius();

// Top screen: title, selection slots, solved categories, mistakes (bottom).
// @p statusColor is used for the optional footer status line.
void drawTop(const Game &g, const char *statusMsg, u16 statusColor = gfx::pal::ink);

// Bottom screen: 4x4 grid + control/sync buttons, plus a transient toast.
void drawBottom(const Game &g, const char *toast, bool syncing);

// Transient toast pill (dark rounded panel + centered white text), drawn on the
// given screen and vertically centered on @p centerY.
void drawToast(bool top, int centerY, const char *msg);

// Per-frame animation update (no background repaint): advance the hardware
// sprite overlays built during the last drawTop/drawBottom. Call each frame while
// anim::busy(), then flush with spr::update().
void animTop(const Game &g);
void animBottom(const Game &g);

// Hit-testing on the bottom screen. Return values are board slot / Btn or -1.
int hitTile(const Game &g, int px, int py);
int hitButton(int px, int py);

// How-to-play help overlay (both screens). Close control is on the bottom screen.
void drawHelp();
Rect helpCloseRect();
bool hitHelpClose(int px, int py);

// End-of-game stats screen (bottom): statistics + Share / Sync / Board buttons.
// `focus` highlights a StatBtn (or -1 for none).
Rect statButtonRect(StatBtn b);
const char *statButtonLabel(StatBtn b);
void drawStats(const Game &g, const Stats &s, int focus);
int hitStatButton(int px, int py);

} // namespace ui

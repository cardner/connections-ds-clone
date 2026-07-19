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

// Buttons available during play, left-to-right (Stats + Share live on the stats
// screen instead). Rendered as a single row of icon buttons. Matches
// data/toolbar-main.png: Info, Settings, Sync, Shuffle, Deselect, Submit.
enum Btn {
	BTN_INFO = 0,
	BTN_SETTINGS,
	BTN_SYNC,
	BTN_SHUFFLE,
	BTN_DESELECT,
	BTN_SUBMIT,
	BTN_COUNT
};

// Buttons on the end-of-game stats screen (enum order = left-to-right focus/nav
// order). Matches data/toolbar-stats.png: Info, Settings, Sync, Stats, Share.
enum StatBtn {
	STAT_INFO = 0,
	STAT_SETTINGS,
	STAT_SYNC,
	STAT_STATS,
	STAT_SHARE,
	STAT_COUNT
};

// Rows in the Settings panel (enum order = top-to-bottom focus/nav order).
enum SettingsItem {
	SET_PLAY_PREV = 0, // jump into the most recent unplayed past puzzle
	SET_BACK_TODAY,    // leave practice, return to today's daily puzzle
	SET_VIEW_STATS,    // open the statistics screen
	SET_AUTOSYNC,      // toggle: fetch today's puzzle on launch
	SET_CONFIRM,       // toggle: require a second Submit tap
	SET_RESET,         // wipe lifetime stats + completed-date log
	SET_COUNT
};

// Snapshot of the state the Settings panel renders (owned by the caller).
struct SettingsView {
	bool autoSync;
	bool confirmSubmit;
	bool archiveActive; // true while a practice/past-date puzzle is loaded
	const char *modeLabel; // e.g. "Today: 2026-07-19" or "Practice: 2025-01-02"
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

// Settings panel overlay (both screens). `focus` highlights a SettingsItem row.
void drawSettings(const SettingsView &v, int focus);
Rect settingsCloseRect();
bool hitSettingsClose(int px, int py);
Rect settingsRowRect(int i);
int hitSettingsRow(int px, int py); // returns a SettingsItem or -1

// End-of-game / statistics screen (bottom): statistics + toolbar buttons.
// `focus` highlights a StatBtn (or -1 for none). `archive` shows the practice
// (not-counted) variant for past-date puzzles.
Rect statButtonRect(StatBtn b);
const char *statButtonLabel(StatBtn b);
void drawStats(const Game &g, const Stats &s, int focus, bool archive);
int hitStatButton(int px, int py);

} // namespace ui

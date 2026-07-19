// Connections DS - dual-screen NYT Connections clone.
//
// State machine (driven by pmMainLoop):
//   Loading -> (Sync | bundled Fallback) -> Playing -> Solved/Failed -> Share
//
// Top screen  : title/date, mistakes remaining, stacked solved categories,
//               live current selection.
// Bottom touch: 4x4 word grid + Shuffle/Deselect/Submit and Sync/Share.

#include <nds.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "config.hpp"
#include "gfx.hpp"
#include "sprite.hpp"
#include "puzzle.hpp"
#include "game.hpp"
#include "ui.hpp"
#include "anim.hpp"
#include "net.hpp"
#include "storage.hpp"
#include "share.hpp"

namespace {

enum class AppState { Playing, Stats, Share, Help };

Game g;
Stats stats;
AppState appState = AppState::Playing;
int statFocus = 0; // focused StatBtn on the stats screen

char toast[64] = {0};
int toastFrames = 0;

char topStatus[64] = {0};

bool dirty = true;
bool resultRecorded = false;

// True while the "correct guess" transition is playing; the end-of-game
// finalize (stats screen) is deferred until it finishes so the animation isn't
// cut off by a screen change.
bool solvePlaying = false;

// Power-management event handler: force a redraw after waking from lid-close
// sleep so both screens are refreshed.
PmEventCookie pmCookie;
void onPmEvent(void *user, PmEvent event) {
	(void)user;
	if (event == PmEvent_OnWakeup)
		dirty = true;
}

// Selection order stack (board positions) so B can undo the last pick.
u8 selOrder[CARDS_PER_CATEGORY];
int selOrderCount = 0;

// D-pad focus. Focus is either on a board tile or on the single row of action
// icon buttons ([Info, Sync, Shuffle, Deselect, Submit]).
enum class Zone { Tiles, Buttons };
Zone focusZone = Zone::Tiles;
int tileCursor = 0; // board slot index
int btnIndex = 0;   // 0..BTN_COUNT-1

ui::Btn focusedButton() {
	int i = btnIndex;
	if (i < 0) i = 0;
	if (i >= ui::BTN_COUNT) i = ui::BTN_COUNT - 1;
	return (ui::Btn)i;
}

void selReset() { selOrderCount = 0; }

void selPush(int pos) {
	if (selOrderCount < CARDS_PER_CATEGORY) selOrder[selOrderCount++] = (u8)pos;
}

void selRemove(int pos) {
	for (int i = 0; i < selOrderCount; i++) {
		if (selOrder[i] == pos) {
			for (int j = i; j < selOrderCount - 1; j++) selOrder[j] = selOrder[j + 1];
			selOrderCount--;
			return;
		}
	}
}

void setToast(const char *msg, int frames = 120) {
	strncpy(toast, msg, sizeof(toast) - 1);
	toast[sizeof(toast) - 1] = 0;
	toastFrames = frames;
	dirty = true;
}

// Today's local date as "YYYY-MM-DD" from the console RTC. Empty on failure.
void todayDate(char *out, int cap) {
	if (cap > 0) out[0] = 0;
	time_t t = time(nullptr);
	struct tm *lt = localtime(&t);
	if (lt) strftime(out, cap, "%Y-%m-%d", lt);
}

// True while the player is mid-way through a game we shouldn't disturb: a
// playable board with at least one solved group, mistake, or live selection.
bool gameInProgress(const Game &g) {
	return g.status == GameStatus::Playing &&
	       (g.solvedCount > 0 || g.mistakes > 0 || g.selectedCount > 0);
}

void startGame(const Puzzle &p) {
	// Resume saved progress for this date if present, else start fresh.
	Game loaded;
	if (storage::loadProgress(loaded, p.date) && loaded.puzzle.valid) {
		g = loaded;
	} else {
		game::init(g, p);
		game::shuffle(g);
	}
	resultRecorded = (g.status != GameStatus::Playing);
	appState = AppState::Playing;
	solvePlaying = false;
	anim::solve().active = false;
	topStatus[0] = 0;
	selReset();
	focusZone = Zone::Tiles;
	tileCursor = 0;
	btnIndex = 0;
	dirty = true;
}

void finalizeIfOver() {
	if (g.status == GameStatus::Playing || resultRecorded)
		return;
	bool won = (g.status == GameStatus::Won);
	storage::recordResult(stats, g, won);
	storage::saveStats(stats);
	// Top screen keeps the puzzle summary; no end-of-game status line.
	topStatus[0] = 0;
	resultRecorded = true;

	// Reveal the stats screen (this is where Share becomes available).
	appState = AppState::Stats;
	statFocus = (int)ui::STAT_SHARE;
	dirty = true;
}

// Build the "correct guess" transition from the board snapshot taken just before
// game::submit() removed the solved tiles and collapsed the grid.
void startSolveAnim(const int *preBoard, int preCount, const bool *preSelected) {
	anim::Solve &s = anim::solve();
	s.active = true;
	s.start = anim::now();
	s.cat = g.solvedOrder[g.solvedCount - 1];

	// The four solved tiles = the pre-submit selection, in board order.
	int n = 0;
	for (int i = 0; i < preCount && n < CARDS_PER_CATEGORY; i++) {
		int pos = preBoard[i];
		if (!preSelected[pos]) continue;
		Rect r = ui::tileRect(i);
		s.fromX[n] = r.x;
		s.fromY[n] = r.y;
		s.word[n] = g.puzzle.words[pos];
		n++;
	}

	// Remaining tiles: from their old slot to their new (collapsed) slot.
	s.nRemain = 0;
	for (int i = 0; i < preCount; i++) {
		int pos = preBoard[i];
		if (preSelected[pos]) continue;
		int newIdx = -1;
		for (int j = 0; j < g.boardCount; j++)
			if (g.board[j] == pos) { newIdx = j; break; }
		if (newIdx < 0) continue;
		Rect from = ui::tileRect(i);
		Rect to = ui::tileRect(newIdx);
		int m = s.nRemain++;
		s.remFromX[m] = from.x; s.remFromY[m] = from.y;
		s.remToX[m] = to.x;     s.remToY[m] = to.y;
		s.remWord[m] = g.puzzle.words[pos];
	}

	anim::keepAwake(anim::now() + anim::SOLVE_TOTAL);
	solvePlaying = true;
	dirty = true;
}

void doSubmit() {
	if (g.status != GameStatus::Playing)
		return;

	// Snapshot the board before submit() mutates it so a correct guess can
	// animate the four tiles flying up into the completed row.
	int preBoard[NUM_CARDS];
	int preCount = g.boardCount;
	for (int i = 0; i < preCount; i++) preBoard[i] = g.board[i];
	bool preSelected[NUM_CARDS];
	for (int i = 0; i < NUM_CARDS; i++) preSelected[i] = g.selected[i];

	SubmitResult r = game::submit(g);
	switch (r) {
		case SubmitResult::NotFourSelected: setToast("Pick 4 tiles"); break;
		case SubmitResult::AlreadyGuessed:  setToast("Already guessed"); break;
		case SubmitResult::Correct:         setToast("Correct!"); break;
		case SubmitResult::OneAway:         setToast("One away..."); break;
		case SubmitResult::Wrong:           setToast("Not quite"); break;
	}
	selReset(); // submit() clears the selection regardless of the outcome
	storage::saveProgress(g);

	if (r == SubmitResult::Correct) {
		startSolveAnim(preBoard, preCount, preSelected);
	} else {
		finalizeIfOver();
	}
	dirty = true;
}

void doSync() {
	// Show the syncing overlay immediately.
	ui::drawBottom(g, "", true);
	ui::drawTop(g, "Connecting to Wi-Fi...");

	if (!net::wifiConnect()) {
		net::wifiDisconnect();
		setToast("Wi-Fi failed (DSi mode?)");
		return;
	}

	static char buf[NET_BUFFER_SIZE];
	int len = 0;
	net::Result nr = net::httpGet(PROXY_HOST, PROXY_PORT, PROXY_PATH_LATEST, buf, sizeof(buf), &len);
	net::wifiDisconnect();

	if (nr != net::Result::Ok) {
		setToast(net::describe(nr));
		return;
	}

	Puzzle p;
	if (!puzzle::parseCompact(buf, len, p)) {
		setToast("Bad puzzle data");
		return;
	}

	startGame(p);
	char msg[64];
	snprintf(msg, sizeof(msg), "Synced %s", p.date);
	setToast(msg);
}

// On launch, if we're not in the middle of a game and the loaded puzzle isn't
// already today's, quietly try to pull the latest puzzle from the proxy and
// swap it in. Any failure (no Wi-Fi, offline, bad data) is silent so the
// bundled / last-saved puzzle stays playable.
void autoSyncIfStale() {
	// Never yank a board out from under an active game.
	if (gameInProgress(g))
		return;

	// If the current puzzle already matches today's date, there's nothing to
	// fetch. (When the RTC gives no date we fall through and let the network
	// check decide.)
	char today[16];
	todayDate(today, sizeof(today));
	if (today[0] && g.puzzle.date[0] &&
	    strncmp(g.puzzle.date, today, sizeof(today)) == 0)
		return;

	ui::drawBottom(g, "", true);
	ui::drawTop(g, "Checking for today's puzzle...");
	gfx::flip();

	if (!net::wifiConnect()) {
		net::wifiDisconnect();
		dirty = true;
		return;
	}

	static char buf[NET_BUFFER_SIZE];
	int len = 0;
	net::Result nr = net::httpGet(PROXY_HOST, PROXY_PORT, PROXY_PATH_LATEST, buf, sizeof(buf), &len);
	net::wifiDisconnect();

	if (nr != net::Result::Ok) {
		dirty = true;
		return;
	}

	Puzzle p;
	if (!puzzle::parseCompact(buf, len, p)) {
		dirty = true;
		return;
	}

	// Only replace the board if the proxy actually has a different puzzle than
	// the one on screen; otherwise just repaint what we already have.
	if (p.valid && strncmp(p.date, g.puzzle.date, sizeof(p.date)) != 0) {
		startGame(p);
		char msg[64];
		snprintf(msg, sizeof(msg), "Updated to %s", p.date);
		setToast(msg);
	} else {
		dirty = true;
	}
}

void doShare() {
	if (g.status == GameStatus::Playing)
		return;
	share::writeFile(g);
	appState = AppState::Share;
	dirty = true;
}

// Toggle a tile via the selection stack so undo (B) tracks the pick order.
void toggleTile(int boardIndex) {
	if (boardIndex < 0 || boardIndex >= g.boardCount) return;
	int pos = g.board[boardIndex];
	bool was = g.selected[pos];
	game::toggle(g, boardIndex);
	bool now = g.selected[pos];
	if (now && !was) selPush(pos);
	else if (!now && was) selRemove(pos);
	dirty = true;
}

void undoSelection() {
	if (selOrderCount == 0) return;
	int pos = selOrder[--selOrderCount];
	if (g.selected[pos]) {
		g.selected[pos] = false;
		g.selectedCount--;
	}
	dirty = true;
}

// Help can be opened from Playing or Stats; closing returns to the opener.
AppState helpReturn = AppState::Playing;

void openHelp() {
	helpReturn = appState;
	appState = AppState::Help;
	dirty = true;
}

void activateButton(ui::Btn b) {
	bool playing = (g.status == GameStatus::Playing);
	switch (b) {
		case ui::BTN_INFO:     openHelp(); break;
		case ui::BTN_STATS:
			appState = AppState::Stats;
			statFocus = (int)ui::STAT_SHARE;
			break;
		case ui::BTN_SHUFFLE:  if (playing) game::shuffle(g); break;
		case ui::BTN_DESELECT:
			if (playing && g.selectedCount > 0) {
				game::deselectAll(g);
				selReset();
			}
			break;
		case ui::BTN_SUBMIT:
			// Active only with a full guess of 4 selected tiles.
			if (playing && g.selectedCount == CARDS_PER_CATEGORY)
				doSubmit();
			break;
		case ui::BTN_SYNC:     doSync(); break;
		default: break;
	}
	dirty = true;
}

void handleTouch(int px, int py) {
	if (appState == AppState::Share)
		return; // Share screen is dismissed with B.

	int b = ui::hitButton(px, py);
	if (b >= 0) {
		// Move focus to the touched button for a consistent cursor position.
		focusZone = Zone::Buttons;
		btnIndex = b;
		activateButton((ui::Btn)b);
		return;
	}

	if (g.status == GameStatus::Playing) {
		int t = ui::hitTile(g, px, py);
		if (t >= 0) {
			focusZone = Zone::Tiles;
			tileCursor = t;
			toggleTile(t);
		}
	}
}

// Keep the focus target valid as the board shrinks / the game ends.
void clampFocus() {
	if (g.boardCount <= 0) {
		focusZone = Zone::Buttons;
		if (btnIndex < 0) btnIndex = 0;
		if (btnIndex >= ui::BTN_COUNT) btnIndex = ui::BTN_COUNT - 1;
		return;
	}
	if (tileCursor >= g.boardCount) tileCursor = g.boardCount - 1;
	if (tileCursor < 0) tileCursor = 0;
}

void navMove(int dx, int dy) {
	int rows = g.boardCount / 4;
	if (focusZone == Zone::Tiles) {
		int row = tileCursor / 4, col = tileCursor % 4;
		if (dx < 0 && col > 0) col--;
		else if (dx > 0 && col < 3) col++;
		else if (dy < 0 && row > 0) row--;
		else if (dy > 0) {
			if (row + 1 < rows) {
				row++;
			} else {
				focusZone = Zone::Buttons; // drop into the icon row
				// Map 4 grid columns across the 5 toolbar buttons.
				btnIndex = col * (ui::BTN_COUNT - 1) / 3;
				dirty = true;
				return;
			}
		}
		int idx = row * 4 + col;
		if (idx >= g.boardCount) idx = g.boardCount - 1;
		tileCursor = idx;
	} else { // Buttons (single row)
		if (dx < 0 && btnIndex > 0) btnIndex--;
		else if (dx > 0 && btnIndex < ui::BTN_COUNT - 1) btnIndex++;
		else if (dy < 0 && rows > 0) {
			focusZone = Zone::Tiles; // back up into the grid
			int col = btnIndex * 3 / (ui::BTN_COUNT - 1);
			if (col > 3) col = 3;
			int idx = (rows - 1) * 4 + col;
			if (idx >= g.boardCount) idx = g.boardCount - 1;
			tileCursor = idx;
		}
	}
	dirty = true;
}

void activateFocus() {
	if (focusZone == Zone::Tiles) {
		if (g.status == GameStatus::Playing) toggleTile(tileCursor);
	} else {
		activateButton(focusedButton());
	}
}

void activateStatButton(ui::StatBtn b) {
	switch (b) {
		case ui::STAT_SHARE: doShare(); break;
		case ui::STAT_INFO:  openHelp(); break;
		case ui::STAT_SYNC:  doSync(); break;
		default: break;
	}
}

void render() {
	// Only the Playing screen uses animation sprites; clear them elsewhere so no
	// stale overlay lingers on the Stats / Help / Share screens.
	if (appState != AppState::Playing) {
		spr::reset(false);
		spr::reset(true);
	}

	if (appState == AppState::Share) {
		char summary[64];
		if (g.status == GameStatus::Won)
			snprintf(summary, sizeof(summary), "Solved in %d guesses", g.guessCount);
		else
			snprintf(summary, sizeof(summary), "Better luck next time");
		ui::drawTop(g, summary);
		share::renderQR(g);
		return;
	}

	if (appState == AppState::Help) {
		ui::drawHelp();
		return;
	}

	if (appState == AppState::Stats) {
		ui::drawTop(g, topStatus);
		ui::drawStats(g, stats, statFocus);
		return;
	}

	const char *tmsg = (toastFrames > 0) ? toast : "";
	ui::drawBottom(g, "", false);

	ui::drawTop(g, topStatus[0] ? topStatus : nullptr);

	// Transient game feedback ("Not quite", "Correct!", ...) floats on the top
	// screen so it doesn't cover the board or toolbar.
	if (tmsg[0])
		ui::drawToast(true, 116, tmsg);

	// D-pad focus ring — periwinkle stroke matching the Figma tile focus state.
	// Hidden while the solve transition animates so it doesn't sit on moving tiles.
	if (anim::solve().active) {
		// no focus ring during the completed-row transition
	} else if (focusZone == Zone::Tiles && g.boardCount > 0) {
		Rect r = ui::tileRect(tileCursor);
		int rad = ui::tileCornerRadius();
		gfx::roundRectBorder(false, r.x, r.y, r.w, r.h, rad, gfx::pal::focus, 2);
	} else if (focusZone == Zone::Buttons) {
		Rect r = ui::buttonRect(focusedButton());
		// Soft focus halo around the icon hit target (toolbar glyphs are bare).
		gfx::roundRectBorder(false, r.x + 4, r.y + 2, r.w - 8, r.h - 4, 8, gfx::pal::focus, 2);
	}
}

} // namespace

int main(void) {
	gfx::init();
	spr::init();
	anim::initClock();
	srand((unsigned)time(nullptr));

	// Conserve battery: let calico put the console to sleep when the lid is
	// closed (pmMainLoop handles the hinge automatically). RAM is retained, so
	// the game state resumes exactly where it left off on wake.
	pmSetSleepAllowed(true);
	pmAddEventHandler(&pmCookie, onPmEvent, nullptr);

	gfx::clear(true, gfx::pal::bg);
	gfx::clear(false, gfx::pal::bg);
	gfx::drawTextCentered(false, 128, 90, "Loading...", gfx::pal::ink, 1);
	gfx::flip();

	bool sdOk = storage::init();
	if (sdOk)
		storage::loadStats(stats);
	else
		memset(&stats, 0, sizeof(stats));

	// Start from the bundled puzzle so the game is playable offline; the user
	// can tap Sync to fetch the latest daily puzzle from the proxy.
	Puzzle p;
	if (!puzzle::loadFallback(p)) {
		// Should never happen, but keep a valid board on screen.
		memset(&p, 0, sizeof(p));
		p.valid = true;
	}
	startGame(p);

	// If we're not resuming an in-progress game and the bundled/last-saved
	// puzzle is stale, try to fetch today's puzzle before the player starts.
	autoSyncIfStale();

	if (!sdOk)
		setToast("No SD: progress won't save", 240);

	bool wasBusy = false;
	while (pmMainLoop()) {
		swiWaitForVBlank();
		anim::advance();
		scanKeys();
		u32 down = keysDown();

		if (down & KEY_START)
			break;

		if (appState == AppState::Help) {
			// B, or any bottom-screen tap (X is the visual affordance), closes
			// help and returns to whichever screen opened it (play or stats).
			if (down & (KEY_B | KEY_TOUCH | KEY_A)) {
				appState = helpReturn;
				dirty = true;
			}
		} else if (appState == AppState::Share) {
			if (down & KEY_B) {
				appState = AppState::Stats; // back to the stats screen
				dirty = true;
			}
			if (down & KEY_TOUCH) {
				appState = AppState::Stats;
				dirty = true;
			}
		} else if (appState == AppState::Stats) {
			if (down & (KEY_LEFT | KEY_UP))    { if (statFocus > 0) { statFocus--; dirty = true; } }
			if (down & (KEY_RIGHT | KEY_DOWN)) { if (statFocus < ui::STAT_COUNT - 1) { statFocus++; dirty = true; } }
			if (down & KEY_A) activateStatButton((ui::StatBtn)statFocus);

			if (down & KEY_TOUCH) {
				touchPosition touch;
				touchRead(&touch);
				int b = ui::hitStatButton(touch.px, touch.py);
				if (b >= 0) { statFocus = b; activateStatButton((ui::StatBtn)b); }
			}
		} else if (solvePlaying) {
			// Gameplay input is locked while the completed-row transition plays.
		} else {
			clampFocus();

			if (down & KEY_LEFT)  navMove(-1, 0);
			if (down & KEY_RIGHT) navMove(1, 0);
			if (down & KEY_UP)    navMove(0, -1);
			if (down & KEY_DOWN)  navMove(0, 1);

			if (down & KEY_A) activateFocus();      // confirm: select tile / press button
			if (down & KEY_B) {                     // undo last selection
				if (g.status == GameStatus::Playing) undoSelection();
			}

			// Convenience shortcuts (unchanged): X shuffle, Y deselect all.
			if (down & KEY_X) { if (g.status == GameStatus::Playing) { game::shuffle(g); dirty = true; } }
			if (down & KEY_Y) { if (g.status == GameStatus::Playing) { game::deselectAll(g); selReset(); dirty = true; } }

			if (down & KEY_TOUCH) {
				touchPosition touch;
				touchRead(&touch);
				handleTouch(touch.px, touch.py);
			}
		}

		if (toastFrames > 0) {
			toastFrames--;
			if (toastFrames == 0)
				dirty = true;
		}

		// Finish the completed-row transition: settle the board, then run the
		// deferred end-of-game finalize (which may reveal the stats screen).
		if (solvePlaying && (int)(anim::now() - anim::solve().start) > anim::SOLVE_TOTAL) {
			anim::solve().active = false;
			solvePlaying = false;
			finalizeIfOver();
			dirty = true;
		}

		// Rendering. A full frame (input changed something, or the solve
		// transition is still on the software path) repaints both back-buffers,
		// flips, and rebuilds the hardware sprite overlays. Otherwise, while an
		// animation is running, we only advance the sprites via OAM — no software
		// repaint — which is what keeps interactive transitions at 60 fps.
		bool nowBusy = anim::busy();
		// When the last animation finishes, force one full render so any element
		// the sprites were standing in for (a popped-in chip, the settled board
		// after a solve) is painted back onto the background before its sprite is
		// released.
		if (wasBusy && !nowBusy) dirty = true;
		wasBusy = nowBusy;

		if (dirty) {
			render();
			gfx::flip();
			spr::update();
			dirty = false;
		} else if (nowBusy) {
			ui::animBottom(g);
			ui::animTop(g);
			spr::update();
		}
	}

	return 0;
}

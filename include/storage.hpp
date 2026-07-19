// SD-card persistence: daily progress + lifetime stats (via libfat).
#pragma once

#include "game.hpp"

struct Stats {
	int played;
	int wins;
	int streak;
	int maxStreak;
	char lastDate[16]; // date of the last completed puzzle (for streak logic)
};

// User-adjustable preferences edited from the in-game Settings panel.
struct Prefs {
	bool autoSync;      // fetch today's puzzle automatically on launch
	bool confirmSubmit; // require a second Submit tap before a guess is counted
};

namespace storage {

// Initialize libfat and create the app data directory. Returns true if the SD
// card is usable.
bool init();

bool loadStats(Stats &s);
bool saveStats(const Stats &s);

// Preferences (defaults applied when no file exists).
void defaultPrefs(Prefs &p);
bool loadPrefs(Prefs &p);
bool savePrefs(const Prefs &p);

// Persist / restore the exact in-progress game state for its puzzle date. The
// daily puzzle lives in one slot; archive (practice) puzzles use a separate
// slot so browsing old days never clobbers today's board.
bool saveProgress(const Game &g);
bool loadProgress(Game &g, const char *date); // true if a save for `date` was loaded
bool saveArchive(const Game &g);
bool loadArchive(Game &g, const char *date);

// Update lifetime stats after a finished daily game (won == true if solved).
void recordResult(Stats &s, const Game &g, bool won);

// Completed-puzzle log (independent of lifetime Stats): used so the archive
// "play a previous puzzle" jump skips days the player already finished, in
// either daily or practice mode.
void markPlayed(const char *date);
bool isPlayed(const char *date);
void clearPlayed();

// Most recent puzzle date strictly before `today` that has not been completed,
// searching back up to `maxLookback` days (but never before the first NYT
// Connections puzzle). Writes "YYYY-MM-DD" to `out`; returns false if none.
bool findLatestUnplayedBefore(const char *today, char *out, int cap, int maxLookback = 400);

} // namespace storage

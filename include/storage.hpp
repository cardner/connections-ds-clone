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

namespace storage {

// Initialize libfat and create the app data directory. Returns true if the SD
// card is usable.
bool init();

bool loadStats(Stats &s);
bool saveStats(const Stats &s);

// Persist / restore the exact in-progress game state for its puzzle date.
bool saveProgress(const Game &g);
bool loadProgress(Game &g, const char *date); // true if a save for `date` was loaded

// Update stats after a finished game (won == true if solved).
void recordResult(Stats &s, const Game &g, bool won);

} // namespace storage

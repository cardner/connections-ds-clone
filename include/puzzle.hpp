// Puzzle data model + parser for the compact proxy/fallback format.
#pragma once

#include <nds.h>

constexpr int NUM_CARDS = 16;
constexpr int NUM_CATEGORIES = 4;
constexpr int CARDS_PER_CATEGORY = 4;
constexpr int MAX_WORD_LEN = 24;  // including null terminator
constexpr int MAX_TITLE_LEN = 40; // including null terminator

struct Puzzle {
	char date[16];
	int id;
	char categories[NUM_CATEGORIES][MAX_TITLE_LEN];
	char words[NUM_CARDS][MAX_WORD_LEN]; // indexed by board position 0..15
	u8 cardCategory[NUM_CARDS];          // category index 0..3 per board position
	bool valid;
};

namespace puzzle {

// Parse the compact line format into `out`. Returns true on a complete,
// well-formed puzzle (4 categories + 16 cards). `len` bytes of `text` are read.
bool parseCompact(const char *text, int len, Puzzle &out);

// Load and parse the puzzle bundled into the ROM (data/fallback.bin).
bool loadFallback(Puzzle &out);

} // namespace puzzle

// Connections game rules: selection, submission, mistakes, reveal, result grid.
#pragma once

#include "puzzle.hpp"

constexpr int MAX_MISTAKES = 4;
// Worst case: 4 correct + 4 wrong guesses that count.
constexpr int MAX_GUESSES = 8;

enum class GameStatus { Playing, Won, Lost };

enum class SubmitResult {
	NotFourSelected,
	AlreadyGuessed,
	Correct,
	OneAway, // wrong, but 3 of 4 shared a category
	Wrong,
};

struct Game {
	Puzzle puzzle;

	// Selection state, keyed by board position 0..15.
	bool selected[NUM_CARDS];
	int selectedCount;

	// Display order of the tiles still on the board.
	int board[NUM_CARDS];
	int boardCount;

	// Solved categories, in the order the player solved them (for stacking).
	bool solved[NUM_CATEGORIES];
	int solvedOrder[NUM_CATEGORIES];
	int solvedCount;

	int mistakes;
	GameStatus status;

	// Result grid: one row per counted guess, 4 category colors each.
	u8 guessRows[MAX_GUESSES][CARDS_PER_CATEGORY];
	int guessCount;

	// Sorted position sets of prior guesses (to reject exact duplicates).
	u8 guessSets[MAX_GUESSES][CARDS_PER_CATEGORY];
};

namespace game {

void init(Game &g, const Puzzle &p);

// Toggle selection of the tile shown at board slot `boardIndex`.
void toggle(Game &g, int boardIndex);

void deselectAll(Game &g);

// Randomize the order of the remaining tiles.
void shuffle(Game &g);

// Category color (0..3) of the tile at a board slot.
int boardCategory(const Game &g, int boardIndex);

// Attempt to submit the current 4-tile selection.
SubmitResult submit(Game &g);

} // namespace game

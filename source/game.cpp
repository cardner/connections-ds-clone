#include "game.hpp"

#include <string.h>
#include <stdlib.h>

namespace {

void removeSolvedFromBoard(Game &g, int category) {
	int w = 0;
	for (int r = 0; r < g.boardCount; r++) {
		int pos = g.board[r];
		if (g.puzzle.cardCategory[pos] != category)
			g.board[w++] = pos;
	}
	g.boardCount = w;
}

void sort4(u8 *a) {
	for (int i = 0; i < 4; i++)
		for (int j = i + 1; j < 4; j++)
			if (a[j] < a[i]) { u8 t = a[i]; a[i] = a[j]; a[j] = t; }
}

} // namespace

void game::init(Game &g, const Puzzle &p) {
	memset(&g, 0, sizeof(g));
	g.puzzle = p;
	g.boardCount = NUM_CARDS;
	for (int i = 0; i < NUM_CARDS; i++)
		g.board[i] = i; // board position order
	g.status = GameStatus::Playing;
}

void game::toggle(Game &g, int boardIndex) {
	if (boardIndex < 0 || boardIndex >= g.boardCount)
		return;
	int pos = g.board[boardIndex];
	if (g.selected[pos]) {
		g.selected[pos] = false;
		g.selectedCount--;
	} else if (g.selectedCount < CARDS_PER_CATEGORY) {
		g.selected[pos] = true;
		g.selectedCount++;
	}
}

void game::deselectAll(Game &g) {
	for (int i = 0; i < NUM_CARDS; i++)
		g.selected[i] = false;
	g.selectedCount = 0;
}

void game::shuffle(Game &g) {
	for (int i = g.boardCount - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		int t = g.board[i];
		g.board[i] = g.board[j];
		g.board[j] = t;
	}
}

int game::boardCategory(const Game &g, int boardIndex) {
	if (boardIndex < 0 || boardIndex >= g.boardCount)
		return -1;
	return g.puzzle.cardCategory[g.board[boardIndex]];
}

SubmitResult game::submit(Game &g) {
	if (g.status != GameStatus::Playing)
		return SubmitResult::NotFourSelected;
	if (g.selectedCount != CARDS_PER_CATEGORY)
		return SubmitResult::NotFourSelected;

	// Collect selected positions (in board order) and their categories.
	u8 positions[CARDS_PER_CATEGORY];
	u8 cats[CARDS_PER_CATEGORY];
	int n = 0;
	for (int r = 0; r < g.boardCount && n < CARDS_PER_CATEGORY; r++) {
		int pos = g.board[r];
		if (g.selected[pos]) {
			positions[n] = (u8)pos;
			cats[n] = g.puzzle.cardCategory[pos];
			n++;
		}
	}

	// Reject an exact duplicate of a previous guess.
	u8 sorted[CARDS_PER_CATEGORY];
	memcpy(sorted, positions, sizeof(sorted));
	sort4(sorted);
	for (int i = 0; i < g.guessCount; i++) {
		if (memcmp(sorted, g.guessSets[i], CARDS_PER_CATEGORY) == 0)
			return SubmitResult::AlreadyGuessed;
	}

	// Record the guess (colors + sorted set) for the result grid & dedupe.
	if (g.guessCount < MAX_GUESSES) {
		memcpy(g.guessRows[g.guessCount], cats, CARDS_PER_CATEGORY);
		memcpy(g.guessSets[g.guessCount], sorted, CARDS_PER_CATEGORY);
		g.guessCount++;
	}

	// Frequency of each category among the four picks.
	int freq[NUM_CATEGORIES] = {0, 0, 0, 0};
	for (int i = 0; i < CARDS_PER_CATEGORY; i++)
		freq[cats[i]]++;
	int best = 0;
	for (int c = 1; c < NUM_CATEGORIES; c++)
		if (freq[c] > freq[best]) best = c;

	if (freq[best] == CARDS_PER_CATEGORY) {
		// Correct group.
		g.solved[best] = true;
		g.solvedOrder[g.solvedCount++] = best;
		removeSolvedFromBoard(g, best);
		deselectAll(g);
		if (g.solvedCount == NUM_CATEGORIES)
			g.status = GameStatus::Won;
		return SubmitResult::Correct;
	}

	// Wrong guess.
	g.mistakes++;
	deselectAll(g);
	bool oneAway = (freq[best] == CARDS_PER_CATEGORY - 1);

	if (g.mistakes >= MAX_MISTAKES) {
		// Reveal the remaining categories in difficulty order.
		for (int c = 0; c < NUM_CATEGORIES; c++) {
			if (!g.solved[c]) {
				g.solved[c] = true;
				g.solvedOrder[g.solvedCount++] = c;
			}
		}
		g.boardCount = 0;
		g.status = GameStatus::Lost;
	}

	return oneAway ? SubmitResult::OneAway : SubmitResult::Wrong;
}

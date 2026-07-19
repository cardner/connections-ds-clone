// Lightweight, wall-clock animation timer for Connections DS.
//
// The whole UI is a single software compositor that normally only repaints on
// input (dirty flag). To animate, main.cpp samples the hardware tick counter
// once per loop iteration (anim::advance) and keeps repainting while
// anim::busy() is true. Progress is measured in real milliseconds, so
// transitions finish on schedule even when a full software repaint takes longer
// than one VBlank. The lid-close sleep / battery path is undisturbed: nothing
// animates while idle, so busy() stays false and rendering is skipped as before.
//
// State that spans main.cpp (which triggers transitions) and the UI drawers
// (which render them) lives here: the millisecond clock, easing helpers, and
// the "correct guess" solve transition payload.
#pragma once

#include <nds.h>

#include "puzzle.hpp"

namespace anim {

// Durations in milliseconds. Everything completes within ~600 ms.
constexpr int TILE_MS = 140;  // word-tile select/deselect color morph
constexpr int CHIP_MS = 130;  // top-screen selection-slot chip pop
constexpr int BTN_MS  = 110;   // toolbar button state crossfade
constexpr int SOLVE_P1 = 250; // solved tiles gather + recolor into a bar
constexpr int SOLVE_P2 = 350; // bar rises/fades off top; remaining tiles reflow
constexpr int SOLVE_TOTAL = SOLVE_P1 + SOLVE_P2;

// Start the free-running hardware timer backing the clock (call once at boot,
// before the main loop). Uses the ARM9's timers 0/1 (free; calico only reserves
// 2/3), so it doesn't depend on any subsystem being initialized for us.
void initClock();

// Sample the hardware clock (call exactly once per main-loop iteration, so
// every draw within one frame sees the same timestamp).
void advance();
u32  now(); // current time in ms

// Ask the main loop to keep repainting through @p untilMs (inclusive).
void keepAwake(u32 untilMs);
// True while any animation still needs a redraw this frame.
bool busy();

float clamp01(float v);
float smooth(float t);                 // smoothstep ease-in-out
float smoothProg(u32 start, int durMs); // eased progress in [0,1]
float linProg(u32 start, int durMs);    // linear progress in [0,1]

// "Correct guess" transition: the four selected tiles fly up on the bottom
// screen, merge + recolor into a single category-colored bar, then rise off the
// top edge while the remaining tiles reflow into their new slots. The top-screen
// completed row fades in as they arrive. main.cpp fills this in before the board
// mutates; the UI consumes it while `active`.
struct Solve {
	bool active;
	u32  start;
	int  cat;                             // solved category (0..3)
	// Solved tiles (pre-submit top-left on the bottom screen) + their words.
	int  fromX[CARDS_PER_CATEGORY];
	int  fromY[CARDS_PER_CATEGORY];
	const char *word[CARDS_PER_CATEGORY];
	// Remaining tiles that slide from their old slot to their new slot.
	int  nRemain;
	int  remFromX[NUM_CARDS], remFromY[NUM_CARDS];
	int  remToX[NUM_CARDS],   remToY[NUM_CARDS];
	const char *remWord[NUM_CARDS];
};
Solve &solve();

} // namespace anim

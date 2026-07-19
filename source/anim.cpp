#include "anim.hpp"

#include <nds/timers.h>

namespace {
u32 g_now = 0;       // ms timestamp, latched once per frame by advance()
u32 g_wake = 0;      // repaint through this ms timestamp (inclusive)
bool g_tail = false; // previous frame was still inside the wake window

u32 g_lastTicks = 0;   // last cpuGetTiming() sample (wraps every ~128 s)
u64 g_totalTicks = 0;  // wrap-extended tick accumulator

anim::Solve g_solve = {};
} // namespace

void anim::initClock() {
	// Free-running 32-bit cascade (ARM9 timers 0/1) at the bus clock. This does
	// not rely on calico's tick scheduler being initialized for us.
	cpuStartTiming(0);
	g_lastTicks = cpuGetTiming();
	g_totalTicks = 0;
	g_now = 0;
}

void anim::advance() {
	// The ms clock advances in irregular steps (a slow repaint can skip several
	// VBlanks), so the deadline can be jumped over without ever rendering the
	// t=1 end state. Remember whether the *previous* frame was inside the wake
	// window; busy() then grants exactly one settle frame past the deadline.
	g_tail = (g_now <= g_wake);

	// Accumulate the wrap-safe tick delta (the raw counter rolls over ~128 s),
	// then latch it as milliseconds so every draw this frame sees one instant.
	u32 t = cpuGetTiming();
	g_totalTicks += (u32)(t - g_lastTicks);
	g_lastTicks = t;
	g_now = (u32)(g_totalTicks * 1000ull / BUS_CLOCK);
}
u32  anim::now() { return g_now; }

void anim::keepAwake(u32 untilMs) {
	if (untilMs > g_wake) g_wake = untilMs;
}
bool anim::busy() { return g_now <= g_wake || g_tail; }

float anim::clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float anim::smooth(float t) {
	t = clamp01(t);
	return t * t * (3.0f - 2.0f * t);
}

float anim::smoothProg(u32 start, int dur) {
	if (dur <= 0) return 1.0f;
	return smooth((float)(g_now - start) / (float)dur);
}

float anim::linProg(u32 start, int dur) {
	if (dur <= 0) return 1.0f;
	return clamp01((float)(g_now - start) / (float)dur);
}

anim::Solve &anim::solve() { return g_solve; }

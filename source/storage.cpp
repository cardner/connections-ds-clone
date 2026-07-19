#include "storage.hpp"
#include "config.hpp"

#include <fat.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace {

bool g_fatReady = false;

const u32 PROGRESS_MAGIC = 0x434E4450; // 'CNDP'
const u32 STATS_MAGIC = 0x434E4453;    // 'CNDS'
const u32 SAVE_VERSION = 1;

struct ProgressHeader {
	u32 magic;
	u32 version;
	char date[16];
};

void ensureDirs() {
	mkdir("/_nds", 0777);
	mkdir(APP_DIR, 0777);
}

} // namespace

bool storage::init() {
	g_fatReady = fatInitDefault();
	if (g_fatReady)
		ensureDirs();
	return g_fatReady;
}

bool storage::loadStats(Stats &s) {
	memset(&s, 0, sizeof(s));
	if (!g_fatReady) return false;
	FILE *f = fopen(STATS_PATH, "rb");
	if (!f) return false;
	u32 magic = 0, version = 0;
	bool ok = fread(&magic, sizeof(magic), 1, f) == 1 &&
	          fread(&version, sizeof(version), 1, f) == 1 &&
	          magic == STATS_MAGIC &&
	          fread(&s, sizeof(s), 1, f) == 1;
	fclose(f);
	if (!ok) memset(&s, 0, sizeof(s));
	return ok;
}

bool storage::saveStats(const Stats &s) {
	if (!g_fatReady) return false;
	ensureDirs();
	FILE *f = fopen(STATS_PATH, "wb");
	if (!f) return false;
	u32 magic = STATS_MAGIC, version = SAVE_VERSION;
	bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 &&
	          fwrite(&version, sizeof(version), 1, f) == 1 &&
	          fwrite(&s, sizeof(s), 1, f) == 1;
	fclose(f);
	return ok;
}

bool storage::saveProgress(const Game &g) {
	if (!g_fatReady) return false;
	ensureDirs();
	FILE *f = fopen(SAVE_PATH, "wb");
	if (!f) return false;
	ProgressHeader h;
	h.magic = PROGRESS_MAGIC;
	h.version = SAVE_VERSION;
	memset(h.date, 0, sizeof(h.date));
	memcpy(h.date, g.puzzle.date, strnlen(g.puzzle.date, sizeof(h.date) - 1));
	bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
	          fwrite(&g, sizeof(g), 1, f) == 1;
	fclose(f);
	return ok;
}

bool storage::loadProgress(Game &g, const char *date) {
	if (!g_fatReady) return false;
	FILE *f = fopen(SAVE_PATH, "rb");
	if (!f) return false;
	ProgressHeader h;
	Game tmp;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 &&
	          h.magic == PROGRESS_MAGIC &&
	          h.version == SAVE_VERSION &&
	          strncmp(h.date, date, sizeof(h.date)) == 0 &&
	          fread(&tmp, sizeof(tmp), 1, f) == 1;
	fclose(f);
	if (ok) g = tmp;
	return ok;
}

void storage::recordResult(Stats &s, const Game &g, bool won) {
	// Guard against double-counting the same puzzle date.
	if (strncmp(s.lastDate, g.puzzle.date, sizeof(s.lastDate)) == 0)
		return;

	s.played++;
	if (won) {
		s.wins++;
		s.streak++;
		if (s.streak > s.maxStreak) s.maxStreak = s.streak;
	} else {
		s.streak = 0;
	}
	memset(s.lastDate, 0, sizeof(s.lastDate));
	memcpy(s.lastDate, g.puzzle.date, strnlen(g.puzzle.date, sizeof(s.lastDate) - 1));
}

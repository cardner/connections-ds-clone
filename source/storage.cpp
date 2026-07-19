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
const u32 PREFS_MAGIC = 0x434E4452;    // 'CNDR'
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

// --- Calendar math (proleptic Gregorian, Howard Hinnant's algorithms) ---

// Days since 1970-01-01 for a Y-M-D (m in 1..12).
long daysFromCivil(int y, int m, int d) {
	y -= (m <= 2);
	long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long)doe - 719468;
}

void civilFromDays(long z, int *y, int *m, int *d) {
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long yy = (long)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp + (mp < 10 ? 3 : -9));
	*y = (int)(yy + (*m <= 2));
}

// First NYT Connections puzzle date (2023-06-12): the archive floor.
const long CONNECTIONS_EPOCH_DAY = daysFromCivil(2023, 6, 12);

// Completed-date bitset: one bit per day since the Connections epoch.
const int PLAYED_BITS = 4096;              // ~11 years of coverage
const int PLAYED_BYTES = PLAYED_BITS / 8;  // 512 bytes on disk

bool parseDate(const char *s, int *y, int *m, int *d) {
	if (!s || strnlen(s, 16) < 10) return false;
	int yy = 0, mm = 0, dd = 0;
	if (sscanf(s, "%4d-%2d-%2d", &yy, &mm, &dd) != 3) return false;
	if (mm < 1 || mm > 12 || dd < 1 || dd > 31) return false;
	*y = yy; *m = mm; *d = dd;
	return true;
}

// Bit index for a date, or -1 if outside the coverable range.
int playedIndex(const char *date) {
	int y, m, d;
	if (!parseDate(date, &y, &m, &d)) return -1;
	long idx = daysFromCivil(y, m, d) - CONNECTIONS_EPOCH_DAY;
	if (idx < 0 || idx >= PLAYED_BITS) return -1;
	return (int)idx;
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

namespace {

bool saveGameTo(const char *path, const Game &g) {
	if (!g_fatReady) return false;
	ensureDirs();
	FILE *f = fopen(path, "wb");
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

bool loadGameFrom(const char *path, Game &g, const char *date) {
	if (!g_fatReady) return false;
	FILE *f = fopen(path, "rb");
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

} // namespace

bool storage::saveProgress(const Game &g) { return saveGameTo(SAVE_PATH, g); }
bool storage::loadProgress(Game &g, const char *date) { return loadGameFrom(SAVE_PATH, g, date); }
bool storage::saveArchive(const Game &g) { return saveGameTo(ARCHIVE_PATH, g); }
bool storage::loadArchive(Game &g, const char *date) { return loadGameFrom(ARCHIVE_PATH, g, date); }

void storage::defaultPrefs(Prefs &p) {
	p.autoSync = true;
	p.confirmSubmit = false;
}

bool storage::loadPrefs(Prefs &p) {
	defaultPrefs(p);
	if (!g_fatReady) return false;
	FILE *f = fopen(PREFS_PATH, "rb");
	if (!f) return false;
	u32 magic = 0, version = 0;
	Prefs tmp;
	bool ok = fread(&magic, sizeof(magic), 1, f) == 1 &&
	          fread(&version, sizeof(version), 1, f) == 1 &&
	          magic == PREFS_MAGIC &&
	          fread(&tmp, sizeof(tmp), 1, f) == 1;
	fclose(f);
	if (ok) p = tmp;
	return ok;
}

bool storage::savePrefs(const Prefs &p) {
	if (!g_fatReady) return false;
	ensureDirs();
	FILE *f = fopen(PREFS_PATH, "wb");
	if (!f) return false;
	u32 magic = PREFS_MAGIC, version = SAVE_VERSION;
	bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 &&
	          fwrite(&version, sizeof(version), 1, f) == 1 &&
	          fwrite(&p, sizeof(p), 1, f) == 1;
	fclose(f);
	return ok;
}

namespace {

void readPlayed(u8 *bits) {
	memset(bits, 0, PLAYED_BYTES);
	if (!g_fatReady) return;
	FILE *f = fopen(PLAYED_PATH, "rb");
	if (!f) return;
	fread(bits, 1, PLAYED_BYTES, f);
	fclose(f);
}

} // namespace

void storage::markPlayed(const char *date) {
	int idx = playedIndex(date);
	if (idx < 0 || !g_fatReady) return;
	u8 bits[PLAYED_BYTES];
	readPlayed(bits);
	bits[idx >> 3] |= (u8)(1 << (idx & 7));
	ensureDirs();
	FILE *f = fopen(PLAYED_PATH, "wb");
	if (!f) return;
	fwrite(bits, 1, PLAYED_BYTES, f);
	fclose(f);
}

bool storage::isPlayed(const char *date) {
	int idx = playedIndex(date);
	if (idx < 0) return false;
	u8 bits[PLAYED_BYTES];
	readPlayed(bits);
	return (bits[idx >> 3] >> (idx & 7)) & 1;
}

void storage::clearPlayed() {
	if (!g_fatReady) return;
	remove(PLAYED_PATH);
}

bool storage::findLatestUnplayedBefore(const char *today, char *out, int cap, int maxLookback) {
	if (cap > 0) out[0] = 0;
	int y, m, d;
	if (!parseDate(today, &y, &m, &d)) return false;
	long todayDay = daysFromCivil(y, m, d);

	// Read the completed-date bitset once instead of reopening it per day.
	u8 bits[PLAYED_BYTES];
	readPlayed(bits);

	for (int i = 1; i <= maxLookback; i++) {
		long day = todayDay - i;
		if (day < CONNECTIONS_EPOCH_DAY) break;
		long idx = day - CONNECTIONS_EPOCH_DAY;
		if (idx >= PLAYED_BITS) continue;
		if ((bits[idx >> 3] >> (idx & 7)) & 1) continue; // already completed
		int yy, mm, dd;
		civilFromDays(day, &yy, &mm, &dd);
		char date[32];
		snprintf(date, sizeof(date), "%04d-%02d-%02d", yy, mm, dd);
		snprintf(out, cap, "%s", date);
		return true;
	}
	return false;
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

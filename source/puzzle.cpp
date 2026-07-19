#include "puzzle.hpp"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Bundled fallback puzzle (data/fallback.bin), embedded via bin2o.
#include "fallback_bin.h"

namespace {

// Copy at most (cap-1) chars of src into dst and null-terminate.
void copyTrunc(char *dst, const char *src, int cap) {
	int i = 0;
	for (; src[i] && i < cap - 1; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

// Read a base-10 integer from *p, advancing *p past it. Skips leading spaces.
int readInt(const char **p) {
	const char *s = *p;
	while (*s == ' ') s++;
	int val = 0;
	bool any = false;
	while (*s >= '0' && *s <= '9') {
		val = val * 10 + (*s - '0');
		s++;
		any = true;
	}
	*p = s;
	return any ? val : -1;
}

} // namespace

bool puzzle::parseCompact(const char *text, int len, Puzzle &out) {
	memset(&out, 0, sizeof(out));
	for (int i = 0; i < NUM_CARDS; i++)
		out.cardCategory[i] = 0xFF;

	int catsSeen = 0;
	int cardsSeen = 0;
	int i = 0;

	while (i < len) {
		// Extract one line (strip CR, cap length).
		char line[160];
		int n = 0;
		while (i < len && text[i] != '\n') {
			char ch = text[i++];
			if (ch != '\r' && n < (int)sizeof(line) - 1)
				line[n++] = ch;
		}
		if (i < len && text[i] == '\n') i++;
		line[n] = 0;
		if (n == 0) continue;

		if (strncmp(line, "ERR", 3) == 0) {
			return false;
		} else if (strncmp(line, "DATE ", 5) == 0) {
			copyTrunc(out.date, line + 5, sizeof(out.date));
		} else if (strncmp(line, "ID ", 3) == 0) {
			out.id = atoi(line + 3);
		} else if (strncmp(line, "CAT ", 4) == 0) {
			const char *p = line + 4;
			int idx = readInt(&p);
			if (*p == ' ') p++;
			if (idx >= 0 && idx < NUM_CATEGORIES) {
				copyTrunc(out.categories[idx], p, MAX_TITLE_LEN);
				catsSeen++;
			}
		} else if (strncmp(line, "CARD ", 5) == 0) {
			const char *p = line + 5;
			int pos = readInt(&p);
			int cat = readInt(&p);
			if (*p == ' ') p++;
			if (pos >= 0 && pos < NUM_CARDS && cat >= 0 && cat < NUM_CATEGORIES) {
				copyTrunc(out.words[pos], p, MAX_WORD_LEN);
				out.cardCategory[pos] = (u8)cat;
				cardsSeen++;
			}
		} else if (strncmp(line, "END", 3) == 0) {
			break;
		}
	}

	// Every board slot must be filled and 4 categories named.
	if (catsSeen != NUM_CATEGORIES || cardsSeen != NUM_CARDS)
		return false;
	for (int c = 0; c < NUM_CARDS; c++) {
		if (out.cardCategory[c] >= NUM_CATEGORIES || out.words[c][0] == 0)
			return false;
	}

	out.valid = true;
	return true;
}

bool puzzle::loadFallback(Puzzle &out) {
	return parseCompact((const char *)fallback_bin, (int)fallback_bin_size, out);
}

/* Battery history and the remaining-time estimate (pure file logic; the level comes from the caller). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"

#define MAX_LOG_BYTES (12 * 1024)
#define MAX_LINES 1024

typedef struct { long t; int lvl, chg; } Sample;

/* the samples in the file, oldest first; at most max */
static int read_log(const char *path, Sample *s, int max)
{
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	int n = 0;
	char line[64];
	while (fgets(line, sizeof line, f)) {
		Sample x;
		if (sscanf(line, "%ld %d %d", &x.t, &x.lvl, &x.chg) != 3 || x.lvl < 0 || x.lvl > 100) continue;
		if (n == max) { /* keep the newest */
			memmove(s, s + 1, (size_t) (max - 1) * sizeof *s);
			n--;
		}
		s[n++] = x;
	}
	fclose(f);
	return n;
}

void kui_batt_log(const char *path, long now, int level, int charging)
{
	static Sample s[MAX_LINES];
	if (level < 0 || level > 100) return;
	charging = charging ? 1 : 0;
	int n = read_log(path, s, MAX_LINES);
	if (n && s[n - 1].lvl == level && s[n - 1].chg == charging) return; /* only changes are written */
	FILE *f = fopen(path, "a");
	if (!f) return;
	fprintf(f, "%ld %d %d\n", now, level, charging);
	long size = ftell(f);
	fclose(f);
	if (size > MAX_LOG_BYTES) { /* keep the newer half */
		n = read_log(path, s, MAX_LINES);
		char tmp[512];
		snprintf(tmp, sizeof tmp, "%s.tmp", path);
		FILE *o = fopen(tmp, "w");
		if (!o) return;
		for (int i = n / 2; i < n; i++) fprintf(o, "%ld %d %d\n", s[i].t, s[i].lvl, s[i].chg);
		fclose(o);
		rename(tmp, path);
	}
}

long kui_batt_estimate_s(const char *path, long now, int level)
{
	static Sample s[MAX_LINES];
	int n = read_log(path, s, MAX_LINES);
	if (!n || s[n - 1].chg) return -1;
	int k = n - 1; /* the first sample of this discharge */
	while (k > 0 && !s[k - 1].chg) k--;
	long dt = now - s[k].t;
	int drop = s[k].lvl - level;
	if (drop < 2 || dt < 3600) return -1;
	return (long) ((double) level * (double) dt / (double) drop);
}

void kui_fmt_duration(long s, char *out, int cap)
{
	long d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
	if (d > 0) snprintf(out, (size_t) cap, T("≈ %d gün %d sa"), (int) d, (int) h);
	else if (h > 0) snprintf(out, (size_t) cap, T("≈ %d sa %d dk"), (int) h, (int) m);
	else snprintf(out, (size_t) cap, T("≈ %d dk"), (int) m);
}

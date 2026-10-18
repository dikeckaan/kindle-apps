#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "si.h"

long si_parse_uptime(const char *s)
{
	char *end;
	double v = strtod(s, &end);
	return end == s || v < 0 ? -1 : (long) v;
}

static long field_kb(const char *m, const char *name)
{
	const char *p = strstr(m, name);
	if (!p || (p != m && p[-1] != '\n')) return -1;
	p += strlen(name);
	char *end;
	long v = strtol(p, &end, 10);
	return end == p ? -1 : v;
}

int si_parse_meminfo(const char *m, long *total, long *avail)
{
	long t = field_kb(m, "MemTotal:"), a = field_kb(m, "MemAvailable:");
	if (t < 0) return -1;
	if (a < 0) {
		long f = field_kb(m, "MemFree:"), b = field_kb(m, "Buffers:"), c = field_kb(m, "Cached:");
		if (f < 0) return -1;
		a = f + (b > 0 ? b : 0) + (c > 0 ? c : 0);
	}
	*total = t * 1024;
	*avail = a * 1024;
	return 0;
}

void si_fmt_bytes(long long b, char *out, int cap)
{
	static const char *const unit[] = { "KB", "MB", "GB", "TB" };
	if (b < 0) {
		snprintf(out, (size_t) cap, "—");
		return;
	}
	if (b < 1024) {
		snprintf(out, (size_t) cap, "%lld B", b);
		return;
	}
	double v = (double) b / 1024;
	int u = 0;
	while (v >= 1024 && u < 3) {
		v /= 1024;
		u++;
	}
	char n[32];
	kui_fmt_num(v, 1, n, sizeof n);
	snprintf(out, (size_t) cap, "%s %s", n, unit[u]);
}

void si_fmt_duration(long s, char *out, int cap)
{
	if (s < 0) snprintf(out, (size_t) cap, "—");
	else if (s >= 86400) snprintf(out, (size_t) cap, T("%ld gün %ld sa"), s / 86400, s % 86400 / 3600);
	else if (s >= 3600) snprintf(out, (size_t) cap, T("%ld sa %ld dk"), s / 3600, s % 3600 / 60);
	else snprintf(out, (size_t) cap, T("%ld dk"), s / 60);
}

int si_batt_temp_c(int f)
{
	if (f < -200) return f;
	return (int) ((f - 32) * 5 / 9);
}

void si_parse_firmware(const char *s, char *out, int cap)
{
	/* first token that starts with a digit and contains a dot, e.g. "Kindle 5.19.2 (4183670082)" */
	for (const char *p = s; *p; p++) {
		if (!isdigit((unsigned char) *p) || (p != s && !isspace((unsigned char) p[-1]))) continue;
		int n = (int) strspn(p, "0123456789.");
		if (memchr(p, '.', (size_t) n)) {
			snprintf(out, (size_t) cap, "%.*s", n, p);
			return;
		}
	}
	snprintf(out, (size_t) cap, "—");
}

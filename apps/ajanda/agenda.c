/* Ajanda logic: Gregorian calendar, ISO weeks, Turkish holidays, the Hijri calendar (Diyanet's table + a computed
 * fallback), the notes file and the upcoming list. See agenda.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ajanda/agenda.h"
#include "kui.h"

/* ---- Gregorian calendar ---- */

long agenda_jdn(int y, int m, int d)
{
	long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
	return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

void agenda_from_jdn(long jdn, int *y, int *m, int *d)
{
	long a = jdn + 32044, b = (4 * a + 3) / 146097, c = a - 146097 * b / 4;
	long dd = (4 * c + 3) / 1461, e = c - 1461 * dd / 4, mm = (5 * e + 2) / 153;
	*d = (int) (e - (153 * mm + 2) / 5 + 1);
	*m = (int) (mm + 3 - 12 * (mm / 10));
	*y = (int) (100 * b + dd - 4800 + mm / 10);
}

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int agenda_days_in_month(int y, int m)
{
	static const int n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m < 1 || m > 12) return 0;
	return m == 2 && leap(y) ? 29 : n[m - 1];
}

int agenda_weekday(int y, int m, int d) { return (int) ((agenda_jdn(y, m, d) + 1) % 7); }

int agenda_iso_week(int y, int m, int d)
{
	long j = agenda_jdn(y, m, d);
	long thu = j - (agenda_weekday(y, m, d) + 6) % 7 + 3; /* the Thursday of this week decides the year */
	int ty, tm, td;
	agenda_from_jdn(thu, &ty, &tm, &td);
	return (int) ((thu - agenda_jdn(ty, 1, 1)) / 7 + 1);
}

void agenda_add_days(int *y, int *m, int *d, int days) { agenda_from_jdn(agenda_jdn(*y, *m, *d) + days, y, m, d); }

void agenda_add_months(int *y, int *m, int months)
{
	int k = *y * 12 + (*m - 1) + months;
	*y = k >= 0 ? k / 12 : -((-k + 11) / 12);
	*m = k - *y * 12 + 1;
}

/* ---- Hijri calendar ---- */

/* the first day of each Hijri month as Diyanet's "Dini Günler" lists give it, from Receb 1444 to Zilkade 1457 */
#define TAB_HY 1444
#define TAB_HM 7
static const long tab_ymd[] = {
	/* 1444 */ 20230123, 20230221, 20230323, 20230421, 20230521, 20230619,
	/* 1445 */ 20230719, 20230817, 20230916, 20231016, 20231114, 20231214, 20240112, 20240211, 20240311, 20240410, 20240509, 20240607,
	/* 1446 */ 20240707, 20240805, 20240904, 20241004, 20241103, 20241202, 20250101, 20250131, 20250301, 20250330, 20250429, 20250528,
	/* 1447 */ 20250626, 20250726, 20250824, 20250923, 20251023, 20251121, 20251221, 20260120, 20260219, 20260320, 20260418, 20260518,
	/* 1448 */ 20260616, 20260715, 20260814, 20260912, 20261012, 20261110, 20261210, 20270109, 20270208, 20270309, 20270408, 20270507,
	/* 1449 */ 20270606, 20270705, 20270803, 20270902, 20271001, 20271031, 20271129, 20271229, 20280128, 20280226, 20280327, 20280426,
	/* 1450 */ 20280525, 20280624, 20280723, 20280821, 20280920, 20281019, 20281118, 20281217, 20290116, 20290214, 20290316, 20290415,
	/* 1451 */ 20290514, 20290613, 20290713, 20290811, 20290909, 20291009, 20291107, 20291207, 20300105, 20300204, 20300305, 20300404,
	/* 1452 */ 20300503, 20300602, 20300702, 20300801, 20300830, 20300928, 20301028, 20301126, 20301226, 20310124, 20310222, 20310324,
	/* 1453 */ 20310423, 20310522, 20310621, 20310721, 20310819, 20310918, 20311017, 20311116, 20311215, 20320114, 20320212, 20320313,
	/* 1454 */ 20320411, 20320511, 20320609, 20320709, 20320807, 20320906, 20321006, 20321104, 20321204, 20330102, 20330201, 20330302,
	/* 1455 */ 20330401, 20330430, 20330530, 20330628, 20330727, 20330826, 20330925, 20331024, 20331123, 20331223, 20340121, 20340220,
	/* 1456 */ 20340321, 20340420, 20340519, 20340617, 20340717, 20340815, 20340914, 20341013, 20341112, 20341212, 20350111, 20350209,
	/* 1457 */ 20350311, 20350409, 20350509, 20350607, 20350706, 20350805, 20350903, 20351003, 20351101, 20351201, 20351231,
};
#define TAB_N ((int) (sizeof tab_ymd / sizeof tab_ymd[0]))

static long month_index(int hy, int hm) { return (long) hy * 12 + (hm - 1); }

/* the arithmetic (civil) Islamic calendar: within a couple of days of the real one, used only as an estimate */
static long tabular_jdn(int hy, int hm, int hd)
{
	return hd + (59L * (hm - 1) + 1) / 2 + (hy - 1) * 354L + (3 + 11L * hy) / 30 + 1948439;
}

/* Meeus, Astronomical Algorithms ch. 49: the true new moon k lunations after 6 Jan 2000 (JDE, ~1 min accuracy) */
static double new_moon_jde(double k)
{
	const double r = 3.14159265358979323846 / 180;
	double t = k / 1236.85, e = 1 - 0.002516 * t - 0.0000074 * t * t;
	double jde = 2451550.09766 + 29.530588861 * k + 0.00015437 * t * t - 0.00000015 * t * t * t + 0.00000000073 * t * t * t * t;
	double mm = r * (2.5534 + 29.10535670 * k - 0.0000014 * t * t);
	double mp = r * (201.5643 + 385.81693528 * k + 0.0107582 * t * t);
	double f = r * (160.7108 + 390.67050284 * k - 0.0016118 * t * t);
	double om = r * (124.7746 - 1.56375588 * k + 0.0020672 * t * t);
	return jde - 0.40720 * sin(mp) + 0.17241 * e * sin(mm) + 0.01608 * sin(2 * mp) + 0.01039 * sin(2 * f) + 0.00739 * e * sin(mp - mm) -
	       0.00514 * e * sin(mp + mm) + 0.00208 * e * e * sin(2 * mm) - 0.00111 * sin(mp - 2 * f) - 0.00057 * sin(mp + 2 * f) +
	       0.00056 * e * sin(2 * mp + mm) - 0.00042 * sin(3 * mp) + 0.00042 * e * sin(mm + 2 * f) + 0.00038 * e * sin(mm - 2 * f) -
	       0.00024 * e * sin(2 * mp - mm) - 0.00017 * sin(om) - 0.00007 * sin(mp + 2 * mm);
}

/* the month after conjunction k starts the next day if the conjunction is before 12:00 UTC, else the day after */
static long start_after(long k)
{
	double jd = new_moon_jde((double) k) - 69.0 / 86400 + 0.5; /* TT → UT; +0.5: civil days start at midnight */
	long day = (long) floor(jd);
	return day + ((jd - day) * 24 < 12 ? 1 : 2);
}

long agenda_hijri_month_start_astro(int hy, int hm)
{
	long est = tabular_jdn(hy, hm, 1), best = 0;
	long k0 = lround((est - 2451550.1) / 29.530588861);
	for (long k = k0 - 2; k <= k0 + 2; k++) {
		long s = start_after(k);
		if (!best || labs(s - est) < labs(best - est)) best = s;
	}
	return best;
}

static long tab_jdn(int i)
{
	static long j[TAB_N];
	if (!j[i]) j[i] = agenda_jdn((int) (tab_ymd[i] / 10000), (int) (tab_ymd[i] / 100 % 100), (int) (tab_ymd[i] % 100));
	return j[i];
}

long agenda_hijri_month_start(int hy, int hm)
{
	long i = month_index(hy, hm) - month_index(TAB_HY, TAB_HM);
	if (i >= 0 && i < TAB_N) return tab_jdn((int) i);
	return agenda_hijri_month_start_astro(hy, hm);
}

static int in_table(long jdn)
{
	/* the last tabled month may have 30 days: its end is known only through the computed next month */
	long last = month_index(TAB_HY, TAB_HM) + TAB_N - 1;
	int ly = (int) (last / 12), lm = (int) (last % 12) + 1;
	int ny = lm == 12 ? ly + 1 : ly, nm = lm == 12 ? 1 : lm + 1;
	return jdn >= tab_jdn(0) && jdn < agenda_hijri_month_start_astro(ny, nm) && jdn < tab_jdn(TAB_N - 1) + 30;
}

int agenda_hijri(int y, int m, int d, int *hy, int *hm, int *hd)
{
	long j = agenda_jdn(y, m, d);
	long n = (long) floor((j - 1948440) / 29.530588853); /* months since 1 Muharrem 1 */
	int cy = (int) (n / 12) + 1, cm = (int) (n % 12) + 1;
	while (agenda_hijri_month_start(cy, cm) > j)
		if (--cm < 1) cm = 12, cy--;
	for (;;) {
		int ny = cm == 12 ? cy + 1 : cy, nm = cm == 12 ? 1 : cm + 1;
		if (agenda_hijri_month_start(ny, nm) > j) break;
		cy = ny, cm = nm;
	}
	*hy = cy;
	*hm = cm;
	*hd = (int) (j - agenda_hijri_month_start(cy, cm)) + 1;
	return in_table(j);
}

const char *agenda_hijri_month_name(int hm)
{
	static const char *const names[12] = { /* N_ */ "Muharrem", "Safer", "Rebiülevvel", "Rebiülahir", "Cemaziyelevvel", "Cemaziyelahir",
					       "Receb", "Şaban", "Ramazan", "Şevval", "Zilkade", "Zilhicce" };
	return hm >= 1 && hm <= 12 ? names[hm - 1] : "";
}

/* ---- holidays ---- */

static int put(AgendaItem *out, int n, int max, int y, int m, int d, AgendaKind kind, const char *text)
{
	if (n >= max) return n;
	AgendaItem *it = &out[n];
	memset(it, 0, sizeof *it);
	it->y = y, it->m = m, it->d = d;
	it->kind = kind;
	it->note = -1;
	snprintf(it->text, sizeof it->text, "%s", text);
	return n + 1;
}

/* the n-th Sunday of a month */
static int nth_sunday(int y, int m, int nth) { return 1 + (7 - agenda_weekday(y, m, 1)) % 7 + 7 * (nth - 1); }

int agenda_holidays(int y, int m, int d, AgendaItem *out, int max)
{
	static const struct { int m, d, since; const char *name; } fixed[] = {
		{ 1, 1, 0, N_("Yılbaşı") },
		{ 4, 23, 1921, N_("Ulusal Egemenlik ve Çocuk Bayramı") },
		{ 5, 1, 2009, N_("Emek ve Dayanışma Günü") },
		{ 5, 19, 1938, N_("Atatürk'ü Anma, Gençlik ve Spor Bayramı") },
		{ 7, 15, 2017, N_("Demokrasi ve Millî Birlik Günü") },
		{ 8, 30, 1923, N_("Zafer Bayramı") },
		{ 10, 29, 1923, N_("Cumhuriyet Bayramı") },
	};
	static const char *const rb[] = { /* N_ */ "Ramazan Bayramı 1. gün", "Ramazan Bayramı 2. gün", "Ramazan Bayramı 3. gün" };
	static const char *const kb[] = { /* N_ */ "Kurban Bayramı 1. gün", "Kurban Bayramı 2. gün", "Kurban Bayramı 3. gün", "Kurban Bayramı 4. gün" };
	int n = 0, hy, hm, hd, ty, tm, td, ny = y, nm = m, nd = d;
	if (max <= 0) return 0;
	agenda_hijri(y, m, d, &hy, &hm, &hd);
	agenda_add_days(&ny, &nm, &nd, 1);
	agenda_hijri(ny, nm, nd, &ty, &tm, &td); /* tomorrow: arife and Regaib are the day before */
	(void) ty;
	for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
		if (fixed[i].m == m && fixed[i].d == d && y >= fixed[i].since) n = put(out, n, max, y, m, d, AGENDA_HOLIDAY, fixed[i].name);
	if (hm == 10 && hd <= 3) n = put(out, n, max, y, m, d, AGENDA_HOLIDAY, rb[hd - 1]);
	if (hm == 12 && hd >= 10 && hd <= 13) n = put(out, n, max, y, m, d, AGENDA_HOLIDAY, kb[hd - 10]);
	if (m == 10 && d == 28 && y >= 1923) n = put(out, n, max, y, m, d, AGENDA_HALF, N_("Cumhuriyet Bayramı arifesi"));
	if (tm == 10 && td == 1) n = put(out, n, max, y, m, d, AGENDA_HALF, N_("Ramazan Bayramı arifesi"));
	if (hm == 12 && hd == 9) n = put(out, n, max, y, m, d, AGENDA_HALF, N_("Kurban Bayramı arifesi"));
	/* kandils: the evening Diyanet lists; Regaib is the Thursday night before Receb's first Friday */
	if (agenda_weekday(y, m, d) == 4 && tm == 7 && td <= 7) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Regaib Kandili"));
	if (hm == 7 && hd == 26) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Miraç Kandili"));
	if (hm == 8 && hd == 14) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Berat Kandili"));
	if (hm == 9 && hd == 1) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Ramazan başlangıcı"));
	if (hm == 9 && hd == 26) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Kadir Gecesi"));
	if (hm == 1 && hd == 1) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Hicri yılbaşı"));
	if (hm == 3 && hd == 11) n = put(out, n, max, y, m, d, AGENDA_KANDIL, N_("Mevlid Kandili"));
	if (m == 5 && d == nth_sunday(y, 5, 2)) n = put(out, n, max, y, m, d, AGENDA_NOTABLE, N_("Anneler Günü"));
	if (m == 6 && d == nth_sunday(y, 6, 3)) n = put(out, n, max, y, m, d, AGENDA_NOTABLE, N_("Babalar Günü"));
	if (m == 11 && d == 10 && y >= 1939) n = put(out, n, max, y, m, d, AGENDA_NOTABLE, N_("Atatürk'ü Anma Günü"));
	if (m == 11 && d == 24) n = put(out, n, max, y, m, d, AGENDA_NOTABLE, N_("Öğretmenler Günü"));
	return n;
}

/* ---- notes ---- */

/* copies at most cap-1 bytes, cutting only between code points */
static void copy_utf8(char *dst, int cap, const char *src, int len)
{
	if (len > cap - 1) {
		len = cap - 1;
		while (len > 0 && ((unsigned char) src[len] & 0xC0) == 0x80) len--;
	}
	memcpy(dst, src, (size_t) len);
	dst[len] = '\0';
}

static int valid_date(int y, int m, int d) { return y >= 1 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= agenda_days_in_month(y, m); }

int agenda_notes_parse(const char *buf, AgendaNote *out, int max)
{
	int n = 0;
	const char *p = buf;
	while (p && *p && n < max) {
		const char *eol = strchr(p, '\n');
		int len = eol ? (int) (eol - p) : (int) strlen(p);
		if (len > 0 && p[len - 1] == '\r') len--;
		int y, m, d, used = 0;
		if (len > 11 && sscanf(p, "%4d-%2d-%2d%n", &y, &m, &d, &used) == 3 && used == 10 && valid_date(y, m, d)) {
			int yearly = p[10] == '*', tab = 10 + yearly;
			if (p[tab] == '\t' && len > tab + 1) {
				AgendaNote *o = &out[n];
				char raw[AGENDA_TEXT_MAX * 2];
				int k = 0;
				for (int i = tab + 1; i < len && k < (int) sizeof raw - 1; i++) {
					char c = p[i];
					if (c == '\\' && i + 1 < len) {
						c = p[++i];
						c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
					}
					raw[k++] = c;
				}
				copy_utf8(o->text, sizeof o->text, raw, k);
				o->y = y, o->m = m, o->d = d, o->yearly = yearly;
				if (o->text[0]) n++;
			}
		}
		p = eol ? eol + 1 : NULL;
	}
	return n;
}

int agenda_notes_format(const AgendaNote *n, int count, char *buf, int cap)
{
	int o = 0;
	for (int i = 0; i < count; i++) {
		char line[AGENDA_TEXT_MAX * 2 + 16];
		int k = snprintf(line, sizeof line, "%04d-%02d-%02d%s\t", n[i].y, n[i].m, n[i].d, n[i].yearly ? "*" : "");
		for (const char *s = n[i].text; *s && k < (int) sizeof line - 3; s++) {
			if (*s == '\\') line[k++] = '\\', line[k++] = '\\';
			else if (*s == '\n') line[k++] = '\\', line[k++] = 'n';
			else if (*s == '\t') line[k++] = '\\', line[k++] = 't';
			else if (*s != '\r') line[k++] = *s;
		}
		line[k++] = '\n';
		if (o + k + 1 > cap) return -1;
		memcpy(buf + o, line, (size_t) k);
		o += k;
	}
	if (cap > 0) buf[o] = '\0';
	return o;
}

#define FILE_CAP (AGENDA_MAX_NOTES * (AGENDA_TEXT_MAX * 2 + 16) + 1)

int agenda_notes_load(const char *path, AgendaNote *out, int max)
{
	char *buf = malloc(FILE_CAP);
	if (!buf) return 0;
	int len = kui_read_file(path, buf, FILE_CAP);
	int n = len > 0 ? agenda_notes_parse(buf, out, max) : 0;
	free(buf);
	return n;
}

int agenda_notes_save(const char *path, const AgendaNote *n, int count)
{
	char *buf = malloc(FILE_CAP);
	if (!buf) return -1;
	int len = agenda_notes_format(n, count, buf, FILE_CAP);
	int r = len < 0 ? -1 : kui_write_atomic(path, buf, len);
	free(buf);
	return r;
}

int agenda_note_on(const AgendaNote *n, int y, int m, int d)
{
	if (!n->yearly) return n->y == y && n->m == m && n->d == d;
	if (y < n->y || n->m != m) return 0;
	return n->d == d || (m == 2 && n->d == 29 && d == 28 && !leap(y));
}

int agenda_note_add(AgendaNote *n, int *count, int max, int y, int m, int d, const char *text, int yearly)
{
	const char *s = text;
	while (*s == ' ' || *s == '\n' || *s == '\t') s++;
	if (*count >= max || !*s || !valid_date(y, m, d)) return -1;
	AgendaNote *o = &n[*count];
	o->y = y, o->m = m, o->d = d, o->yearly = yearly ? 1 : 0;
	copy_utf8(o->text, sizeof o->text, text, (int) strlen(text));
	return (*count)++;
}

void agenda_note_delete(AgendaNote *n, int *count, int idx)
{
	if (idx < 0 || idx >= *count) return;
	memmove(&n[idx], &n[idx + 1], (size_t) (*count - idx - 1) * sizeof *n);
	(*count)--;
}

/* ---- items ---- */

int agenda_day_items(const AgendaNote *notes, int n_notes, int y, int m, int d, AgendaItem *out, int max)
{
	int n = agenda_holidays(y, m, d, out, max);
	for (int i = 0; i < n_notes && n < max; i++) {
		if (!agenda_note_on(&notes[i], y, m, d)) continue;
		n = put(out, n, max, y, m, d, AGENDA_NOTE, notes[i].text);
		out[n - 1].note = i;
		out[n - 1].yearly = notes[i].yearly;
	}
	return n;
}

int agenda_upcoming_notes(const AgendaNote *notes, int n_notes, int y, int m, int d, int days, AgendaItem *out, int max)
{
	int n = 0;
	for (int i = 0; i < days && n < max; i++) {
		n += agenda_day_items(notes, n_notes, y, m, d, out + n, max - n);
		agenda_add_days(&y, &m, &d, 1);
	}
	return n;
}

int agenda_upcoming(const char *data_dir, int y, int m, int d, int days, AgendaItem *out, int max)
{
	char path[512];
	AgendaNote *notes = malloc(AGENDA_MAX_NOTES * sizeof *notes);
	snprintf(path, sizeof path, "%s/notes.txt", data_dir);
	int nn = notes ? agenda_notes_load(path, notes, AGENDA_MAX_NOTES) : 0;
	int n = agenda_upcoming_notes(notes, nn, y, m, d, days, out, max);
	free(notes);
	return n;
}

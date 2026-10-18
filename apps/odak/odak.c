/* Odak logic: plans, the session state machine, wake times, the focus log and its stats. */
#include "odak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h" /* N_() only */

/* ---- plan ---- */

int odak_step(int v, int delta, int lo, int hi)
{
	v += delta;
	return v < lo ? lo : v > hi ? hi : v;
}

void odak_plan(int preset, int custom_focus, int custom_break, OdakPlan *p)
{
	switch (preset) {
	case ODAK_PRESET_LONG: *p = (OdakPlan){ 50, 10, 0, 0 }; break;
	case ODAK_PRESET_CUSTOM:
		*p = (OdakPlan){ odak_step(custom_focus, 0, ODAK_CUSTOM_MIN, ODAK_CUSTOM_MAX), odak_step(custom_break, 0, 0, ODAK_BREAK_MAX), 0, 0 };
		break;
	default: *p = (OdakPlan){ 25, 5, 15, 4 }; break;
	}
}

/* ---- session ---- */

int odak_is_break(OdakPhase p) { return p == ODAK_BREAK || p == ODAK_LONG_BREAK; }

static void run_phase(OdakSession *s, OdakPhase ph, long len, long now)
{
	s->phase = ph;
	s->length = len;
	s->left = len;
	s->phase_start = now;
	s->end = now + len;
	s->state = ODAK_RUNNING;
}

void odak_start(OdakSession *s, const OdakPlan *p, long now)
{
	memset(s, 0, sizeof *s);
	s->plan = *p;
	run_phase(s, ODAK_FOCUS, p->focus_min * 60L, now);
}

long odak_left(const OdakSession *s, long now)
{
	long l = s->state == ODAK_RUNNING ? s->end - now : s->state == ODAK_STOPPED || s->state == ODAK_FINISHED ? 0 : s->left;
	return l > 0 ? l : 0;
}

int odak_left_min(const OdakSession *s, long now) { return (int) ((odak_left(s, now) + 59) / 60); }

int odak_progress(const OdakSession *s, long now, int scale)
{
	if (s->length <= 0) return scale;
	long done = s->length - odak_left(s, now);
	if (done < 0) done = 0;
	return (int) (done * scale / s->length);
}

void odak_pause(OdakSession *s, long now)
{
	if (s->state != ODAK_RUNNING) return;
	s->left = odak_left(s, now);
	s->state = ODAK_PAUSED;
}

void odak_resume(OdakSession *s, long now)
{
	if (s->state != ODAK_PAUSED) return;
	s->end = now + s->left;
	s->state = ODAK_RUNNING;
}

void odak_extend(OdakSession *s, long now, int minutes)
{
	(void) now;
	long add = minutes * 60L;
	if (s->state == ODAK_RUNNING) s->end += add;
	else if (s->state == ODAK_PAUSED || s->state == ODAK_WAITING) s->left += add;
	else return;
	s->length += add;
}

int odak_due(const OdakSession *s, long now) { return s->state == ODAK_RUNNING && now >= s->end; }

int odak_complete(OdakSession *s, long now)
{
	(void) now;
	if (s->state != ODAK_RUNNING && s->state != ODAK_PAUSED) return 0;
	int logged = 0;
	if (s->phase == ODAK_FOCUS) {
		logged = (int) (s->length / 60);
		s->done_focus++;
		const OdakPlan *p = &s->plan;
		if (p->break_min <= 0) {
			s->state = ODAK_FINISHED;
			s->left = 0;
			return logged;
		}
		int lng = p->long_every > 0 && p->long_min > 0 && s->done_focus % p->long_every == 0;
		s->phase = lng ? ODAK_LONG_BREAK : ODAK_BREAK;
		s->length = (lng ? p->long_min : p->break_min) * 60L;
	} else {
		s->phase = ODAK_FOCUS;
		s->length = s->plan.focus_min * 60L;
	}
	s->left = s->length;
	s->state = ODAK_WAITING;
	return logged;
}

void odak_begin_next(OdakSession *s, long now)
{
	if (s->state != ODAK_WAITING) return;
	run_phase(s, s->phase, s->left, now); /* extended while waiting: that is the phase's length now */
}

int odak_stop(OdakSession *s, long now)
{
	int m = 0;
	if (s->phase == ODAK_FOCUS && (s->state == ODAK_RUNNING || s->state == ODAK_PAUSED))
		m = (int) ((s->length - odak_left(s, now)) / 60);
	s->state = ODAK_STOPPED;
	return m;
}

long odak_next_wake(const OdakSession *s, long now)
{
	if (s->state != ODAK_RUNNING) return 0;
	long rem = s->end - now;
	if (rem <= 0) return now;
	return s->end - 60 * ((rem - 1) / 60); /* the next whole minute before the end, or the end */
}

/* ---- log ---- */

int odak_log_format(const OdakEntry *e, char *out, int cap)
{
	int n = snprintf(out, (size_t) cap, "%ld %d %s\n", e->start, e->min, e->label);
	if (n < 0 || n >= cap) {
		if (cap > 0) out[0] = '\0';
		return 0;
	}
	return n;
}

int odak_log_parse(const char *buf, OdakEntry *out, int max)
{
	int n, total = 0; /* out is a ring of the newest max entries */
	while (buf && *buf) {
		const char *eol = strchr(buf, '\n');
		int len = eol ? (int) (eol - buf) : (int) strlen(buf);
		char line[160];
		if (len < (int) sizeof line) {
			memcpy(line, buf, (size_t) len);
			line[len] = '\0';
			long start;
			int min, used = 0;
			if (sscanf(line, "%ld %d%n", &start, &min, &used) == 2 && min > 0 && max > 0) {
				OdakEntry *e = &out[total % max];
				const char *l = line + used;
				while (*l == ' ') l++;
				e->start = start;
				e->min = min;
				odak_label_clean(l, e->label);
				total++;
			}
		}
		buf += len + (eol ? 1 : 0);
	}
	n = total < max ? total : max;
	if (total > max) { /* rotate so the oldest kept entry comes first */
		int first = total % max;
		OdakEntry *tmp = malloc(sizeof *tmp * (size_t) max);
		if (tmp) {
			for (int i = 0; i < max; i++) tmp[i] = out[(first + i) % max];
			memcpy(out, tmp, sizeof *tmp * (size_t) max);
			free(tmp);
		}
	}
	return n;
}

int odak_log_append(char *buf, int len, int cap, const char *line)
{
	int ll = (int) strlen(line);
	if (ll + 1 > cap) return len;
	int drop = 0;
	while (len - drop + ll + 1 > cap) {
		const char *eol = memchr(buf + drop, '\n', (size_t) (len - drop));
		drop = eol ? (int) (eol - buf) + 1 : len;
	}
	if (drop) {
		memmove(buf, buf + drop, (size_t) (len - drop));
		len -= drop;
	}
	memcpy(buf + len, line, (size_t) ll + 1);
	return len + ll;
}

/* ---- stats ---- */

long odak_day(long t, int utc_off)
{
	long l = t + utc_off;
	return l >= 0 ? l / 86400 : -((-l + 86399) / 86400);
}

static int has_day(const OdakEntry *e, int n, long day, int utc_off)
{
	for (int i = 0; i < n; i++)
		if (odak_day(e[i].start, utc_off) == day) return 1;
	return 0;
}

void odak_stats(const OdakEntry *e, int n, long now, int utc_off, OdakStats *st)
{
	memset(st, 0, sizeof *st);
	long today = odak_day(now, utc_off);
	long monday = today - ((today % 7 + 7 + 3) % 7); /* day 0 (1970-01-01) was a Thursday */
	/* per label: all of them first, then the largest ODAK_MAX_LABELS */
	static char lab[64][ODAK_LABEL_MAX];
	static int lmin[64];
	int nl = 0;
	for (int i = 0; i < n; i++) {
		long d = odak_day(e[i].start, utc_off);
		st->total_min += e[i].min;
		if (d == today) {
			st->today_min += e[i].min;
			st->today_n++;
		}
		if (d >= monday && d <= today) st->week_min += e[i].min;
		if (d > today - 7 && d <= today) st->day_min[6 - (today - d)] += e[i].min;
		int k = 0;
		while (k < nl && strcmp(lab[k], e[i].label) != 0) k++;
		if (k == nl && nl < 64) {
			snprintf(lab[nl], ODAK_LABEL_MAX, "%s", e[i].label);
			lmin[nl++] = 0;
		}
		if (k < nl) lmin[k] += e[i].min;
	}
	/* streak: days back from today (or from yesterday, before today's first session) that have a session */
	long d = today;
	if (!has_day(e, n, d, utc_off)) d--;
	while (has_day(e, n, d, utc_off)) {
		st->streak++;
		d--;
	}
	while (st->n_labels < ODAK_MAX_LABELS) { /* selection: the largest left (ties: the first seen) */
		int best = -1;
		for (int k = 0; k < nl; k++)
			if (lmin[k] >= 0 && (best < 0 || lmin[k] > lmin[best])) best = k;
		if (best < 0) break;
		snprintf(st->label[st->n_labels], ODAK_LABEL_MAX, "%s", lab[best]);
		st->label_min[st->n_labels++] = lmin[best];
		lmin[best] = -1;
	}
}

/* ---- labels ---- */

static const char *const default_labels[] = { N_("Okuma"), N_("Ders"), N_("İş"), N_("Yazı") };

void odak_label_clean(const char *in, char out[ODAK_LABEL_MAX])
{
	int o = 0;
	while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') in++;
	for (; *in && o < ODAK_LABEL_MAX - 1; in++) {
		unsigned char c = (unsigned char) *in;
		out[o++] = c == '\n' || c == '\r' || c == '\t' ? ' ' : (char) c;
	}
	/* cut at a code point: drop a trailing partial UTF-8 sequence */
	if (*in) {
		int k = o;
		while (k > 0 && ((unsigned char) out[k - 1] & 0xC0) == 0x80) k--;
		if (k > 0 && ((unsigned char) out[k - 1] & 0x80)) {
			unsigned char lead = (unsigned char) out[k - 1];
			int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
			if (o - (k - 1) < need) o = k - 1;
		}
	}
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = '\0';
}

int odak_labels_add(char l[][ODAK_LABEL_MAX], int n, int max, const char *label)
{
	char c[ODAK_LABEL_MAX];
	odak_label_clean(label, c);
	if (!c[0] || n >= max) return n;
	for (int i = 0; i < n; i++)
		if (!strcmp(l[i], c)) return n;
	memcpy(l[n], c, sizeof c);
	return n + 1;
}

int odak_labels_parse(const char *buf, char out[][ODAK_LABEL_MAX], int max)
{
	int n = 0;
	while (buf && *buf) {
		const char *eol = strchr(buf, '\n');
		int len = eol ? (int) (eol - buf) : (int) strlen(buf);
		char line[128];
		if (len >= (int) sizeof line) len = (int) sizeof line - 1;
		memcpy(line, buf, (size_t) len);
		line[len] = '\0';
		n = odak_labels_add(out, n, max, line);
		buf = eol ? eol + 1 : buf + strlen(buf);
	}
	for (size_t i = 0; n == 0 && i < sizeof default_labels / sizeof default_labels[0]; i++)
		if ((int) i < max) snprintf(out[i], ODAK_LABEL_MAX, "%s", default_labels[i]);
	if (n == 0) n = max < 4 ? max : 4;
	return n;
}

int odak_labels_format(char l[][ODAK_LABEL_MAX], int n, char *out, int cap)
{
	int o = 0;
	if (cap > 0) out[0] = '\0';
	for (int i = 0; i < n; i++) {
		int k = snprintf(out + o, (size_t) (cap - o), "%s\n", l[i]);
		if (k < 0 || k >= cap - o) break;
		o += k;
	}
	return o;
}

int odak_labels_remove(char l[][ODAK_LABEL_MAX], int n, int i)
{
	if (i < 0 || i >= n || n <= 1) return n;
	memmove(l[i], l[i + 1], (size_t) (n - i - 1) * ODAK_LABEL_MAX);
	return n - 1;
}

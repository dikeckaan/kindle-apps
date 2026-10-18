#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "wordle.h"

int wl_load(const char *path, WlList *l)
{
	memset(l, 0, sizeof *l);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	l->buf = malloc((size_t) size + 1);
	if (!l->buf || fread(l->buf, 1, (size_t) size, f) != (size_t) size) {
		fclose(f);
		wl_free(l);
		return -1;
	}
	fclose(f);
	l->buf[size] = '\0';
	int lines = 1;
	for (long i = 0; i < size; i++) lines += l->buf[i] == '\n';
	l->w = malloc(sizeof *l->w * (size_t) lines);
	if (!l->w) {
		wl_free(l);
		return -1;
	}
	for (char *p = l->buf; *p;) {
		char *nl = strchr(p, '\n');
		if (nl) *nl = '\0';
		if (*p) l->w[l->n++] = p;
		if (!nl) break;
		p = nl + 1;
	}
	return l->n;
}

void wl_free(WlList *l)
{
	free(l->buf);
	free((void *) l->w);
	memset(l, 0, sizeof *l);
}

int wl_contains(const WlList *l, const char *word)
{
	int lo = 0, hi = l->n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2, c = strcmp(l->w[mid], word);
		if (c == 0) return 1;
		if (c < 0) lo = mid + 1;
		else hi = mid - 1;
	}
	return 0;
}

int wl_pick(const WlList *l, unsigned seed)
{
	if (l->n <= 0) return -1;
	unsigned x = seed * 2654435761u + 0x9E3779B9u; /* spread consecutive seeds */
	x ^= x >> 16;
	x *= 0x85EBCA6Bu;
	x ^= x >> 13;
	return (int) (x % (unsigned) l->n);
}

int wl_normalize(const char *in, char out[WL_BYTES])
{
	int n = 0, o = 0;
	while (*in) {
		int cp;
		in += kui_utf8_decode(in, &cp);
		cp = kui_tr_fold_circumflex(kui_tr_lower(cp));
		if (++n > WL_LEN || cp == 0xFFFD) return -1; /* too long, or not valid UTF-8 */
		char enc[4];
		int k = kui_utf8_encode(cp, enc);
		if (o + k >= WL_BYTES) return -1; /* never write past the output */
		memcpy(out + o, enc, (size_t) k);
		o += k;
	}
	out[o] = '\0';
	return n;
}

static int letters(const char *s, int cp[WL_LEN])
{
	int n = 0;
	while (*s && n < WL_LEN) s += kui_utf8_decode(s, &cp[n++]);
	for (int i = n; i < WL_LEN; i++) cp[i] = 0;
	return n;
}

void wl_score(const char *answer, const char *guess, WlMark out[WL_LEN])
{
	int a[WL_LEN], g[WL_LEN], used[WL_LEN] = { 0 };
	letters(answer, a);
	letters(guess, g);
	for (int i = 0; i < WL_LEN; i++) {
		out[i] = WL_ABSENT;
		if (g[i] == a[i]) {
			out[i] = WL_CORRECT;
			used[i] = 1;
		}
	}
	/* a letter is "present" only while the answer still has an unmatched copy of it */
	for (int i = 0; i < WL_LEN; i++) {
		if (out[i] == WL_CORRECT) continue;
		for (int j = 0; j < WL_LEN; j++)
			if (!used[j] && a[j] == g[i]) {
				used[j] = 1;
				out[i] = WL_PRESENT;
				break;
			}
	}
}

void wl_kb_update(WlKb *kb, const char *guess, const WlMark m[WL_LEN])
{
	int g[WL_LEN];
	letters(guess, g);
	for (int i = 0; i < WL_LEN; i++) {
		int k = 0;
		while (k < kb->n && kb->cp[k] != g[i]) k++;
		if (k == kb->n) {
			if (kb->n == (int) (sizeof kb->cp / sizeof kb->cp[0])) continue;
			kb->cp[kb->n] = g[i];
			kb->m[kb->n++] = WL_UNKNOWN;
		}
		if (m[i] > kb->m[k]) kb->m[k] = m[i];
	}
}

WlMark wl_kb_get(const WlKb *kb, int cp)
{
	for (int k = 0; k < kb->n; k++)
		if (kb->cp[k] == cp) return kb->m[k];
	return WL_UNKNOWN;
}

void wl_stats_record(WlStats *s, int won, int tries)
{
	s->played++;
	if (won) {
		s->won++;
		s->streak++;
		if (s->streak > s->best) s->best = s->streak;
		if (tries >= 1 && tries <= WL_TRIES) s->dist[tries - 1]++;
	} else {
		s->streak = 0;
	}
}

int wl_stats_format(const WlStats *s, char *buf, int cap)
{
	int n = snprintf(buf, (size_t) cap, "v1 %d %d %d %d %d %d %d %d %d %d\n", s->played, s->won, s->streak, s->best, s->dist[0],
			 s->dist[1], s->dist[2], s->dist[3], s->dist[4], s->dist[5]);
	return n < cap ? n : -1;
}

/* chart counts are non-negative and add up to at most the games won */
static int dist_ok(const WlStats *t)
{
	long sum = 0;
	for (int i = 0; i < WL_TRIES; i++) {
		if (t->dist[i] < 0) return 0;
		sum += t->dist[i];
	}
	return sum <= t->won;
}

int wl_stats_parse(const char *buf, WlStats *s)
{
	WlStats t;
	if (sscanf(buf, "v1 %d %d %d %d %d %d %d %d %d %d", &t.played, &t.won, &t.streak, &t.best, &t.dist[0], &t.dist[1], &t.dist[2],
		   &t.dist[3], &t.dist[4], &t.dist[5]) != 10 ||
	    t.played < 0 || t.played > 1000000 || t.won < 0 || t.won > t.played || t.streak < 0 || t.streak > t.won ||
	    t.best < t.streak || t.best > t.won || !dist_ok(&t)) {
		memset(s, 0, sizeof *s);
		return -1;
	}
	*s = t;
	return 0;
}

int wl_game_format(const WlGame *g, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "v1\n%s\n%d\n", g->answer, g->n);
	for (int i = 0; i < g->n && o < cap; i++) o += snprintf(buf + o, (size_t) (cap - o), "%s\n", g->guess[i]);
	return o < cap ? o : -1;
}

/* copies one line into out (normalized, exactly WL_LEN letters); returns the next line or NULL */
static const char *take_word(const char *p, char out[WL_BYTES])
{
	const char *nl = strchr(p, '\n');
	if (!nl || nl - p >= WL_BYTES) return NULL;
	char tmp[WL_BYTES];
	memcpy(tmp, p, (size_t) (nl - p));
	tmp[nl - p] = '\0';
	if (wl_normalize(tmp, out) != WL_LEN || strcmp(out, tmp) != 0) return NULL;
	return nl + 1;
}

int wl_game_parse(const char *buf, WlGame *g)
{
	WlGame t;
	memset(&t, 0, sizeof t);
	if (strncmp(buf, "v1\n", 3) != 0) return -1;
	const char *p = take_word(buf + 3, t.answer);
	if (!p) return -1;
	char *end;
	long n = strtol(p, &end, 10);
	if (end == p || *end != '\n' || n < 0 || n >= WL_TRIES) return -1;
	t.n = (int) n;
	p = end + 1;
	for (int i = 0; i < t.n; i++)
		if (!(p = take_word(p, t.guess[i]))) return -1;
	*g = t;
	return 0;
}

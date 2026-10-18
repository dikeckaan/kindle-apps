/* Klondike rules: deal, moves, draw/recycle, undo snapshots, auto-complete, text save. */
#include "klondike.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char RANKS[] = "A23456789TJQK";
static const char SUITS_UP[] = "SHDC", SUITS_DOWN[] = "shdc";

void kl_init(Klondike *k) { memset(k, 0, sizeof *k); k->draw = 1; }

void kl_free(Klondike *k)
{
	free(k->hist);
	k->hist = NULL;
	k->nhist = k->cap = 0;
}

static unsigned rng_next(unsigned *s) /* xorshift32 */
{
	unsigned x = *s;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *s = x;
}

void kl_new(Klondike *k, unsigned seed, int draw)
{
	unsigned rng = seed * 2654435761u + 0x9E3779B9u;
	if (!rng) rng = 1;
	for (int i = 0; i < 8; i++) rng_next(&rng);
	unsigned char deck[52];
	for (int i = 0; i < 52; i++) deck[i] = (unsigned char) i;
	for (int i = 51; i > 0; i--) { /* Fisher-Yates */
		int j = (int) (rng_next(&rng) % (unsigned) (i + 1));
		unsigned char t = deck[i];
		deck[i] = deck[j];
		deck[j] = t;
	}
	memset(&k->s, 0, sizeof k->s);
	k->draw = draw == 3 ? 3 : 1;
	k->nhist = 0;
	int d = 0;
	for (int row = 0; row < 7; row++) /* deal row by row, like at the table */
		for (int t = row; t < 7; t++) k->s.c[KL_T0 + t][k->s.n[KL_T0 + t]++] = deck[d++];
	for (int t = 0; t < 7; t++) k->s.down[t] = (unsigned char) t;
	while (d < 52) k->s.c[KL_STOCK][k->s.n[KL_STOCK]++] = deck[d++];
}

static void push_undo(Klondike *k)
{
	if (k->nhist == k->cap) {
		int cap = k->cap ? k->cap * 2 : 64;
		KlState *h = realloc(k->hist, (size_t) cap * sizeof *h);
		if (!h) { /* out of memory: forget the oldest half rather than fail the move */
			int drop = k->nhist / 2;
			memmove(k->hist, k->hist + drop, (size_t) (k->nhist - drop) * sizeof *k->hist);
			k->nhist -= drop;
		} else {
			k->hist = h;
			k->cap = cap;
		}
	}
	if (k->nhist < k->cap) k->hist[k->nhist++] = k->s;
}

int kl_undo(Klondike *k)
{
	if (k->nhist == 0) return 0;
	k->s = k->hist[--k->nhist];
	return 1;
}

int kl_draw(Klondike *k)
{
	KlState *s = &k->s;
	if (s->n[KL_STOCK] == 0 && s->n[KL_WASTE] == 0) return 0;
	push_undo(k);
	if (s->n[KL_STOCK] == 0) { /* recycle: the first card drawn is on top of the stock again */
		int n = s->n[KL_WASTE];
		for (int i = 0; i < n; i++) s->c[KL_STOCK][i] = s->c[KL_WASTE][n - 1 - i];
		s->n[KL_STOCK] = (unsigned char) n;
		s->n[KL_WASTE] = 0;
		memset(s->c[KL_WASTE], 0, sizeof s->c[KL_WASTE]); /* unused slots stay zero: states compare with memcmp */
		return 1;
	}
	for (int i = 0; i < k->draw && s->n[KL_STOCK]; i++) {
		s->c[KL_WASTE][s->n[KL_WASTE]++] = s->c[KL_STOCK][--s->n[KL_STOCK]];
		s->c[KL_STOCK][s->n[KL_STOCK]] = 0;
	}
	return 1;
}

static int is_tab(int p) { return p >= KL_T0 && p < KL_T0 + 7; }
static int is_found(int p) { return p >= KL_F0 && p < KL_F0 + 4; }

int kl_can_move(const Klondike *k, int from, int index, int to)
{
	const KlState *s = &k->s;
	if (from < KL_WASTE || from >= KL_PILES || to < KL_F0 || to >= KL_PILES || from == to) return 0;
	int n = s->n[from];
	if (index < 0 || index >= n) return 0;
	if (is_tab(from) ? index < s->down[from - KL_T0] : index != n - 1) return 0;
	int c = s->c[from][index];
	if (is_found(to)) {
		if (index != n - 1 || is_found(from)) return 0;
		int m = s->n[to];
		if (m == 0) return kl_rank(c) == 1;
		int t = s->c[to][m - 1];
		return kl_suit(t) == kl_suit(c) && kl_rank(c) == kl_rank(t) + 1;
	}
	int m = s->n[to];
	if (m == 0) return kl_rank(c) == 13;
	int t = s->c[to][m - 1];
	return kl_red(t) != kl_red(c) && kl_rank(c) + 1 == kl_rank(t);
}

int kl_move(Klondike *k, int from, int index, int to)
{
	if (!kl_can_move(k, from, index, to)) return 0;
	push_undo(k);
	KlState *s = &k->s;
	int cnt = s->n[from] - index;
	memcpy(&s->c[to][s->n[to]], &s->c[from][index], (size_t) cnt);
	s->n[to] = (unsigned char) (s->n[to] + cnt);
	s->n[from] = (unsigned char) index;
	memset(&s->c[from][index], 0, (size_t) cnt);
	if (is_tab(from) && s->down[from - KL_T0] >= s->n[from] && s->n[from] > 0)
		s->down[from - KL_T0] = (unsigned char) (s->n[from] - 1); /* turn the exposed card up */
	if (is_tab(from) && s->n[from] == 0) s->down[from - KL_T0] = 0;
	return 1;
}

int kl_best_target(const Klondike *k, int from, int index)
{
	const KlState *s = &k->s;
	if (from >= 0 && from < KL_PILES && index == s->n[from] - 1)
		for (int f = KL_F0; f < KL_F0 + 4; f++)
			if (kl_can_move(k, from, index, f)) return f;
	int empty = -1;
	for (int i = 0; i < 7; i++) {
		int t = KL_T0 + (from >= KL_T0 ? (from - KL_T0 + 1 + i) % 7 : i); /* look rightwards from the source */
		if (!kl_can_move(k, from, index, t)) continue;
		if (s->n[t]) return t;
		if (empty < 0) empty = t;
	}
	if (empty >= 0 && is_tab(from) && index == 0) return -1; /* a K already heading its column stays */
	return empty;
}

int kl_auto_move(Klondike *k, int from, int index)
{
	int t = kl_best_target(k, from, index);
	return t >= 0 && kl_move(k, from, index, t) ? t : -1;
}

int kl_won(const Klondike *k)
{
	for (int f = 0; f < 4; f++)
		if (k->s.n[KL_F0 + f] != 13) return 0;
	return 1;
}

int kl_can_autocomplete(const Klondike *k)
{
	if (k->s.n[KL_STOCK] || k->s.n[KL_WASTE] || kl_won(k)) return 0;
	for (int t = 0; t < 7; t++)
		if (k->s.down[t]) return 0;
	return 1;
}

int kl_autocomplete_step(Klondike *k)
{
	if (!kl_can_autocomplete(k)) return 0;
	int best = -1, best_rank = 99, best_f = -1;
	for (int p = KL_T0; p < KL_T0 + 7; p++) {
		int n = k->s.n[p];
		if (!n) continue;
		int r = kl_rank(k->s.c[p][n - 1]);
		if (r >= best_rank) continue;
		for (int f = KL_F0; f < KL_F0 + 4; f++)
			if (kl_can_move(k, p, n - 1, f)) {
				best = p, best_rank = r, best_f = f;
				break;
			}
	}
	return best >= 0 && kl_move(k, best, k->s.n[best] - 1, best_f);
}

/* ---- save / load ---- */

#define STATE_LINE (2 + 52 * 2 + (KL_PILES - 1) + 1) /* "s " + cards + commas + '\n' */

static int write_state(char *o, char tag, const KlState *s)
{
	int w = 0;
	o[w++] = tag;
	o[w++] = ' ';
	for (int p = 0; p < KL_PILES; p++) {
		if (p) o[w++] = ',';
		for (int i = 0; i < s->n[p]; i++) {
			int c = s->c[p][i];
			int down = p == KL_STOCK || (is_tab(p) && i < s->down[p - KL_T0]);
			o[w++] = RANKS[kl_rank(c) - 1];
			o[w++] = (down ? SUITS_DOWN : SUITS_UP)[kl_suit(c)];
		}
	}
	o[w++] = '\n';
	return w;
}

int kl_save(const Klondike *k, char *buf, int cap)
{
	char head[64];
	int hn = 0, keep = k->nhist;
	for (;;) {
		hn = snprintf(head, sizeof head, "klondike v1\ndraw %d\nhist %d\n", k->draw, keep);
		long need = (long) hn + (long) STATE_LINE * (keep + 1) + 4 + 1; /* "end\n" + NUL */
		if (need <= cap) break;
		if (keep == 0) return -1;
		keep--;
	}
	int w = 0;
	memcpy(buf, head, (size_t) hn);
	w += hn;
	w += write_state(buf + w, 's', &k->s);
	for (int i = k->nhist - keep; i < k->nhist; i++) w += write_state(buf + w, 'h', &k->hist[i]);
	memcpy(buf + w, "end\n", 5);
	return w + 4;
}

/* parses one state line (after the tag and space); returns pointer past '\n' or NULL */
static const char *read_state(const char *p, KlState *s)
{
	memset(s, 0, sizeof *s);
	int seen[52] = { 0 }, total = 0;
	for (int pile = 0; pile < KL_PILES; pile++) {
		if (pile) {
			if (*p != ',') return NULL;
			p++;
		}
		while (*p && *p != ',' && *p != '\n') {
			const char *r = strchr(RANKS, p[0]);
			if (!r || !p[0] || !p[1]) return NULL;
			const char *su = strchr(SUITS_UP, p[1]), *sd = strchr(SUITS_DOWN, p[1]);
			if ((!su && !sd) || !p[1]) return NULL;
			int down = sd != NULL;
			int c = (int) ((su ? su - SUITS_UP : sd - SUITS_DOWN) * 13 + (r - RANKS));
			if (seen[c]++ || s->n[pile] >= KL_MAXPILE) return NULL;
			if (pile == KL_STOCK ? !down : !is_tab(pile) ? down : 0) return NULL;
			if (is_tab(pile)) {
				int t = pile - KL_T0;
				if (down) {
					if (s->down[t] != s->n[pile]) return NULL; /* face-down cards only under face-up ones */
					s->down[t]++;
				}
			}
			s->c[pile][s->n[pile]++] = (unsigned char) c;
			total++;
			p += 2;
		}
	}
	if (*p != '\n' || total != 52) return NULL;
	for (int f = KL_F0; f < KL_F0 + 4; f++)
		for (int i = 0; i < s->n[f]; i++)
			if (kl_rank(s->c[f][i]) != i + 1 || kl_suit(s->c[f][i]) != kl_suit(s->c[f][0])) return NULL;
	for (int t = 0; t < 7; t++) {
		int pile = KL_T0 + t, n = s->n[pile];
		if (n && s->down[t] >= n) return NULL; /* the top card is always face up */
		for (int i = s->down[t] + 1; i < n; i++) {
			int a = s->c[pile][i - 1], b = s->c[pile][i];
			if (kl_red(a) == kl_red(b) || kl_rank(b) + 1 != kl_rank(a)) return NULL;
		}
	}
	return p + 1;
}

int kl_load(Klondike *k, const char *buf)
{
	int draw = 0, nh = -1, used = 0;
	if (sscanf(buf, "klondike v1\ndraw %d\nhist %d\n%n", &draw, &nh, &used) != 2 || used == 0) return -1;
	const char *p = buf + used;
	if ((draw != 1 && draw != 3) || nh < 0 || (size_t) nh > strlen(p) / STATE_LINE) return -1; /* bounds the allocation */
	KlState cur;
	if (strncmp(p, "s ", 2) != 0 || !(p = read_state(p + 2, &cur))) return -1;
	KlState *h = nh ? malloc((size_t) nh * sizeof *h) : NULL;
	if (nh && !h) return -1;
	for (int i = 0; i < nh; i++)
		if (strncmp(p, "h ", 2) != 0 || !(p = read_state(p + 2, &h[i]))) {
			free(h);
			return -1;
		}
	if (strcmp(p, "end\n") != 0 && strcmp(p, "end") != 0) {
		free(h);
		return -1;
	}
	free(k->hist);
	k->s = cur;
	k->draw = draw;
	k->hist = h;
	k->nhist = k->cap = nh;
	return 0;
}

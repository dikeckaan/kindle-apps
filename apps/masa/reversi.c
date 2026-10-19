/* Reversi rules and AI (see reversi.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reversi.h"

#define NOT_A 0xFEFEFEFEFEFEFEFEULL /* every column but the first */
#define NOT_H 0x7F7F7F7F7F7F7F7FULL /* every column but the last */
#define CORNERS 0x8100000000000081ULL

static int pop(uint64_t b) { return __builtin_popcountll(b); }

/* one step in direction d (0..7: E, W, S, N, SE, SW, NE, NW) with wrap-around masked off */
static uint64_t step(uint64_t b, int d)
{
	switch (d) {
	case 0: return (b << 1) & NOT_A;
	case 1: return (b >> 1) & NOT_H;
	case 2: return b << 8;
	case 3: return b >> 8;
	case 4: return (b << 9) & NOT_A;
	case 5: return (b << 7) & NOT_H;
	case 6: return (b >> 7) & NOT_A;
	default: return (b >> 9) & NOT_H;
	}
}

static uint64_t moves_of(uint64_t P, uint64_t O)
{
	uint64_t empty = ~(P | O), m = 0;
	for (int d = 0; d < 8; d++) {
		uint64_t x = step(P, d) & O;
		for (int i = 0; i < 5; i++) x |= step(x, d) & O;
		m |= step(x, d) & empty;
	}
	return m;
}

static uint64_t flips_of(uint64_t P, uint64_t O, int sq)
{
	uint64_t bit = 1ULL << sq, f = 0;
	if ((P | O) & bit) return 0;
	for (int d = 0; d < 8; d++) {
		uint64_t line = 0, x = step(bit, d);
		while (x & O) {
			line |= x;
			x = step(x, d);
		}
		if (x & P) f |= line;
	}
	return f;
}

void rv_init(Rv *r)
{
	r->disc[0] = (1ULL << 27) | (1ULL << 36); /* black d5, e4 */
	r->disc[1] = (1ULL << 28) | (1ULL << 35); /* white e5, d4 */
	r->side = 0;
}

uint64_t rv_moves(const Rv *r) { return moves_of(r->disc[r->side], r->disc[!r->side]); }
uint64_t rv_flips(const Rv *r, int sq) { return sq < 0 || sq > 63 ? 0 : flips_of(r->disc[r->side], r->disc[!r->side], sq); }

int rv_play(Rv *r, int sq)
{
	if (sq == RV_PASS) {
		if (rv_moves(r)) return -1;
		r->side = !r->side;
		return 0;
	}
	uint64_t f = rv_flips(r, sq);
	if (!f) return -1;
	r->disc[r->side] |= f | (1ULL << sq);
	r->disc[!r->side] &= ~f;
	r->side = !r->side;
	return 0;
}

int rv_over(const Rv *r)
{
	return !moves_of(r->disc[0], r->disc[1]) && !moves_of(r->disc[1], r->disc[0]);
}

int rv_count(const Rv *r, int side) { return pop(r->disc[side & 1]); }

int rv_at(const Rv *r, int sq)
{
	uint64_t b = 1ULL << sq;
	return r->disc[0] & b ? 0 : r->disc[1] & b ? 1 : -1;
}

/* ---- evaluation ---- */

/* discs that can never be flipped, grown from owned corners: a disc is stable when, on each of the four lines
 * through it, a neighbour is a stable disc of its own colour or the board edge */
static uint64_t stable_of(uint64_t P)
{
	static const uint64_t edge[4] = { 0x8181818181818181ULL, 0xFF000000000000FFULL, 0xFF818181818181FFULL, 0xFF818181818181FFULL };
	uint64_t s = P & CORNERS;
	while (s) {
		uint64_t ns = P & ~s;
		static const int pa[4] = { 0, 2, 4, 5 }, pb[4] = { 1, 3, 7, 6 }; /* E-W, S-N, SE-NW, SW-NE */
		for (int pair = 0; pair < 4; pair++) ns &= step(s, pa[pair]) | step(s, pb[pair]) | edge[pair];
		if (!ns) break;
		s |= ns;
	}
	return s;
}

int rv_eval(const Rv *r)
{
	uint64_t P = r->disc[r->side], O = r->disc[!r->side], empty = ~(P | O);
	int mp = pop(moves_of(P, O)), mo = pop(moves_of(O, P));
	int score = 0;
	score += 12 * (mp - mo);
	score += 100 * (pop(P & CORNERS) - pop(O & CORNERS));
	static const int xsq[4] = { 9, 14, 49, 54 }, csq[4][2] = { { 1, 8 }, { 6, 15 }, { 48, 57 }, { 55, 62 } };
	static const int corner[4] = { 0, 7, 56, 63 };
	for (int i = 0; i < 4; i++) {
		if (!(empty >> corner[i] & 1)) continue;
		uint64_t xb = 1ULL << xsq[i], cb = (1ULL << csq[i][0]) | (1ULL << csq[i][1]);
		score -= 40 * (pop(P & xb) - pop(O & xb));
		score -= 12 * (pop(P & cb) - pop(O & cb));
	}
	score += 20 * (pop(stable_of(P)) - pop(stable_of(O)));
	uint64_t near_empty = 0;
	for (int d = 0; d < 8; d++) near_empty |= step(empty, d);
	score -= 5 * (pop(P & near_empty) - pop(O & near_empty));
	if (pop(empty) <= 12) score += 6 * (pop(P) - pop(O));
	return score;
}

/* ---- search ---- */

static const signed char sq_weight[64] = {
	 9, -4,  3,  2,  2,  3, -4,  9,
	-4, -6, -1, -1, -1, -1, -6, -4,
	 3, -1,  1,  0,  0,  1, -1,  3,
	 2, -1,  0,  0,  0,  0, -1,  2,
	 2, -1,  0,  0,  0,  0, -1,  2,
	 3, -1,  1,  0,  0,  1, -1,  3,
	-4, -6, -1, -1, -1, -1, -6, -4,
	 9, -4,  3,  2,  2,  3, -4,  9,
};

#define WIN 100000

typedef struct {
	long nodes, deadline;
	int aborted;
} Ctx;

static int final_score(uint64_t P, uint64_t O)
{
	int d = pop(P) - pop(O);
	return d > 0 ? WIN + d : d < 0 ? -WIN + d : 0;
}

static int order(uint64_t m, int *out)
{
	int n = 0;
	while (m) {
		int sq = __builtin_ctzll(m);
		m &= m - 1;
		int j = n++;
		while (j > 0 && sq_weight[out[j - 1]] < sq_weight[sq]) { out[j] = out[j - 1]; j--; }
		out[j] = sq;
	}
	return n;
}

static int search(Ctx *c, uint64_t P, uint64_t O, int depth, int alpha, int beta, int passed)
{
	if ((++c->nodes & 1023) == 0 && c->deadline && masa_now_ms() > c->deadline) c->aborted = 1;
	if (c->aborted) return 0;
	uint64_t m = moves_of(P, O);
	if (!m) {
		if (passed) return final_score(P, O);
		return -search(c, O, P, depth, -beta, -alpha, 1);
	}
	if (depth <= 0) {
		Rv r = { { P, O }, 0 };
		return rv_eval(&r);
	}
	int ms[32], n = order(m, ms), best = -WIN * 2;
	for (int i = 0; i < n; i++) {
		uint64_t f = flips_of(P, O, ms[i]);
		int v = -search(c, O & ~f, P | f | (1ULL << ms[i]), depth - 1, -beta, -alpha, 0);
		if (c->aborted) return 0;
		if (v > best) best = v;
		if (v > alpha) alpha = v;
		if (alpha >= beta) break;
	}
	return best;
}

int rv_ai(const Rv *r, const MasaLimits *lim, MasaStats *st)
{
	long t0 = masa_now_ms();
	uint64_t P = r->disc[r->side], O = r->disc[!r->side], m = moves_of(P, O);
	MasaStats dummy;
	if (!st) st = &dummy;
	memset(st, 0, sizeof *st);
	if (!m) return RV_PASS;
	int ms[32], n = order(m, ms);
	if (n == 1) return ms[0];
	int level = lim ? lim->level : 2;
	int maxd = lim && lim->max_depth ? lim->max_depth : level <= 1 ? 1 : level == 2 ? 3 : 60;
	int tms = lim && lim->time_ms ? lim->time_ms : level <= 1 ? 300 : level == 2 ? 1000 : 2500;
	uint32_t seed = lim && lim->seed ? lim->seed : (uint32_t) t0 | 1;
	int empties = 64 - pop(P | O);
	if (maxd > empties) maxd = empties;
	Ctx c = { 0, t0 + tms, 0 };
	int best = ms[0];
	for (int d = 1; d <= maxd; d++) {
		int alpha = -WIN * 2, bm = best;
		for (int i = 0; i < n; i++) {
			uint64_t f = flips_of(P, O, ms[i]);
			int v = -search(&c, O & ~f, P | f | (1ULL << ms[i]), d - 1, -WIN * 2, level <= 1 ? WIN * 2 : -alpha, 0);
			if (c.aborted) break;
			if (level <= 1) v += (int) (masa_rand(&seed) % 241) - 120; /* Kolay: a noisy one-move look */
			if (v > alpha) { alpha = v; bm = ms[i]; }
		}
		if (c.aborted) break;
		best = bm;
		st->depth = d;
		st->score = alpha;
		/* best move first for the next iteration */
		for (int i = 0; i < n; i++)
			if (ms[i] == best) { int t = ms[0]; ms[0] = ms[i]; ms[i] = t; break; }
		if (alpha > WIN / 2 || alpha < -WIN / 2) break; /* solved */
	}
	st->nodes = c.nodes;
	st->ms = masa_now_ms() - t0;
	return best;
}

/* ---- game record ---- */

void rv_game_new(RvGame *g, int mode, int level, int human)
{
	memset(g, 0, sizeof *g);
	rv_init(&g->pos);
	g->mode = mode;
	g->level = level;
	g->human = human;
}

int rv_game_play(RvGame *g, int sq)
{
	if (g->n >= RV_MAX_PLIES || rv_over(&g->pos)) return -1;
	if (rv_play(&g->pos, sq) != 0) return -1;
	g->hist[g->n++] = (unsigned char) sq;
	return 0;
}

int rv_game_undo(RvGame *g, int plies)
{
	if (plies > g->n) plies = g->n;
	int n = g->n - plies;
	rv_init(&g->pos);
	g->n = 0;
	for (int i = 0; i < n; i++) {
		rv_play(&g->pos, g->hist[i]);
		g->n++;
	}
	return plies;
}

int rv_game_save(const RvGame *g, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "reversi 1\nmode %d\nlevel %d\nhuman %d\nmoves", g->mode, g->level, g->human);
	for (int i = 0; i < g->n && o < cap; i++) o += snprintf(buf + o, (size_t) (cap - o), " %d", g->hist[i]);
	if (o < cap) o += snprintf(buf + o, (size_t) (cap - o), "\n");
	return o < cap ? o : -1;
}

int rv_game_load(RvGame *g, const char *buf)
{
	RvGame t;
	int mode, level, human, off = 0;
	if (sscanf(buf, "reversi 1\nmode %d\nlevel %d\nhuman %d\nmoves%n", &mode, &level, &human, &off) != 3 || !off) return -1;
	if (mode < 1 || mode > 2 || level < 1 || level > 3 || human < 0 || human > 1) return -1;
	rv_game_new(&t, mode, level, human);
	const char *p = buf + off;
	for (;;) {
		char *end;
		long v = strtol(p, &end, 10);
		if (end == p) break;
		if (v < 0 || v > RV_PASS || rv_game_play(&t, (int) v) != 0) return -1;
		p = end;
	}
	*g = t;
	return 0;
}

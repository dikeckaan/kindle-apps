/* Go rules, scoring and Monte Carlo AI (see go.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "go.h"

static uint64_t zob[GO_MAXP][3];
static int zob_ready;

static void zob_init(void)
{
	if (zob_ready) return;
	uint64_t s = 0x9E3779B97F4A7C15ULL;
	for (int p = 0; p < GO_MAXP; p++)
		for (int c = 0; c < 3; c++) {
			s += 0x9E3779B97F4A7C15ULL; /* splitmix64 */
			uint64_t z = s;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			zob[p][c] = z ^ (z >> 31);
		}
	zob_ready = 1;
}

#define IS_STONE(x) ((x) == GO_BLACK || (x) == GO_WHITE)
#define FOR_NB(g, p, q) for (int k_ = 0, q; k_ < 4 && ((q = (p) + ((const int[4]){ -(g)->W, -1, 1, (g)->W })[k_]), 1); k_++)

void go_init(Go *g, int n)
{
	zob_init();
	if (n < 5) n = 5;
	if (n > GO_MAXN) n = GO_MAXN;
	memset(g, 0, sizeof *g);
	g->n = n;
	g->W = n + 2;
	memset(g->c, GO_EDGE, sizeof g->c);
	for (int r = 0; r < n; r++)
		for (int c = 0; c < n; c++) g->c[go_pt(g, r, c)] = GO_EMPTY;
	g->side = GO_BLACK;
	g->last = -2;
}

static int on_board(const Go *g, int p) { return p > 0 && p < g->W * g->W && g->c[p] != GO_EDGE; }

/* stones of chain h next to p (= pseudo-liberties of h that p accounts for) */
static int adj_count(const Go *g, int p, int h)
{
	int k = 0;
	FOR_NB(g, p, q) if (IS_STONE(g->c[q]) && g->head[q] == h) k++;
	return k;
}

int go_legal(const Go *g, int p)
{
	if (!on_board(g, p) || g->c[p] != GO_EMPTY || p == g->ko) return 0;
	int me = g->side;
	FOR_NB(g, p, q) {
		int c = g->c[q];
		if (c == GO_EMPTY) return 1;
		if (!IS_STONE(c)) continue;
		int h = g->head[q], a = adj_count(g, p, h);
		if (c == me ? g->libs[h] > a : g->libs[h] == a) return 1; /* own chain keeps another liberty / capture */
	}
	return 0; /* suicide */
}

static int merge(Go *g, int a, int b)
{
	if (g->size[a] < g->size[b]) { int t = a; a = b; b = t; }
	int r = b;
	do { g->head[r] = (short) a; r = g->next[r]; } while (r != b);
	short t = g->next[a]; g->next[a] = g->next[b]; g->next[b] = t;
	g->libs[a] = (short) (g->libs[a] + g->libs[b]);
	g->size[a] = (short) (g->size[a] + g->size[b]);
	return a;
}

static int remove_chain(Go *g, int h)
{
	int col = g->c[h], cnt = 0, r = h;
	do {
		g->c[r] = GO_EMPTY;
		g->hash ^= zob[r][col];
		cnt++;
		r = g->next[r];
	} while (r != h);
	r = h;
	do {
		FOR_NB(g, r, q) if (IS_STONE(g->c[q])) g->libs[g->head[q]]++;
		r = g->next[r];
	} while (r != h);
	return cnt;
}

int go_play(Go *g, int p)
{
	if (p == GO_PASS) {
		g->passes++;
		g->ko = 0;
		g->last = GO_PASS;
		g->side = 3 - g->side;
		return 0;
	}
	if (!go_legal(g, p)) return -1;
	int me = g->side, op = 3 - me;
	g->c[p] = (unsigned char) me;
	g->hash ^= zob[p][me];
	g->head[p] = g->next[p] = (short) p;
	g->size[p] = 1;
	g->libs[p] = 0;
	FOR_NB(g, p, q) {
		if (g->c[q] == GO_EMPTY) g->libs[p]++;
		else if (IS_STONE(g->c[q])) g->libs[g->head[q]]--;
	}
	int h = p;
	FOR_NB(g, p, q) if (g->c[q] == me && g->head[q] != h) h = merge(g, h, g->head[q]);
	int captured = 0, last_cap = 0;
	FOR_NB(g, p, q) {
		if (g->c[q] == op && g->libs[g->head[q]] == 0) {
			captured += remove_chain(g, g->head[q]);
			last_cap = q;
		}
	}
	h = g->head[p];
	g->ko = captured == 1 && g->size[h] == 1 && g->libs[h] == 1 ? last_cap : 0;
	g->caps[me] += captured;
	g->passes = 0;
	g->last = p;
	g->side = op;
	return captured;
}

/* real liberties of the chain at p: count (up to `stop`) and the first one found */
static int chain_libs(const Go *g, int p, int stop, int *first)
{
	unsigned char seen[GO_MAXP];
	memset(seen, 0, (size_t) (g->W * g->W));
	int n = 0, r = p;
	do {
		FOR_NB(g, r, q) {
			if (g->c[q] == GO_EMPTY && !seen[q]) {
				seen[q] = 1;
				if (n++ == 0 && first) *first = q;
				if (n >= stop) return n;
			}
		}
		r = g->next[r];
	} while (r != p);
	return n;
}

int go_libs(const Go *g, int p)
{
	if (!on_board(g, p) || !IS_STONE(g->c[p])) return 0;
	return chain_libs(g, g->head[p], GO_MAXP, NULL);
}

int go_is_eye(const Go *g, int p, int color)
{
	if (g->c[p] != GO_EMPTY) return 0;
	int edge = 0;
	FOR_NB(g, p, q) {
		if (g->c[q] == GO_EDGE) edge = 1;
		else if (g->c[q] != color) return 0;
	}
	int W = g->W, bad = 0;
	const int diag[4] = { -W - 1, -W + 1, W - 1, W + 1 };
	for (int i = 0; i < 4; i++)
		if (g->c[p + diag[i]] == 3 - color) bad++;
	return edge ? bad == 0 : bad < 2;
}

/* ---- scoring ---- */

void go_area(const Go *g, const unsigned char *dead, int *black, int *white, unsigned char *owner)
{
	unsigned char t[GO_MAXP], seen[GO_MAXP];
	short stack[GO_MAXP], region[GO_MAXP];
	int P = g->W * g->W, sc[3] = { 0, 0, 0 };
	memcpy(t, g->c, (size_t) P);
	if (dead)
		for (int p = 0; p < P; p++)
			if (dead[p] && IS_STONE(t[p])) t[p] = GO_EMPTY;
	memset(seen, 0, (size_t) P);
	if (owner) memset(owner, GO_EMPTY, (size_t) P);
	for (int p = 0; p < P; p++) {
		if (IS_STONE(t[p])) {
			sc[t[p]]++;
			if (owner) owner[p] = t[p];
			continue;
		}
		if (t[p] != GO_EMPTY || seen[p]) continue;
		int sp = 0, nr = 0, border = 0;
		stack[sp++] = (short) p;
		seen[p] = 1;
		while (sp) {
			int r = stack[--sp];
			region[nr++] = (short) r;
			FOR_NB(g, r, q) {
				if (t[q] == GO_EMPTY && !seen[q]) { seen[q] = 1; stack[sp++] = (short) q; }
				else if (IS_STONE(t[q])) border |= t[q];
			}
		}
		if (border == GO_BLACK || border == GO_WHITE) {
			sc[border] += nr;
			if (owner)
				for (int i = 0; i < nr; i++) owner[region[i]] = (unsigned char) border;
		}
	}
	if (black) *black = sc[GO_BLACK];
	if (white) *white = sc[GO_WHITE];
}

int go_score2(const Go *g, const unsigned char *dead)
{
	int b, w;
	go_area(g, dead, &b, &w, NULL);
	return 2 * (b - w) - GO_KOMI2;
}

void go_toggle_dead(const Go *g, int p, unsigned char *dead)
{
	if (!on_board(g, p) || !IS_STONE(g->c[p])) return;
	int h = g->head[p], r = h, v = !dead[p];
	do { dead[r] = (unsigned char) v; r = g->next[r]; } while (r != h);
}

/* ---- playouts ---- */

typedef struct {
	unsigned char col[GO_MAXP]; /* colour that first played each point (AMAF), 0 = nobody */
	short idx[GO_MAXP];         /* move number of that first play */
	int n;                      /* moves so far */
} Amaf;

static void amaf_note(Amaf *a, int p, int color)
{
	if (p > 0 && !a->col[p]) {
		a->col[p] = (unsigned char) color;
		a->idx[p] = (short) a->n;
	}
	a->n++;
}

static int npts, pts_n;
static short pts[GO_MAXN * GO_MAXN];

static void pts_init(const Go *g)
{
	if (pts_n == g->n) return;
	npts = 0;
	for (int r = 0; r < g->n; r++)
		for (int c = 0; c < g->n; c++) pts[npts++] = (short) go_pt(g, r, c);
	pts_n = g->n;
}

/* a cheap tactical reply to the last move: capture a chain it left in atari, or save an own chain it put in atari */
static int tactical_move(const Go *g, uint32_t *rng)
{
	int L = g->last;
	if (L <= 0 || !IS_STONE(g->c[L])) return 0;
	if (masa_rand(rng) % 10 == 0) return 0;
	int lib;
	if (chain_libs(g, g->head[L], 2, &lib) == 1 && go_legal(g, lib)) return lib;
	FOR_NB(g, L, q) {
		if (g->c[q] == g->side && chain_libs(g, g->head[q], 2, &lib) == 1 && go_legal(g, lib)) {
			/* only if the escape gains liberties */
			Go t = *g;
			go_play(&t, lib);
			if (go_libs(&t, lib) >= 3) return lib;
		}
	}
	return 0;
}

static int random_move(const Go *g, uint32_t *rng)
{
	int s = (int) (masa_rand(rng) % (uint32_t) npts);
	for (int i = 0; i < npts; i++) {
		int p = pts[(s + i) % npts];
		if (g->c[p] == GO_EMPTY && !go_is_eye(g, p, g->side) && go_legal(g, p)) return p;
	}
	return GO_PASS;
}

/* fast final count for playouts: stones plus empty points whose neighbours are all one colour */
static int playout_winner(const Go *g)
{
	int s[3] = { 0, 0, 0 };
	for (int i = 0; i < npts; i++) {
		int p = pts[i], c = g->c[p];
		if (c == GO_EMPTY) {
			int m = 0;
			FOR_NB(g, p, q) if (IS_STONE(g->c[q])) m |= g->c[q];
			if (m == GO_BLACK || m == GO_WHITE) s[m]++;
		} else {
			s[c]++;
		}
	}
	return 2 * (s[GO_BLACK] - s[GO_WHITE]) > GO_KOMI2 ? GO_BLACK : GO_WHITE;
}

static int playout(Go *b, uint32_t *rng, Amaf *a)
{
	int limit = 3 * b->n * b->n;
	for (int m = 0; m < limit && b->passes < 2; m++) {
		int mv = tactical_move(b, rng);
		if (!mv) mv = random_move(b, rng);
		int col = b->side;
		go_play(b, mv);
		if (a) amaf_note(a, mv, col);
	}
	return playout_winner(b);
}

void go_estimate_dead(const Go *g, int playouts, uint32_t seed, unsigned char *dead)
{
	static int own[3][GO_MAXP]; /* playouts in which each colour ended up owning the point */
	int P = g->W * g->W;
	pts_init(g);
	memset(own, 0, sizeof own);
	memset(dead, 0, (size_t) P);
	if (seed == 0) seed = 12345;
	if (playouts < 1) playouts = 1;
	for (int i = 0; i < playouts; i++) {
		Go b = *g;
		b.passes = 0;
		playout(&b, &seed, NULL);
		for (int k = 0; k < npts; k++) {
			int p = pts[k], c = b.c[p];
			if (c == GO_EMPTY) {
				int m = 0;
				FOR_NB(&b, p, q) if (IS_STONE(b.c[q])) m |= b.c[q];
				c = m;
			}
			if (IS_STONE(c)) own[c][p]++;
		}
	}
	/* a chain is dead when its colour kept less than a third of its points on average */
	for (int k = 0; k < npts; k++) {
		int p = pts[k];
		if (!IS_STONE(g->c[p]) || g->head[p] != p) continue;
		long sum = 0;
		int r = p;
		do { sum += own[g->c[p]][r]; r = g->next[r]; } while (r != p);
		if (sum * 3 < (long) g->size[p] * playouts) {
			r = p;
			do { dead[r] = 1; r = g->next[r]; } while (r != p);
		}
	}
}

/* ---- UCT with RAVE ---- */

typedef struct {
	short move, nch;
	int first; /* first child in the pool */
	int v;     /* real visits */
	float N, W, rN, rW;
} Node;

#define GO_NODES 300000
static Node pool[GO_NODES];
static int npool, last_winrate = 50;

int go_ai_winrate(void) { return last_winrate; }

/* stones the move at p captures (chains whose only liberty is p), and whether it is the only liberty of an own chain */
static int capture_size(const Go *g, int p, int *saves)
{
	int k = 0;
	*saves = 0;
	FOR_NB(g, p, q) {
		if (!IS_STONE(g->c[q])) continue;
		int h = g->head[q];
		if (g->libs[h] != adj_count(g, p, h)) continue;
		if (g->c[q] == g->side) *saves = 1;
		else k += g->size[h];
	}
	return k;
}

static const unsigned char *root_forbid; /* root moves ruled out by superko */

static void expand(Node *nd, const Go *g, const unsigned char *forbid)
{
	int stones = 0;
	for (int i = 0; i < npts; i++) stones += IS_STONE(g->c[pts[i]]);
	nd->first = npool;
	nd->nch = 0;
	for (int i = 0; i < npts && npool < GO_NODES; i++) {
		int p = pts[i];
		if (g->c[p] != GO_EMPTY || go_is_eye(g, p, g->side) || !go_legal(g, p) || (forbid && forbid[p])) continue;
		int r = go_row(g, p), c = go_col(g, p), n = g->n;
		int line = r < c ? r : c;
		if (n - 1 - r < line) line = n - 1 - r;
		if (n - 1 - c < line) line = n - 1 - c;
		float pn = 10, pw = 5;
		int saves, k = capture_size(g, p, &saves);
		if (!k && saves) { /* an escape from atari: only worth it when it gains liberties */
			Go t = *g;
			go_play(&t, p);
			if (go_libs(&t, p) >= 3) saves = 2;
		}
		if (k) { pn = 20; pw = pn * (k >= 4 ? 0.95f : 0.75f + 0.05f * (float) k); }
		else if (saves == 2) { pn = 20; pw = 17; }
		else if (line == 0) pw = stones < n * n / 2 ? 1.5f : 4;
		else if (line == 1 && stones < n * n / 4) pw = 4;
		else if (line >= 2 && stones < n * n / 4) pw = 6;
		pool[npool++] = (Node){ (short) p, 0, 0, 0, pn, pw, 0, 0 };
		nd->nch++;
	}
}

static Node *select_child(Node *nd)
{
	Node *best = NULL;
	double bv = -1e9, lp = log((double) nd->v + 1);
	for (int i = 0; i < nd->nch; i++) {
		Node *c = &pool[nd->first + i];
		double q = c->W / c->N;
		double rq = c->rN > 0 ? c->rW / c->rN : q;
		double beta = c->rN / (c->rN + c->N + c->rN * c->N / 2000.0);
		double v = (1 - beta) * q + beta * rq + 0.15 * sqrt(lp / (c->v + 1));
		if (v > bv) { bv = v; best = c; }
	}
	return best;
}

int go_ai(const Go *g, const MasaLimits *lim, MasaStats *st)
{
	long t0 = masa_now_ms();
	MasaStats dummy;
	if (!st) st = &dummy;
	memset(st, 0, sizeof *st);
	pts_init(g);
	int level = lim ? lim->level : 2;
	int tms = lim && lim->time_ms ? lim->time_ms : level <= 1 ? 700 : level == 2 ? 1800 : 3000;
	int maxp = lim && lim->max_playouts ? lim->max_playouts : level <= 1 ? 600 : 0;
	uint32_t seed = lim && lim->seed ? lim->seed : (uint32_t) t0 | 1;
	int me = g->side;

	/* pass when the count (estimated dead stones taken off) already wins and either the opponent just passed or
	 * the board is settled (every empty point belongs to someone): further moves would only fill own territory */
	int stones0 = 0;
	for (int i = 0; i < npts; i++) stones0 += IS_STONE(g->c[pts[i]]);
	if (g->last == GO_PASS || stones0 > g->n * g->n / 2) {
		static unsigned char dead[GO_MAXP], own[GO_MAXP];
		go_estimate_dead(g, 150, seed, dead);
		int b, w;
		go_area(g, dead, &b, &w, own);
		int s = 2 * (b - w) - GO_KOMI2, settled = 1;
		for (int i = 0; i < npts && settled; i++)
			if (own[pts[i]] == GO_EMPTY) settled = 0; /* dame left: worth a point under area scoring */
		if ((me == GO_BLACK) == (s > 0) && (g->last == GO_PASS || settled)) { last_winrate = 99; return GO_PASS; }
	}

	npool = 0;
	Node *root = &pool[npool++];
	*root = (Node){ 0, 0, 0, 0, 0, 0, 0, 0 };
	expand(root, g, root_forbid);
	if (root->nch == 0) { last_winrate = 50; return GO_PASS; }

	Node *path[GO_MAX_PLIES];
	static Amaf a;
	long playouts = 0;
	for (;;) {
		if (maxp && playouts >= maxp) break;
		if (tms && (playouts & 7) == 0 && masa_now_ms() - t0 > tms) break;
		Go b = *g;
		memset(a.col, 0, sizeof a.col);
		a.n = 0;
		int depth = 0;
		Node *nd = root;
		path[depth++] = nd;
		while (nd->nch > 0 && depth < GO_MAX_PLIES - 1) {
			Node *c = select_child(nd);
			int col = b.side;
			go_play(&b, c->move);
			amaf_note(&a, c->move, col);
			nd = c;
			path[depth++] = nd;
			if (nd->v >= 1 && nd->nch == 0 && npool + npts < GO_NODES) {
				expand(nd, &b, NULL);
				if (nd->nch == 0) break;
			} else if (nd->nch == 0) {
				break;
			}
		}
		int winner = playout(&b, &seed, &a);
		playouts++;
		/* back up: path[i] (i >= 1) was played by the side to move at path[i-1] */
		int col = me;
		for (int i = 0; i < depth; i++) {
			Node *p = path[i];
			p->v++;
			if (i > 0) {
				p->N += 1;
				p->W += winner == 3 - col ? 1 : 0; /* path[i]'s move was made by the opponent of the side to move now */
			}
			/* RAVE for the children of p: moves the side to move at p played first, at or after this depth */
			for (int k = 0; k < p->nch; k++) {
				Node *c = &pool[p->first + k];
				int m = c->move;
				if (a.col[m] == col && a.idx[m] >= i) {
					c->rN += 1;
					c->rW += winner == col ? 1 : 0;
				}
			}
			col = 3 - col;
		}
	}
	Node *best = &pool[root->first];
	for (int i = 1; i < root->nch; i++)
		if (pool[root->first + i].v > best->v) best = &pool[root->first + i];
	double wr = best->W / best->N;
	last_winrate = (int) (wr * 100 + 0.5);
	st->nodes = playouts;
	st->depth = npool;
	st->score = last_winrate;
	st->ms = masa_now_ms() - t0;
	int stones = 0;
	for (int i = 0; i < npts; i++) stones += IS_STONE(g->c[pts[i]]);
	if (level >= 2 && playouts >= 500 && wr < 0.04 && stones > g->n * g->n / 3) return GO_RESIGN;
	return best->move;
}

int go_game_ai(const GoGame *g, const MasaLimits *lim, MasaStats *st)
{
	static unsigned char forbid[GO_MAXP];
	memset(forbid, 0, sizeof forbid);
	const Go *b = &g->pos;
	for (int p = 0; p < b->W * b->W; p++)
		if (b->c[p] == GO_EMPTY && go_legal(b, p) && !go_game_legal(g, p)) forbid[p] = 1;
	root_forbid = forbid;
	int m = go_ai(b, lim, st);
	root_forbid = NULL;
	return m;
}

/* ---- game record ---- */

void go_game_new(GoGame *g, int size, int mode, int level, int human)
{
	memset(g, 0, sizeof *g);
	go_init(&g->pos, size);
	g->size = g->pos.n;
	g->mode = mode;
	g->level = level;
	g->human = human;
	g->hashes[0] = g->pos.hash;
}

int go_game_over(const GoGame *g) { return g->resigned || g->pos.passes >= 2; }

int go_game_legal(const GoGame *g, int p)
{
	if (go_game_over(g)) return 0;
	if (p == GO_PASS || p == GO_RESIGN) return 1;
	if (!go_legal(&g->pos, p)) return 0;
	Go t = g->pos;
	go_play(&t, p);
	for (int i = 0; i <= g->n; i++)
		if (g->hashes[i] == t.hash) return 0; /* positional superko */
	return 1;
}

int go_game_play(GoGame *g, int p)
{
	if (g->n >= GO_MAX_PLIES || !go_game_legal(g, p)) return -1;
	if (p == GO_RESIGN) g->resigned = g->pos.side;
	else go_play(&g->pos, p);
	g->hist[g->n++] = (short) p;
	g->hashes[g->n] = g->pos.hash;
	return 0;
}

int go_game_undo(GoGame *g, int plies)
{
	if (plies > g->n) plies = g->n;
	int n = g->n - plies;
	short h[GO_MAX_PLIES];
	memcpy(h, g->hist, sizeof h);
	go_game_new(g, g->size, g->mode, g->level, g->human);
	for (int i = 0; i < n; i++) go_game_play(g, h[i]);
	return plies;
}

int go_game_save(const GoGame *g, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "go 1\nsize %d\nmode %d\nlevel %d\nhuman %d\nmoves", g->size, g->mode, g->level, g->human);
	for (int i = 0; i < g->n && o < cap; i++) {
		int p = g->hist[i];
		if (p == GO_PASS) o += snprintf(buf + o, (size_t) (cap - o), " p");
		else if (p == GO_RESIGN) o += snprintf(buf + o, (size_t) (cap - o), " r");
		else o += snprintf(buf + o, (size_t) (cap - o), " %d", go_row(&g->pos, p) * g->size + go_col(&g->pos, p));
	}
	if (o < cap) o += snprintf(buf + o, (size_t) (cap - o), "\n");
	return o < cap ? o : -1;
}

int go_game_load(GoGame *g, const char *buf)
{
	static GoGame t;
	int size, mode, level, human, off = 0;
	if (sscanf(buf, "go 1\nsize %d\nmode %d\nlevel %d\nhuman %d\nmoves%n", &size, &mode, &level, &human, &off) != 4 || !off) return -1;
	if ((size != 9 && size != 13) || mode < 1 || mode > 2 || level < 1 || level > 3 || (human != GO_BLACK && human != GO_WHITE)) return -1;
	go_game_new(&t, size, mode, level, human);
	const char *p = buf + off;
	for (;;) {
		while (*p == ' ') p++;
		if (*p == 'p' || *p == 'r') {
			if (go_game_play(&t, *p == 'p' ? GO_PASS : GO_RESIGN) != 0) return -1;
			p++;
			continue;
		}
		char *end;
		long v = strtol(p, &end, 10);
		if (end == p) break;
		if (v < 0 || v >= size * size || go_game_play(&t, go_pt(&t.pos, (int) v / size, (int) v % size)) != 0) return -1;
		p = end;
	}
	*g = t;
	return 0;
}

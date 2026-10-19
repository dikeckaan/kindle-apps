#include <stdlib.h>
#include "masa/dama.h"
#include "masa/go.h"
#include "masa/reversi.h"
#include "t.h"

/* ================= Reversi ================= */

static int rv_sq(const char *s) { return (8 - (s[1] - '0')) * 8 + (s[0] - 'a'); } /* "a8" = 0, "h1" = 63 */

static void test_rv_start(void)
{
	Rv r;
	rv_init(&r);
	T_EQ_INT(rv_count(&r, 0), 2);
	T_EQ_INT(rv_count(&r, 1), 2);
	uint64_t m = rv_moves(&r);
	T_EQ_INT(__builtin_popcountll(m), 4);
	T_ASSERT(m >> rv_sq("d3") & 1);
	T_ASSERT(m >> rv_sq("c4") & 1);
	T_ASSERT(m >> rv_sq("f5") & 1);
	T_ASSERT(m >> rv_sq("e6") & 1);
	T_EQ_INT(rv_play(&r, rv_sq("a1")), -1);
	T_EQ_INT(rv_play(&r, RV_PASS), -1); /* passing is only allowed without a move */
	T_EQ_INT(rv_play(&r, rv_sq("d3")), 0);
	T_EQ_INT(rv_count(&r, 0), 4);
	T_EQ_INT(rv_count(&r, 1), 1);
	T_EQ_INT(rv_at(&r, rv_sq("d4")), 0); /* flipped */
	T_EQ_INT(r.side, 1);
}

static void test_rv_multi_direction_flips(void)
{
	Rv r = { { 0, 0 }, 0 };
	/* d4 empty, with white lines towards black discs in five directions */
	r.disc[0] = 1ULL << rv_sq("d1") | 1ULL << rv_sq("a4") | 1ULL << rv_sq("g4") | 1ULL << rv_sq("d7") | 1ULL << rv_sq("g7");
	r.disc[1] = 1ULL << rv_sq("d2") | 1ULL << rv_sq("d3") | 1ULL << rv_sq("b4") | 1ULL << rv_sq("c4") | 1ULL << rv_sq("e4") |
		    1ULL << rv_sq("f4") | 1ULL << rv_sq("d5") | 1ULL << rv_sq("d6") | 1ULL << rv_sq("e5") | 1ULL << rv_sq("f6");
	uint64_t f = rv_flips(&r, rv_sq("d4"));
	T_EQ_INT(__builtin_popcountll(f), 10);
	T_EQ_INT(rv_play(&r, rv_sq("d4")), 0);
	T_EQ_INT(rv_count(&r, 1), 0);
	T_EQ_INT(rv_count(&r, 0), 16);
}

static void test_rv_pass_and_end(void)
{
	Rv r = { { 0, 0 }, 1 };
	/* white to move with no move at all; black can still move: white must pass */
	r.disc[0] = 1ULL << rv_sq("a1") | 1ULL << rv_sq("b1");
	r.disc[1] = 1ULL << rv_sq("c1");
	T_EQ_INT(__builtin_popcountll(rv_moves(&r)), 0);
	T_ASSERT(!rv_over(&r)); /* black can play d1 */
	T_EQ_INT(rv_play(&r, rv_sq("d1")), -1);
	T_EQ_INT(rv_play(&r, RV_PASS), 0);
	T_EQ_INT(r.side, 0);
	T_EQ_INT(rv_play(&r, rv_sq("d1")), 0);
	T_ASSERT(rv_over(&r)); /* all black */
	T_EQ_INT(rv_ai(&r, NULL, NULL), RV_PASS);
}

static void test_rv_wraparound(void)
{
	Rv r = { { 0, 0 }, 0 };
	/* a flank must not wrap around the board edge (h4 white, a3 black) */
	r.disc[1] = 1ULL << rv_sq("h4");
	r.disc[0] = 1ULL << rv_sq("a3");
	T_EQ_INT(rv_flips(&r, rv_sq("g4")), 0);
	T_EQ_INT(__builtin_popcountll(rv_moves(&r)), 0);
}

static void test_rv_game_save_undo(void)
{
	RvGame g, h;
	rv_game_new(&g, 1, 2, 0);
	MasaLimits lim = { 2, 0, 2, 0, 7 };
	for (int i = 0; i < 20 && !rv_over(&g.pos); i++) T_EQ_INT(rv_game_play(&g, rv_ai(&g.pos, &lim, NULL)), 0);
	char buf[2048];
	T_ASSERT(rv_game_save(&g, buf, sizeof buf) > 0);
	T_EQ_INT(rv_game_load(&h, buf), 0);
	T_EQ_INT(h.n, g.n);
	T_ASSERT(h.pos.disc[0] == g.pos.disc[0] && h.pos.disc[1] == g.pos.disc[1]);
	Rv before = g.pos;
	int last = g.hist[g.n - 1];
	T_EQ_INT(rv_game_undo(&g, 1), 1);
	T_EQ_INT(rv_game_play(&g, last), 0);
	T_ASSERT(before.disc[0] == g.pos.disc[0] && before.side == g.pos.side);
	T_EQ_INT(rv_game_load(&h, "reversi 1\nmode 1\nlevel 2\nhuman 0\nmoves 0\n"), -1); /* illegal move */
	T_EQ_INT(rv_game_load(&h, "garbage"), -1);
}

/* independent exact solver: final disc difference for the side to move with perfect play */
static int rv_solve(const Rv *r, int alpha, int beta, int passed)
{
	uint64_t m = rv_moves(r);
	if (!m) {
		if (passed) return rv_count(r, r->side) - rv_count(r, !r->side);
		Rv t = *r;
		rv_play(&t, RV_PASS);
		return -rv_solve(&t, -beta, -alpha, 1);
	}
	int best = -999;
	for (int sq = 0; sq < 64; sq++) {
		if (!(m >> sq & 1)) continue;
		Rv t = *r;
		rv_play(&t, sq);
		int v = -rv_solve(&t, -beta, -alpha, 0);
		if (v > best) best = v;
		if (v > alpha) alpha = v;
		if (alpha >= beta) break;
	}
	return best;
}

static void test_rv_ai_endgame_exact(void)
{
	for (int trial = 0; trial < 6; trial++) {
		Rv r;
		rv_init(&r);
		uint32_t seed = 1234u + (uint32_t) trial * 77u;
		/* random play down to 10 empty squares */
		while (64 - rv_count(&r, 0) - rv_count(&r, 1) > 10 && !rv_over(&r)) {
			uint64_t m = rv_moves(&r);
			if (!m) { rv_play(&r, RV_PASS); continue; }
			int k = (int) (masa_rand(&seed) % (uint32_t) __builtin_popcountll(m));
			int sq = 0;
			for (; sq < 64; sq++) if ((m >> sq & 1) && k-- == 0) break;
			rv_play(&r, sq);
		}
		if (rv_over(&r) || !rv_moves(&r)) continue;
		int best = rv_solve(&r, -999, 999, 0);
		MasaLimits lim = { 3, 0, 0, 0, 1 };
		lim.time_ms = 100000;
		int m = rv_ai(&r, &lim, NULL);
		Rv t = r;
		T_EQ_INT(rv_play(&t, m), 0);
		T_EQ_INT(-rv_solve(&t, -999, 999, 0), best); /* the AI's move keeps the perfect-play result */
	}
}

static void test_rv_ai_timing(void)
{
	Rv r;
	rv_init(&r);
	MasaStats st;
	MasaLimits lim = { 3, 400, 0, 0, 5 };
	int m = rv_ai(&r, &lim, &st);
	T_ASSERT(rv_flips(&r, m) != 0);
	T_ASSERT(st.ms < 1000);
	T_ASSERT(st.depth >= 3);
	fprintf(stderr, "reversi: depth %d, %ld nodes in %ld ms (%.0f kN/s host)\n", st.depth, st.nodes, st.ms,
		st.ms ? st.nodes / (double) st.ms : 0.0);
	/* a full AI game (Zor vs Orta) ends with a legal result */
	RvGame g;
	rv_game_new(&g, 1, 3, 0);
	while (!rv_over(&g.pos)) {
		MasaLimits l = { g.pos.side ? 2 : 3, 50, 0, 0, (uint32_t) g.n + 1 };
		T_EQ_INT(rv_game_play(&g, rv_ai(&g.pos, &l, NULL)), 0);
	}
	T_ASSERT(rv_count(&g.pos, 0) + rv_count(&g.pos, 1) <= 64);
}

/* ================= Dama ================= */

#define SQ(r, c) ((r) * 8 + (c))

static int find_move(const DmMove *ms, int n, int from, int to)
{
	for (int i = 0; i < n; i++)
		if (ms[i].from == from && ms[i].path[ms[i].n - 1] == to) return i;
	return -1;
}

static void test_dm_start(void)
{
	Dm d;
	dm_init(&d);
	DmMove ms[DM_MAX_MOVES];
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 8); /* the front row steps forward; everything else is blocked */
	for (int i = 0; i < n; i++) {
		T_EQ_INT(ms[i].from >> 3, 5);
		T_EQ_INT(ms[i].path[0], ms[i].from - 8);
	}
	T_EQ_INT(dm_result(&d), DM_ONGOING);
}

static void test_dm_man_moves(void)
{
	Dm d;
	dm_clear(&d, 0);
	d.b[SQ(4, 3)] = DM_WMAN;
	d.b[SQ(0, 0)] = DM_BMAN;
	DmMove ms[DM_MAX_MOVES];
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 3); /* up, left, right — never back or diagonal */
	T_ASSERT(find_move(ms, n, SQ(4, 3), SQ(3, 3)) >= 0);
	T_ASSERT(find_move(ms, n, SQ(4, 3), SQ(4, 2)) >= 0);
	T_ASSERT(find_move(ms, n, SQ(4, 3), SQ(4, 4)) >= 0);
	T_ASSERT(find_move(ms, n, SQ(4, 3), SQ(5, 3)) < 0);
	/* black men move down */
	dm_clear(&d, 1);
	d.b[SQ(3, 3)] = DM_BMAN;
	d.b[SQ(7, 7)] = DM_WMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 3);
	T_ASSERT(find_move(ms, n, SQ(3, 3), SQ(4, 3)) >= 0);
}

static void test_dm_mandatory_and_backward(void)
{
	Dm d;
	DmMove ms[DM_MAX_MOVES];
	dm_clear(&d, 0);
	d.b[SQ(4, 3)] = DM_WMAN;
	d.b[SQ(4, 4)] = DM_BMAN; /* sideways capture available */
	d.b[SQ(5, 1)] = DM_BMAN;
	d.b[SQ(6, 0)] = DM_WMAN; /* (6,0) could step: but a capture exists, so only it counts */
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 1);
	T_EQ_INT(ms[0].from, SQ(4, 3));
	T_EQ_INT(ms[0].path[0], SQ(4, 5));
	T_EQ_INT(ms[0].cap[0], SQ(4, 4));
	/* backwards: a black man behind a white man cannot be taken by it */
	dm_clear(&d, 0);
	d.b[SQ(4, 3)] = DM_WMAN;
	d.b[SQ(5, 3)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	for (int i = 0; i < n; i++) T_EQ_INT(ms[i].ncap, 0);
	/* diagonal neighbours are never captured */
	dm_clear(&d, 0);
	d.b[SQ(4, 3)] = DM_WMAN;
	d.b[SQ(3, 4)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	for (int i = 0; i < n; i++) T_EQ_INT(ms[i].ncap, 0);
}

static void test_dm_maximum_capture(void)
{
	Dm d;
	DmMove ms[DM_MAX_MOVES];
	dm_clear(&d, 0);
	/* man A at (6,0) can take one (left path); man B at (6,5) can take two in a row upward */
	d.b[SQ(6, 0)] = DM_WMAN;
	d.b[SQ(5, 0)] = DM_BMAN;
	d.b[SQ(6, 5)] = DM_WMAN;
	d.b[SQ(5, 5)] = DM_BMAN;
	d.b[SQ(3, 5)] = DM_BMAN;
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 1);
	T_EQ_INT(ms[0].from, SQ(6, 5));
	T_EQ_INT(ms[0].ncap, 2);
	T_EQ_INT(ms[0].path[1], SQ(2, 5));
	/* a man may turn sideways inside a sequence: up, then right, then up */
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WMAN;
	d.b[SQ(6, 0)] = DM_BMAN;
	d.b[SQ(5, 1)] = DM_BMAN;
	d.b[SQ(4, 2)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 1);
	T_EQ_INT(ms[0].ncap, 3); /* (7,0)->(5,0)->(5,2)->(3,2) */
	T_EQ_INT(ms[0].path[2], SQ(3, 2));
	/* equal maxima: both offered */
	dm_clear(&d, 0);
	d.b[SQ(4, 3)] = DM_WMAN;
	d.b[SQ(4, 2)] = DM_BMAN;
	d.b[SQ(4, 4)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 2);
}

static void test_dm_king_flying(void)
{
	Dm d;
	DmMove ms[DM_MAX_MOVES];
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WKING;
	d.b[SQ(0, 7)] = DM_BMAN;
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 14); /* 7 up + 7 right */
	/* a capture at a distance, with every landing square beyond */
	d.b[SQ(3, 0)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 3); /* lands on (2,0), (1,0) or (0,0) */
	for (int i = 0; i < n; i++) T_EQ_INT(ms[i].cap[0], SQ(3, 0));
	/* two pieces in a row cannot be jumped together */
	d.b[SQ(2, 0)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	for (int i = 0; i < n; i++) T_EQ_INT(ms[i].ncap, 0);
	/* the landing square must allow the longest continuation */
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WKING;
	d.b[SQ(4, 0)] = DM_BMAN;
	d.b[SQ(2, 4)] = DM_BMAN; /* reachable along row 2 only when landing on (2,0) */
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_ASSERT(n >= 1);
	for (int i = 0; i < n; i++) {
		T_EQ_INT(ms[i].ncap, 2);
		T_EQ_INT(ms[i].path[0], SQ(2, 0));
	}
	T_EQ_INT(n, 3); /* then lands (2,5), (2,6) or (2,7) */
}

static void test_dm_king_no_180(void)
{
	Dm d;
	DmMove ms[DM_MAX_MOVES];
	dm_clear(&d, 0);
	d.b[SQ(4, 4)] = DM_WKING;
	d.b[SQ(4, 2)] = DM_BMAN;
	d.b[SQ(4, 6)] = DM_BMAN;
	d.b[SQ(0, 0)] = DM_BMAN;
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	/* after taking one, turning back would take the other: forbidden, so only single captures */
	T_EQ_INT(n, 3);
	for (int i = 0; i < n; i++) T_EQ_INT(ms[i].ncap, 1);
}

static void test_dm_immediate_removal(void)
{
	Dm d;
	DmMove ms[DM_MAX_MOVES];
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WKING;
	d.b[SQ(5, 0)] = DM_BMAN;
	d.b[SQ(2, 2)] = DM_BMAN;
	d.b[SQ(4, 4)] = DM_BMAN;
	d.b[SQ(6, 2)] = DM_BMAN;
	d.b[SQ(1, 0)] = DM_BMAN;
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_ASSERT(n >= 1);
	/* up over (5,0), right over (2,2), down over (4,4), left over (6,2), then up column 0 again — through the square
	 * of the first captured man, already removed — over (1,0) */
	T_EQ_INT(ms[0].ncap, 5);
	int ok = 0;
	for (int i = 0; i < n; i++)
		if (ms[i].path[ms[i].n - 1] == SQ(0, 0) && ms[i].cap[4] == SQ(1, 0)) ok = 1;
	T_ASSERT(ok);
	Dm e = d;
	dm_apply(&e, &ms[0]);
	for (int s = 0; s < 64; s++) T_ASSERT(dm_color(e.b[s]) != 1);
	T_EQ_INT(dm_result(&e), DM_WHITE_WINS);
}

static void test_dm_promotion(void)
{
	Dm d;
	DmMove ms[DM_MAX_MOVES];
	dm_clear(&d, 0);
	d.b[SQ(1, 3)] = DM_WMAN;
	d.b[SQ(7, 7)] = DM_BMAN;
	d.b[SQ(6, 7)] = DM_BMAN;
	int n = dm_moves(&d, ms, DM_MAX_MOVES);
	int i = find_move(ms, n, SQ(1, 3), SQ(0, 3));
	T_ASSERT(i >= 0);
	dm_apply(&d, &ms[i]);
	T_EQ_INT(d.b[SQ(0, 3)], DM_WKING);
	/* reaching the far row while capturing: carries on as a man (sideways only), crowned at the end */
	dm_clear(&d, 0);
	d.b[SQ(2, 0)] = DM_WMAN;
	d.b[SQ(1, 0)] = DM_BMAN;
	d.b[SQ(0, 1)] = DM_BMAN;   /* adjacent on the far row: a man takes it sideways */
	d.b[SQ(0, 4)] = DM_BMAN;   /* only a flying king could take this one after (0,2) */
	d.b[SQ(7, 7)] = DM_BMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	T_EQ_INT(n, 1);
	T_EQ_INT(ms[0].ncap, 2);
	T_EQ_INT(ms[0].path[1], SQ(0, 2));
	dm_apply(&d, &ms[0]);
	T_EQ_INT(d.b[SQ(0, 2)], DM_WKING);
	T_EQ_INT(d.b[SQ(0, 4)], DM_BMAN);
	/* black promotes on row 7 */
	dm_clear(&d, 1);
	d.b[SQ(6, 6)] = DM_BMAN;
	d.b[SQ(0, 0)] = DM_WMAN;
	n = dm_moves(&d, ms, DM_MAX_MOVES);
	i = find_move(ms, n, SQ(6, 6), SQ(7, 6));
	T_ASSERT(i >= 0);
	dm_apply(&d, &ms[i]);
	T_EQ_INT(d.b[SQ(7, 6)], DM_BKING);
}

static void test_dm_results(void)
{
	Dm d;
	dm_clear(&d, 0);
	d.b[SQ(3, 3)] = DM_WKING;
	d.b[SQ(6, 6)] = DM_BMAN;
	T_EQ_INT(dm_result(&d), DM_DRAW); /* one piece each */
	d.b[SQ(6, 0)] = DM_BMAN;
	T_EQ_INT(dm_result(&d), DM_ONGOING);
	/* white's only man is boxed in: no legal move, black wins */
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WMAN;
	d.b[SQ(6, 0)] = DM_BMAN;
	d.b[SQ(5, 0)] = DM_BMAN;
	d.b[SQ(7, 1)] = DM_BMAN;
	d.b[SQ(7, 2)] = DM_BMAN;
	T_EQ_INT(dm_result(&d), DM_BLACK_WINS);
	/* quiet king moves: house-rule draw after 100 plies */
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WKING;
	d.b[SQ(7, 7)] = DM_WKING;
	d.b[SQ(0, 0)] = DM_BKING;
	d.b[SQ(0, 7)] = DM_BKING;
	DmMove ms[DM_MAX_MOVES];
	for (int i = 0; i < DM_QUIET_DRAW; i++) {
		T_EQ_INT(dm_result(&d), DM_ONGOING);
		int n = dm_moves(&d, ms, DM_MAX_MOVES);
		int k = -1;
		for (int j = 0; j < n; j++) if (ms[j].ncap == 0 && (ms[j].from >> 3) == (ms[j].path[0] >> 3) && abs((ms[j].from & 7) - (ms[j].path[0] & 7)) == 1) { k = j; break; }
		T_ASSERT(k >= 0);
		if (k < 0) break;
		dm_apply(&d, &ms[k]);
	}
	T_EQ_INT(dm_result(&d), DM_DRAW);
}

static void test_dm_game_save_undo(void)
{
	static DmGame g, h;
	dm_game_new(&g, 2, 1, 0);
	for (int i = 0; i < 30 && dm_result(&g.pos) == DM_ONGOING; i++) {
		DmMove m;
		MasaLimits lim = { 2, 0, 2, 0, (uint32_t) i + 1 };
		T_EQ_INT(dm_ai(&g.pos, &lim, &m, NULL), 0);
		T_EQ_INT(dm_game_play(&g, &m), 0);
	}
	static char buf[16384];
	T_ASSERT(dm_game_save(&g, buf, sizeof buf) > 0);
	T_EQ_INT(dm_game_load(&h, buf), 0);
	T_EQ_INT(h.n, g.n);
	T_ASSERT(memcmp(h.pos.b, g.pos.b, 64) == 0);
	T_EQ_INT(dm_game_undo(&h, 2), 2);
	T_EQ_INT(h.n, g.n - 2);
	T_EQ_INT(h.pos.side, g.pos.side);
	T_EQ_INT(dm_game_load(&h, "dama 1\nmode 1\nlevel 2\nhuman 0\nmoves 48-32\n"), -1); /* two squares: illegal */
	DmMove bad = { SQ(5, 0), 1, 0, { SQ(3, 0) }, { 0 } };
	T_EQ_INT(dm_game_play(&g, &bad), -1);
}

static void test_dm_ai(void)
{
	Dm d;
	DmMove m;
	MasaStats st;
	/* the AI plays the forced capture */
	dm_clear(&d, 0);
	d.b[SQ(6, 0)] = DM_WMAN;
	d.b[SQ(5, 0)] = DM_BMAN;
	d.b[SQ(0, 7)] = DM_BMAN;
	d.b[SQ(7, 7)] = DM_WMAN;
	MasaLimits lim = { 3, 0, 4, 0, 1 };
	T_EQ_INT(dm_ai(&d, &lim, &m, NULL), 0);
	T_EQ_INT(m.ncap, 1);
	/* from the start: time-bounded, fast enough */
	dm_init(&d);
	MasaLimits zor = { 3, 400, 0, 0, 9 };
	T_EQ_INT(dm_ai(&d, &zor, &m, &st), 0);
	T_ASSERT(st.ms < 1000);
	T_ASSERT(st.depth >= 4);
	fprintf(stderr, "dama: depth %d, %ld nodes in %ld ms (%.0f kN/s host)\n", st.depth, st.nodes, st.ms, st.ms ? st.nodes / (double) st.ms : 0.0);
	/* a king against two men of which one hangs: it should take, not wander */
	dm_clear(&d, 0);
	d.b[SQ(7, 0)] = DM_WKING;
	d.b[SQ(7, 7)] = DM_WMAN;
	d.b[SQ(3, 5)] = DM_BMAN;
	d.b[SQ(2, 2)] = DM_BMAN;
	T_EQ_INT(dm_ai(&d, &lim, &m, NULL), 0);
	T_ASSERT(m.ncap == 0 || m.ncap == 1);
}

static void test_dm_full_game(void)
{
	static DmGame g;
	dm_game_new(&g, 1, 3, 0);
	while (dm_result(&g.pos) == DM_ONGOING && g.n < DM_MAX_PLIES) {
		DmMove m;
		MasaLimits l = { g.pos.side ? 1 : 3, 30, 0, 0, (uint32_t) g.n + 3 };
		T_EQ_INT(dm_ai(&g.pos, &l, &m, NULL), 0);
		if (dm_game_play(&g, &m) != 0) { T_ASSERT(0); break; }
	}
	DmResult r = dm_result(&g.pos);
	fprintf(stderr, "dama: Zor vs Kolay ended after %d plies, result %d\n", g.n, r);
	T_ASSERT(r != DM_ONGOING);
}

/* ================= Go ================= */

static int P(const Go *g, int r, int c) { return go_pt(g, r, c); }

static void test_go_capture(void)
{
	Go g;
	go_init(&g, 9);
	/* white stone at (4,4) surrounded by black */
	T_EQ_INT(go_play(&g, P(&g, 3, 4)), 0); /* B */
	T_EQ_INT(go_play(&g, P(&g, 4, 4)), 0); /* W */
	T_EQ_INT(go_play(&g, P(&g, 5, 4)), 0); /* B */
	T_EQ_INT(go_play(&g, P(&g, 0, 0)), 0); /* W */
	T_EQ_INT(go_play(&g, P(&g, 4, 3)), 0); /* B */
	T_EQ_INT(go_libs(&g, P(&g, 4, 4)), 1);
	T_EQ_INT(go_play(&g, P(&g, 0, 1)), 0); /* W */
	T_EQ_INT(go_play(&g, P(&g, 4, 5)), 1); /* B captures */
	T_EQ_INT(g.c[P(&g, 4, 4)], GO_EMPTY);
	T_EQ_INT(g.caps[GO_BLACK], 1);
	/* a chain: liberties after merging */
	go_init(&g, 9);
	go_play(&g, P(&g, 4, 4));
	go_play(&g, P(&g, 0, 0));
	go_play(&g, P(&g, 4, 5));
	T_EQ_INT(go_libs(&g, P(&g, 4, 4)), 6);
	/* corner capture of two stones */
	go_init(&g, 9);
	go_play(&g, P(&g, 1, 0)); /* B */
	go_play(&g, P(&g, 0, 0)); /* W */
	go_play(&g, P(&g, 1, 1)); /* B */
	go_play(&g, P(&g, 0, 1)); /* W */
	T_EQ_INT(go_play(&g, P(&g, 0, 2)), 2); /* B takes both */
	T_EQ_INT(go_libs(&g, P(&g, 0, 2)), 3); /* (0,1) is free again, and (0,3), (1,2) */
}

static void test_go_suicide(void)
{
	Go g;
	go_init(&g, 9);
	/* black ring around (0,0): white playing there is suicide */
	go_play(&g, P(&g, 0, 1));
	go_play(&g, P(&g, 8, 8));
	go_play(&g, P(&g, 1, 0));
	T_EQ_INT(g.side, GO_WHITE);
	T_ASSERT(!go_legal(&g, P(&g, 0, 0)));
	T_EQ_INT(go_play(&g, P(&g, 0, 0)), -1);
	/* black filling its own last liberty of a two-stone chain is suicide too */
	go_init(&g, 9);
	go_play(&g, P(&g, 0, 1)); /* B */
	go_play(&g, P(&g, 0, 2)); /* W */
	go_play(&g, P(&g, 8, 8)); /* B */
	go_play(&g, P(&g, 1, 1)); /* W */
	go_play(&g, P(&g, 8, 7)); /* B */
	go_play(&g, P(&g, 1, 0)); /* W */
	T_EQ_INT(g.side, GO_BLACK);
	/* black (0,1) has one liberty: (0,0). Playing there captures nothing (white (1,0) has libs) → suicide */
	T_ASSERT(!go_legal(&g, P(&g, 0, 0)));
	/* filling the last liberty is legal when it captures */
	go_init(&g, 9);
	go_play(&g, P(&g, 0, 1)); /* B */
	go_play(&g, P(&g, 1, 0)); /* W */
	go_play(&g, P(&g, 2, 0)); /* B */
	go_play(&g, P(&g, 1, 2)); /* W */
	go_play(&g, P(&g, 1, 1)); /* B  — now W (1,0) has one liberty (0,0) */
	go_play(&g, P(&g, 8, 8)); /* W */
	T_ASSERT(go_legal(&g, P(&g, 0, 0)));
	T_EQ_INT(go_play(&g, P(&g, 0, 0)), 1);
}

static void test_go_ko(void)
{
	GoGame gg;
	go_game_new(&gg, 9, 2, 2, GO_BLACK);
	Go *g = &gg.pos;
	/* a ko in the middle: black (4,4) and white (4,3) can take each other back */
	int B[] = { P(g, 3, 3), P(g, 4, 2), P(g, 5, 3), P(g, 4, 4) };
	int W[] = { P(g, 3, 4), P(g, 5, 4), P(g, 4, 5), P(g, 8, 8) };
	for (int i = 0; i < 4; i++) {
		T_EQ_INT(go_game_play(&gg, B[i]), 0);
		T_EQ_INT(go_game_play(&gg, W[i]), 0);
	}
	/* black (4,4) has one liberty (4,3); white plays (4,3) capturing it */
	T_EQ_INT(g->side, GO_BLACK);
	T_EQ_INT(go_game_play(&gg, P(g, 0, 0)), 0); /* B elsewhere */
	T_EQ_INT(go_game_play(&gg, P(g, 4, 3)), 0); /* W captures one stone */
	T_EQ_INT(g->c[P(g, 4, 4)], GO_EMPTY);
	T_EQ_INT(g->ko, P(g, 4, 4));
	T_ASSERT(!go_legal(g, P(g, 4, 4)));        /* immediate retake forbidden */
	T_ASSERT(!go_game_legal(&gg, P(g, 4, 4)));
	T_EQ_INT(go_game_play(&gg, P(g, 0, 8)), 0); /* ko threat elsewhere */
	T_EQ_INT(go_game_play(&gg, P(g, 8, 0)), 0); /* answer */
	T_ASSERT(go_game_legal(&gg, P(g, 4, 4)));  /* now the retake is fine */
	T_EQ_INT(go_game_play(&gg, P(g, 4, 4)), 0);
	T_EQ_INT(g->c[P(g, 4, 3)], GO_EMPTY);
	/* and white cannot retake at once (simple ko) */
	T_ASSERT(!go_game_legal(&gg, P(g, 4, 3)));
}

static void test_go_superko(void)
{
	/* a pass lifts the simple ko; the retake is fine when the board differs from every earlier one */
	GoGame gg;
	go_game_new(&gg, 9, 2, 2, GO_BLACK);
	Go *g = &gg.pos;
	int B[] = { P(g, 3, 3), P(g, 4, 2), P(g, 5, 3), P(g, 4, 4) };
	int W[] = { P(g, 3, 4), P(g, 5, 4), P(g, 4, 5), P(g, 8, 8) };
	for (int i = 0; i < 4; i++) { go_game_play(&gg, B[i]); go_game_play(&gg, W[i]); }
	go_game_play(&gg, P(g, 0, 0));
	go_game_play(&gg, P(g, 4, 3)); /* W takes the ko */
	go_game_play(&gg, GO_PASS);    /* B passes: simple ko is lifted */
	go_game_play(&gg, P(g, 8, 0)); /* W plays elsewhere */
	T_ASSERT(go_legal(g, P(g, 4, 4)));
	T_ASSERT(go_game_legal(&gg, P(g, 4, 4))); /* the board differs (8,0), so this is no repetition */
	T_EQ_INT(go_game_play(&gg, P(g, 4, 4)), 0);
	/* now white retaking at once is simple ko */
	T_ASSERT(!go_game_legal(&gg, P(g, 4, 3)));
}

static void test_go_eye(void)
{
	Go g;
	go_init(&g, 9);
	/* black stones around (0,0): (0,1), (1,0) — corner eye */
	go_play(&g, P(&g, 0, 1));
	go_play(&g, P(&g, 8, 8));
	go_play(&g, P(&g, 1, 0));
	T_ASSERT(go_is_eye(&g, P(&g, 0, 0), GO_BLACK));
	T_ASSERT(!go_is_eye(&g, P(&g, 0, 0), GO_WHITE));
	go_play(&g, P(&g, 1, 1)); /* white diagonal: a corner eye with an enemy diagonal is false */
	T_ASSERT(!go_is_eye(&g, P(&g, 0, 0), GO_BLACK));
}

static void test_go_scoring(void)
{
	Go g;
	go_init(&g, 9);
	T_EQ_INT(go_score2(&g, NULL), -GO_KOMI2); /* empty board: white wins by komi */
	/* black wall on column 3, white wall on column 4: black owns columns 0..3 (36), white 4..8 (45) */
	for (int r = 0; r < 9; r++) {
		go_play(&g, P(&g, r, 3));
		go_play(&g, P(&g, r, 4));
	}
	int b, w;
	unsigned char owner[GO_MAXP];
	go_area(&g, NULL, &b, &w, owner);
	T_EQ_INT(b, 36);
	T_EQ_INT(w, 45);
	T_EQ_INT(owner[P(&g, 0, 0)], GO_BLACK);
	T_EQ_INT(owner[P(&g, 8, 8)], GO_WHITE);
	T_EQ_INT(go_score2(&g, NULL), 2 * (36 - 45) - 15);
	/* a lone white stone inside black's area makes it neutral; marked dead it is black's again */
	go_play(&g, GO_PASS); /* B */
	go_play(&g, P(&g, 4, 1)); /* W invades */
	go_area(&g, NULL, &b, &w, NULL);
	T_EQ_INT(b, 9);  /* only black's wall: the region now touches both colours */
	T_EQ_INT(w, 46);
	unsigned char dead[GO_MAXP] = { 0 };
	go_toggle_dead(&g, P(&g, 4, 1), dead);
	T_EQ_INT(dead[P(&g, 4, 1)], 1);
	go_area(&g, dead, &b, &w, NULL);
	T_EQ_INT(b, 36);
	T_EQ_INT(w, 45);
	/* the estimate agrees */
	unsigned char est[GO_MAXP];
	go_estimate_dead(&g, 200, 77, est);
	T_EQ_INT(est[P(&g, 4, 1)], 1);
	T_EQ_INT(est[P(&g, 4, 3)], 0);
	T_EQ_INT(est[P(&g, 4, 4)], 0);
	go_toggle_dead(&g, P(&g, 4, 1), dead);
	T_EQ_INT(dead[P(&g, 4, 1)], 0);
	/* dame: a point next to both colours is nobody's */
	go_init(&g, 5);
	go_play(&g, P(&g, 2, 1));
	go_play(&g, P(&g, 2, 3));
	go_area(&g, NULL, &b, &w, owner);
	T_EQ_INT(b, 1);
	T_EQ_INT(w, 1);
}

static void test_go_game_flow(void)
{
	static GoGame g, h;
	go_game_new(&g, 9, 1, 2, GO_BLACK);
	T_EQ_INT(go_game_play(&g, P(&g.pos, 2, 2)), 0);
	T_EQ_INT(go_game_play(&g, P(&g.pos, 6, 6)), 0);
	T_EQ_INT(go_game_play(&g, GO_PASS), 0);
	T_ASSERT(!go_game_over(&g));
	static char buf[8192];
	T_ASSERT(go_game_save(&g, buf, sizeof buf) > 0);
	T_EQ_INT(go_game_load(&h, buf), 0);
	T_EQ_INT(h.n, 3);
	T_EQ_INT(h.pos.c[P(&h.pos, 6, 6)], GO_WHITE);
	T_EQ_INT(h.pos.side, GO_WHITE);
	T_EQ_INT(go_game_play(&g, GO_PASS), 0);
	T_ASSERT(go_game_over(&g));
	T_EQ_INT(go_game_play(&g, P(&g.pos, 4, 4)), -1); /* over */
	T_EQ_INT(go_game_undo(&g, 2), 2);
	T_ASSERT(!go_game_over(&g));
	T_EQ_INT(g.pos.side, GO_BLACK);
	T_EQ_INT(go_game_play(&g, GO_RESIGN), 0);
	T_EQ_INT(g.resigned, GO_BLACK);
	T_ASSERT(go_game_over(&g));
	T_ASSERT(go_game_save(&g, buf, sizeof buf) > 0);
	T_EQ_INT(go_game_load(&h, buf), 0);
	T_EQ_INT(h.resigned, GO_BLACK);
	T_EQ_INT(go_game_load(&h, "go 1\nsize 9\nmode 1\nlevel 2\nhuman 1\nmoves 40 40\n"), -1);
	go_game_new(&g, 13, 2, 3, GO_WHITE);
	T_EQ_INT(g.pos.n, 13);
	T_EQ_INT(go_game_play(&g, P(&g.pos, 12, 12)), 0);
	T_ASSERT(go_game_save(&g, buf, sizeof buf) > 0);
	T_EQ_INT(go_game_load(&h, buf), 0);
	T_EQ_INT(h.pos.c[P(&h.pos, 12, 12)], GO_BLACK);
}

static void test_go_ai(void)
{
	Go g;
	MasaStats st;
	go_init(&g, 9);
	/* a big white chain in atari: black should capture it */
	int ws[] = { P(&g, 4, 3), P(&g, 4, 4), P(&g, 4, 5) };
	int bs[] = { P(&g, 3, 3), P(&g, 3, 4), P(&g, 3, 5), P(&g, 5, 3), P(&g, 5, 4), P(&g, 5, 5), P(&g, 4, 2) };
	for (int i = 0; i < 7; i++) {
		go_play(&g, bs[i]);
		go_play(&g, i < 3 ? ws[i] : P(&g, 0, i));
	}
	T_EQ_INT(g.side, GO_BLACK);
	T_EQ_INT(go_libs(&g, P(&g, 4, 4)), 1);
	MasaLimits lim = { 3, 0, 0, 3000, 11 };
	int m = go_ai(&g, &lim, &st);
	T_EQ_INT(m, P(&g, 4, 6));
	/* time-bounded from the empty board, legal answer */
	go_init(&g, 9);
	MasaLimits t = { 3, 300, 0, 0, 5 };
	m = go_ai(&g, &t, &st);
	T_ASSERT(go_legal(&g, m));
	T_ASSERT(st.ms < 600);
	fprintf(stderr, "go 9x9: %ld playouts in %ld ms (%.0f playouts/s host), %d nodes\n", st.nodes, st.ms,
		st.ms ? st.nodes * 1000.0 / st.ms : 0.0, st.depth);
	go_init(&g, 13);
	m = go_ai(&g, &t, &st);
	T_ASSERT(go_legal(&g, m));
	fprintf(stderr, "go 13x13: %ld playouts in %ld ms (%.0f playouts/s host)\n", st.nodes, st.ms, st.ms ? st.nodes * 1000.0 / st.ms : 0.0);
	/* 5x5 split by two walls: black owns columns 0-1 (10), white columns 2-4 (15) + komi */
	go_init(&g, 5);
	for (int r = 0; r < 5; r++) {
		go_play(&g, P(&g, r, 1));
		go_play(&g, P(&g, r, 2));
	}
	T_EQ_INT(go_score2(&g, NULL), 2 * (10 - 15) - GO_KOMI2);
	MasaLimits q = { 2, 0, 0, 300, 3 };
	T_EQ_INT(g.side, GO_BLACK);
	T_ASSERT(go_ai(&g, &q, NULL) != GO_PASS); /* behind: keeps trying */
	Go h = g;
	go_play(&h, GO_PASS);
	T_EQ_INT(go_ai(&h, &q, NULL), GO_PASS); /* white is ahead and black passed: pass back, the game ends */
	/* settled board, white ahead, no pass before: white passes too */
	go_play(&g, P(&g, 0, 0));
	go_play(&g, P(&g, 4, 4));
	go_play(&g, P(&g, 4, 0));
	T_EQ_INT(g.side, GO_WHITE);
	T_EQ_INT(go_ai(&g, &q, NULL), GO_PASS);
}

static void test_go_ai_selfplay(void)
{
	static GoGame g;
	go_game_new(&g, 9, 1, 2, GO_BLACK);
	int moves = 0;
	while (!go_game_over(&g) && g.n < 200) {
		MasaLimits l = { 2, 0, 0, 150, (uint32_t) g.n + 1 };
		int m = go_game_ai(&g, &l, NULL);
		T_ASSERT(m <= 0 || go_game_legal(&g, m));
		T_EQ_INT(go_game_play(&g, m), 0);
		moves++;
	}
	T_ASSERT(go_game_over(&g));
	fprintf(stderr, "go self-play ended after %d moves\n", moves);
}

int main(void)
{
	T_RUN(test_rv_start);
	T_RUN(test_rv_multi_direction_flips);
	T_RUN(test_rv_pass_and_end);
	T_RUN(test_rv_wraparound);
	T_RUN(test_rv_game_save_undo);
	T_RUN(test_rv_ai_endgame_exact);
	T_RUN(test_rv_ai_timing);
	T_RUN(test_dm_start);
	T_RUN(test_dm_man_moves);
	T_RUN(test_dm_mandatory_and_backward);
	T_RUN(test_dm_maximum_capture);
	T_RUN(test_dm_king_flying);
	T_RUN(test_dm_king_no_180);
	T_RUN(test_dm_immediate_removal);
	T_RUN(test_dm_promotion);
	T_RUN(test_dm_results);
	T_RUN(test_dm_game_save_undo);
	T_RUN(test_dm_ai);
	T_RUN(test_dm_full_game);
	T_RUN(test_go_capture);
	T_RUN(test_go_suicide);
	T_RUN(test_go_ko);
	T_RUN(test_go_superko);
	T_RUN(test_go_eye);
	T_RUN(test_go_scoring);
	T_RUN(test_go_game_flow);
	T_RUN(test_go_ai);
	T_RUN(test_go_ai_selfplay);
	return T_DONE();
}

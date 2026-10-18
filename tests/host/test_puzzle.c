#include <stdlib.h>
#include "chess/chess_game.h"
#include "puzzle/puzzle.h"
#include "t.h"

static char *slurp(const char *path)
{
	static char buf[2][65536];
	static int k;
	char *b = buf[k++ & 1];
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(b, 1, sizeof buf[0] - 1, f) : 0;
	if (f) fclose(f);
	b[n] = '\0';
	return b;
}

static int sq(const char *s) { return (s[0] - 'a') + 8 * (s[1] - '1'); }

static Move mv(const Board *b, const char *uci)
{
	Game g;
	game_new(&g, 2, 3, 0);
	g.pos = *b;
	if (game_play_uci(&g, uci) != 0) return (Move){ 0, 0, 0, 0 };
	return g.hist[0];
}

/* FEN without the move counters */
static const char *fen4(const Board *b)
{
	static char f[100];
	chess_to_fen(b, f, sizeof f);
	char *p = f;
	for (int sp = 0; *p; p++)
		if (*p == ' ' && ++sp == 4) *p = '\0';
	return f;
}

static void test_parse_daily(void)
{
	static Puzzle p;
	T_EQ_INT(pz_parse(slurp("tests/host/fixtures/puzzle_daily.json"), &p), 0);
	T_EQ_STR(p.id, "VlHF6");
	T_EQ_STR(p.game_id, "6cDo4JpJ");
	T_EQ_INT(p.rating, 1900);
	T_EQ_INT(p.plays, 20566);
	T_EQ_INT(p.initial_ply, 39);
	T_EQ_INT(p.n_sol, 5);
	T_EQ_INT(p.sol[0].from, sq("c5"));
	T_EQ_INT(p.sol[0].to, sq("a6"));
	T_EQ_INT(p.sol[4].to, sq("b5"));
	T_EQ_INT(p.n_themes, 6);
	T_EQ_STR(p.themes[3], "mateIn3");
	/* the PGN replay gives Lichess's own FEN */
	T_EQ_STR(fen4(&p.start), "r3r3/1pk2pp1/p6p/2N2b2/1P6/N4P2/Pb4PP/2RR2K1 w - -");
	T_EQ_INT(p.has_last, 1);
	T_EQ_INT(p.last.from, sq("c3"));
	T_EQ_INT(p.last.to, sq("b2"));
}

static void test_parse_next(void)
{
	static Puzzle p;
	T_EQ_INT(pz_parse(slurp("tests/host/fixtures/puzzle_next.json"), &p), 0); /* no "fen" in this response */
	T_EQ_STR(p.id, "c9bwS");
	T_EQ_INT(p.start.side, 1);
	T_EQ_INT(p.n_sol, 1);
	T_EQ_INT(p.last.from, sq("h1"));
	T_EQ_INT(p.last.to, sq("h2"));
	T_EQ_INT(p.start.sq[sq("h8")], BR);
}

static void test_parse_bad(void)
{
	static Puzzle p;
	T_EQ_INT(pz_parse("junk", &p), -1);
	T_EQ_INT(pz_parse("{}", &p), -1);
	T_EQ_INT(pz_parse("{\"error\":\"Not found\"}", &p), -1);
	/* a solution move that is illegal in the position */
	T_EQ_INT(pz_parse("{\"game\":{\"pgn\":\"e4 e5\"},\"puzzle\":{\"id\":\"x\",\"rating\":1000,\"solution\":[\"e1e3\"],\"themes\":[],\"initialPly\":1}}", &p), -1);
	/* a broken PGN */
	T_EQ_INT(pz_parse("{\"game\":{\"pgn\":\"e4 Ke7 Qq9\"},\"puzzle\":{\"id\":\"x\",\"rating\":1000,\"solution\":[\"d1h5\"],\"themes\":[],\"initialPly\":2}}", &p), -1);
	/* a minimal one is fine */
	T_EQ_INT(pz_parse("{\"game\":{\"pgn\":\"e4 e5\"},\"puzzle\":{\"id\":\"x\",\"rating\":1000,\"solution\":[\"d1h5\"],\"themes\":[\"opening\"],\"initialPly\":1}}", &p), 0);
	T_EQ_INT(p.start.side, 0);
}

static void test_themes(void)
{
	T_EQ_STR(pz_theme_name("mateIn1"), "1 hamlede mat");
	T_EQ_STR(pz_theme_name("mateIn2"), "2 hamlede mat");
	T_EQ_STR(pz_theme_name("mateIn3"), "3 hamlede mat");
	T_EQ_STR(pz_theme_name("fork"), "Çatal");
	T_EQ_STR(pz_theme_name("pin"), "Açmaz");
	T_EQ_STR(pz_theme_name("skewer"), "Şiş");
	T_EQ_STR(pz_theme_name("sacrifice"), "Fedâ");
	T_EQ_STR(pz_theme_name("endgame"), "Oyun sonu");
	T_ASSERT(pz_theme_name("long") == NULL);       /* length tags are not shown */
	T_ASSERT(pz_theme_name("noSuchTheme") == NULL); /* unknown keys are skipped */
	static Puzzle p;
	pz_parse(slurp("tests/host/fixtures/puzzle_daily.json"), &p);
	char s[256];
	pz_themes_text(&p, NULL, s, sizeof s);
	T_EQ_STR(s, "Oyun ortası · Açarak şah · 3 hamlede mat · Köşe matı · Çifte şah");
	pz_themes_text(&p, NULL, s, 20); /* truncated on a whole theme */
	T_EQ_STR(s, "Oyun ortası");
}

static void test_play_daily(void)
{
	static Puzzle p;
	static PzPlay s;
	pz_parse(slurp("tests/host/fixtures/puzzle_daily.json"), &p);
	pz_play_start(&s, &p);
	T_EQ_INT(s.player, 0);
	T_EQ_INT(pz_play_hint(&s), sq("c5"));
	T_EQ_INT(pz_play_expects_reply(&s), 0);
	/* a legal but wrong move: rejected, position unchanged */
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "c5b7")), PZ_WRONG);
	T_EQ_INT(s.mistakes, 1);
	T_EQ_INT(s.step, 0);
	T_EQ_STR(fen4(&s.pos), fen4(&p.start));
	/* an illegal move: nothing happens */
	T_EQ_INT(pz_play_try(&s, (Move){ (unsigned char) sq("a2"), (unsigned char) sq("a5"), 0, 0 }), PZ_ILLEGAL);
	T_EQ_INT(s.mistakes, 1);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "c5a6")), PZ_GOOD);
	T_EQ_INT(s.step, 1);
	T_EQ_INT(pz_play_expects_reply(&s), 1);
	Move r;
	T_EQ_INT(pz_play_reply(&s, &r), 1);
	T_EQ_INT(r.from, sq("c7"));
	T_EQ_INT(r.to, sq("b6"));
	T_EQ_INT(pz_play_reply(&s, &r), 0); /* the player's turn again */
	T_EQ_INT(pz_play_hint(&s), sq("d1"));
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "d1d6")), PZ_GOOD);
	T_EQ_INT(pz_play_reply(&s, &r), 1);
	T_EQ_INT(pz_play_done(&s), 0);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "a3b5")), PZ_WIN);
	T_EQ_INT(pz_play_done(&s), 1);
	T_EQ_INT(pz_play_result(&s), 'F'); /* one mistake */
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "a6c7")), PZ_ILLEGAL); /* over */
	T_EQ_INT(pz_play_hint(&s), -1);
}

static void test_play_clean_and_restart(void)
{
	static Puzzle p;
	static PzPlay s;
	pz_parse(slurp("tests/host/fixtures/puzzle_next.json"), &p);
	pz_play_start(&s, &p);
	T_EQ_INT(s.player, 1);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "h8h3")), PZ_WIN);
	T_EQ_INT(pz_play_result(&s), 'S');
	pz_play_restart(&s);
	T_EQ_INT(s.step, 0);
	T_EQ_INT(pz_play_done(&s), 0);
	T_EQ_STR(fen4(&s.pos), fen4(&p.start));
}

static void test_alternative_mate(void)
{
	/* both Ra8# and Re8# mate: Lichess accepts any mating move */
	static Puzzle p;
	static PzPlay s;
	Board b;
	chess_from_fen(&b, "6k1/5ppp/8/8/8/8/8/R3R1K1 w - - 0 1");
	const char *sol[] = { "e1e8" };
	T_EQ_INT(pz_setup(&p, &b, sol, 1), 0);
	pz_play_start(&s, &p);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "a1a2")), PZ_WRONG);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "a1a8")), PZ_WIN);
	T_EQ_INT(s.pos.sq[sq("a8")], WR);
	T_EQ_INT(pz_setup(&p, &b, (const char *[]){ "e1e9" }, 1), -1);
}

static void test_promotion_needed(void)
{
	static Puzzle p;
	static PzPlay s;
	Board b;
	chess_from_fen(&b, "8/1P4k1/8/8/8/8/6K1/8 w - - 0 1");
	const char *sol[] = { "b7b8q" };
	pz_setup(&p, &b, sol, 1);
	pz_play_start(&s, &p);
	T_EQ_INT(pz_play_needs_promo(&s, sq("b7"), sq("b8")), 1);
	T_EQ_INT(pz_play_needs_promo(&s, sq("g2"), sq("g3")), 0);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "b7b8n")), PZ_WRONG);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "b7b8q")), PZ_WIN);
}

static void test_show_solution(void)
{
	static Puzzle p;
	static PzPlay s;
	pz_parse(slurp("tests/host/fixtures/puzzle_daily.json"), &p);
	pz_play_start(&s, &p);
	T_EQ_INT(pz_play_try(&s, mv(&s.pos, "c5a6")), PZ_GOOD);
	Move m;
	/* the pending reply comes first, then each solution move in turn */
	int k = 0;
	while (pz_play_show_next(&s, &m)) k++;
	T_EQ_INT(k, 4);
	T_EQ_INT(m.to, sq("b5"));
	T_EQ_INT(pz_play_done(&s), 1);
	T_EQ_INT(pz_play_result(&s), 'X');
	T_EQ_INT(pz_play_show_next(&s, &m), 0);
}

static void test_history(void)
{
	static PzHist h[8];
	int n = pz_hist_parse("v1\nabc 20261017 S 1500 1\nxyz 20261018 F 1700 0\nbad line\n", h, 8);
	T_EQ_INT(n, 2);
	T_EQ_STR(h[0].id, "abc");
	T_EQ_INT(h[0].day, 20261017);
	T_EQ_INT(h[0].res, 'S');
	T_EQ_INT(h[1].rating, 1700);
	T_EQ_INT(h[1].daily, 0);
	T_EQ_INT(pz_hist_parse("garbage", h, 8), 0);
	T_EQ_INT(pz_hist_parse("", h, 8), 0);

	n = 0;
	n = pz_hist_add(h, n, 4, (PzHist){ "a", 20261001, 'S', 1200, 1 });
	n = pz_hist_add(h, n, 4, (PzHist){ "b", 20261002, 'S', 1300, 1 });
	n = pz_hist_add(h, n, 4, (PzHist){ "a", 20261003, 'X', 1200, 1 }); /* the first result counts */
	T_EQ_INT(n, 2);
	T_EQ_INT(h[pz_hist_find(h, n, "a")].res, 'S');
	T_EQ_INT(pz_hist_find(h, n, "zz"), -1);
	PzStats st;
	pz_hist_stats(h, n, &st);
	T_EQ_INT(st.total, 2);
	T_EQ_INT(st.solved, 2);
	T_EQ_INT(st.streak, 2);
	T_EQ_INT(st.best, 2);
	n = pz_hist_add(h, n, 4, (PzHist){ "c", 20261003, 'F', 1400, 0 });
	n = pz_hist_add(h, n, 4, (PzHist){ "d", 20261004, 'S', 1500, 0 });
	pz_hist_stats(h, n, &st);
	T_EQ_INT(st.total, 4);
	T_EQ_INT(st.solved, 4); /* F still solved, with a mistake */
	T_EQ_INT(st.streak, 1);  /* but it broke the streak */
	T_EQ_INT(st.best, 2);
	n = pz_hist_add(h, n, 4, (PzHist){ "e", 20261005, 'X', 1600, 0 }); /* full: the oldest goes */
	T_EQ_INT(n, 4);
	T_EQ_STR(h[0].id, "b");
	T_EQ_STR(h[3].id, "e");
	pz_hist_stats(h, n, &st);
	T_EQ_INT(st.solved, 3);
	T_EQ_INT(st.streak, 0);

	char buf[512];
	int len = pz_hist_format(h, n, buf, sizeof buf);
	T_ASSERT(len > 0);
	T_EQ_STR(buf, "v1\nb 20261002 S 1300 1\nc 20261003 F 1400 0\nd 20261004 S 1500 0\ne 20261005 X 1600 0\n");
	static PzHist back[8];
	T_EQ_INT(pz_hist_parse(buf, back, 8), 4);
	T_EQ_STR(back[3].id, "e");
	T_EQ_INT(pz_hist_format(h, n, buf, 10), -1);
}

static void test_urls(void)
{
	T_EQ_STR(PZ_URL_DAILY, "https://lichess.org/api/puzzle/daily");
	T_EQ_STR(PZ_URL_NEXT, "https://lichess.org/api/puzzle/next");
	T_ASSERT(pz_valid_id("VlHF6"));
	T_ASSERT(!pz_valid_id("../etc"));
	T_ASSERT(!pz_valid_id(""));
}

int main(void)
{
	T_RUN(test_parse_daily);
	T_RUN(test_parse_next);
	T_RUN(test_parse_bad);
	T_RUN(test_themes);
	T_RUN(test_play_daily);
	T_RUN(test_play_clean_and_restart);
	T_RUN(test_alternative_mate);
	T_RUN(test_promotion_needed);
	T_RUN(test_show_solution);
	T_RUN(test_history);
	T_RUN(test_urls);
	return T_DONE();
}

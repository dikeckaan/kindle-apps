#include "chess/chess_game.h"
#include "t.h"

static Game g;

static void play_all(Game *x, const char *const *ms)
{
	for (; *ms; ms++)
		if (game_play_uci(x, *ms) != 0) fprintf(stderr, "illegal %s\n", *ms);
}

static void test_fools_mate_is_mate(void)
{
	game_new(&g, 2, 3, 0);
	const char *ms[] = { "f2f3", "e7e5", "g2g4", "d8h4", NULL };
	play_all(&g, ms);
	T_EQ_INT(game_result(&g), RES_MATE);
	T_EQ_STR(result_text(RES_MATE, g.pos.side), "Şah mat — Siyah kazandı");
}

static Game from_fen(const char *fen)
{
	Game x;
	game_new(&x, 2, 3, 0);
	snprintf(x.start_fen, sizeof x.start_fen, "%s", fen);
	chess_from_fen(&x.pos, fen);
	return x;
}

static void test_stalemate(void)
{
	Game x = from_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
	T_EQ_INT(game_result(&x), RES_STALEMATE);
	T_EQ_STR(result_text(RES_STALEMATE, 1), "Pat — berabere");
}

static void test_fifty_move(void)
{
	Game x = from_fen("4k3/8/8/8/8/8/8/4K2R w - - 100 80");
	T_EQ_INT(game_result(&x), RES_FIFTY);
}

static void test_threefold(void)
{
	game_new(&g, 2, 3, 0);
	const char *ms[] = { "g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8", NULL };
	play_all(&g, ms);
	T_EQ_INT(game_result(&g), RES_REPETITION);
}

static void test_insufficient_material(void)
{
	Game x = from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
	T_EQ_INT(game_result(&x), RES_MATERIAL);
	x = from_fen("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1");
	T_EQ_INT(game_result(&x), RES_MATERIAL);
	x = from_fen("4k3/8/8/8/8/8/8/1N2K3 w - - 0 1");
	T_EQ_INT(game_result(&x), RES_MATERIAL);
	x = from_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
	T_EQ_INT(game_result(&x), RES_ONGOING);
	x = from_fen("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
	T_EQ_INT(game_result(&x), RES_ONGOING);
}

static void test_play_uci_rejects_illegal(void)
{
	game_new(&g, 2, 3, 0);
	T_EQ_INT(game_play_uci(&g, "e2e5"), -1);
	T_EQ_INT(game_play_uci(&g, "zz"), -1);
	T_EQ_INT(game_play_uci(&g, "e7e5"), -1); /* not black's turn */
	T_EQ_INT(g.n, 0);
	T_EQ_INT(game_play_uci(&g, "e2e4"), 0);
	T_EQ_INT(g.n, 1);
}

static void test_uci_promotion_roundtrip(void)
{
	Game x = from_fen("8/P6k/8/8/8/8/8/K7 w - - 0 1");
	T_EQ_INT(game_play_uci(&x, "a7a8n"), 0);
	char s[6];
	move_to_uci(x.hist[0], s);
	T_EQ_STR(s, "a7a8n");
	T_EQ_INT(x.pos.sq[56], WN);
	T_EQ_INT(game_play_uci(&x, "h7h8"), 0); /* plain move has no promo letter */
	move_to_uci(x.hist[1], s);
	T_EQ_STR(s, "h7h8");
}

static void test_undo_two_plies(void)
{
	game_new(&g, 1, 3, 0);
	const char *ms[] = { "e2e4", "e7e5", "g1f3", NULL };
	play_all(&g, ms);
	T_EQ_INT(game_undo(&g, 2), 2);
	T_EQ_INT(g.n, 1);
	char fen[100];
	chess_to_fen(&g.pos, fen, sizeof fen);
	T_EQ_STR(fen, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
	T_EQ_INT(game_undo(&g, 5), 1);
	T_EQ_INT(g.n, 0);
	T_EQ_INT(game_undo(&g, 1), 0);
}

static void test_uci_position_string(void)
{
	game_new(&g, 1, 3, 0);
	char s[256];
	game_uci_position(&g, s, sizeof s);
	T_EQ_STR(s, "position fen " CHESS_START_FEN);
	game_play_uci(&g, "e2e4");
	game_play_uci(&g, "c7c5");
	game_uci_position(&g, s, sizeof s);
	T_EQ_STR(s, "position fen " CHESS_START_FEN " moves e2e4 c7c5");
}

static void test_save_load_roundtrip(void)
{
	game_new(&g, 1, 5, 1);
	g.flip = 1;
	const char *ms[] = { "e2e4", "e7e5", "g1f3", NULL };
	play_all(&g, ms);
	char buf[4096];
	T_ASSERT(game_save(&g, buf, sizeof buf) > 0);
	Game h;
	memset(&h, 0, sizeof h);
	T_EQ_INT(game_load(&h, buf), 0);
	T_EQ_INT(h.n, 3);
	T_EQ_INT(h.mode, 1);
	T_EQ_INT(h.level, 5);
	T_EQ_INT(h.human_side, 1);
	T_EQ_INT(h.flip, 1);
	T_ASSERT(memcmp(&h.pos, &g.pos, sizeof h.pos) == 0);
}

static void test_game_load_corrupt(void)
{
	game_new(&g, 1, 3, 0);
	game_play_uci(&g, "d2d4");
	Game before = g;
	T_EQ_INT(game_load(&g, ""), -1);
	T_EQ_INT(game_load(&g, "v9\n1 3 0 0\n" CHESS_START_FEN "\n\n"), -1);
	T_EQ_INT(game_load(&g, "v1\n1 3 0 0\nnot a fen\n\n"), -1);
	T_EQ_INT(game_load(&g, "v1\n1 3 0 0\n" CHESS_START_FEN "\ne2e4 e2e4\n"), -1);
	T_EQ_INT(game_load(&g, "v1\n7 3 0 0\n" CHESS_START_FEN "\n\n"), -1); /* bad mode */
	T_EQ_INT(game_load(&g, "v1\n1 3 0 0\n" CHESS_START_FEN), -1);         /* truncated */
	T_ASSERT(memcmp(&g, &before, sizeof g) == 0);
}

int main(void)
{
	T_RUN(test_fools_mate_is_mate);
	T_RUN(test_stalemate);
	T_RUN(test_fifty_move);
	T_RUN(test_threefold);
	T_RUN(test_insufficient_material);
	T_RUN(test_play_uci_rejects_illegal);
	T_RUN(test_uci_promotion_roundtrip);
	T_RUN(test_undo_two_plies);
	T_RUN(test_uci_position_string);
	T_RUN(test_save_load_roundtrip);
	T_RUN(test_game_load_corrupt);
	return T_DONE();
}

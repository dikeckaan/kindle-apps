#include <stdio.h>
#include <stdlib.h>
#include "chess/chess_game.h"
#include "chess/history.h"
#include "t.h"

static char sans[GAME_MAX_PLIES][HIST_SAN_MAX];

/* plays space-separated UCI moves from fen; 0 when all are legal */
static int play(Game *g, const char *fen, const char *ucis)
{
	game_new(g, 2, 3, 0);
	snprintf(g->start_fen, sizeof g->start_fen, "%s", fen);
	if (chess_from_fen(&g->pos, fen) != 0) return -1;
	char m[8];
	for (const char *p = ucis; *p;) {
		int k = 0;
		while (*p == ' ') p++;
		while (*p && *p != ' ' && k < 7) m[k++] = *p++;
		m[k] = '\0';
		if (k && game_play_uci(g, m) != 0) return -1;
	}
	return 0;
}

/* the SAN of the last of the moves */
static const char *last_san(const char *fen, const char *ucis)
{
	static Game g;
	if (play(&g, fen, ucis) != 0) return "illegal";
	int n = history_sans(&g, sans);
	return n ? sans[n - 1] : "";
}

static void test_tricky_san(void)
{
	/* knights on b1 and f1 both reach d2: file; rooks on a1 and a5 reach a3: rank */
	T_EQ_STR(last_san("4k3/8/8/8/8/8/8/1N1K1N2 w - - 0 1", "b1d2"), "Nbd2");
	T_EQ_STR(last_san("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1", "a5a3"), "R5a3");
	/* three queens: file and rank */
	T_EQ_STR(last_san("4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1", "a1b2"), "Qa1b2");
	/* a pinned knight does not count for disambiguation */
	T_EQ_STR(last_san("4k3/8/8/8/1b6/8/3N4/1N2K3 w - - 0 1", "b1c3"), "Nc3");
	/* en passant (black), promotion with capture and check, underpromotion */
	T_EQ_STR(last_san("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1", "e2e4 d4e3"), "dxe3");
	T_EQ_STR(last_san("3rk3/2P5/8/8/8/8/8/4K3 w - - 0 1", "c7d8q"), "cxd8=Q+");
	T_EQ_STR(last_san("4k3/8/8/8/8/8/p7/4K3 b - - 0 1", "a2a1n"), "a1=N");
	/* castling both ways, black, and a castle that gives check */
	T_EQ_STR(last_san("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8"), "O-O-O");
	T_EQ_STR(last_san("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8"), "O-O");
	T_EQ_STR(last_san("5k2/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1"), "O-O+");
	/* fool's mate */
	T_EQ_STR(last_san(CHESS_START_FEN, "f2f3 e7e5 g2g4 d8h4"), "Qh4#");
}

static void test_figurine(void)
{
	char out[HIST_SAN_MAX];
	san_figurine("Nf3", out, sizeof out);
	T_EQ_STR(out, "♞f3");
	san_figurine("exd8=Q+", out, sizeof out);
	T_EQ_STR(out, "exd8=♛+");
	san_figurine("O-O-O", out, sizeof out);
	T_EQ_STR(out, "O-O-O");
	san_figurine("Kxe2", out, sizeof out);
	T_EQ_STR(out, "♚xe2");
	san_figurine("Rae1#", out, sizeof out);
	T_EQ_STR(out, "♜ae1#");
	/* a short buffer never cuts a glyph in half */
	san_figurine("Nf3", out, 3);
	T_EQ_STR(out, "");
}

static void test_numbers(void)
{
	Game g;
	T_ASSERT(play(&g, CHESS_START_FEN, "e2e4 e7e5 g1f3") == 0);
	T_EQ_INT(history_move_no(&g, 0), 1);
	T_EQ_INT(history_is_black(&g, 1), 1);
	T_EQ_INT(history_move_no(&g, 2), 2);
	/* black starts on move 12 */
	T_ASSERT(play(&g, "4k3/8/8/8/8/8/8/4K3 b - - 0 12", "e8d8 e1d1") == 0);
	T_EQ_INT(history_move_no(&g, 0), 12);
	T_EQ_INT(history_is_black(&g, 0), 1);
	T_EQ_INT(history_move_no(&g, 1), 13);
	T_EQ_INT(history_is_black(&g, 1), 0);
}

static void test_pgn(void)
{
	static Game g;
	static char buf[16384];
	T_ASSERT(play(&g, CHESS_START_FEN, "f2f3 e7e5 g2g4 d8h4") == 0);
	T_EQ_STR(history_result_tag(&g), "0-1");
	int n = history_pgn(&g, "2026.10.20", "Siz", "Kindle (Orta)", buf, sizeof buf);
	T_ASSERT(n > 0);
	T_EQ_STR(buf, "[Event \"Kindle Satranç\"]\n[Site \"Kindle\"]\n[Date \"2026.10.20\"]\n[Round \"-\"]\n"
		      "[White \"Siz\"]\n[Black \"Kindle (Orta)\"]\n[Result \"0-1\"]\n\n"
		      "1. f3 e5 2. g4 Qh4# 0-1\n");
	T_EQ_INT(n, (int) strlen(buf));
	/* too small a buffer */
	T_EQ_INT(history_pgn(&g, "2026.10.20", "A", "B", buf, 40), -1);

	/* running game: "*"; non-standard start: SetUp/FEN and "12..." for black's first move */
	T_ASSERT(play(&g, "4k3/8/8/8/8/8/8/4K3 b - - 0 12", "e8d8 e1d1") == 0);
	T_EQ_STR(history_result_tag(&g), "1/2-1/2"); /* bare kings */
	T_ASSERT(play(&g, "4k3/8/8/8/8/8/8/R3K3 b - - 0 12", "e8d8 e1d1") == 0);
	history_pgn(&g, "2026.10.20", "Beyaz", "Siyah", buf, sizeof buf);
	T_ASSERT(strstr(buf, "[SetUp \"1\"]\n[FEN \"4k3/8/8/8/8/8/8/R3K3 b - - 0 12\"]\n\n12... Kd8 13. Kd1 *\n") != NULL);

	/* a long game wraps at 79 columns and ends with the result */
	T_ASSERT(play(&g, CHESS_START_FEN, "") == 0);
	const char *cycle[] = { "g1f3", "g8f6", "f3g1", "f6g8" };
	for (int i = 0; i < 40; i++) game_play_uci(&g, cycle[i % 4]);
	n = history_pgn(&g, "2026.10.20", "Beyaz", "Siyah", buf, sizeof buf);
	T_ASSERT(n > 0);
	int col = 0;
	for (const char *p = strstr(buf, "\n\n") + 2; *p; p++) {
		col = *p == '\n' ? 0 : col + 1;
		T_ASSERT(col <= 79);
	}
	T_ASSERT(strstr(buf, "20. Ng1 Ng8 1/2-1/2\n") != NULL); /* threefold repetition */

	/* the PGN survives save/load: the history is the same */
	static char save[8192];
	static Game h;
	T_ASSERT(game_save(&g, save, sizeof save) > 0);
	T_ASSERT(game_load(&h, save) == 0);
	static char buf2[16384];
	history_pgn(&h, "2026.10.20", "Beyaz", "Siyah", buf2, sizeof buf2);
	T_EQ_STR(buf2, buf);
	/* undo shortens it */
	game_undo(&h, 2);
	history_pgn(&h, "2026.10.20", "Beyaz", "Siyah", buf2, sizeof buf2);
	T_ASSERT(strstr(buf2, "19. Nf3 Nf6 1/2-1/2\n") != NULL);
}

int main(void)
{
	T_RUN(test_tricky_san);
	T_RUN(test_figurine);
	T_RUN(test_numbers);
	T_RUN(test_pgn);
	return T_DONE();
}

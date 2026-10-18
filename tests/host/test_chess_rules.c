#include <time.h>
#include "chess/chess_rules.h"
#include "t.h"

#define KIWIPETE "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
#define POS3 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
#define POS4 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"

static Board fen(const char *f)
{
	Board b;
	if (chess_from_fen(&b, f) != 0) fprintf(stderr, "bad fen %s\n", f);
	return b;
}

static int sq(const char *s) { return (s[0] - 'a') + 8 * (s[1] - '1'); }

static Move find(const Board *b, const char *from, const char *to, int promo)
{
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(b, ms);
	for (int i = 0; i < n; i++)
		if (ms[i].from == sq(from) && ms[i].to == sq(to) && ms[i].promo == promo) return ms[i];
	return (Move){ 255, 255, 0, 0 };
}

static void test_fen_roundtrip_start(void)
{
	char out[100];
	Board b = fen(CHESS_START_FEN);
	chess_to_fen(&b, out, sizeof out);
	T_EQ_STR(out, CHESS_START_FEN);
	b = fen(KIWIPETE);
	chess_to_fen(&b, out, sizeof out);
	T_EQ_STR(out, KIWIPETE);
}

static void test_fen_rejects_garbage(void)
{
	Board b = fen(CHESS_START_FEN), before = b;
	T_EQ_INT(chess_from_fen(&b, "xyz"), -1);
	T_EQ_INT(chess_from_fen(&b, "8/8/8/8/8/8/8 w - - 0 1"), -1);                 /* 7 ranks */
	T_EQ_INT(chess_from_fen(&b, "K6K/8/8/8/8/8/8/k7 w - - 0 1"), -1);           /* two white kings */
	T_EQ_INT(chess_from_fen(&b, "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"), -1);
	T_EQ_INT(chess_from_fen(&b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"), -1);
	T_EQ_INT(chess_from_fen(&b, ""), -1);
	T_ASSERT(memcmp(&b, &before, sizeof b) == 0);
}

static void test_perft_start(void)
{
	Board b = fen(CHESS_START_FEN);
	T_EQ_INT(chess_perft(&b, 1), 20);
	T_EQ_INT(chess_perft(&b, 2), 400);
	T_EQ_INT(chess_perft(&b, 3), 8902);
	clock_t t = clock();
	T_EQ_INT(chess_perft(&b, 4), 197281);
	T_ASSERT((clock() - t) < 2 * CLOCKS_PER_SEC);
}

static void test_perft_kiwipete(void)
{
	Board b = fen(KIWIPETE);
	T_EQ_INT(chess_perft(&b, 1), 48);
	T_EQ_INT(chess_perft(&b, 2), 2039);
	T_EQ_INT(chess_perft(&b, 3), 97862);
}

static void test_perft_position3(void)
{
	Board b = fen(POS3);
	T_EQ_INT(chess_perft(&b, 1), 14);
	T_EQ_INT(chess_perft(&b, 2), 191);
	T_EQ_INT(chess_perft(&b, 3), 2812);
	T_EQ_INT(chess_perft(&b, 4), 43238);
}

static void test_perft_position4(void)
{
	Board b = fen(POS4);
	T_EQ_INT(chess_perft(&b, 1), 6);
	T_EQ_INT(chess_perft(&b, 2), 264);
	T_EQ_INT(chess_perft(&b, 3), 9467);
}

static void test_ep_capture_removes_pawn(void)
{
	Board b = fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
	Move m = find(&b, "e5", "d6", 0);
	T_ASSERT(m.flags & MV_EP);
	chess_make(&b, m);
	T_EQ_INT(b.sq[sq("d5")], EMPTY);
	T_EQ_INT(b.sq[sq("d6")], WP);
}

static void test_castling_moves_rook(void)
{
	Board b = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	Move m = find(&b, "e1", "g1", 0);
	T_ASSERT(m.flags & MV_CASTLE);
	chess_make(&b, m);
	T_EQ_INT(b.sq[sq("f1")], WR);
	T_EQ_INT(b.sq[sq("h1")], EMPTY);
	T_EQ_INT(b.castling & 3, 0);
	m = find(&b, "e8", "c8", 0);
	chess_make(&b, m);
	T_EQ_INT(b.sq[sq("d8")], BR);
	T_EQ_INT(b.sq[sq("a8")], EMPTY);
}

static void test_no_castling_through_check(void)
{
	Board b = fen("4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1"); /* f2 rook does not attack f1… */
	b = fen("4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1");         /* f8 rook attacks f1 */
	T_EQ_INT(find(&b, "e1", "g1", 0).from, 255);
	T_ASSERT(find(&b, "e1", "c1", 0).from != 255);
}

static void test_promotion_four_choices(void)
{
	Board b = fen("8/P6k/8/8/8/8/8/K7 w - - 0 1");
	int found = 0;
	for (int p = WN; p <= WQ; p++) found += find(&b, "a7", "a8", p).from != 255;
	T_EQ_INT(found, 4);
	Move m = find(&b, "a7", "a8", WN);
	chess_make(&b, m);
	T_EQ_INT(b.sq[sq("a8")], WN);
	b = fen("k7/8/8/8/8/8/7p/K7 b - - 0 1");
	chess_make(&b, find(&b, "h2", "h1", WQ));
	T_EQ_INT(b.sq[sq("h1")], BQ);
}

static void test_in_check(void)
{
	Board b = fen("4k3/8/8/8/8/8/8/4K2r w - - 0 1");
	T_ASSERT(chess_in_check(&b, 0));
	T_ASSERT(!chess_in_check(&b, 1));
}

int main(void)
{
	T_RUN(test_fen_roundtrip_start);
	T_RUN(test_fen_rejects_garbage);
	T_RUN(test_perft_start);
	T_RUN(test_perft_kiwipete);
	T_RUN(test_perft_position3);
	T_RUN(test_perft_position4);
	T_RUN(test_ep_capture_removes_pawn);
	T_RUN(test_castling_moves_rook);
	T_RUN(test_no_castling_through_check);
	T_RUN(test_promotion_four_choices);
	T_RUN(test_in_check);
	return T_DONE();
}

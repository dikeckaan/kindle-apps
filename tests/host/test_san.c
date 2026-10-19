#include <stdlib.h>
#include "chess/chess_game.h"
#include "chess/san.h"
#include "t.h"

static Board fen(const char *f)
{
	Board b;
	if (chess_from_fen(&b, f) != 0) {
		fprintf(stderr, "bad test FEN %s\n", f);
		exit(2);
	}
	return b;
}

static int sq(const char *s) { return (s[0] - 'a') + 8 * (s[1] - '1'); }

/* parses san on f; returns its UCI text, "-" when rejected */
static const char *uci_of(const char *f, const char *san)
{
	static char out[8];
	Board b = fen(f);
	Move m;
	if (san_parse(&b, san, &m) != 0) return "-";
	move_to_uci(m, out);
	return out;
}

static void test_pawns_and_pieces(void)
{
	T_EQ_STR(uci_of(CHESS_START_FEN, "e4"), "e2e4");
	T_EQ_STR(uci_of(CHESS_START_FEN, "e3"), "e2e3");
	T_EQ_STR(uci_of(CHESS_START_FEN, "Nf3"), "g1f3");
	T_EQ_STR(uci_of(CHESS_START_FEN, "Nc3"), "b1c3");
	T_EQ_STR(uci_of(CHESS_START_FEN, "e5"), "-");  /* not reachable */
	T_EQ_STR(uci_of(CHESS_START_FEN, "Ke2"), "-"); /* occupied */
	T_EQ_STR(uci_of(CHESS_START_FEN, "O-O"), "-");
	T_EQ_STR(uci_of(CHESS_START_FEN, "Zz9"), "-");
	T_EQ_STR(uci_of(CHESS_START_FEN, ""), "-");
	T_EQ_STR(uci_of(CHESS_START_FEN, "Nf3extra"), "-");
	T_EQ_STR(uci_of(CHESS_START_FEN, "Ni3"), "-");
	/* annotations and check marks are ignored */
	T_EQ_STR(uci_of(CHESS_START_FEN, "e4!?"), "e2e4");
	T_EQ_STR(uci_of(CHESS_START_FEN, "Nf3?!"), "g1f3");
	T_EQ_STR(uci_of("rnbqkbnr/pppp1ppp/8/4p3/2B1P3/8/PPPP1PPP/RNBQK1NR w KQkq - 0 3", "Qh5"), "d1h5");
	T_EQ_STR(uci_of("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 2 4", "Qxf7#"), "h5f7");
	T_EQ_STR(uci_of("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 2 4", "Qxf7+"), "h5f7");
	/* black to move */
	T_EQ_STR(uci_of("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "c5"), "c7c5");
	T_EQ_STR(uci_of("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "Nf6"), "g8f6");
	T_EQ_STR(uci_of("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "e4"), "-");
}

static void test_captures(void)
{
	const char *f = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2";
	T_EQ_STR(uci_of(f, "exd5"), "e4d5");
	T_EQ_STR(uci_of(f, "xd5"), "-");
	T_EQ_STR(uci_of(f, "dxe5"), "-");
	/* b-file pawn versus bishop */
	const char *g = "4k3/8/8/8/8/2p5/1P1B4/4K3 w - - 0 1";
	T_EQ_STR(uci_of(g, "bxc3"), "b2c3");
	T_EQ_STR(uci_of(g, "Bxc3"), "d2c3");
	/* en passant */
	T_EQ_STR(uci_of("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2", "exd6"), "e5d6");
	T_EQ_STR(uci_of("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 2", "exd3"), "e4d3");
}

static void test_disambiguation(void)
{
	const char *knights = "4k3/8/8/8/8/8/8/1N1K1N2 w - - 0 1";
	T_EQ_STR(uci_of(knights, "Nd2"), "-"); /* ambiguous */
	T_EQ_STR(uci_of(knights, "Nbd2"), "b1d2");
	T_EQ_STR(uci_of(knights, "Nfd2"), "f1d2");
	T_EQ_STR(uci_of(knights, "Ncd2"), "-");
	const char *rooks = "4k3/8/8/R7/8/8/8/R3K3 w - - 0 1";
	T_EQ_STR(uci_of(rooks, "Ra3"), "-");
	T_EQ_STR(uci_of(rooks, "R1a3"), "a1a3");
	T_EQ_STR(uci_of(rooks, "R5a3"), "a5a3");
	T_EQ_STR(uci_of(rooks, "R5xa3"), "a5a3");
	const char *queens = "4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1";
	T_EQ_STR(uci_of(queens, "Qb2"), "-");
	T_EQ_STR(uci_of(queens, "Qab2"), "-");
	T_EQ_STR(uci_of(queens, "Q1b2"), "-");
	T_EQ_STR(uci_of(queens, "Qa1b2"), "a1b2");
	T_EQ_STR(uci_of(queens, "Qcb2"), "c1b2");
	T_EQ_STR(uci_of(queens, "Q3b2"), "a3b2");
	/* a pinned knight does not count for ambiguity */
	T_EQ_STR(uci_of("4k3/8/8/8/1b6/8/3N4/4K1N1 w - - 0 1", "Nf3"), "g1f3");
}

static void test_promotion_and_castling(void)
{
	const char *p = "5n2/4P3/8/8/8/8/k7/4K3 w - - 0 1";
	T_EQ_STR(uci_of(p, "e8=Q"), "e7e8q");
	T_EQ_STR(uci_of(p, "e8Q"), "e7e8q");
	T_EQ_STR(uci_of(p, "e8=N"), "e7e8n");
	T_EQ_STR(uci_of(p, "exf8=R+"), "e7f8r");
	T_EQ_STR(uci_of(p, "exf8=B"), "e7f8b");
	T_EQ_STR(uci_of(p, "e8"), "-"); /* the promotion piece is required */
	T_EQ_STR(uci_of(p, "e8=K"), "-");
	T_EQ_STR(uci_of("4k3/8/8/8/8/8/1p6/4K3 b - - 0 1", "b1=Q+"), "b2b1q");
	const char *c = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
	T_EQ_STR(uci_of(c, "O-O"), "e1g1");
	T_EQ_STR(uci_of(c, "O-O-O"), "e1c1");
	T_EQ_STR(uci_of(c, "0-0"), "e1g1");
	T_EQ_STR(uci_of(c, "0-0-0+"), "e1c1");
	T_EQ_STR(uci_of("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "O-O-O"), "e8c8");
	T_EQ_STR(uci_of("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1", "O-O"), "-");
}

static void fmt_is(const char *f, const char *uci, const char *want)
{
	Board b = fen(f);
	Game g;
	game_new(&g, 2, 3, 0);
	snprintf(g.start_fen, sizeof g.start_fen, "%s", f);
	g.pos = b;
	T_ASSERT(game_play_uci(&g, uci) == 0);
	char out[16];
	san_format(&b, g.hist[0], out, sizeof out);
	T_EQ_STR(out, want);
}

static void test_format(void)
{
	fmt_is(CHESS_START_FEN, "e2e4", "e4");
	fmt_is(CHESS_START_FEN, "g1f3", "Nf3");
	fmt_is("4k3/8/8/8/8/8/8/1N1K1N2 w - - 0 1", "b1d2", "Nbd2");
	fmt_is("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1", "a5a3", "R5a3");
	fmt_is("4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1", "a1b2", "Qa1b2");
	fmt_is("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", "e4d5", "exd5");
	fmt_is("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2", "e5d6", "exd6");
	fmt_is("5n2/4P3/8/8/8/8/k7/4K3 w - - 0 1", "e7f8q", "exf8=Q");
	fmt_is("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "O-O-O");
	fmt_is("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "O-O");
	fmt_is("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 2 4", "h5f7", "Qxf7#");
	fmt_is("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "a1a8", "Ra8+");
}

/* every legal move formats to text that parses back to the same move */
static void roundtrip(const char *f)
{
	Board b = fen(f);
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&b, ms);
	for (int i = 0; i < n; i++) {
		char s[16];
		Move m;
		san_format(&b, ms[i], s, sizeof s);
		T_ASSERT(san_parse(&b, s, &m) == 0);
		T_ASSERT(m.from == ms[i].from && m.to == ms[i].to && m.promo == ms[i].promo && m.flags == ms[i].flags);
	}
}

static void test_roundtrip(void)
{
	roundtrip(CHESS_START_FEN);
	roundtrip("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"); /* kiwipete */
	roundtrip("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1");
	roundtrip("4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1");
	roundtrip("n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1");
	roundtrip("n1n5/PPPk4/8/8/8/8/4Kppp/5N1N w - - 0 1");
}

static void test_replay(void)
{
	Board b;
	chess_from_fen(&b, CHESS_START_FEN);
	Move last;
	T_EQ_INT(san_replay(&b, "1. e4 e5 2. Nf3 {a comment} Nc6 3. Bb5 a6 $1 4. Ba4 Nf6 5. O-O 1-0", 99, &last), 9);
	char f[100];
	chess_to_fen(&b, f, sizeof f);
	T_EQ_STR(f, "r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 3 5");
	T_EQ_INT(last.from, sq("e1"));
	T_EQ_INT(last.to, sq("g1"));
	/* stops after max_plies */
	chess_from_fen(&b, CHESS_START_FEN);
	T_EQ_INT(san_replay(&b, "e4 e5 Nf3 Nc6", 3, &last), 3);
	T_EQ_INT(b.side, 1);
	T_EQ_INT(last.to, sq("f3"));
	/* black move numbers "3..." */
	chess_from_fen(&b, CHESS_START_FEN);
	T_EQ_INT(san_replay(&b, "1. d4 1... d5 2. c4", 99, &last), 3);
	/* an illegal move: -1, and the board is left untouched */
	chess_from_fen(&b, CHESS_START_FEN);
	T_EQ_INT(san_replay(&b, "e4 e5 Ke3", 99, &last), -1);
	chess_to_fen(&b, f, sizeof f);
	T_EQ_STR(f, CHESS_START_FEN);
	chess_from_fen(&b, CHESS_START_FEN);
	T_EQ_INT(san_replay(&b, "", 99, &last), 0);
}

int main(void)
{
	T_RUN(test_pawns_and_pieces);
	T_RUN(test_captures);
	T_RUN(test_disambiguation);
	T_RUN(test_promotion_and_castling);
	T_RUN(test_format);
	T_RUN(test_roundtrip);
	T_RUN(test_replay);
	return T_DONE();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chess_rules.h"

#define FILE_OF(s) ((s) & 7)
#define RANK_OF(s) ((s) >> 3)

static const int knight_d[8][2] = { { 1, 2 }, { 2, 1 }, { 2, -1 }, { 1, -2 }, { -1, -2 }, { -2, -1 }, { -2, 1 }, { -1, 2 } };
static const int king_d[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
static const int rook_d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
static const int bishop_d[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

int chess_piece_color(int p)
{
	if (p == EMPTY) return -1;
	return p >= BP ? 1 : 0;
}

static int type_of(int p) { return p >= BP ? p - 6 : p; } /* WP..WK */
static int make_piece(int type, int color) { return color ? type + 6 : type; }

/* square at (file + df, rank + dr) or -1 when off the board */
static int step(int s, int df, int dr)
{
	int f = FILE_OF(s) + df, r = RANK_OF(s) + dr;
	return (f < 0 || f > 7 || r < 0 || r > 7) ? -1 : r * 8 + f;
}

static int attacked(const Board *b, int s, int by)
{
	int pawn_dir = by == 0 ? -1 : 1; /* look back toward the attacking pawns */
	for (int df = -1; df <= 1; df += 2) {
		int t = step(s, df, pawn_dir);
		if (t >= 0 && b->sq[t] == make_piece(WP, by)) return 1;
	}
	for (int i = 0; i < 8; i++) {
		int t = step(s, knight_d[i][0], knight_d[i][1]);
		if (t >= 0 && b->sq[t] == make_piece(WN, by)) return 1;
		t = step(s, king_d[i][0], king_d[i][1]);
		if (t >= 0 && b->sq[t] == make_piece(WK, by)) return 1;
	}
	for (int i = 0; i < 4; i++) {
		for (int t = step(s, rook_d[i][0], rook_d[i][1]); t >= 0; t = step(t, rook_d[i][0], rook_d[i][1])) {
			int p = b->sq[t];
			if (p == EMPTY) continue;
			if (p == make_piece(WR, by) || p == make_piece(WQ, by)) return 1;
			break;
		}
		for (int t = step(s, bishop_d[i][0], bishop_d[i][1]); t >= 0; t = step(t, bishop_d[i][0], bishop_d[i][1])) {
			int p = b->sq[t];
			if (p == EMPTY) continue;
			if (p == make_piece(WB, by) || p == make_piece(WQ, by)) return 1;
			break;
		}
	}
	return 0;
}

static int king_square(const Board *b, int side)
{
	int k = make_piece(WK, side);
	for (int s = 0; s < 64; s++)
		if (b->sq[s] == k) return s;
	return -1;
}

int chess_in_check(const Board *b, int side)
{
	int k = king_square(b, side);
	return k >= 0 && attacked(b, k, !side);
}

static void add(Move *out, int *n, int from, int to, int promo, int flags)
{
	out[(*n)++] = (Move){ (unsigned char) from, (unsigned char) to, (unsigned char) promo, (unsigned char) flags };
}

static void add_pawn(Move *out, int *n, int from, int to, int flags)
{
	if (RANK_OF(to) == 0 || RANK_OF(to) == 7) {
		for (int p = WQ; p >= WN; p--) add(out, n, from, to, p, flags);
	} else {
		add(out, n, from, to, 0, flags);
	}
}

static int pseudo_moves(const Board *b, Move *out)
{
	int n = 0, us = b->side;
	for (int s = 0; s < 64; s++) {
		int p = b->sq[s];
		if (chess_piece_color(p) != us) continue;
		int t = type_of(p);
		if (t == WP) {
			int dir = us == 0 ? 1 : -1, start = us == 0 ? 1 : 6;
			int one = step(s, 0, dir);
			if (one >= 0 && b->sq[one] == EMPTY) {
				add_pawn(out, &n, s, one, 0);
				int two = step(s, 0, 2 * dir);
				if (RANK_OF(s) == start && b->sq[two] == EMPTY) add(out, &n, s, two, 0, MV_DOUBLE);
			}
			for (int df = -1; df <= 1; df += 2) {
				int c = step(s, df, dir);
				if (c < 0) continue;
				if (chess_piece_color(b->sq[c]) == !us) add_pawn(out, &n, s, c, MV_CAPTURE);
				else if (c == b->ep) add(out, &n, s, c, 0, MV_CAPTURE | MV_EP);
			}
		} else if (t == WN || t == WK) {
			const int(*d)[2] = t == WN ? knight_d : king_d;
			for (int i = 0; i < 8; i++) {
				int to = step(s, d[i][0], d[i][1]);
				if (to < 0) continue;
				int c = chess_piece_color(b->sq[to]);
				if (c == us) continue;
				add(out, &n, s, to, 0, c < 0 ? 0 : MV_CAPTURE);
			}
		} else {
			for (int pass = 0; pass < 2; pass++) {
				const int(*d)[2] = pass == 0 ? rook_d : bishop_d;
				if ((pass == 0 && t == WB) || (pass == 1 && t == WR)) continue;
				for (int i = 0; i < 4; i++) {
					for (int to = step(s, d[i][0], d[i][1]); to >= 0; to = step(to, d[i][0], d[i][1])) {
						int c = chess_piece_color(b->sq[to]);
						if (c == us) break;
						add(out, &n, s, to, 0, c < 0 ? 0 : MV_CAPTURE);
						if (c >= 0) break;
					}
				}
			}
		}
	}
	/* castling: rights, empty squares between, king not in/through/into check */
	int home = us == 0 ? 4 : 60;
	if (b->sq[home] == make_piece(WK, us) && !attacked(b, home, !us)) {
		int kbit = us == 0 ? 1 : 4, qbit = us == 0 ? 2 : 8;
		if ((b->castling & kbit) && b->sq[home + 3] == make_piece(WR, us) && b->sq[home + 1] == EMPTY &&
		    b->sq[home + 2] == EMPTY && !attacked(b, home + 1, !us) && !attacked(b, home + 2, !us))
			add(out, &n, home, home + 2, 0, MV_CASTLE);
		if ((b->castling & qbit) && b->sq[home - 4] == make_piece(WR, us) && b->sq[home - 1] == EMPTY &&
		    b->sq[home - 2] == EMPTY && b->sq[home - 3] == EMPTY && !attacked(b, home - 1, !us) &&
		    !attacked(b, home - 2, !us))
			add(out, &n, home, home - 2, 0, MV_CASTLE);
	}
	return n;
}

void chess_make(Board *b, Move m)
{
	int us = b->side, p = b->sq[m.from];
	int captured = b->sq[m.to];
	b->sq[m.to] = m.promo ? (unsigned char) make_piece(m.promo, us) : (unsigned char) p;
	b->sq[m.from] = EMPTY;
	if (m.flags & MV_EP) {
		b->sq[m.to + (us == 0 ? -8 : 8)] = EMPTY;
		captured = make_piece(WP, !us);
	}
	if (m.flags & MV_CASTLE) {
		if (m.to > m.from) {
			b->sq[m.from + 1] = b->sq[m.from + 3];
			b->sq[m.from + 3] = EMPTY;
		} else {
			b->sq[m.from - 1] = b->sq[m.from - 4];
			b->sq[m.from - 4] = EMPTY;
		}
	}
	/* castling rights: any move from/to a king or rook home square clears the matching right */
	static const struct { int sq, mask; } rights[] = { { 4, 3 }, { 60, 12 }, { 0, 2 }, { 7, 1 }, { 56, 8 }, { 63, 4 } };
	for (int i = 0; i < 6; i++)
		if (m.from == rights[i].sq || m.to == rights[i].sq) b->castling &= ~rights[i].mask;
	b->ep = (m.flags & MV_DOUBLE) ? (m.from + m.to) / 2 : -1;
	b->halfmove = (type_of(p) == WP || captured != EMPTY) ? 0 : b->halfmove + 1;
	if (us == 1) b->fullmove++;
	b->side = !us;
}

int chess_legal_moves(const Board *b, Move *out)
{
	Move ps[CHESS_MAX_MOVES];
	int n = pseudo_moves(b, ps), k = 0;
	for (int i = 0; i < n; i++) {
		Board c = *b;
		chess_make(&c, ps[i]);
		if (!chess_in_check(&c, b->side)) out[k++] = ps[i];
	}
	return k;
}

long chess_perft(const Board *b, int depth)
{
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(b, ms);
	if (depth <= 1) return depth == 1 ? n : 1;
	long total = 0;
	for (int i = 0; i < n; i++) {
		Board c = *b;
		chess_make(&c, ms[i]);
		total += chess_perft(&c, depth - 1);
	}
	return total;
}

static const char piece_chars[] = ".PNBRQKpnbrqk";

int chess_from_fen(Board *out, const char *fen)
{
	Board b;
	memset(&b, 0, sizeof b);
	const char *p = fen;
	int kings[2] = { 0, 0 };
	for (int r = 7; r >= 0; r--) {
		int f = 0;
		while (*p && *p != '/' && *p != ' ') {
			if (*p >= '1' && *p <= '8') {
				f += *p - '0';
			} else {
				const char *c = strchr(piece_chars + 1, *p);
				if (!c || f > 7) return -1;
				int piece = (int) (c - piece_chars);
				if ((piece == WP || piece == BP) && (r == 0 || r == 7)) return -1;
				if (piece == WK) kings[0]++;
				if (piece == BK) kings[1]++;
				b.sq[r * 8 + f++] = (unsigned char) piece;
			}
			if (f > 8) return -1;
			p++;
		}
		if (f != 8) return -1;
		if (r > 0) {
			if (*p != '/') return -1;
			p++;
		}
	}
	if (kings[0] != 1 || kings[1] != 1 || *p != ' ') return -1;
	p++;
	if (*p == 'w') b.side = 0;
	else if (*p == 'b') b.side = 1;
	else return -1;
	p++;
	if (*p++ != ' ') return -1;
	if (*p == '-') {
		p++;
	} else {
		for (; *p && *p != ' '; p++) {
			const char *c = strchr("KQkq", *p);
			if (!c) return -1;
			b.castling |= 1 << (c - "KQkq");
		}
	}
	if (*p++ != ' ') return -1;
	b.ep = -1;
	if (*p == '-') {
		p++;
	} else {
		if (p[0] < 'a' || p[0] > 'h' || (p[1] != '3' && p[1] != '6')) return -1;
		b.ep = (p[0] - 'a') + 8 * (p[1] - '1');
		p += 2;
	}
	b.halfmove = 0;
	b.fullmove = 1;
	if (*p == ' ') {
		char *end;
		long h = strtol(p + 1, &end, 10), fm = h;
		if (end == p + 1 || h < 0 || h > 1000) return -1;
		p = end;
		if (*p == ' ') {
			fm = strtol(p + 1, &end, 10);
			if (end == p + 1 || fm < 1 || fm > 10000) return -1;
			p = end;
		} else {
			fm = 1;
		}
		b.halfmove = (int) h;
		b.fullmove = (int) fm;
	}
	if (*p != '\0') return -1;
	if (chess_in_check(&b, !b.side)) return -1; /* side not to move cannot be in check */
	*out = b;
	return 0;
}

void chess_to_fen(const Board *b, char *out, int cap)
{
	char s[100];
	int o = 0;
	for (int r = 7; r >= 0; r--) {
		int empty = 0;
		for (int f = 0; f < 8; f++) {
			int p = b->sq[r * 8 + f];
			if (p == EMPTY) {
				empty++;
				continue;
			}
			if (empty) s[o++] = (char) ('0' + empty);
			empty = 0;
			s[o++] = piece_chars[p];
		}
		if (empty) s[o++] = (char) ('0' + empty);
		if (r) s[o++] = '/';
	}
	s[o++] = ' ';
	s[o++] = b->side ? 'b' : 'w';
	s[o++] = ' ';
	if (!b->castling) s[o++] = '-';
	for (int i = 0; i < 4; i++)
		if (b->castling & (1 << i)) s[o++] = "KQkq"[i];
	s[o++] = ' ';
	if (b->ep < 0) {
		s[o++] = '-';
	} else {
		s[o++] = (char) ('a' + FILE_OF(b->ep));
		s[o++] = (char) ('1' + RANK_OF(b->ep));
	}
	s[o] = '\0';
	snprintf(out, (size_t) cap, "%s %d %d", s, b->halfmove, b->fullmove);
}

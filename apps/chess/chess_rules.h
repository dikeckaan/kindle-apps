/* Chess rules (pure): board, FEN, legal move generation, make move, check detection, perft. */
#ifndef CHESS_RULES_H
#define CHESS_RULES_H

typedef enum { EMPTY = 0, WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK } Piece;

#define MV_CAPTURE 1
#define MV_EP 2
#define MV_CASTLE 4
#define MV_DOUBLE 8
/* squares 0 = a1 … 63 = h8; promo is the white piece type (WN..WQ) regardless of colour, 0 if none */
typedef struct { unsigned char from, to, promo, flags; } Move;

typedef struct {
	unsigned char sq[64];
	int side;     /* 0 white, 1 black to move */
	int castling; /* bit0 K, bit1 Q, bit2 k, bit3 q */
	int ep;       /* en-passant target square or -1 */
	int halfmove, fullmove;
} Board;

#define CHESS_START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define CHESS_MAX_MOVES 256

int  chess_from_fen(Board *b, const char *fen); /* 0 ok, -1 invalid (b untouched) */
void chess_to_fen(const Board *b, char *out, int cap);
int  chess_legal_moves(const Board *b, Move *out); /* count */
void chess_make(Board *b, Move m);                 /* m must be legal for b */
int  chess_in_check(const Board *b, int side);
int  chess_piece_color(int p); /* 0 white, 1 black, -1 empty */
long chess_perft(const Board *b, int depth);
#endif

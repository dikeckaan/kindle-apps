/* Türk daması (Turkish draughts), pure logic: rules, game record with undo/save, alpha-beta AI.
 *
 * Rules as played in Turkey (Türkiye Dama Federasyonu; en/tr Wikipedia "Turkish draughts" / "Türk daması"):
 * - 8x8 board, every square used. 16 men each, on the 2nd and 3rd rows from each player; the back rows stay empty.
 *   White (side 0, bottom) moves first.
 * - A man (taş) steps one square forward or sideways, never backward or diagonally.
 * - A king (dama) moves any number of empty squares orthogonally (like a rook), in all four directions.
 * - Capturing is mandatory, and the sequence that captures the most pieces must be chosen (men and kings count the
 *   same; among equal sequences the player chooses). A man jumps an adjacent enemy forward or sideways to the empty
 *   square just beyond. A king jumps an enemy at any distance along a line (only empty squares between) and lands on
 *   any empty square beyond it.
 * - Captured pieces are removed immediately, one by one, so a square may be crossed again in the same sequence;
 *   a king may not reverse direction (180°) between two captures.
 * - A man that reaches the far row becomes a king at the end of its move. If it gets there during a capture and can
 *   still capture, it carries on as a man (sideways) and is crowned when the move ends.
 * - A player with no legal move (no pieces, or all blocked) loses.
 * - Draw: both sides are down to one piece each (so king vs a lone man is a draw, "gayyım").
 *   House rule of this app: also a draw after 100 plies (50 moves each) with neither a capture nor a man move.
 *
 * Squares: 0..63 = row * 8 + col, row 0 at the top (black's back row); white men move up (towards row 0). */
#ifndef MASA_DAMA_H
#define MASA_DAMA_H
#include "mcommon.h"

enum { DM_EMPTY, DM_WMAN, DM_WKING, DM_BMAN, DM_BKING };
#define DM_MAX_PATH 20
#define DM_MAX_MOVES 256
#define DM_MAX_PLIES 600
#define DM_QUIET_DRAW 100

typedef struct {
	unsigned char b[64];
	int side;  /* 0 white, 1 black */
	int quiet; /* plies since the last capture or man move */
} Dm;

typedef struct {
	unsigned char from, n, ncap;
	unsigned char path[DM_MAX_PATH]; /* landing squares; the last is the destination */
	unsigned char cap[DM_MAX_PATH];  /* captured squares, in order */
} DmMove;

typedef enum { DM_ONGOING, DM_WHITE_WINS, DM_BLACK_WINS, DM_DRAW } DmResult;

void dm_init(Dm *d);
void dm_clear(Dm *d, int side); /* empty board (tests, positions) */
int  dm_color(int piece);       /* 0 white, 1 black, -1 empty */
int  dm_is_king(int piece);
int  dm_moves(const Dm *d, DmMove *out, int max); /* all legal moves (already filtered to the maximum capture) */
void dm_apply(Dm *d, const DmMove *m);
DmResult dm_result(const Dm *d);
int  dm_eval(const Dm *d);      /* heuristic for the side to move */
/* best move for the side to move; -1 when there is none */
int  dm_ai(const Dm *d, const MasaLimits *lim, DmMove *best, MasaStats *st);

/* lessons: board rows 'w'/'W' white man/king, 'b'/'B' black man/king; moves as "d3-d4" or, capturing,
 * "d3xd5xf5" (start square, then every landing square; files a–h, rank 1 at the bottom = white's side) */
void dm_sq_name(int sq, char out[3]);
int  dm_parse_move(const Dm *d, const char *s, DmMove *out); /* 0 when s names a legal move */
void dm_from_rows(Dm *d, const char *const *rows, int side);
extern const MasaPage dm_pages[];
extern const int dm_npages;
extern const MasaStep dm_demo[]; /* from the start position */
extern const int dm_ndemo;

typedef struct {
	Dm pos;
	DmMove hist[DM_MAX_PLIES];
	int n;
	int mode, level, human;
} DmGame;

void dm_game_new(DmGame *g, int mode, int level, int human);
int  dm_game_play(DmGame *g, const DmMove *m); /* 0 ok, -1 not legal */
int  dm_game_undo(DmGame *g, int plies);
int  dm_game_save(const DmGame *g, char *buf, int cap);
int  dm_game_load(DmGame *g, const char *buf);
#endif

/* Reversi (Othello) 8x8, pure logic: bitboard rules, game record with undo/save, alpha-beta AI.
 * Squares: 0..63 = row * 8 + col, row 0 at the top. Black (side 0) moves first; the start position has
 * black on d5/e4 and white on e5/d4. A side without a legal move must pass (RV_PASS). */
#ifndef MASA_REVERSI_H
#define MASA_REVERSI_H
#include <stdint.h>
#include "mcommon.h"

#define RV_PASS 64
#define RV_MAX_PLIES 140

typedef struct {
	uint64_t disc[2]; /* 0 black, 1 white */
	int side;         /* to move */
} Rv;

void     rv_init(Rv *r);
uint64_t rv_moves(const Rv *r);              /* legal squares for the side to move */
uint64_t rv_flips(const Rv *r, int sq);      /* discs the move at sq turns (0 = illegal) */
int      rv_play(Rv *r, int sq);             /* sq or RV_PASS (only when there is no move); 0 ok, -1 illegal */
int      rv_over(const Rv *r);               /* neither side can move */
int      rv_count(const Rv *r, int side);
int      rv_at(const Rv *r, int sq);         /* -1 empty, 0 black, 1 white */
int      rv_eval(const Rv *r);               /* heuristic for the side to move */
/* best move for the side to move (RV_PASS when it has none) */
int      rv_ai(const Rv *r, const MasaLimits *lim, MasaStats *st);

typedef struct {
	Rv pos;
	unsigned char hist[RV_MAX_PLIES];
	int n;
	int mode;  /* 1 vs the Kindle, 2 two players */
	int level; /* 1..3 */
	int human; /* side the human plays in mode 1 */
} RvGame;

/* lessons: board rows 'B' black, 'W' white; squares as "d3" (file a–h, rank 1 at the bottom), "pas" = RV_PASS */
int  rv_parse_sq(const char *s);          /* -1 when not a square */
void rv_sq_name(int sq, char out[3]);
void rv_from_rows(Rv *r, const char *const *rows, int side);
extern const MasaPage rv_pages[];
extern const int rv_npages;
extern const MasaStep rv_demo[]; /* from the start position */
extern const int rv_ndemo;

void rv_game_new(RvGame *g, int mode, int level, int human);
int  rv_game_play(RvGame *g, int sq);  /* 0 ok */
int  rv_game_undo(RvGame *g, int plies); /* plies undone */
int  rv_game_save(const RvGame *g, char *buf, int cap);
int  rv_game_load(RvGame *g, const char *buf); /* 0 ok, -1 corrupt (g untouched) */
#endif

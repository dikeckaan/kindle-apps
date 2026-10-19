/* Go 9x9 / 13x13, pure logic: rules, area scoring, game record with undo/save, Monte Carlo (UCT + RAVE) AI.
 *
 * Rules (Chinese, area scoring):
 * - Black moves first; komi 7.5 points for White.
 * - A stone (or chain) with no liberties is captured. Suicide is not allowed.
 * - Ko: positional superko for the game (a move may not recreate any earlier board position); the AI's own
 *   look-ahead uses the simple ko rule only.
 * - Two passes in a row end the game; then dead stones are agreed on (the app proposes them from playouts and
 *   either player can toggle a chain) and the score is stones on the board + empty points surrounded only by one
 *   colour. Resigning ends the game at once.
 *
 * Points are padded indices: p = (row + 1) * W + col + 1, W = n + 2, so neighbours are p ± 1 and p ± W and the
 * border holds GO_EDGE. Use go_pt / go_row / go_col to convert. */
#ifndef MASA_GO_H
#define MASA_GO_H
#include <stdint.h>
#include "mcommon.h"

#define GO_MAXN 13
#define GO_MAXW (GO_MAXN + 2)
#define GO_MAXP (GO_MAXW * GO_MAXW)
#define GO_PASS 0     /* 0 is a border point, so it never clashes with a board point */
#define GO_RESIGN (-1)
#define GO_KOMI2 15   /* komi × 2 (7.5) */
enum { GO_EMPTY, GO_BLACK, GO_WHITE, GO_EDGE };

typedef struct {
	int n, W;
	unsigned char c[GO_MAXP];
	short head[GO_MAXP], next[GO_MAXP], libs[GO_MAXP], size[GO_MAXP]; /* chains; libs (pseudo) and size by head */
	int ko;      /* point forbidden by simple ko, 0 = none */
	int side;    /* GO_BLACK or GO_WHITE to move */
	int passes;  /* consecutive passes */
	int last;    /* last move: a point, GO_PASS, or -2 at the start */
	int caps[3]; /* stones captured by each colour */
	uint64_t hash;
} Go;

static inline int go_pt(const Go *g, int row, int col) { return (row + 1) * g->W + col + 1; }
static inline int go_row(const Go *g, int p) { return p / g->W - 1; }
static inline int go_col(const Go *g, int p) { return p % g->W - 1; }

void go_init(Go *g, int n);
int  go_legal(const Go *g, int p);   /* simple-ko legality of a stone at p for the side to move */
int  go_play(Go *g, int p);          /* p or GO_PASS; returns stones captured, -1 if illegal */
int  go_libs(const Go *g, int p);    /* real liberties of the chain at p (0 for empty points) */
int  go_is_eye(const Go *g, int p, int color);
/* area score with the chains marked in dead[] (indexed by point, may be NULL) taken off: black and white points,
 * and optionally each point's owner (GO_BLACK/GO_WHITE/GO_EMPTY for dame) */
void go_area(const Go *g, const unsigned char *dead, int *black, int *white, unsigned char *owner);
/* black minus white minus komi, ×2 (so 7.5 komi stays integral); > 0 Black wins */
int  go_score2(const Go *g, const unsigned char *dead);
/* proposes dead chains (dead[p] = 1 for each of their points) from `playouts` random playouts */
void go_estimate_dead(const Go *g, int playouts, uint32_t seed, unsigned char *dead);
void go_toggle_dead(const Go *g, int p, unsigned char *dead); /* flips the whole chain at p */
/* best move for the side to move: a point, GO_PASS or GO_RESIGN */
int  go_ai(const Go *g, const MasaLimits *lim, MasaStats *st);
int  go_ai_winrate(void); /* the last go_ai's estimated winning chance, per cent */

#define GO_MAX_PLIES 600
typedef struct {
	Go pos;
	short hist[GO_MAX_PLIES];
	uint64_t hashes[GO_MAX_PLIES + 1]; /* board hash before each move, and after the last */
	int n;
	int size, mode, level, human; /* human: GO_BLACK or GO_WHITE */
	int resigned;                 /* colour that resigned, 0 = none */
} GoGame;

void go_game_new(GoGame *g, int size, int mode, int level, int human);
int  go_game_legal(const GoGame *g, int p); /* with positional superko */
int  go_game_play(GoGame *g, int p);        /* point, GO_PASS or GO_RESIGN; 0 ok */
int  go_game_over(const GoGame *g);         /* two passes or a resignation */
int  go_game_undo(GoGame *g, int plies);
int  go_game_ai(const GoGame *g, const MasaLimits *lim, MasaStats *st); /* go_ai that also respects superko */
int  go_game_save(const GoGame *g, char *buf, int cap);
int  go_game_load(GoGame *g, const char *buf);
#endif

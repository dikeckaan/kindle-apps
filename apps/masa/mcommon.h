/* Masa Oyunları: small helpers shared by the pure game modules (clock, random numbers, search limits). */
#ifndef MASA_MCOMMON_H
#define MASA_MCOMMON_H
#include <stdint.h>
#include <time.h>

static inline long masa_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* xorshift32; state must not be 0 */
static inline uint32_t masa_rand(uint32_t *s)
{
	uint32_t x = *s ? *s : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *s = x;
}

/* what an AI may spend: time_ms 0 = no time limit; max_depth / max_playouts 0 = the level's default */
typedef struct {
	int level;        /* 1 Kolay, 2 Orta, 3 Zor */
	int time_ms;
	int max_depth;    /* alpha-beta games */
	int max_playouts; /* Go */
	uint32_t seed;
} MasaLimits;

typedef struct {
	long nodes;   /* positions searched (or playouts for Go) */
	int depth;    /* deepest completed iteration */
	long ms;      /* time spent */
	int score;    /* best score from the mover's point of view (game specific scale) */
} MasaStats;

/* ---- lessons: the "Nasıl oynanır?" pages and the scripted "Örnek oyun" moves of each game. Pure data next to the
 * rules, so tests/host/test_masa.c checks every picture and move with the rules engine; the UI shows the texts
 * with T() (they are marked with N_ for tools/i18n/extract.py). ---- */
#ifndef N_
#define N_(s) (s)
#endif

/* A page is a board picture plus a few sentences. board: rows from the top, one char per cell ('.' empty; pieces
 * as each game's *_from_rows reads them). marks: the same shape (or NULL = none), one of
 *   '*' thick frame (the piece to tap / selected)   '@' frame and dot (tap this empty point)
 *   'o' dot (a square you may move to)               'n' the move just played
 *   'c' highlighted (just turned / changed)          'x' cross (taken from here / not allowed)
 *   '#' must-capture frame (dama)                    'v' ring (a square passed on the way, dama)
 *   'l' small ring (a liberty, Go)                   't' / 'u' black / white area (Go)  */
#define MASA_MAX_ROWS 9
typedef struct {
	const char *head; /* short title */
	const char *text; /* 2–4 short sentences */
	int side;         /* side to move in the picture, in the game's own numbering */
	const char *board[MASA_MAX_ROWS];
	const char *marks[MASA_MAX_ROWS];
} MasaPage;

/* one scripted move of a demo game (notation of the game, "pas" for a pass) and what the caption says about it */
typedef struct {
	const char *move;
	const char *caption;
} MasaStep;

#endif

/* Lichess puzzles (pure): API response parsing, the solving state machine, themes, and the solved history. */
#ifndef CHESS_PUZZLE_H
#define CHESS_PUZZLE_H
#include "chess_rules.h"

#define PZ_URL_DAILY "https://lichess.org/api/puzzle/daily"
#define PZ_URL_NEXT  "https://lichess.org/api/puzzle/next" /* a random puzzle; works without a token */
#define PZ_URL_ID    "https://lichess.org/api/puzzle/%s"
#define PZ_MAX_SOL 32
#define PZ_MAX_THEMES 12

typedef struct {
	char id[16], game_id[16];
	int rating, plays, initial_ply;
	char themes[PZ_MAX_THEMES][32];
	int n_themes;
	Move sol[PZ_MAX_SOL]; /* solution[0] is the player's move */
	int n_sol;
	Board start;          /* after the game's first initialPly + 1 plies */
	Move last;            /* the opponent's move that led to the puzzle */
	int has_last;
} Puzzle;

/* Lichess puzzle API JSON (daily, next, by id): replays game.pgn to the puzzle position and checks the solution; 0 ok, -1 bad */
int  pz_parse(const char *json, Puzzle *p);
/* puzzle from a position and UCI solution (no themes); 0 ok, -1 when a move is illegal */
int  pz_setup(Puzzle *p, const Board *start, const char **uci, int n);
int  pz_valid_id(const char *id); /* letters and digits only (used in file names and URLs) */
const char *pz_theme_name(const char *key); /* Turkish name (translate with T()), NULL = not shown */
/* known themes joined with " · ", each passed through tr (NULL = Turkish); stops before one that does not fit */
void pz_themes_text(const Puzzle *p, const char *(*tr)(const char *), char *out, int cap);

/* ---- solving ---- */
typedef enum { PZ_ILLEGAL = -1, PZ_WRONG, PZ_GOOD, PZ_WIN } PzVerdict;
typedef struct {
	Puzzle pz;
	Board pos;
	int step;     /* index of the next solution move */
	int player;   /* side the user plays */
	int mistakes, shown;
} PzPlay;

void pz_play_start(PzPlay *s, const Puzzle *p);
void pz_play_restart(PzPlay *s); /* back to the start; mistakes and shown are kept */
/* the user's move: PZ_GOOD (made; the reply is pending), PZ_WIN (made; solved — any mating move wins),
 * PZ_WRONG (legal but not the solution: not made, mistakes++), PZ_ILLEGAL (or the puzzle is over) */
PzVerdict pz_play_try(PzPlay *s, Move m);
int  pz_play_expects_reply(const PzPlay *s); /* 1 when the opponent's solution move is next */
int  pz_play_reply(PzPlay *s, Move *out);    /* makes that move; 1 if made */
int  pz_play_hint(const PzPlay *s);          /* from-square of the user's next move, -1 if none */
int  pz_play_needs_promo(const PzPlay *s, int from, int to); /* a pawn move to the last rank */
/* "show solution": makes the next solution move of either side; 1 if made, 0 when nothing is left */
int  pz_play_show_next(PzPlay *s, Move *out);
int  pz_play_done(const PzPlay *s);
char pz_play_result(const PzPlay *s); /* 'S' solved cleanly, 'F' solved after a mistake, 'X' solution shown */

/* ---- history: "v1\n" then one "id yyyymmdd result rating daily" line per puzzle, oldest first ---- */
typedef struct {
	char id[16];
	int day;  /* yyyymmdd */
	char res; /* 'S', 'F' or 'X' */
	int rating, daily;
} PzHist;
typedef struct { int total, solved, streak, best; } PzStats; /* streak: latest run of clean solves ('S') */
#define PZ_HIST_MAX 500

int  pz_hist_parse(const char *buf, PzHist *h, int max); /* entries read; malformed lines are skipped */
int  pz_hist_format(const PzHist *h, int n, char *buf, int cap); /* bytes, -1 if it does not fit */
int  pz_hist_find(const PzHist *h, int n, const char *id);
/* records e unless its id is already there (the first try counts); drops the oldest when full; new count */
int  pz_hist_add(PzHist *h, int n, int max, PzHist e);
void pz_hist_stats(const PzHist *h, int n, PzStats *st);
#endif

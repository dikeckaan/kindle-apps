/* Chess game state (pure): history, results, UCI move text, undo, save/load. */
#ifndef CHESS_GAME_H
#define CHESS_GAME_H
#include "chess_rules.h"

typedef enum { RES_ONGOING, RES_MATE, RES_STALEMATE, RES_FIFTY, RES_REPETITION, RES_MATERIAL } Result;
#define GAME_MAX_PLIES 600

typedef struct {
	char start_fen[100];
	Board pos;
	Move hist[GAME_MAX_PLIES];
	int n;
	int mode;       /* 1 single player, 2 two players */
	int level;      /* 1..6 */
	int human_side; /* 0 white, 1 black */
	int flip;       /* two players: turn the board after every move */
} Game;

void   game_new(Game *g, int mode, int level, int human_side);
int    game_play(Game *g, Move m);              /* -1 if illegal or history full */
int    game_play_uci(Game *g, const char *uci); /* "e2e4", "e7e8q" */
void   move_to_uci(Move m, char out[6]);
int    game_undo(Game *g, int plies);           /* plies actually undone */
Result game_result(const Game *g);
void   game_uci_position(const Game *g, char *out, int cap);
int    game_save(const Game *g, char *buf, int cap);
int    game_load(Game *g, const char *buf); /* 0 ok, -1 corrupt (g untouched) */
const char *result_text(Result r, int loser_side);
#endif

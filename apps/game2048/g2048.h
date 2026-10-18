/* 2048 game logic (pure, no I/O). */
#ifndef G2048_H
#define G2048_H
#include "kui.h"

typedef struct {
	int cell[4][4]; /* [row][col], 0 = empty */
	int score, best;
	int prev[4][4];
	int prev_score;
	int can_undo;
	unsigned rng;
} G2048;

void g2048_new(G2048 *g, unsigned seed); /* two random tiles; best is kept */
int  g2048_move(G2048 *g, KuiDir d);     /* 1 if anything moved (a tile is then spawned) */
int  g2048_can_move(const G2048 *g);
int  g2048_max_tile(const G2048 *g);
int  g2048_undo(G2048 *g);                           /* one step; 1 on success */
int  g2048_save(const G2048 *g, char *buf, int cap); /* bytes written, "v1" header */
int  g2048_load(G2048 *g, const char *buf);          /* 0 ok, -1 corrupt (g untouched) */
#endif

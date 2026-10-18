#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "g2048.h"

static unsigned next_rand(G2048 *g) /* xorshift32 */
{
	unsigned x = g->rng ? g->rng : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return g->rng = x;
}

static void spawn(G2048 *g)
{
	int empty[16], n = 0;
	for (int i = 0; i < 16; i++)
		if (!g->cell[i / 4][i % 4]) empty[n++] = i;
	if (!n) return;
	int at = empty[next_rand(g) % (unsigned) n];
	g->cell[at / 4][at % 4] = next_rand(g) % 10 == 0 ? 4 : 2;
}

void g2048_new(G2048 *g, unsigned seed)
{
	int best = g->best;
	memset(g, 0, sizeof *g);
	g->best = best;
	g->rng = seed;
	spawn(g);
	spawn(g);
}

/* cell pointer for position i (0..3) along line l, walking in direction d (i = 0 is the destination edge) */
static int *at(G2048 *g, KuiDir d, int l, int i)
{
	switch (d) {
	case KUI_DIR_LEFT: return &g->cell[l][i];
	case KUI_DIR_RIGHT: return &g->cell[l][3 - i];
	case KUI_DIR_UP: return &g->cell[i][l];
	default: return &g->cell[3 - i][l];
	}
}

int g2048_move(G2048 *g, KuiDir d)
{
	if (d == KUI_DIR_NONE) return 0;
	int saved[4][4], saved_score = g->score, moved = 0;
	memcpy(saved, g->cell, sizeof saved);
	for (int l = 0; l < 4; l++) {
		int line[4] = { 0 }, n = 0, out[4] = { 0 }, o = 0;
		for (int i = 0; i < 4; i++)
			if (*at(g, d, l, i)) line[n++] = *at(g, d, l, i);
		for (int i = 0; i < n; i++) {
			if (i + 1 < n && line[i] == line[i + 1]) {
				out[o++] = line[i] * 2;
				g->score += line[i] * 2;
				i++;
			} else {
				out[o++] = line[i];
			}
		}
		for (int i = 0; i < 4; i++) {
			if (*at(g, d, l, i) != out[i]) moved = 1;
			*at(g, d, l, i) = out[i];
		}
	}
	if (!moved) return 0;
	memcpy(g->prev, saved, sizeof saved);
	g->prev_score = saved_score;
	g->can_undo = 1;
	if (g->score > g->best) g->best = g->score;
	spawn(g);
	return 1;
}

int g2048_can_move(const G2048 *g)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			int v = g->cell[r][c];
			if (!v) return 1;
			if (c < 3 && g->cell[r][c + 1] == v) return 1;
			if (r < 3 && g->cell[r + 1][c] == v) return 1;
		}
	return 0;
}

int g2048_max_tile(const G2048 *g)
{
	int m = 0;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (g->cell[r][c] > m) m = g->cell[r][c];
	return m;
}

int g2048_undo(G2048 *g)
{
	if (!g->can_undo) return 0;
	memcpy(g->cell, g->prev, sizeof g->cell);
	g->score = g->prev_score;
	g->can_undo = 0;
	return 1;
}

int g2048_save(const G2048 *g, char *buf, int cap)
{
	int n = snprintf(buf, (size_t) cap, "v1\n");
	for (int i = 0; i < 16 && n < cap; i++)
		n += snprintf(buf + n, (size_t) (cap - n), i < 15 ? "%d " : "%d\n", g->cell[i / 4][i % 4]);
	if (n < cap) n += snprintf(buf + n, (size_t) (cap - n), "%d %d %u\n", g->score, g->best, g->rng);
	return n < cap ? n : -1;
}

static int valid_tile(long v)
{
	if (v == 0) return 1;
	if (v < 2 || v > (1 << 17)) return 0;
	return (v & (v - 1)) == 0;
}

int g2048_load(G2048 *g, const char *buf)
{
	if (strncmp(buf, "v1\n", 3) != 0) return -1;
	const char *p = buf + 3;
	char *end;
	G2048 t;
	memset(&t, 0, sizeof t);
	for (int i = 0; i < 16; i++) {
		long v = strtol(p, &end, 10);
		if (end == p || !valid_tile(v)) return -1;
		t.cell[i / 4][i % 4] = (int) v;
		p = end;
	}
	long score = strtol(p, &end, 10);
	if (end == p || score < 0) return -1;
	p = end;
	long best = strtol(p, &end, 10);
	if (end == p || best < 0) return -1;
	p = end;
	unsigned long rng = strtoul(p, &end, 10);
	if (end == p) return -1;
	t.score = (int) score;
	t.best = (int) (best > score ? best : score);
	t.rng = (unsigned) rng;
	*g = t;
	return 0;
}

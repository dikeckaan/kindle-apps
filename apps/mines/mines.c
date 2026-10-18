/* Minesweeper rules (pure, no I/O). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mines.h"

static const struct { const char *name; int w, h, mines; } levels[MS_LEVELS] = {
	{ "Kolay", 9, 9, 10 },
	{ "Orta", 12, 16, 30 },
	{ "Zor", 14, 20, 50 },
};

int ms_level_info(int level, int *w, int *h, int *mines)
{
	if (level < 0 || level >= MS_LEVELS) return -1;
	*w = levels[level].w;
	*h = levels[level].h;
	*mines = levels[level].mines;
	return 0;
}

const char *ms_level_name(int level) { return level >= 0 && level < MS_LEVELS ? levels[level].name : ""; }

void ms_new(Mines *g, int level, unsigned seed)
{
	if (level < 0 || level >= MS_LEVELS) level = 0;
	memset(g, 0, sizeof *g);
	g->level = level;
	ms_level_info(level, &g->w, &g->h, &g->mines);
	g->status = MS_PLAYING;
	g->boom_r = g->boom_c = -1;
	g->t_start = -1;
	g->rng = seed ? seed : 0x9E3779B9u;
}

static unsigned next_rand(Mines *g)
{
	/* xorshift32 */
	unsigned x = g->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g->rng = x ? x : 0x9E3779B9u;
	return g->rng;
}

static int inside(const Mines *g, int r, int c) { return r >= 0 && c >= 0 && r < g->h && c < g->w; }

/* lays the mines anywhere except the 3x3 block around (r0, c0) */
static void place_mines(Mines *g, int r0, int c0)
{
	int cand[MS_MAX_W * MS_MAX_H], n = 0;
	for (int r = 0; r < g->h; r++)
		for (int c = 0; c < g->w; c++)
			if (abs(r - r0) > 1 || abs(c - c0) > 1) cand[n++] = r * g->w + c;
	int m = g->mines < n ? g->mines : n;
	for (int i = 0; i < m; i++) { /* partial Fisher-Yates */
		int j = i + (int) (next_rand(g) % (unsigned) (n - i));
		int t = cand[i];
		cand[i] = cand[j];
		cand[j] = t;
		g->mine[cand[i] / g->w][cand[i] % g->w] = 1;
	}
	g->mines = m;
	g->placed = 1;
}

int ms_neighbours(const Mines *g, int r, int c)
{
	int n = 0;
	for (int dr = -1; dr <= 1; dr++)
		for (int dc = -1; dc <= 1; dc++)
			if ((dr || dc) && inside(g, r + dr, c + dc)) n += g->mine[r + dr][c + dc];
	return n;
}

static int flags_around(const Mines *g, int r, int c)
{
	int n = 0;
	for (int dr = -1; dr <= 1; dr++)
		for (int dc = -1; dc <= 1; dc++)
			if ((dr || dc) && inside(g, r + dr, c + dc)) n += g->cell[r + dr][c + dc] == MS_FLAGGED;
	return n;
}

static void check_win(Mines *g)
{
	if (g->status != MS_PLAYING || g->opened != g->w * g->h - g->mines) return;
	g->status = MS_WON;
	for (int r = 0; r < g->h; r++)
		for (int c = 0; c < g->w; c++)
			if (g->mine[r][c] && g->cell[r][c] != MS_FLAGGED) {
				g->cell[r][c] = MS_FLAGGED;
				g->flags++;
			}
}

/* opens one closed cell; zeros flood iteratively with an explicit stack */
static void open_cell(Mines *g, int r0, int c0)
{
	if (g->cell[r0][c0] != MS_CLOSED || g->status != MS_PLAYING) return;
	if (g->mine[r0][c0]) {
		g->cell[r0][c0] = MS_OPEN;
		g->status = MS_LOST;
		g->boom_r = r0;
		g->boom_c = c0;
		return;
	}
	int stack[MS_MAX_W * MS_MAX_H], sp = 0;
	g->cell[r0][c0] = MS_OPEN;
	g->opened++;
	stack[sp++] = r0 * g->w + c0;
	while (sp > 0) {
		int at = stack[--sp], r = at / g->w, c = at % g->w;
		if (ms_neighbours(g, r, c)) continue;
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = r + dr, cc = c + dc;
				if (!inside(g, rr, cc) || g->cell[rr][cc] != MS_CLOSED || g->mine[rr][cc]) continue;
				g->cell[rr][cc] = MS_OPEN; /* marked before pushing: each cell is pushed at most once */
				g->opened++;
				stack[sp++] = rr * g->w + cc;
			}
	}
}

int ms_open(Mines *g, int r, int c)
{
	if (!inside(g, r, c) || g->status != MS_PLAYING) return 0;
	if (g->cell[r][c] == MS_FLAGGED) return 0;
	if (g->cell[r][c] == MS_CLOSED) {
		if (!g->placed) place_mines(g, r, c);
		open_cell(g, r, c);
		check_win(g);
		return 1;
	}
	/* chord on an opened number */
	int n = ms_neighbours(g, r, c);
	if (n == 0 || flags_around(g, r, c) != n) return 0;
	int before = g->opened;
	for (int dr = -1; dr <= 1; dr++)
		for (int dc = -1; dc <= 1; dc++) {
			int rr = r + dr, cc = c + dc;
			if (inside(g, rr, cc)) open_cell(g, rr, cc); /* a mine behind a wrong flag ends the game */
		}
	check_win(g);
	return g->opened != before || g->status != MS_PLAYING;
}

int ms_toggle_flag(Mines *g, int r, int c)
{
	if (!inside(g, r, c) || g->status != MS_PLAYING) return 0;
	if (g->cell[r][c] == MS_CLOSED) {
		g->cell[r][c] = MS_FLAGGED;
		g->flags++;
		return 1;
	}
	if (g->cell[r][c] == MS_FLAGGED) {
		g->cell[r][c] = MS_CLOSED;
		g->flags--;
		return 1;
	}
	return 0;
}

int ms_view(const Mines *g, int r, int c)
{
	if (!inside(g, r, c)) return MS_V_CLOSED;
	int lost = g->status == MS_LOST;
	switch (g->cell[r][c]) {
	case MS_FLAGGED: return lost && !g->mine[r][c] ? MS_V_WRONG_FLAG : MS_V_FLAG;
	case MS_OPEN:
		if (g->mine[r][c]) return MS_V_BOOM;
		return ms_neighbours(g, r, c);
	default: return lost && g->mine[r][c] ? MS_V_MINE : MS_V_CLOSED;
	}
}

int ms_mines_left(const Mines *g) { return g->mines - g->flags; }

void ms_tick(Mines *g, long now)
{
	if (!g->placed || g->status != MS_PLAYING) {
		g->t_start = -1;
		return;
	}
	if (g->t_start >= 0 && now > g->t_start) g->secs += now - g->t_start;
	g->t_start = now;
}

long ms_seconds(const Mines *g) { return g->secs; }

/* ---- save format ----
 * mines v1
 * <level> <w> <h> <mines>
 * <status> <placed> <secs> <rng>
 * h rows of w chars: '.' closed, '*' closed mine, 'f' flag, 'F' flag on a mine, 'o' open, 'x' open mine (boom) */

int ms_save(const Mines *g, char *buf, int cap)
{
	int n = snprintf(buf, (size_t) cap, "mines v1\n%d %d %d %d\n%d %d %ld %u\n", g->level, g->w, g->h, g->mines,
			 (int) g->status, g->placed, g->secs, g->rng);
	if (n < 0 || n + g->h * (g->w + 1) + 1 > cap) return -1;
	for (int r = 0; r < g->h; r++) {
		for (int c = 0; c < g->w; c++) {
			int m = g->mine[r][c];
			char ch = g->cell[r][c] == MS_OPEN ? (m ? 'x' : 'o') : g->cell[r][c] == MS_FLAGGED ? (m ? 'F' : 'f') : (m ? '*' : '.');
			buf[n++] = ch;
		}
		buf[n++] = '\n';
	}
	buf[n] = '\0';
	return n;
}

int ms_load(Mines *g, const char *buf)
{
	if (strncmp(buf, "mines v1\n", 9) != 0) return -1;
	int level, w, h, mines, status, placed, used = 0;
	long secs;
	unsigned rng;
	if (sscanf(buf + 9, "%d %d %d %d %d %d %ld %u%n", &level, &w, &h, &mines, &status, &placed, &secs, &rng, &used) != 8)
		return -1;
	Mines t;
	ms_new(&t, level, rng);
	if (t.level != level || w != t.w || h != t.h || mines != t.mines) return -1;
	if (status < MS_PLAYING || status > MS_LOST || (placed != 0 && placed != 1) || secs < 0) return -1;
	t.status = (MsStatus) status;
	t.placed = placed;
	t.secs = secs;
	t.rng = rng;
	const char *p = buf + 9 + used;
	if (*p++ != '\n') return -1;
	int nmines = 0, booms = 0;
	for (int r = 0; r < h; r++) {
		for (int c = 0; c < w; c++, p++) {
			const char *pos = *p ? strchr(".*fFox", *p) : NULL;
			if (!pos) return -1;
			int k = (int) (pos - ".*fFox");
			t.mine[r][c] = (unsigned char) (k & 1);
			t.cell[r][c] = (unsigned char) (k < 2 ? MS_CLOSED : k < 4 ? MS_FLAGGED : MS_OPEN);
			nmines += k & 1;
			if (k == 3 || k == 2) t.flags++;
			if (k == 4) t.opened++;
			if (k == 5) {
				booms++;
				t.boom_r = r;
				t.boom_c = c;
			}
		}
		if (*p++ != '\n') return -1;
	}
	if (nmines != (placed ? mines : 0)) return -1;
	if (!placed && (t.opened || booms)) return -1;
	if (booms != (status == MS_LOST)) return -1;
	*g = t;
	return 0;
}

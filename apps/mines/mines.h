/* Minesweeper rules (pure, no I/O). Randomness comes from the seed given to ms_new. */
#ifndef MINES_H
#define MINES_H

#define MS_MAX_W 14
#define MS_MAX_H 20
#define MS_LEVELS 3

typedef enum { MS_PLAYING, MS_WON, MS_LOST } MsStatus;
typedef enum { MS_CLOSED, MS_OPEN, MS_FLAGGED } MsCell;

/* What a cell shows (ms_view): 0..8 = opened with that many neighbouring mines */
enum { MS_V_CLOSED = 10, MS_V_FLAG, MS_V_MINE, MS_V_BOOM, MS_V_WRONG_FLAG };

typedef struct {
	int level, w, h, mines;
	unsigned char mine[MS_MAX_H][MS_MAX_W];  /* 1 = mine */
	unsigned char cell[MS_MAX_H][MS_MAX_W];  /* MsCell */
	int placed;                              /* mines are laid on the first open */
	MsStatus status;
	int flags, opened;
	int boom_r, boom_c;                      /* the mine that ended the game, -1 if none */
	long secs;                               /* elapsed play time folded in by ms_tick */
	long t_start;                            /* last ms_tick time while running, -1 = not running */
	unsigned rng;
} Mines;

/* Level 0 Kolay 9x9/10, 1 Orta 12x16/30, 2 Zor 14x20/50 (cols x rows). 0 ok, -1 bad level. */
int  ms_level_info(int level, int *w, int *h, int *mines);
const char *ms_level_name(int level);
void ms_new(Mines *g, int level, unsigned seed); /* bad level → Kolay */

/* Tap on a cell: opens a closed cell (flood fill on 0), chords an opened number whose flag count matches,
 * ignores flags. Returns 1 if anything changed. */
int  ms_open(Mines *g, int r, int c);
int  ms_toggle_flag(Mines *g, int r, int c); /* closed <-> flagged; 1 if changed */
int  ms_neighbours(const Mines *g, int r, int c); /* mines around (r, c) */
int  ms_view(const Mines *g, int r, int c);       /* MS_V_* or 0..8 */
int  ms_mines_left(const Mines *g);               /* mines - flags (may go negative) */

/* Time: call with the current time (seconds) around every action; time runs only while mines are placed
 * and the game is in progress. */
void ms_tick(Mines *g, long now);
long ms_seconds(const Mines *g);

int  ms_save(const Mines *g, char *buf, int cap); /* bytes written or -1; "mines v1" header */
int  ms_load(Mines *g, const char *buf);          /* 0 ok, -1 corrupt (g untouched) */
#endif

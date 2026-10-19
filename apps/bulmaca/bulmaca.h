/* Bulmaca: Turkish crossword logic — puzzle file, grid model, clue numbering, word extraction, Turkish letter
 * folding, play state (fill, reveal, checks), saved progress, daily puzzle and statistics. No drawing here. */
#ifndef BULMACA_H
#define BULMACA_H

#define BM_MAX 13                 /* largest grid side */
#define BM_CELLS (BM_MAX * BM_MAX)
#define BM_MAX_WORDS 80
#define BM_CLUE 128               /* clue bytes (UTF-8) */
#define BM_SAVE_CAP 1024

enum { BM_ACROSS = 0, BM_DOWN = 1 };
enum { BM_REVEALED = 1, BM_WRONG = 2 }; /* cell flags; BM_WRONG is shown until the letter changes */

typedef struct {
	int dir, r, c, len, num;
	char clue[BM_CLUE];
} BmWord;

typedef struct {
	int id, w, h;
	int sol[BM_CELLS];             /* upper-case code point, 0 = black square */
	int num[BM_CELLS];             /* clue number shown in the cell, 0 = none */
	int word_at[2][BM_CELLS];      /* word through the cell across / down, -1 = none */
	BmWord words[BM_MAX_WORDS];    /* across words by number, then down words by number */
	int n_words, n_across;
} BmPuzzle;

typedef struct {
	char *buf;
	const char **p; /* start of each "P " block */
	int n;
} BmDb;

typedef struct {
	int id;
	int fill[BM_CELLS];            /* code point typed (upper case), 0 = empty */
	unsigned char flags[BM_CELLS];
	int seconds, done, helped;     /* helped: a letter was revealed */
} BmState;

typedef struct { int solved, clean, streak, best, last_day, total_sec, best_sec; } BmStats;

/* ---- puzzle file ---- */
int  bm_db_load(const char *path, BmDb *db); /* puzzles found, -1 if the file is missing */
void bm_db_free(BmDb *db);
int  bm_db_get(const BmDb *db, int index, BmPuzzle *p); /* 0 ok, -1 invalid */
/* One puzzle: "P id w h", h × "G <row>" ('#' black), then "A|D r c ANSWER\tclue" lines. Numbers the grid, extracts
 * its words and attaches each clue; -1 when a clue is missing, an answer does not match the grid or a run has no clue. */
int  bm_parse(const char *text, BmPuzzle *p);
/* numbering and words from p->sol (sets num, word_at, words without clues, n_words, n_across) */
void bm_number(BmPuzzle *p);
int  bm_word_cell(const BmPuzzle *p, int word, int i); /* cell index of the word's i-th letter */
void bm_word_answer(const BmPuzzle *p, int word, char *out, int cap); /* UTF-8 */

/* ---- letters ---- */
int  bm_fold(int cp); /* what a typed letter counts as: Turkish upper case (i→İ, ı→I), circumflex dropped (â→A, î→İ) */
int  bm_is_letter(int cp); /* one of the 29 letters of the Turkish alphabet (any case) */

/* ---- selection and cursor ---- */
/* the word to select when cell is tapped; same_tap: the cell was already selected (switches direction when it can) */
int  bm_select(const BmPuzzle *p, int cell, int dir, int same_tap, int *out_dir);
int  bm_word_pos(const BmPuzzle *p, int word, int cell); /* position of cell in the word, -1 if not in it */
int  bm_next_cell(const BmPuzzle *p, const BmState *s, int word, int cell); /* after typing: next empty cell, else next */
int  bm_prev_cell(const BmPuzzle *p, int word, int cell);  /* the cell before (or the same at the start) */
int  bm_first_empty(const BmPuzzle *p, const BmState *s, int word); /* the word's first empty cell, else its first */
int  bm_step_word(const BmPuzzle *p, int word, int delta); /* next / previous clue, wrapping */

/* ---- play ---- */
void bm_state_init(BmState *s, const BmPuzzle *p);
int  bm_type(const BmPuzzle *p, BmState *s, int cell, int cp); /* 1 if the cell changed (revealed cells are locked) */
int  bm_erase(const BmPuzzle *p, BmState *s, int cell);
int  bm_reveal(const BmPuzzle *p, BmState *s, int cell);      /* 1 if the cell changed */
int  bm_check_word(const BmPuzzle *p, BmState *s, int word, int *empty); /* wrong letters (now flagged) */
int  bm_check_all(const BmPuzzle *p, BmState *s, int *empty);
int  bm_solved(const BmPuzzle *p, const BmState *s);
int  bm_filled_pct(const BmPuzzle *p, const BmState *s);
int  bm_word_full(const BmPuzzle *p, const BmState *s, int word);

/* ---- saved progress: "bulmaca 1", "id N", "grid <hash of the solution>", "fill <letters, '-' empty, '#' black>", "rev <0/1…>", "sec", "done", "help" ---- */
unsigned bm_hash(const BmPuzzle *p);
int  bm_save_format(const BmPuzzle *p, const BmState *s, char *buf, int cap); /* bytes, -1 if it does not fit */
int  bm_save_parse(const char *buf, const BmPuzzle *p, BmState *s);          /* 0 ok, -1 garbage or another puzzle */

/* ---- days and the puzzle of the day ---- */
long bm_days(int y, int m, int d); /* days since 1970-01-01 */
void bm_civil(long days, int *y, int *m, int *d);
#define BM_EPOCH_Y 2026 /* puzzle 1 was the puzzle of 1 January 2026 */
int  bm_daily(long day, int n); /* index of the day's puzzle (0-based), cycling through all n */

/* ---- statistics ---- */
/* daily_day: the day whose puzzle of the day this was (keeps the streak), -1 for an archive puzzle */
void bm_stats_record(BmStats *st, long daily_day, int seconds, int clean);
int  bm_stats_format(const BmStats *st, char *buf, int cap);
int  bm_stats_parse(const char *buf, BmStats *st); /* missing keys stay 0 */
void bm_fmt_time(int seconds, char *out, int cap); /* "4:05", "1:02:09" */
#endif

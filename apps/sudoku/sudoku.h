/* Sudoku logic (pure, no I/O): solver, unique-solution generator, board with notes and undo. */
#ifndef SUDOKU_H
#define SUDOKU_H

#define SU_UNDO_MAX 64
#define SU_SAVE_CAP 4096 /* enough for su_save with a full undo stack */

enum { SU_EASY, SU_MEDIUM, SU_HARD, SU_LEVELS };

/* one undo step: the state of `cell` before the change */
typedef struct { unsigned char cell, value; unsigned short notes; } SuUndo;

typedef struct {
	unsigned char solution[81];
	unsigned char given[81];  /* 0 = empty, else the clue digit */
	unsigned char value[81];  /* current digit (givens included), 0 = empty */
	unsigned short notes[81]; /* bit d-1 = note d */
	int level;
	SuUndo undo[SU_UNDO_MAX]; /* ring buffer: newest at (undo_head + undo_n - 1) % SU_UNDO_MAX */
	int undo_head, undo_n;
} Sudoku;

/* Grids are 81 cells, row-major, 0 = empty. */
int su_solve(const unsigned char grid[81], unsigned char out[81]); /* 1 solved (first solution in out), 0 none */
int su_count_solutions(const unsigned char grid[81], int limit);   /* stops counting at limit */
int su_clue_target(int level);                                     /* 40 / 32 / 26 */
/* Random full grid, then cells removed while the solution stays unique until `target` clues or nothing more can go.
 * Returns the clue count. */
int su_generate(unsigned seed, int target, unsigned char puzzle[81], unsigned char solution[81]);

void su_new(Sudoku *s, int level, unsigned seed);
int  su_set(Sudoku *s, int cell, int digit);         /* digit 1..9 (clears the cell's notes) or 0 to erase; 1 if changed */
int  su_toggle_note(Sudoku *s, int cell, int digit); /* only on an empty, non-given cell; 1 if changed */
int  su_hint(Sudoku *s, int cell);                   /* writes the solution digit; 1 if changed */
int  su_undo(Sudoku *s);                             /* restores the last change; the cell index, or -1 if none */
int  su_is_wrong(const Sudoku *s, int cell);         /* a user digit that differs from the solution */
int  su_is_won(const Sudoku *s);
int  su_save(const Sudoku *s, char *buf, int cap);   /* bytes written ("sudoku v1" header), -1 if cap is too small */
int  su_load(Sudoku *s, const char *buf);            /* 0 ok, -1 corrupt (s untouched) */
#endif

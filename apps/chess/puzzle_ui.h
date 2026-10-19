/* Satranç Bulmacası screens inside the chess app: Lichess puzzles, history and streak. */
#ifndef CHESS_PUZZLE_UI_H
#define CHESS_PUZZLE_UI_H
/* runs until ✕ (returns 0, back to chess) or the app is asked to quit (returns 1); kui_init must have run */
int puzzle_run(void);
#endif

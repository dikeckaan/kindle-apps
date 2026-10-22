/* Move history (pure): the game's moves as SAN, figurine text for the screen, move numbers and PGN export. */
#ifndef CHESS_HISTORY_H
#define CHESS_HISTORY_H
#include "chess_game.h"

#define HIST_SAN_MAX 24 /* figurine SAN is UTF-8: "♛xe8=♛#" fits */

/* SAN (English letters, + and #) of every move of g->hist, replayed from g->start_fen; returns g->n */
int  history_sans(const Game *g, char (*out)[HIST_SAN_MAX]);
/* "Nf3" → "♞f3", "exd8=Q+" → "exd8=♛+": language-neutral piece figurines (solid glyphs read best on e-ink) */
void san_figurine(const char *san, char *out, int cap);
/* full-move number of ply i (0-based) and whether that ply is a black move */
int  history_move_no(const Game *g, int ply);
int  history_is_black(const Game *g, int ply);
/* "1-0", "0-1", "1/2-1/2" or "*" (still running) */
const char *history_result_tag(const Game *g);
/* A PGN of the game: the seven-tag roster (Event "Kindle Satranç", Site, Date "YYYY.MM.DD", Round, White, Black,
 * Result), SetUp/FEN for a non-standard start, then the move text with English SAN wrapped at 79 columns.
 * Returns its length, -1 when cap is too small. */
int  history_pgn(const Game *g, const char *date, const char *white, const char *black, char *buf, int cap);
#endif

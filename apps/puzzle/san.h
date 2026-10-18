/* Standard Algebraic Notation (pure): parse and format moves, replay a PGN move list. */
#ifndef PUZZLE_SAN_H
#define PUZZLE_SAN_H
#include "chess/chess_rules.h"

/* "Nbd2", "exd6", "e8=Q+", "O-O-O", "Qxf7#", "e4!?" → the legal move on b. Check marks, annotations and a
 * missing/extra 'x' are tolerated; ambiguous, illegal or malformed text gives -1 (out untouched). */
int  san_parse(const Board *b, const char *san, Move *out);
/* the SAN of legal move m on b, with the minimal disambiguation and + or # */
void san_format(const Board *b, Move m, char *out, int cap);
/* Plays up to max_plies moves of a PGN move text ("1. e4 e5 2. Nf3 …"; move numbers, {comments}, $NAGs and the
 * result are skipped) on b. Returns the plies played and the last one in *last; -1 on a bad move (b untouched). */
int  san_replay(Board *b, const char *pgn, int max_plies, Move *last);
#endif

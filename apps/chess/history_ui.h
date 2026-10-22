/* Hamle geçmişi screen (Satranç → Menü or a tap on the move strip). */
#ifndef CHESS_HISTORY_UI_H
#define CHESS_HISTORY_UI_H
#include "chess_game.h"
/* The numbered move list, page by page, and "PGN olarak kaydet". Returns the plies of the tapped move's position
 * (1..g->n: the board after that move), -1 for ✕ (back), -2 when the app should quit. */
int history_ui_run(const Game *g, int focus); /* focus: the ply whose page opens first, -1 = the latest */
/* the PGN player names: "Siz" / "Kindle (Orta)", or "Beyaz" / "Siyah" for two players */
void history_player_names(const Game *g, char *white, char *black, int cap);
#endif

/* Masa Oyunları: screen helpers shared by the three game screens (main.c). */
#ifndef MASA_UI_H
#define MASA_UI_H
#include "kui.h"
#include "mcommon.h"

/* layout: a square board under the top bar, a status line, one row of buttons at the bottom */
#define UI_BOARD_X 40
#define UI_BOARD_Y (KUI_TOPBAR_H + 36)
#define UI_BOARD_W 992
#define UI_STATUS_Y (UI_BOARD_Y + UI_BOARD_W + 18)
#define UI_STATUS_H 84
#define UI_BTN_H 120

#define UI_BOARD_GRAY 0xCC /* light gray board (solid fill) */
#define UI_QUIT (-2)       /* returned by screens when the app should end (SIGTERM / ✕ on the menu) */

/* filled disc; white discs get a black outline */
void ui_disc(int cx, int cy, int r, int white);
void ui_dot(int cx, int cy, int r, unsigned char gray); /* plain filled circle */
void ui_ring(int cx, int cy, int r, int t, unsigned char gray);

/* full-screen list of big buttons; index, -1 for ✕, UI_QUIT */
int  ui_choose(const char *title, const char *subtitle, const char **labels, int n);
/* buttons evenly across the bottom row */
void ui_layout_buttons(KuiButton *bs, int n, const char **labels, const int *ids);
void ui_status(const char *text, int force); /* redraws the status line when the text changed */
void ui_status_reset(void);
int  ui_yes_no(const char *title, const char *msg, void (*redraw)(void)); /* 1 = Evet */

/* statistics of games against the Kindle: outcome 1 win, 0 draw, -1 loss (from the player's side) */
void ui_stats_record(const char *game, int level, int outcome);
/* level chooser (Kolay/Orta/Zor): 1..3, -1 back, UI_QUIT */
int  ui_choose_level(const char *title);
const char *ui_level_name(int level); /* translated */

/* "thinking" pause before a synchronous AI search: shows the status at once (partial refresh) */
void ui_thinking(void);
void ui_after_thinking(void); /* drops taps made while the Kindle was thinking */

const char *ui_save_path(const char *game); /* $KUI_ROOT/data/masa/<game>.txt */

int reversi_screen(void);
int dama_screen(void);
int go_screen(void);
#endif

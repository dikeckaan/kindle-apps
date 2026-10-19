/* Masa Oyunları: screen helpers shared by the three game screens (main.c). */
#ifndef MASA_UI_H
#define MASA_UI_H
#include "kui.h"
#include "mcommon.h"

/* layout: a square board under the top bar, a status line, one row of buttons at the bottom */
#define UI_BOARD_X 40
#define UI_BOARD_Y (KUI_TOPBAR_H + 36)
#define UI_BOARD_W 992
#define UI_STATUS_Y (UI_BOARD_Y + UI_BOARD_W + 12)
#define UI_STATUS_H 136 /* two lines: what to do now (bold), then the score */
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
/* redraws the status line when the text changed; "first\nsecond": a bold instruction over a smaller second line */
void ui_status(const char *text, int force);
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

/* ---- board cell marks, shared by the games and the lessons ---- */
enum {
	UM_FRAME = 1,     /* thick black frame: selected / tap here */
	UM_DOT = 2,       /* dot: a square you may move to */
	UM_HL = 4,        /* inner frame: just turned / changed */
	UM_LAST = 8,      /* the move just played (drawn by each game) */
	UM_X = 16,        /* cross: taken from here / not allowed */
	UM_MUST = 32,     /* gray frame: this piece must capture (dama) */
	UM_VIA = 64,      /* ring: a landing square on the way (dama) */
	UM_LIB = 128,     /* small ring: a liberty (Go) */
	UM_TERR_B = 256,  /* black / white area square (Go) */
	UM_TERR_W = 512,
	UM_DEAD = 1024    /* faded stone (Go scoring) */
};
int  ui_mark_bits(char c); /* a MasaPage mark character → UM_* */
/* HL, X, VIA, LIB, DOT and FRAME over a cell already drawn; under: what the cell's centre shows
 * (0 black piece, 255 white piece, -1 the board) so the cross stays visible */
void ui_cell_marks(KuiRect r, int marks, int under);

/* ---- lessons: a board picture with a few sentences per page, ◀ ▶ (or a swipe) between pages ---- */
#define UI_PIC_MAX (GO_LESSON_N * GO_LESSON_N)
#define GO_LESSON_N 9
typedef struct {
	unsigned char piece[UI_PIC_MAX];   /* game specific */
	unsigned short marks[UI_PIC_MAX];  /* UM_* */
	char head[120];                    /* page title (translated) */
	char text[700];                    /* translated */
} UiPic;
typedef struct {
	const char *title; /* top bar, translated */
	int n;             /* cells per side */
	int go;            /* Go coordinates (columns without I) */
	int count;         /* pages */
	int demo;          /* buttons "◀ Geri · Baştan · Sonraki hamle ▶" instead of "◀ · Kapat · Sonraki ▶" */
	void (*page)(int k, UiPic *pic);
	void (*cell)(const UiPic *pic, int i, KuiRect r);
} UiLesson;
int  ui_lesson(const UiLesson *l); /* 0 back, UI_QUIT */
/* fills a picture from a MasaPage; piece maps a board character to the game's piece code */
void ui_pic_from_page(const MasaPage *p, int n, int (*piece)(char), UiPic *pic);
/* first visit of a game: offers "Nasıl oynanır?" once (remembered in data/masa/learn) */
int  ui_offer_howto(const char *game, const char *name, const UiLesson *howto);
/* the in-game Menü: Oyuna dön / Nasıl oynanır? / Örnek oyun; 0 or UI_QUIT */
int  ui_game_menu(const UiLesson *howto, const UiLesson *demo);
/* "Sıra sizde (Siyah)" against the Kindle, "Sıra Siyah'ta" / "Sıra Beyaz'da" for two players */
const char *ui_turn_text(int vs_kindle, int black);

int reversi_screen(void);
int dama_screen(void);
int go_screen(void);
#endif

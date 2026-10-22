/* Hamle geçmişi: every move of the game as a numbered two-column list (1. e4 e5), 32 full moves per page in two panes,
 * ◀ ▶ or a swipe turns the page (partial refresh). A tap on a move returns its position for the read-only review on
 * the game board (main.c). "PGN olarak kaydet" writes documents/Satranc/<date>-<time>.pgn with English SAN. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "history.h"
#include "history_ui.h"
#include "kui.h"
#include "levels.h"

#define ROWS 16 /* per pane */
#define ROW_H 66
#define PANES 2
#define PER_PAGE (ROWS * PANES)

enum { BTN_PREV = 1, BTN_PGN, BTN_NEXT };

static const Game *gm;
static char sans[GAME_MAX_PLIES][HIST_SAN_MAX];
static int mark, n_plies, first_black, n_rows, page, pages;
static KuiRect list_r, page_r;
static KuiButton buttons[3];
static int n_btn;

/* plies of full-move row r: white (may be -1 when the game started with black) and black (may be past the end) */
static int row_ply(int row, int black) { return 2 * row + black - first_black; }

static KuiRect cell_rect(int slot, int col) /* col 0 number, 1 white, 2 black */
{
	int pane = slot / ROWS, row = slot % ROWS, pw = list_r.w / PANES;
	int x = list_r.x + pane * pw, y = list_r.y + row * ROW_H;
	static const int cx[3] = { 0, 112, 316 }, cw[3] = { 104, 196, 196 };
	return (KuiRect){ x + cx[col], y, cw[col], ROW_H };
}

static void draw_list(void)
{
	kui_fill(list_r, 255);
	kui_fill((KuiRect){ list_r.x + list_r.w / 2 - 1, list_r.y, 2, ROWS * ROW_H }, 0x88);
	if (!n_plies) {
		kui_text(list_r, T("Henüz hamle yok."), KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0x55);
	}
	for (int slot = 0; slot < PER_PAGE; slot++) {
		int row = page * PER_PAGE + slot;
		if (row >= n_rows) break;
		KuiRect num = cell_rect(slot, 0);
		if (slot % 2) kui_fill((KuiRect){ num.x, num.y, list_r.w / PANES - 12, ROW_H }, 0xEE);
		char s[16];
		snprintf(s, sizeof s, "%d.", history_move_no(gm, row_ply(row, 0) < 0 ? 0 : row_ply(row, 0)));
		kui_text(num, s, KUI_FONT_REG, 36, KUI_ALIGN_RIGHT, 0x55);
		for (int c = 0; c < 2; c++) {
			int ply = row_ply(row, c);
			KuiRect r = cell_rect(slot, c + 1);
			r.x += 14;
			if (ply < 0) kui_text(r, "…", KUI_FONT_REG, 40, KUI_ALIGN_LEFT, 0x55);
			if (ply < 0 || ply >= n_plies) continue;
			char fig[HIST_SAN_MAX];
			san_figurine(sans[ply], fig, sizeof fig);
			kui_text(r, fig, KUI_FONT_REG, 46, KUI_ALIGN_LEFT, 0);
			if (ply == mark) /* the position on the board */
				kui_fill((KuiRect){ r.x, r.y + ROW_H - 10, kui_text_width(fig, KUI_FONT_REG, 46), 4 }, 0);
		}
	}
	kui_mark_dirty(list_r);
	kui_fill(page_r, 255);
	if (pages > 1) {
		char s[64];
		snprintf(s, sizeof s, T("Sayfa %d/%d"), page + 1, pages);
		kui_text(page_r, s, KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 0x55);
	}
	kui_mark_dirty(page_r);
}

static void draw_screen(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Hamle geçmişi"), NULL);
	list_r = (KuiRect){ 20, KUI_TOPBAR_H + 20, w - 40, ROWS * ROW_H };
	page_r = (KuiRect){ 20, list_r.y + list_r.h + 8, w - 40, 56 };
	int by = h - 24 - 120;
	/* ◀ ▶ only when there is more than one page */
	buttons[0] = (KuiButton){ { 260, by, w - 520, 120 }, T("PGN olarak kaydet"), BTN_PGN };
	buttons[1] = (KuiButton){ { 40, by, 180, 120 }, "◀", BTN_PREV };
	buttons[2] = (KuiButton){ { w - 220, by, 180, 120 }, "▶", BTN_NEXT };
	n_btn = pages > 1 ? 3 : 1;
	for (int i = 0; i < n_btn; i++) kui_button(&buttons[i], 0);
	draw_list();
	kui_flush(1);
}

static void turn(int d)
{
	int p = page + d;
	if (p < 0 || p >= pages) return;
	page = p;
	draw_list();
	kui_flush(0);
}

void history_player_names(const Game *g, char *white, char *black, int cap)
{
	if (g->mode == 1) {
		char kindle[96];
		snprintf(kindle, sizeof kindle, "Kindle (%s)", T(level_get(g->level)->name));
		snprintf(white, (size_t) cap, "%s", g->human_side == 0 ? T("Siz") : kindle);
		snprintf(black, (size_t) cap, "%s", g->human_side == 1 ? T("Siz") : kindle);
	} else {
		snprintf(white, (size_t) cap, "%s", T("Beyaz"));
		snprintf(black, (size_t) cap, "%s", T("Siyah"));
	}
}

/* documents/Satranc next to the kindle-apps folder: /mnt/us/documents/Satranc on the device */
static void save_pgn(void)
{
	static char buf[24576];
	const char *root = getenv("KUI_ROOT");
	char dir[300], path[400], name[64], date[40], white[96], black[96], msg[300];
	snprintf(dir, sizeof dir, "%s", root && *root ? root : "/mnt/us/kindle-apps");
	char *slash = strrchr(dir, '/');
	if (slash && slash != dir) *slash = '\0';
	strncat(dir, "/documents", sizeof dir - strlen(dir) - 1);
	mkdir(dir, 0755);
	strncat(dir, "/Satranc", sizeof dir - strlen(dir) - 1);
	mkdir(dir, 0755);
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	snprintf(date, sizeof date, "%04d.%02d.%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	snprintf(name, sizeof name, "%04d-%02d-%02d-%02d%02d.pgn", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
	snprintf(path, sizeof path, "%s/%s", dir, name);
	history_player_names(gm, white, black, sizeof white);
	int n = history_pgn(gm, date, white, black, buf, sizeof buf);
	int rc = n > 0 ? kui_write_atomic(path, buf, n) : -1;
	if (rc == 0) snprintf(msg, sizeof msg, T("Oyun PGN olarak kaydedildi:\ndocuments/Satranc\n%s"), name);
	else snprintf(msg, sizeof msg, "%s", T("PGN kaydedilemedi."));
	const char *ok[] = { T("Tamam") };
	kui_dialog(T("PGN olarak kaydet"), msg, ok, 1, draw_screen);
}

int history_ui_run(const Game *g, int focus)
{
	gm = g;
	n_plies = history_sans(g, sans);
	first_black = history_is_black(g, 0);
	n_rows = (n_plies + first_black + 1) / 2;
	pages = n_rows ? (n_rows + PER_PAGE - 1) / PER_PAGE : 1;
	page = pages - 1; /* the latest moves */
	if (focus >= 0 && focus < n_plies) page = ((focus + first_black) / 2) / PER_PAGE;
	mark = focus >= 0 && focus < n_plies ? focus : n_plies - 1;
	draw_screen();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return -2;
		if (e.type == KUI_EV_WAKE) draw_screen();
		if (e.type == KUI_EV_SWIPE) {
			if (e.dir == KUI_DIR_LEFT) turn(1);
			if (e.dir == KUI_DIR_RIGHT) turn(-1);
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
		int id = kui_buttons_hit(buttons, n_btn, e.x, e.y);
		if (id == BTN_PREV) turn(-1);
		else if (id == BTN_NEXT) turn(1);
		else if (id == BTN_PGN) {
			save_pgn();
			draw_screen();
		} else if (kui_rect_contains(list_r, e.x, e.y)) {
			for (int slot = 0; slot < PER_PAGE; slot++)
				for (int c = 0; c < 2; c++) {
					int ply = row_ply(page * PER_PAGE + slot, c);
					KuiRect r = cell_rect(slot, c + 1);
					if (ply < 0 || ply >= n_plies || !kui_rect_contains(r, e.x, e.y)) continue;
					kui_frame(r, 4, 0); /* touch feedback */
					kui_mark_dirty(r);
					kui_flush(0);
					return ply + 1;
				}
		}
	}
}

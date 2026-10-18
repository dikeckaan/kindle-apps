/* 2048 for Kindle: only the tiles that change (and the score) are refreshed after a move. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "g2048.h"
#include "kui.h"

enum { BTN_UNDO = 1, BTN_LEFT, BTN_UP, BTN_DOWN, BTN_RIGHT };

static G2048 game;
static char save_path[300];
static int won_shown;
static KuiRect board, score_box, best_box;
static KuiButton buttons[5];

static void layout(void)
{
	int w = kui_width();
	score_box = (KuiRect){ 40, KUI_TOPBAR_H + 24, (w - 120) / 2, 130 };
	best_box = (KuiRect){ 80 + (w - 120) / 2, KUI_TOPBAR_H + 24, (w - 120) / 2, 130 };
	int bh = 120, y = kui_height() - 24 - bh, bw = (w - 80 - 4 * 20) / 5;
	int top = KUI_TOPBAR_H + 180, side = y - 30 - top; /* board fills the space between scores and buttons */
	if (side > w - 80) side = w - 80;
	board = (KuiRect){ (w - side) / 2, top, side, side };
	const char *labels[5] = { T("Geri al"), "◀", "▲", "▼", "▶" };
	for (int i = 0; i < 5; i++)
		buttons[i] = (KuiButton){ { 40 + i * (bw + 20), y, bw, bh }, labels[i], i + 1 };
}

static KuiRect cell_rect(int r, int c) { return kui_grid_cell(board, 4, 4, c, r, 16); }

static void draw_cell(int r, int c)
{
	static const unsigned char shade[] = { 0xEE, 0xFF, 0xDD, 0xBB, 0x99, 0x77, 0x55 }; /* empty, 2, 4 … 64 */
	KuiRect rc = cell_rect(r, c);
	int v = game.cell[r][c], level = 0;
	for (int t = v; t > 1; t >>= 1) level++;
	unsigned char bg = level < 7 ? shade[level] : 0;
	kui_fill(rc, bg);
	if (v == 2) kui_frame(rc, 3, 0);
	if (v) {
		char s[12];
		snprintf(s, sizeof s, "%d", v);
		int digits = (int) strlen(s);
		int px = digits <= 2 ? 120 : digits == 3 ? 100 : digits == 4 ? 76 : 60;
		kui_text_on(rc, s, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, bg >= 0x99 ? 0 : 255, bg);
	}
	kui_mark_dirty(rc);
}

static void draw_score(KuiRect box, const char *label, int value)
{
	char s[16];
	snprintf(s, sizeof s, "%d", value);
	kui_fill(box, 255);
	kui_frame(box, 3, 0);
	kui_text((KuiRect){ box.x, box.y + 6, box.w, 44 }, label, KUI_FONT_REG, kui_fit_px(label, KUI_FONT_REG, 36, box.w - 20), KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ box.x, box.y + 50, box.w, 74 }, s, KUI_FONT_BOLD, 60, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(box);
}

static void draw_all(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar("2048", T("Yeni"));
	draw_score(score_box, T("Skor"), game.score);
	draw_score(best_box, T("En iyi"), game.best);
	kui_fill(board, 0xAA);
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) draw_cell(r, c);
	for (int i = 0; i < 5; i++) kui_button(&buttons[i], 0);
	kui_flush(1);
}

static void save(void)
{
	char buf[512];
	int n = g2048_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(save_path, buf, n);
}

/* redraws the cells that differ from `before` plus the scores */
static void draw_changes(int before[4][4], int old_score, int old_best)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (before[r][c] != game.cell[r][c]) draw_cell(r, c);
	if (old_score != game.score) draw_score(score_box, T("Skor"), game.score);
	if (old_best != game.best) draw_score(best_box, T("En iyi"), game.best);
	kui_flush(0);
}

static void new_game(void)
{
	g2048_new(&game, (unsigned) time(NULL));
	won_shown = 0;
	save();
	draw_all();
}

/* returns 0 to keep running, 1 to quit */
static int after_move(void)
{
	save();
	if (!won_shown && g2048_max_tile(&game) >= 2048) {
		won_shown = 1;
		const char *ch[] = { T("Devam"), T("Yeni oyun") };
		int k = kui_dialog(T("Tebrikler!"), T("2048'e ulaştın."), ch, 2, draw_all);
		if (k == 1) new_game();
		else draw_all();
	}
	if (!g2048_can_move(&game)) {
		char msg[64];
		snprintf(msg, sizeof msg, T("Skor: %d"), game.score);
		const char *ch[] = { T("Yeni oyun"), T("Kapat") };
		int k = kui_dialog(T("Oyun bitti"), msg, ch, 2, draw_all);
		if (k == 0) new_game();
		else return 1;
	}
	return 0;
}

static int do_move(KuiDir d)
{
	int before[4][4], s = game.score, b = game.best;
	memcpy(before, game.cell, sizeof before);
	if (!g2048_move(&game, d)) return 0;
	draw_changes(before, s, b);
	return after_move();
}

int main(void)
{
	if (kui_init("game2048") != 0) return 1;
	snprintf(save_path, sizeof save_path, "%s/save.txt", kui_data_dir("game2048"));
	layout();
	char buf[512];
	if (kui_read_file(save_path, buf, sizeof buf) < 0 || g2048_load(&game, buf) != 0) {
		g2048_new(&game, (unsigned) time(NULL));
		save();
	}
	won_shown = g2048_max_tile(&game) >= 2048;
	draw_all();
	if (!g2048_can_move(&game) && after_move()) { /* a finished game was saved: offer a new one */
		kui_quit();
		return 0;
	}

	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(-1);
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_WAKE: draw_all(); break;
		case KUI_EV_SWIPE: quit = do_move(e.dir); break;
		case KUI_EV_TAP: {
			int t = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (t == KUI_BTN_EXIT) { quit = 1; break; }
			if (t == KUI_BTN_MENU) {
				const char *ch[] = { T("Evet"), T("Hayır") };
				if (kui_dialog(T("Yeni oyun"), T("Mevcut oyun silinsin mi?"), ch, 2, draw_all) == 0) new_game();
				else draw_all();
				break;
			}
			int id = kui_buttons_hit(buttons, 5, e.x, e.y);
			if (id == BTN_UNDO) {
				int before[4][4], s = game.score, b = game.best;
				memcpy(before, game.cell, sizeof before);
				if (g2048_undo(&game)) { save(); draw_changes(before, s, b); }
			} else if (id >= BTN_LEFT) {
				static const KuiDir dirs[] = { KUI_DIR_LEFT, KUI_DIR_UP, KUI_DIR_DOWN, KUI_DIR_RIGHT };
				quit = do_move(dirs[id - BTN_LEFT]);
			}
			break;
		}
		default: break;
		}
	}
	save();
	kui_quit();
	return 0;
}

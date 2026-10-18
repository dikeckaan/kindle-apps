/* Sudoku for Kindle: each tap redraws only the cells whose look changed. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kui.h"
#include "sudoku.h"

#define CELL 108
#define GRID (9 * CELL)
#define THICK 6
#define THIN 2

enum { BTN_DIGIT = 1, BTN_ERASE = 10, BTN_NOTE, BTN_UNDO }; /* digits 1..9 are ids 1..9 */
#define NBTN 12

static const char *level_names[SU_LEVELS] = { N_("Kolay"), N_("Orta"), N_("Zor") }; /* shown with T() */

static Sudoku game;
static char save_path[300], cfg_path[300];
static int sel = -1, note_mode, show_errors, solved;
static int gx, gy; /* grid origin (outer edge of cell 0) */
static KuiButton buttons[NBTN];
static char note_label[96];
static long drawn[81]; /* look of each cell as last drawn; -1 = unknown */

static void layout(void)
{
	int w = kui_width();
	gx = (w - GRID) / 2;
	gy = KUI_TOPBAR_H + 24;
	int pad_y = gy + GRID + 20, gap = 9, dw = (GRID - 8 * gap) / 9;
	static const char *digits[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
	for (int i = 0; i < 9; i++)
		buttons[i] = (KuiButton){ { gx + i * (dw + gap), pad_y, dw, 142 }, digits[i], BTN_DIGIT + i };
	int y2 = pad_y + 142 + 18, bw = (GRID - 2 * 20) / 3;
	buttons[9] = (KuiButton){ { gx, y2, bw, 116 }, T("Sil"), BTN_ERASE };
	buttons[10] = (KuiButton){ { gx + bw + 20, y2, bw, 116 }, note_label, BTN_NOTE };
	buttons[11] = (KuiButton){ { gx + 2 * (bw + 20), y2, bw, 116 }, T("Geri al"), BTN_UNDO };
}

static int line_w(int k) { return k % 3 == 0 ? THICK : THIN; }

/* the cell's inside, between its grid lines */
static KuiRect cell_rect(int i)
{
	int r = i / 9, c = i % 9;
	int x0 = gx + c * CELL + (line_w(c) + 1) / 2, x1 = gx + (c + 1) * CELL - line_w(c + 1) / 2;
	int y0 = gy + r * CELL + (line_w(r) + 1) / 2, y1 = gy + (r + 1) * CELL - line_w(r + 1) / 2;
	return (KuiRect){ x0, y0, x1 - x0, y1 - y0 };
}

static int cell_at(int x, int y)
{
	if (x < gx || y < gy || x >= gx + GRID || y >= gy + GRID) return -1;
	return (y - gy) / CELL * 9 + (x - gx) / CELL;
}

static int wrong(int i) { return show_errors && su_is_wrong(&game, i); }

/* everything that decides how cell i looks, packed into one number */
static long cell_look(int i)
{
	int hl = sel >= 0 ? game.value[sel] : 0;
	long k = game.value[i] | (long) game.notes[i] << 4;
	k |= (long) (game.given[i] != 0) << 13;
	k |= (long) wrong(i) << 14;
	k |= (long) (i == sel) << 15;
	k |= (long) (hl && game.value[i] == hl && i != sel) << 16;
	return k;
}

static void draw_cell(int i)
{
	KuiRect rc = cell_rect(i);
	int v = game.value[i], bad = wrong(i), hl = sel >= 0 && game.value[sel] && v == game.value[sel];
	unsigned char bg = bad ? 0x33 : i == sel ? 0xCC : hl ? 0xEE : 0xFF;
	kui_fill(rc, bg);
	if (bad && i == sel) kui_frame(rc, 8, 0xCC); /* keep the selection visible on a dark cell */
	if (v) {
		char s[2] = { (char) ('0' + v), '\0' };
		KuiFont f = game.given[i] ? KUI_FONT_BOLD : KUI_FONT_REG;
		kui_text_on(rc, s, f, 72, KUI_ALIGN_CENTER, bad ? 255 : 0, bg);
	} else if (game.notes[i]) {
		int sw = rc.w / 3, sh = rc.h / 3;
		for (int d = 1; d <= 9; d++) {
			if (!(game.notes[i] & (1 << (d - 1)))) continue;
			char s[2] = { (char) ('0' + d), '\0' };
			KuiRect nr = { rc.x + (d - 1) % 3 * sw, rc.y + (d - 1) / 3 * sh, sw, sh };
			kui_text_on(nr, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0x22, bg);
		}
	}
	kui_mark_dirty(rc);
	drawn[i] = cell_look(i);
}

/* redraws only the cells whose look changed since they were last drawn */
static void refresh_cells(void)
{
	for (int i = 0; i < 81; i++)
		if (drawn[i] != cell_look(i)) draw_cell(i);
}

static void draw_note_button(void)
{
	snprintf(note_label, sizeof note_label, "%s", note_mode ? T("Not: Açık") : T("Not: Kapalı"));
	kui_button(&buttons[10], note_mode);
}

/* digit keys get a bigger label than kui_button would choose */
static void draw_digit_button(int i)
{
	const KuiButton *b = &buttons[i];
	kui_fill(b->r, 255);
	kui_frame(b->r, 4, 0);
	kui_text_on(b->r, b->label, KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 0, 255);
	kui_mark_dirty(b->r);
}

static void draw_all(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar(T("Sudoku"), T("Menü"));
	kui_fill((KuiRect){ gx - THICK / 2, gy - THICK / 2, GRID + THICK, GRID + THICK }, 0); /* lines show between cells */
	for (int i = 0; i < 81; i++) draw_cell(i);
	for (int i = 0; i < 9; i++) draw_digit_button(i);
	kui_button(&buttons[9], 0);
	draw_note_button();
	kui_button(&buttons[11], 0);
	kui_flush(1);
}

static void save(void)
{
	if (solved) return;
	static char buf[SU_SAVE_CAP];
	int n = su_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(save_path, buf, n);
}

/* full-screen list of big buttons; the chosen index, -1 for ✕, -2 on quit */
static int choose(const char *title, const char *msg, const char **labels, int n)
{
	int w = kui_width();
	KuiButton bs[6];
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(title, NULL);
		int top = KUI_TOPBAR_H + 70;
		if (msg) {
			kui_text((KuiRect){ 60, top, w - 120, 100 }, msg, KUI_FONT_REG, kui_fit_px(msg, KUI_FONT_REG, 44, w - 120), KUI_ALIGN_CENTER, 0);
			top += 130;
		}
		for (int i = 0; i < n; i++) {
			bs[i] = (KuiButton){ { 120, top + i * 180, w - 240, 150 }, labels[i], i + 1 };
			kui_button(&bs[i], 0);
		}
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -2;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
			int id = kui_buttons_hit(bs, n, e.x, e.y);
			if (id) {
				kui_button(&bs[id - 1], 1);
				kui_flush(0);
				return id - 1;
			}
		}
	}
}

static void start_game(int level)
{
	su_new(&game, level, (unsigned) time(NULL) ^ (unsigned) getpid() << 16);
	sel = -1;
	note_mode = 0;
	solved = 0;
	save();
}

/* level picker for a new game; 1 = started, 0 = cancelled, -1 = quit */
static int pick_level(const char *title)
{
	const char *labels[SU_LEVELS];
	for (int i = 0; i < SU_LEVELS; i++) labels[i] = T(level_names[i]);
	int k = choose(title, T("Zorluk seçin"), labels, SU_LEVELS);
	if (k == -2) return -1;
	if (k < 0) return 0;
	start_game(k);
	return 1;
}

/* start screen; 1 when a game is ready, 0 to quit */
static int start_screen(int can_resume)
{
	const char *labels[4];
	int n = 0;
	if (can_resume) labels[n++] = T("Devam et");
	for (int i = 0; i < SU_LEVELS; i++) labels[n++] = T(level_names[i]);
	int k = choose(T("Sudoku"), can_resume ? NULL : T("Zorluk seçin"), labels, n);
	if (k < 0) return 0;
	if (can_resume && k == 0) return 1;
	start_game(k - can_resume);
	return 1;
}

static void redraw_screen(void)
{
	for (int i = 0; i < 81; i++) drawn[i] = -1;
	draw_all();
}

/* returns 1 to quit */
static int on_win(void)
{
	solved = 1;
	remove(save_path);
	sel = -1;
	refresh_cells(); /* refreshed together with the dialog */
	const char *ch[] = { T("Yeni oyun"), T("Çıkış") };
	for (;;) {
		int k = kui_dialog(T("Tebrikler!"), T("Bulmacayı çözdünüz."), ch, 2, redraw_screen);
		if (k != 0) return 1;
		int r = pick_level(T("Yeni oyun"));
		if (r < 0) return 1;
		if (r > 0) {
			redraw_screen();
			return 0;
		}
		redraw_screen(); /* cancelled: back to the solved board and ask again */
	}
}

/* returns 1 to quit */
static int menu(void)
{
	for (;;) {
		const char *labels[] = { T("Yeni oyun"), show_errors ? T("Hataları göster: Açık") : T("Hataları göster: Kapalı"), T("İpucu") };
		int k = choose(T("Menü"), NULL, labels, 3);
		if (k == -2) return 1;
		if (k == 0) {
			int r = pick_level(T("Yeni oyun"));
			if (r < 0) return 1;
			if (r == 0) continue;
		} else if (k == 1) {
			show_errors = !show_errors;
			kui_cfg_set_int(cfg_path, "show_errors", show_errors);
			continue;
		} else if (k == 2) {
			if (sel < 0 || game.given[sel]) {
				const char *ok[] = { T("Tamam") };
				if (kui_dialog(T("İpucu"), T("Önce boş bir hücre seçin."), ok, 1, NULL) < 0) return 0;
				redraw_screen();
				return 0;
			}
			if (su_hint(&game, sel)) save();
		}
		redraw_screen();
		return !solved && su_is_won(&game) ? on_win() : 0;
	}
}

/* applies a digit button; returns 1 to quit */
static int press_digit(int d)
{
	if (sel < 0 || solved) return 0;
	int changed;
	if (note_mode) changed = su_toggle_note(&game, sel, d);
	else changed = su_set(&game, sel, game.value[sel] == d ? 0 : d); /* the same digit again erases it */
	if (!changed) return 0;
	save();
	refresh_cells();
	kui_flush(0);
	return su_is_won(&game) ? on_win() : 0;
}

int main(void)
{
	if (kui_init("sudoku") != 0) return 1;
	const char *dir = kui_data_dir("sudoku");
	snprintf(save_path, sizeof save_path, "%s/save.txt", dir);
	snprintf(cfg_path, sizeof cfg_path, "%s/settings.txt", dir);
	show_errors = kui_cfg_get_int(cfg_path, "show_errors", 1) != 0;
	layout();

	static char buf[SU_SAVE_CAP];
	int can_resume = kui_read_file(save_path, buf, sizeof buf) > 0 && su_load(&game, buf) == 0 && !su_is_won(&game);
	if (!start_screen(can_resume)) {
		kui_quit();
		return 0;
	}
	redraw_screen();

	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(-1);
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_WAKE: redraw_screen(); break;
		case KUI_EV_TAP: {
			int t = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (t == KUI_BTN_EXIT) { quit = 1; break; }
			if (t == KUI_BTN_MENU) { quit = menu(); break; }
			int c = cell_at(e.x, e.y);
			if (c >= 0) {
				sel = c;
				refresh_cells();
				kui_flush(0);
				break;
			}
			int id = kui_buttons_hit(buttons, NBTN, e.x, e.y);
			if (id >= BTN_DIGIT && id < BTN_DIGIT + 9) quit = press_digit(id - BTN_DIGIT + 1);
			else if (id == BTN_ERASE && sel >= 0 && !solved && su_set(&game, sel, 0)) {
				save();
				refresh_cells();
				kui_flush(0);
			} else if (id == BTN_NOTE) {
				note_mode = !note_mode;
				draw_note_button();
				kui_flush(0);
			} else if (id == BTN_UNDO && !solved) {
				int u = su_undo(&game);
				if (u >= 0) {
					save();
					refresh_cells();
					kui_flush(0);
				}
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

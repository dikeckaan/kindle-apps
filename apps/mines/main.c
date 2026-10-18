/* Mayın Tarlası for Kindle: only the cells that change (and the status line) are refreshed after a tap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kui.h"
#include "mines.h"

static Mines game;
static char save_path[300], cfg_path[300];
static int flag_mode;
static int drawn[MS_MAX_H][MS_MAX_W]; /* last view drawn per cell, -1 = needs drawing */
static char drawn_status[96];
static KuiRect board, status_box;
static int cs; /* cell size in pixels */

static long now_s(void) { return (long) time(NULL); }

static void layout(void)
{
	int w = kui_width(), h = kui_height();
	status_box = (KuiRect){ 24, KUI_TOPBAR_H + 8, w - 48, 84 };
	int top = status_box.y + status_box.h + 16, avail_w = w - 32, avail_h = h - top - 20;
	cs = avail_w / game.w;
	if (avail_h / game.h < cs) cs = avail_h / game.h;
	board = (KuiRect){ (w - cs * game.w) / 2, top, cs * game.w, cs * game.h };
}

static KuiRect cell_rect(int r, int c) { return (KuiRect){ board.x + c * cs, board.y + r * cs, cs, cs }; }

static void draw_cell(int r, int c)
{
	KuiRect rc = cell_rect(r, c), in = { rc.x + 2, rc.y + 2, rc.w - 4, rc.h - 4 };
	int v = ms_view(&game, r, c), px = cs * 62 / 100;
	switch (v) {
	case MS_V_CLOSED:
	case MS_V_FLAG:
		kui_fill(rc, 0xAA);
		kui_frame(rc, 2, 0);
		if (v == MS_V_FLAG) kui_text(in, "⚑", KUI_FONT_BOLD, cs * 80 / 100, KUI_ALIGN_CENTER, 0);
		break;
	case MS_V_BOOM:
		kui_fill(rc, 0);
		kui_text_on(in, "●", KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 255, 0);
		break;
	default: /* opened: number, revealed mine or a wrong flag */
		kui_fill(rc, 255);
		kui_frame(rc, 1, 0);
		if (v == MS_V_MINE) kui_text(in, "●", KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
		else if (v == MS_V_WRONG_FLAG) kui_text(in, "✕", KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
		else if (v > 0 && v <= 8) {
			char s[2] = { (char) ('0' + v), '\0' };
			kui_text(in, s, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
		}
		break;
	}
	drawn[r][c] = v;
	kui_mark_dirty(rc);
}

static void draw_status(int force)
{
	char s[96];
	long t = ms_seconds(&game);
	snprintf(s, sizeof s, "Mayın: %d|%s|Süre: %ld:%02ld", ms_mines_left(&game), flag_mode ? "Dokun: bayrak" : "Dokun: aç", t / 60, t % 60);
	if (!force && strcmp(s, drawn_status) == 0) return;
	snprintf(drawn_status, sizeof drawn_status, "%s", s);
	char *a = s, *b = strchr(a, '|');
	*b++ = '\0';
	char *c = strchr(b, '|');
	*c++ = '\0';
	kui_fill(status_box, 255);
	KuiRect left = { status_box.x, status_box.y, status_box.w / 3, status_box.h };
	KuiRect mid = { status_box.x + status_box.w / 3, status_box.y, status_box.w / 3, status_box.h };
	KuiRect right = { status_box.x + 2 * status_box.w / 3, status_box.y, status_box.w / 3, status_box.h };
	kui_text(left, a, KUI_FONT_BOLD, 44, KUI_ALIGN_LEFT, 0);
	kui_text(mid, b, KUI_FONT_REG, kui_fit_px(b, KUI_FONT_REG, 40, mid.w - 8), KUI_ALIGN_CENTER, 0);
	kui_text(right, c, KUI_FONT_BOLD, 44, KUI_ALIGN_RIGHT, 0);
	kui_mark_dirty(status_box);
}

static const char *flag_label(void) { return flag_mode ? "⚑ Açık" : "⚑ Kapalı"; }

static void draw_all(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar("Mayın Tarlası", flag_label());
	draw_status(1);
	for (int r = 0; r < game.h; r++)
		for (int c = 0; c < game.w; c++) draw_cell(r, c);
	kui_flush(1);
}

/* redraws the cells whose view changed plus the status line */
static void update(void)
{
	for (int r = 0; r < game.h; r++)
		for (int c = 0; c < game.w; c++)
			if (drawn[r][c] != ms_view(&game, r, c)) draw_cell(r, c);
	draw_status(0);
	kui_flush(0);
}

static void save(void)
{
	if (game.status != MS_PLAYING || !game.placed) { /* nothing worth resuming */
		unlink(save_path);
		return;
	}
	char buf[1024];
	ms_tick(&game, now_s());
	int n = ms_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(save_path, buf, n);
}

/* full-screen list of big buttons; returns the chosen index, -1 for ✕, -2 for quit */
static int choose(const char *title, const char **labels, int n)
{
	int w = kui_width();
	KuiButton bs[4];
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(title, NULL);
		for (int i = 0; i < n; i++) {
			bs[i] = (KuiButton){ { 120, KUI_TOPBAR_H + 70 + i * 180, w - 240, 150 }, labels[i], i + 1 };
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

static void start_game(void)
{
	layout();
	for (int r = 0; r < MS_MAX_H; r++)
		for (int c = 0; c < MS_MAX_W; c++) drawn[r][c] = -1;
	ms_tick(&game, now_s());
	draw_all();
}

/* level screen (with "Devam et" when a game can be resumed); returns 1 when a game is ready, 0 to quit */
static int setup(int can_resume)
{
	const char *labels[4];
	char names[MS_LEVELS][48];
	int n = 0, resume_idx = -1;
	if (can_resume) { resume_idx = n; labels[n++] = "Devam et"; }
	int first_level = n;
	for (int l = 0; l < MS_LEVELS; l++) {
		int w, h, m;
		ms_level_info(l, &w, &h, &m);
		snprintf(names[l], sizeof names[l], "%s · %d×%d · %d mayın", ms_level_name(l), w, h, m);
		labels[n++] = names[l];
	}
	int k = choose("Mayın Tarlası", labels, n);
	if (k < 0) return 0;
	if (k != resume_idx) {
		const char *fixed = getenv("MINES_SEED"); /* reproducible boards in the simulator */
		unsigned seed = fixed && *fixed ? (unsigned) strtoul(fixed, NULL, 10) : (unsigned) time(NULL) ^ (unsigned) getpid();
		ms_new(&game, k - first_level, seed);
		unlink(save_path);
	}
	start_game();
	return 1;
}

/* after a win or a loss: returns 1 to quit */
static int game_over(void)
{
	unlink(save_path);
	char msg[64];
	long t = ms_seconds(&game);
	snprintf(msg, sizeof msg, "%s · Süre: %ld:%02ld", ms_level_name(game.level), t / 60, t % 60);
	const char *ch[] = { "Yeni oyun", "Çıkış" };
	int k = kui_dialog(game.status == MS_WON ? "Kazandın!" : "Mayına bastın", msg, ch, 2, draw_all);
	if (k != 0) return 1;
	return !setup(0);
}

static int cell_at(int x, int y, int *r, int *c)
{
	if (!kui_rect_contains(board, x, y)) return 0;
	*c = (x - board.x) / cs;
	*r = (y - board.y) / cs;
	return *r < game.h && *c < game.w;
}

/* returns 1 to quit */
static int act(int r, int c, int flag)
{
	ms_tick(&game, now_s());
	int changed = flag && game.cell[r][c] != MS_OPEN ? ms_toggle_flag(&game, r, c) : ms_open(&game, r, c);
	ms_tick(&game, now_s());
	if (!changed) return 0;
	update();
	if (game.status != MS_PLAYING) return game_over();
	return 0;
}

int main(void)
{
	if (kui_init("mines") != 0) return 1;
	const char *dir = kui_data_dir("mines");
	snprintf(save_path, sizeof save_path, "%s/save.txt", dir);
	snprintf(cfg_path, sizeof cfg_path, "%s/settings.txt", dir);
	flag_mode = kui_cfg_get_int(cfg_path, "flag_mode", 0) != 0;

	char buf[1024];
	int can_resume = kui_read_file(save_path, buf, sizeof buf) > 0 && ms_load(&game, buf) == 0 &&
			 game.status == MS_PLAYING && game.placed;
	if (!setup(can_resume)) {
		kui_quit();
		return 0;
	}

	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(-1);
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_WAKE:
			for (int r = 0; r < game.h; r++)
				for (int c = 0; c < game.w; c++) drawn[r][c] = -1;
			draw_all();
			break;
		case KUI_EV_TAP:
		case KUI_EV_LONGPRESS: {
			int t = e.type == KUI_EV_TAP ? kui_topbar_hit(kui_width(), 1, e.x, e.y) : 0;
			if (t == KUI_BTN_EXIT) { quit = 1; break; }
			if (t == KUI_BTN_MENU) {
				flag_mode = !flag_mode;
				kui_cfg_set_int(cfg_path, "flag_mode", flag_mode);
				kui_topbar("Mayın Tarlası", flag_label());
				draw_status(0);
				kui_flush(0);
				break;
			}
			int r, c;
			if (cell_at(e.x, e.y, &r, &c)) quit = act(r, c, e.type == KUI_EV_LONGPRESS || flag_mode);
			break;
		}
		default: break;
		}
	}
	save();
	kui_quit();
	return 0;
}

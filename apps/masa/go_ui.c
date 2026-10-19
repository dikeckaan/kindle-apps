/* Go screen: tap an intersection to play. After two passes the dead stones proposed by the Kindle are shown
 * faded; tap a chain to change it, then confirm the count (Chinese area scoring, komi 7.5). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "go.h"
#include "ui.h"

enum { BTN_UNDO = 1, BTN_PASS, BTN_RESIGN, BTN_NEW, BTN_RESUME, BTN_CONFIRM };

static GoGame game;
static int cell, ox, oy; /* intersection spacing and the board's top-left */
static int scoring, finished, counted, end_pending;
static unsigned char dead[GO_MAXP], owner[GO_MAXP];
static int drawn[GO_MAXN * GO_MAXN];
static KuiButton buttons[4];
static int nbuttons;
static char note[80];

static int N(void) { return game.size; }
static int human_turn(void) { return game.mode == 2 || game.pos.side == game.human; }
static KuiRect cell_rect(int i) { return (KuiRect){ ox + (i % N()) * cell, oy + (i / N()) * cell, cell, cell }; }
static int pt(int i) { return go_pt(&game.pos, i / N(), i % N()); }

static int is_star(int r, int c)
{
	int n = N(), a = n == 9 ? 2 : 3, m = n / 2;
	return ((r == a || r == n - 1 - a) && (c == a || c == n - 1 - a)) || (r == m && c == m);
}

static void recount(void)
{
	go_area(&game.pos, dead, NULL, NULL, owner);
}

static int look(int i)
{
	int p = pt(i), st = game.pos.c[p];
	int l = st | (p == game.pos.last) << 2;
	if (scoring) l |= 1 << 3 | dead[p] << 4 | owner[p] << 5;
	return l;
}

static void draw_cell(int i)
{
	KuiRect r = cell_rect(i);
	int row = i / N(), col = i % N(), cx = r.x + cell / 2, cy = r.y + cell / 2, p = pt(i), st = game.pos.c[p];
	kui_fill(r, UI_BOARD_GRAY);
	/* grid lines through the centre, cut at the board's edge; the outer lines are thicker */
	int lt = row == 0 || row == N() - 1 ? 4 : 2, ct = col == 0 || col == N() - 1 ? 4 : 2;
	int x0 = col == 0 ? cx : r.x, x1 = col == N() - 1 ? cx + ct / 2 : r.x + r.w;
	int y0 = row == 0 ? cy : r.y, y1 = row == N() - 1 ? cy + lt / 2 : r.y + r.h;
	kui_fill((KuiRect){ x0, cy - lt / 2, x1 - x0, lt }, 0);
	kui_fill((KuiRect){ cx - ct / 2, y0, ct, y1 - y0 }, 0);
	if (is_star(row, col)) ui_dot(cx, cy, cell / 14 + 2, 0);
	int rad = cell / 2 - 3;
	if (st == GO_BLACK || st == GO_WHITE) {
		int is_dead = scoring && dead[p];
		if (is_dead) { /* faded: gray body, outline in the stone's colour */
			ui_dot(cx, cy, rad, st == GO_BLACK ? 0 : 0x44);
			ui_dot(cx, cy, rad - 6, st == GO_BLACK ? 0x88 : 0xEE);
		} else {
			ui_disc(cx, cy, rad, st == GO_WHITE);
		}
		if (p == game.pos.last && !scoring) ui_ring(cx, cy, rad / 2, cell > 90 ? 6 : 4, st == GO_BLACK ? 255 : 0);
	}
	if (scoring && (owner[p] == GO_BLACK || owner[p] == GO_WHITE) && (st == GO_EMPTY || dead[p])) {
		int s = cell / 4;
		KuiRect m = { cx - s / 2, cy - s / 2, s, s };
		kui_fill(m, owner[p] == GO_BLACK ? 0 : 255);
		kui_frame(m, 3, 0);
	}
	kui_mark_dirty(r);
	drawn[i] = look(i);
}

static void half(int v2, char *out, int cap) /* v2 = value × 2 → "7,5" */
{
	kui_fmt_num(v2 / 2.0, v2 % 2 ? 1 : 0, out, cap);
}

static void result_line(char *out, int cap)
{
	if (game.resigned) {
		const char *who = game.resigned == GO_BLACK ? T("Siyah") : T("Beyaz");
		if (game.mode == 1)
			snprintf(out, (size_t) cap, "%s", game.resigned == game.human ? T("Teslim oldunuz · Kindle kazandı") : T("Kindle teslim oldu · Kazandınız!"));
		else
			snprintf(out, (size_t) cap, T("%s teslim oldu"), who);
		return;
	}
	int b, w;
	go_area(&game.pos, dead, &b, &w, NULL);
	int s2 = 2 * (b - w) - GO_KOMI2;
	char komi[16], margin[16];
	half(GO_KOMI2, komi, sizeof komi);
	half(abs(s2), margin, sizeof margin);
	const char *win = s2 > 0 ? T("Siyah") : T("Beyaz");
	if (!finished) {
		snprintf(out, (size_t) cap, T("Siyah %d – Beyaz %d + %s · %s %s puanla önde"), b, w, komi, win, margin);
		return;
	}
	char res[80];
	if (game.mode == 1) snprintf(res, sizeof res, "%s", (s2 > 0) == (game.human == GO_BLACK) ? T("Kazandınız!") : T("Kindle kazandı"));
	else snprintf(res, sizeof res, T("%s kazandı"), win);
	snprintf(out, (size_t) cap, T("Siyah %d – Beyaz %d + %s · %s puan fark · %s"), b, w, komi, margin, res);
}

static void status_text(char *out, int cap)
{
	if (game.resigned || finished) {
		result_line(out, cap);
		return;
	}
	if (scoring) {
		char r[160];
		result_line(r, sizeof r);
		snprintf(out, (size_t) cap, "%s", r);
		return;
	}
	const char *side = game.pos.side == GO_BLACK ? T("Siyah") : T("Beyaz");
	char base[110];
	snprintf(base, sizeof base, T("Sıra: %s · esir: Siyah %d, Beyaz %d"), side, game.pos.caps[GO_BLACK], game.pos.caps[GO_WHITE]);
	if (note[0]) snprintf(out, (size_t) cap, "%s · %s", note, base);
	else snprintf(out, (size_t) cap, "%s", base);
}

static void layout_buttons(void)
{
	if (scoring && !finished) {
		const char *labels[2] = { T("Oyuna dön"), T("Sayımı onayla") };
		static const int ids[2] = { BTN_RESUME, BTN_CONFIRM };
		ui_layout_buttons(buttons, 2, labels, ids);
		nbuttons = 2;
	} else {
		const char *labels[4] = { T("Geri al"), T("Pas"), T("Teslim ol"), T("Yeni oyun") };
		static const int ids[4] = { BTN_UNDO, BTN_PASS, BTN_RESIGN, BTN_NEW };
		ui_layout_buttons(buttons, 4, labels, ids);
		nbuttons = 4;
	}
}

static void draw_buttons(void)
{
	int y = kui_height() - 24 - UI_BTN_H;
	kui_fill((KuiRect){ 0, y, kui_width(), UI_BTN_H }, 255);
	kui_mark_dirty((KuiRect){ 0, y, kui_width(), UI_BTN_H });
	layout_buttons();
	for (int i = 0; i < nbuttons; i++) kui_button(&buttons[i], 0);
}

static void update_board(void)
{
	for (int i = 0; i < N() * N(); i++)
		if (drawn[i] != look(i)) draw_cell(i);
	char s[200];
	status_text(s, sizeof s);
	ui_status(s, 0);
	kui_flush(0);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(N() == 9 ? T("Go 9×9") : T("Go 13×13"), NULL);
	kui_fill((KuiRect){ UI_BOARD_X - 5, UI_BOARD_Y - 5, UI_BOARD_W + 10, UI_BOARD_W + 10 }, 0);
	kui_fill((KuiRect){ UI_BOARD_X, UI_BOARD_Y, UI_BOARD_W, UI_BOARD_W }, UI_BOARD_GRAY);
	for (int i = 0; i < N() * N(); i++) draw_cell(i);
	draw_buttons();
	char s[200];
	status_text(s, sizeof s);
	ui_status(s, 1);
	kui_flush(1);
}

static void save(void)
{
	static char buf[8192];
	int n = go_game_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(ui_save_path("go"), buf, n);
}

static void enter_scoring(void)
{
	ui_status(T("Ölü taşlar hesaplanıyor…"), 0);
	kui_flush(0);
	go_estimate_dead(&game.pos, 400, (uint32_t) time(NULL) | 1, dead);
	recount();
	scoring = 1;
	snprintf(note, sizeof note, "%s", "");
	draw_buttons();
	update_board();
	const char *ch[] = { T("Tamam") };
	kui_dialog(T("Sayım"), T("İki pas: oyun bitti.\nSoluk taşlar ölü sayıldı; değiştirmek için bir zincire dokunun."), ch, 1, draw_all);
	draw_all();
}

static void finish(void)
{
	finished = 1;
	int s2 = game.resigned ? (game.resigned == GO_BLACK ? -1 : 1) : go_score2(&game.pos, dead);
	if (game.mode == 1 && !counted) {
		ui_stats_record("go", game.level, (s2 > 0) == (game.human == GO_BLACK) ? 1 : -1);
		counted = 1;
	}
	end_pending = 1;
	draw_buttons();
	update_board();
}

static int check_end(void)
{
	char s[200];
	status_text(s, sizeof s);
	const char *ch[] = { T("Yeni oyun"), T("Tahtaya bak") };
	int k = kui_dialog(T("Oyun bitti"), s, ch, 2, draw_all);
	draw_all();
	return k == 0;
}

static void after_move(void)
{
	save();
	if (game.resigned) { finish(); return; }
	if (go_game_over(&game)) { update_board(); enter_scoring(); return; }
	update_board();
}

static void ai_turn(void)
{
	while (game.mode == 1 && !go_game_over(&game) && game.pos.side != game.human) {
		ui_thinking();
		MasaLimits lim = { game.level, 0, 0, 0, (uint32_t) time(NULL) | 1 };
		int m = go_game_ai(&game, &lim, NULL);
		ui_after_thinking();
		if (go_game_play(&game, m) != 0) go_game_play(&game, GO_PASS);
		note[0] = '\0';
		if (m == GO_PASS) snprintf(note, sizeof note, "%s", T("Kindle pas geçti"));
		after_move();
	}
}

static void tap(int x, int y)
{
	if (scoring) {
		if (finished) return;
		int c = (x - ox) / cell, r = (y - oy) / cell;
		if (x < ox || y < oy || c >= N() || r >= N()) return;
		go_toggle_dead(&game.pos, go_pt(&game.pos, r, c), dead);
		recount();
		update_board();
		return;
	}
	if (go_game_over(&game) || !human_turn()) return;
	if (x < ox || y < oy) return;
	int c = (x - ox) / cell, r = (y - oy) / cell;
	if (c >= N() || r >= N()) return;
	int p = go_pt(&game.pos, r, c);
	if (game.pos.c[p] != GO_EMPTY) return;
	if (!go_game_legal(&game, p)) {
		snprintf(note, sizeof note, "%s", T("Buraya oynanamaz (ko ya da intihar)"));
		update_board();
		return;
	}
	note[0] = '\0';
	go_game_play(&game, p);
	after_move();
	ai_turn();
}

static void undo(void)
{
	if (game.n == 0) return;
	if (game.mode == 1) {
		do go_game_undo(&game, 1);
		while (game.n > 0 && game.pos.side != game.human);
	} else {
		go_game_undo(&game, 1);
	}
	scoring = finished = 0;
	note[0] = '\0';
	save();
	draw_buttons();
	update_board();
	ai_turn();
}

static void resume_play(void)
{
	/* take back the passes that ended the game */
	while (game.n > 0 && go_game_over(&game)) go_game_undo(&game, 1);
	if (game.mode == 1)
		while (game.n > 0 && game.pos.side != game.human) go_game_undo(&game, 1);
	scoring = finished = 0;
	note[0] = '\0';
	save();
	draw_buttons();
	update_board();
	ai_turn();
}

static int setup(int can_resume)
{
	for (;;) {
		const char *opts[3];
		int n = 0, resume = -1;
		if (can_resume) { resume = n; opts[n++] = T("Devam et"); }
		int one = n;
		opts[n++] = T("Kindle'a karşı");
		opts[n++] = T("İki oyunculu");
		int k = ui_choose(T("Go"), T("Çin kuralları: alan sayımı, komi 7,5"), opts, n);
		if (k < 0) return k == UI_QUIT ? UI_QUIT : 0;
		if (k == resume) return 1;
		const char *sz[] = { T("9×9 (kısa oyun)"), T("13×13") };
		int s = ui_choose(T("Tahta"), NULL, sz, 2);
		if (s == UI_QUIT) return UI_QUIT;
		if (s < 0) continue;
		int size = s ? 13 : 9;
		if (k != one) {
			go_game_new(&game, size, 2, 2, GO_BLACK);
			return 1;
		}
		int l = ui_choose_level(T("Zorluk"));
		if (l == UI_QUIT) return UI_QUIT;
		if (l < 0) continue;
		const char *cl[] = { T("Siyah (ilk hamle)"), T("Beyaz"), T("Rastgele") };
		int c = ui_choose(T("Renginiz"), NULL, cl, 3);
		if (c == UI_QUIT) return UI_QUIT;
		if (c < 0) continue;
		if (c == 2) c = (int) (time(NULL) & 1);
		go_game_new(&game, size, 1, l, c ? GO_WHITE : GO_BLACK);
		return 1;
	}
}

static void start(void)
{
	cell = UI_BOARD_W / N();
	ox = UI_BOARD_X + (UI_BOARD_W - cell * N()) / 2;
	oy = UI_BOARD_Y + (UI_BOARD_W - cell * N()) / 2;
	scoring = finished = end_pending = 0;
	counted = 0;
	note[0] = '\0';
	memset(dead, 0, sizeof dead);
	save();
	for (int i = 0; i < GO_MAXN * GO_MAXN; i++) drawn[i] = -1;
	ui_status_reset();
	draw_all();
	ai_turn();
}

int go_screen(void)
{
	static char buf[8192];
	int can_resume = kui_read_file(ui_save_path("go"), buf, sizeof buf) > 0 && go_game_load(&game, buf) == 0 && !go_game_over(&game);
	int s = setup(can_resume);
	if (s != 1) return s;
	start();
	for (;;) {
		if (end_pending && (end_pending = 0, check_end())) {
			int r = setup(0);
			if (r != 1) return r;
			start();
			continue;
		}
		KuiEvent e = kui_next_event(-1);
		switch (e.type) {
		case KUI_EV_QUIT: save(); return UI_QUIT;
		case KUI_EV_WAKE:
			for (int i = 0; i < GO_MAXN * GO_MAXN; i++) drawn[i] = -1;
			draw_all();
			break;
		case KUI_EV_TAP: {
			if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) { save(); return 0; }
			int id = kui_buttons_hit(buttons, nbuttons, e.x, e.y);
			if (id == BTN_UNDO) {
				undo();
			} else if (id == BTN_PASS) {
				if (!go_game_over(&game) && human_turn()) {
					note[0] = '\0';
					go_game_play(&game, GO_PASS);
					after_move();
					ai_turn();
				}
			} else if (id == BTN_RESIGN) {
				if (!go_game_over(&game) && human_turn() && ui_yes_no(T("Teslim ol"), T("Oyunu bırakıp teslim olunsun mu?"), draw_all)) {
					draw_all();
					go_game_play(&game, GO_RESIGN);
					after_move();
				} else {
					draw_all();
				}
			} else if (id == BTN_NEW) {
				if (ui_yes_no(T("Yeni oyun"), T("Bu oyun bitirilsin mi?"), draw_all)) {
					int r = setup(0);
					if (r != 1) return r;
					start();
				} else {
					draw_all();
				}
			} else if (id == BTN_RESUME) {
				resume_play();
			} else if (id == BTN_CONFIRM) {
				finish();
			} else {
				tap(e.x, e.y);
			}
			break;
		}
		default: break;
		}
		if (kui_quit_pending()) { save(); return UI_QUIT; }
	}
}

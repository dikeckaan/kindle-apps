/* Dama (Türk daması) screen: tap a piece, then its landing squares one by one (a capture sequence step by step;
 * when only one way is left it is completed at once). Pieces that must capture are framed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dama.h"
#include "ui.h"

#define CELL (UI_BOARD_W / 8)
enum { BTN_UNDO = 1, BTN_NEW };

static DmGame game;
static int over, counted, end_pending, flip;
static int sel = -1, npre;
static unsigned char pre[DM_MAX_PATH]; /* landing squares tapped so far */
static DmMove legal[DM_MAX_MOVES];
static int nlegal;
static int drawn[64];
static KuiButton buttons[2];

static int human_turn(void) { return game.mode == 2 || game.pos.side == game.human; }

static KuiRect cell_rect(int sq)
{
	int r = sq >> 3, c = sq & 7;
	if (flip) { r = 7 - r; c = 7 - c; }
	return (KuiRect){ UI_BOARD_X + c * CELL, UI_BOARD_Y + r * CELL, CELL, CELL };
}

static int cell_at(int x, int y)
{
	if (!kui_rect_contains((KuiRect){ UI_BOARD_X, UI_BOARD_Y, UI_BOARD_W, UI_BOARD_W }, x, y)) return -1;
	int c = (x - UI_BOARD_X) / CELL, r = (y - UI_BOARD_Y) / CELL;
	if (flip) { r = 7 - r; c = 7 - c; }
	return r * 8 + c;
}

static void refresh_legal(void) { nlegal = over ? 0 : dm_moves(&game.pos, legal, DM_MAX_MOVES); }

static int matches(const DmMove *m)
{
	if (m->from != sel || m->n < npre) return 0;
	return memcmp(m->path, pre, (size_t) npre) == 0;
}

static int is_target(int sq)
{
	if (sel < 0 || !human_turn()) return 0;
	for (int i = 0; i < nlegal; i++)
		if (matches(&legal[i]) && legal[i].n > npre && legal[i].path[npre] == sq) return 1;
	return 0;
}

static int must_capture(int sq)
{
	if (!human_turn() || sel >= 0 || nlegal == 0 || legal[0].ncap == 0) return 0;
	for (int i = 0; i < nlegal; i++)
		if (legal[i].from == sq) return 1;
	return 0;
}

static const DmMove *last(void) { return game.n ? &game.hist[game.n - 1] : NULL; }

static int last_mark(int sq) /* 1 from/to, 2 captured */
{
	const DmMove *m = last();
	if (!m) return 0;
	if (m->from == sq || m->path[m->n - 1] == sq) return 1;
	for (int i = 0; i < m->ncap; i++)
		if (m->cap[i] == sq) return 2;
	return 0;
}

static int is_via(int sq)
{
	for (int i = 0; i < npre; i++)
		if (pre[i] == sq) return 1;
	return 0;
}

static int look(int sq)
{
	return game.pos.b[sq] | (sq == sel) << 3 | is_target(sq) << 4 | is_via(sq) << 5 | last_mark(sq) << 6 |
	       must_capture(sq) << 8 | flip << 9;
}

static void draw_cell(int sq)
{
	KuiRect r = cell_rect(sq);
	int cx = r.x + CELL / 2, cy = r.y + CELL / 2, p = game.pos.b[sq], lm = last_mark(sq);
	kui_fill(r, UI_BOARD_GRAY);
	kui_frame(r, 1, 0x44);
	if (lm == 1) kui_frame((KuiRect){ r.x + 5, r.y + 5, r.w - 10, r.h - 10 }, 3, 0x55);
	if (must_capture(sq)) kui_frame((KuiRect){ r.x + 3, r.y + 3, r.w - 6, r.h - 6 }, 7, 0x77);
	if (p) {
		int white = dm_color(p) == 0;
		ui_disc(cx, cy, CELL / 2 - 14, white);
		if (dm_is_king(p)) {
			ui_ring(cx, cy, CELL / 2 - 24, 4, white ? 0 : 255);
			KuiRect g = { cx - 30, cy - 30, 60, 60 };
			if (white) kui_text(g, "♛", KUI_FONT_REG, 46, KUI_ALIGN_CENTER, 0);
			else kui_text_on(g, "♛", KUI_FONT_REG, 46, KUI_ALIGN_CENTER, 255, 0);
		}
	} else if (lm == 2) {
		ui_dot(cx, cy, 9, 0x88); /* a piece was taken here by the last move */
	}
	if (is_via(sq)) ui_ring(cx, cy, 26, 6, 0x22);
	if (is_target(sq)) ui_dot(cx, cy, 15, 0x22);
	if (sq == sel) kui_frame(r, 8, 0);
	kui_mark_dirty(r);
	drawn[sq] = look(sq);
}

static const char *side_name(int s) { return s ? T("Siyah") : T("Beyaz"); }

static const char *result_text(DmResult r)
{
	if (r == DM_DRAW) return T("Berabere");
	int winner = r == DM_WHITE_WINS ? 0 : 1;
	if (game.mode == 1) return winner == game.human ? T("Kazandınız!") : T("Kindle kazandı");
	return winner ? T("Siyah kazandı") : T("Beyaz kazandı");
}

static void status_text(char *out, int cap)
{
	int cnt[2] = { 0, 0 };
	for (int s = 0; s < 64; s++)
		if (game.pos.b[s]) cnt[dm_color(game.pos.b[s])]++;
	if (over) {
		snprintf(out, (size_t) cap, T("Beyaz %d – Siyah %d · %s"), cnt[0], cnt[1], result_text(dm_result(&game.pos)));
		return;
	}
	if (npre) {
		snprintf(out, (size_t) cap, "%s", T("Almaya devam: sıradaki kareye dokunun"));
		return;
	}
	char base[120];
	snprintf(base, sizeof base, T("Sıra: %s · Beyaz %d – Siyah %d"), side_name(game.pos.side), cnt[0], cnt[1]);
	if (human_turn() && nlegal && legal[0].ncap)
		snprintf(out, (size_t) cap, T("Almak zorunlu (%d taş) · %s"), legal[0].ncap, base);
	else
		snprintf(out, (size_t) cap, "%s", base);
}

static void update_board(void)
{
	for (int sq = 0; sq < 64; sq++)
		if (drawn[sq] != look(sq)) draw_cell(sq);
	char s[200];
	status_text(s, sizeof s);
	ui_status(s, 0);
	kui_flush(0);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Dama"), NULL);
	kui_frame((KuiRect){ UI_BOARD_X - 5, UI_BOARD_Y - 5, UI_BOARD_W + 10, UI_BOARD_W + 10 }, 5, 0);
	for (int sq = 0; sq < 64; sq++) draw_cell(sq);
	const char *labels[2] = { T("Geri al"), T("Yeni oyun") };
	static const int ids[2] = { BTN_UNDO, BTN_NEW };
	ui_layout_buttons(buttons, 2, labels, ids);
	for (int i = 0; i < 2; i++) kui_button(&buttons[i], 0);
	char s[200];
	status_text(s, sizeof s);
	ui_status(s, 1);
	kui_flush(1);
}

static void save(void)
{
	static char buf[16384];
	int n = dm_game_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(ui_save_path("dama"), buf, n);
}

static void after_move(void)
{
	sel = -1;
	npre = 0;
	if (dm_result(&game.pos) != DM_ONGOING) over = end_pending = 1;
	refresh_legal();
	save();
	update_board();
}

static int check_end(void)
{
	DmResult r = dm_result(&game.pos);
	if (game.mode == 1 && !counted) {
		int out = r == DM_DRAW ? 0 : (r == DM_WHITE_WINS) == (game.human == 0) ? 1 : -1;
		ui_stats_record("dama", game.level, out);
		counted = 1;
	}
	char s[200];
	status_text(s, sizeof s);
	const char *ch[] = { T("Yeni oyun"), T("Tahtaya bak") };
	int k = kui_dialog(T("Oyun bitti"), s, ch, 2, draw_all);
	draw_all();
	return k == 0;
}

static void ai_turn(void)
{
	while (game.mode == 1 && !over && game.pos.side != game.human) {
		ui_thinking();
		MasaLimits lim = { game.level, 0, 0, 0, (uint32_t) time(NULL) };
		DmMove m;
		if (dm_ai(&game.pos, &lim, &m, NULL) != 0) break;
		ui_after_thinking();
		dm_game_play(&game, &m);
		after_move();
	}
}

static void tap(int sq)
{
	if (over || !human_turn() || sq < 0) return;
	if (sel >= 0 && is_target(sq)) {
		pre[npre++] = (unsigned char) sq;
		int cand = -1, count = 0;
		for (int i = 0; i < nlegal; i++)
			if (matches(&legal[i])) { cand = i; count++; }
		if (count == 1) { /* only one way on: finish it */
			dm_game_play(&game, &legal[cand]);
			after_move();
			ai_turn();
			return;
		}
		update_board();
		return;
	}
	if (npre) { /* in the middle of a capture: only the next landing square, or the piece again to start over */
		if (sq == sel) {
			sel = -1;
			npre = 0;
			update_board();
		}
		return;
	}
	int has = 0;
	for (int i = 0; i < nlegal; i++)
		if (legal[i].from == sq) has = 1;
	sel = has && sq != sel ? sq : -1;
	update_board();
}

static void undo(void)
{
	if (game.n == 0) return;
	if (game.mode == 1) {
		do dm_game_undo(&game, 1);
		while (game.n > 0 && game.pos.side != game.human);
	} else {
		dm_game_undo(&game, 1);
	}
	over = 0;
	sel = -1;
	npre = 0;
	refresh_legal();
	save();
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
		int k = ui_choose(T("Dama"), T("Türk daması · almak zorunlu, en çok taşı alan yol seçilir"), opts, n);
		if (k < 0) return k == UI_QUIT ? UI_QUIT : 0;
		if (k == resume) return 1;
		if (k != one) {
			dm_game_new(&game, 2, 2, 0);
			return 1;
		}
		int l = ui_choose_level(T("Zorluk"));
		if (l == UI_QUIT) return UI_QUIT;
		if (l < 0) continue;
		const char *cl[] = { T("Beyaz (ilk hamle)"), T("Siyah"), T("Rastgele") };
		int c = ui_choose(T("Renginiz"), NULL, cl, 3);
		if (c == UI_QUIT) return UI_QUIT;
		if (c < 0) continue;
		if (c == 2) c = (int) (time(NULL) & 1);
		dm_game_new(&game, 1, l, c);
		return 1;
	}
}

static void start(void)
{
	over = dm_result(&game.pos) != DM_ONGOING;
	counted = over;
	end_pending = 0;
	flip = game.mode == 1 && game.human == 1;
	sel = -1;
	npre = 0;
	refresh_legal();
	save();
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	ui_status_reset();
	draw_all();
	ai_turn();
}

int dama_screen(void)
{
	static char buf[16384];
	int can_resume = kui_read_file(ui_save_path("dama"), buf, sizeof buf) > 0 && dm_game_load(&game, buf) == 0 &&
			 dm_result(&game.pos) == DM_ONGOING;
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
			for (int i = 0; i < 64; i++) drawn[i] = -1;
			draw_all();
			break;
		case KUI_EV_TAP: {
			if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) { save(); return 0; }
			int id = kui_buttons_hit(buttons, 2, e.x, e.y);
			if (id == BTN_UNDO) {
				undo();
			} else if (id == BTN_NEW) {
				if (ui_yes_no(T("Yeni oyun"), T("Bu oyun bitirilsin mi?"), draw_all)) {
					int r = setup(0);
					if (r != 1) return r;
					start();
				} else {
					draw_all();
				}
			} else {
				tap(cell_at(e.x, e.y));
			}
			break;
		}
		default: break;
		}
		if (kui_quit_pending()) { save(); return UI_QUIT; }
	}
}

/* Reversi screen: tap a square to play; small dots show the legal moves (İpucu on/off). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "reversi.h"
#include "ui.h"

#define CELL (UI_BOARD_W / 8)
enum { BTN_UNDO = 1, BTN_NEW, BTN_HINT };

static RvGame game;
static int over, counted, end_pending, hints = 1;
static int drawn[64];
static KuiButton buttons[3];
static char note[80]; /* "Beyaz pas geçti" until the next move */

static const char *cfg_path(void)
{
	static char p[320];
	snprintf(p, sizeof p, "%s/reversi.cfg", kui_data_dir("masa"));
	return p;
}

static KuiRect cell_rect(int sq) { return (KuiRect){ UI_BOARD_X + (sq & 7) * CELL, UI_BOARD_Y + (sq >> 3) * CELL, CELL, CELL }; }

static int human_turn(void) { return game.mode == 2 || game.pos.side == game.human; }

static int last_move(void)
{
	for (int i = game.n - 1; i >= 0; i--)
		if (game.hist[i] != RV_PASS) return game.hist[i];
	return -1;
}

/* disc + 1 | hint << 2 | last move << 3 */
static int look(int sq)
{
	int d = rv_at(&game.pos, sq) + 1;
	int hint = hints && !over && human_turn() && (rv_moves(&game.pos) >> sq & 1);
	return d | hint << 2 | (sq == last_move()) << 3;
}

static void draw_cell(int sq)
{
	KuiRect r = cell_rect(sq);
	int cx = r.x + CELL / 2, cy = r.y + CELL / 2, d = rv_at(&game.pos, sq);
	kui_fill(r, UI_BOARD_GRAY);
	kui_frame(r, 1, 0x44);
	if (d >= 0) {
		ui_disc(cx, cy, CELL / 2 - 10, d == 1);
		if (sq == last_move()) ui_dot(cx, cy, 11, d == 1 ? 0 : 255);
	} else if (look(sq) & 4) {
		ui_dot(cx, cy, 11, 0x44);
	}
	kui_mark_dirty(r);
	drawn[sq] = look(sq);
}

static const char *side_name(int s) { return s ? T("Beyaz") : T("Siyah"); }

static void status_text(char *out, int cap)
{
	int b = rv_count(&game.pos, 0), w = rv_count(&game.pos, 1);
	if (over) {
		const char *res;
		if (b == w) res = T("Berabere");
		else if (game.mode == 1) res = (b > w) == (game.human == 0) ? T("Kazandınız!") : T("Kindle kazandı");
		else res = b > w ? T("Siyah kazandı") : T("Beyaz kazandı");
		snprintf(out, (size_t) cap, T("Siyah %d – Beyaz %d · %s"), b, w, res);
		return;
	}
	char base[110];
	snprintf(base, sizeof base, T("Sıra: %s · Siyah %d – Beyaz %d"), side_name(game.pos.side), b, w);
	if (note[0]) snprintf(out, (size_t) cap, "%s · %s", note, base);
	else snprintf(out, (size_t) cap, "%s", base);
}

static void draw_status(int force)
{
	char s[200];
	status_text(s, sizeof s);
	ui_status(s, force);
}

static void update_board(void)
{
	for (int sq = 0; sq < 64; sq++)
		if (drawn[sq] != look(sq)) draw_cell(sq);
	draw_status(0);
	kui_flush(0);
}

static void draw_buttons(void)
{
	const char *labels[3] = { T("Geri al"), T("Yeni oyun"), hints ? T("İpucu: açık") : T("İpucu: kapalı") };
	static const int ids[3] = { BTN_UNDO, BTN_NEW, BTN_HINT };
	ui_layout_buttons(buttons, 3, labels, ids);
	for (int i = 0; i < 3; i++) kui_button(&buttons[i], 0);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Reversi"), NULL);
	kui_frame((KuiRect){ UI_BOARD_X - 5, UI_BOARD_Y - 5, UI_BOARD_W + 10, UI_BOARD_W + 10 }, 5, 0);
	for (int sq = 0; sq < 64; sq++) draw_cell(sq);
	draw_buttons();
	draw_status(1);
	kui_flush(1);
}

static void save(void)
{
	char buf[2048];
	int n = rv_game_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(ui_save_path("reversi"), buf, n);
}

/* a side without a move passes at once */
static void auto_pass(void)
{
	note[0] = '\0';
	if (rv_over(&game.pos)) {
		over = end_pending = 1;
		return;
	}
	if (!rv_moves(&game.pos)) {
		snprintf(note, sizeof note, T("%s pas geçti"), side_name(game.pos.side));
		rv_game_play(&game, RV_PASS);
	}
}

static int check_end(void)
{
	if (!over) return 0;
	int b = rv_count(&game.pos, 0), w = rv_count(&game.pos, 1);
	if (game.mode == 1 && !counted) {
		int mine = game.human ? w : b, theirs = game.human ? b : w;
		ui_stats_record("reversi", game.level, mine > theirs ? 1 : mine < theirs ? -1 : 0);
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
		int m = rv_ai(&game.pos, &lim, NULL);
		ui_after_thinking();
		rv_game_play(&game, m);
		auto_pass();
		save();
		update_board();
	}
}

static void play(int sq)
{
	if (over || !human_turn() || sq < 0 || !(rv_moves(&game.pos) >> sq & 1)) return;
	rv_game_play(&game, sq);
	auto_pass();
	save();
	update_board();
	ai_turn();
}

static void undo(void)
{
	if (game.n == 0) return;
	if (game.mode == 1) {
		/* back to the human's turn, taking back the human's last real move (and the Kindle's answers) */
		do {
			rv_game_undo(&game, 1);
		} while (game.n > 0 && !(game.pos.side == game.human && game.hist[game.n] != RV_PASS));
	} else {
		rv_game_undo(&game, 1);
		while (game.n > 0 && game.hist[game.n - 1] == RV_PASS) rv_game_undo(&game, 1);
	}
	over = 0;
	note[0] = '\0';
	save();
	update_board();
	ai_turn();
}

/* returns 1 when a game is ready, 0 back, UI_QUIT */
static int setup(int can_resume)
{
	for (;;) {
		const char *opts[3];
		int n = 0, resume = -1;
		if (can_resume) { resume = n; opts[n++] = T("Devam et"); }
		int one = n;
		opts[n++] = T("Kindle'a karşı");
		opts[n++] = T("İki oyunculu");
		int k = ui_choose(T("Reversi"), T("Siyah başlar · köşeler değerlidir"), opts, n);
		if (k < 0) return k == UI_QUIT ? UI_QUIT : 0;
		if (k == resume) return 1;
		if (k != one) {
			rv_game_new(&game, 2, 2, 0);
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
		rv_game_new(&game, 1, l, c);
		return 1;
	}
}

static void start(void)
{
	over = rv_over(&game.pos);
	counted = over;
	end_pending = 0;
	note[0] = '\0';
	save();
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	ui_status_reset();
	draw_all();
	ai_turn();
}

int reversi_screen(void)
{
	static char buf[2048];
	hints = kui_cfg_get_int(cfg_path(), "hints", 1);
	int can_resume = kui_read_file(ui_save_path("reversi"), buf, sizeof buf) > 0 && rv_game_load(&game, buf) == 0 &&
			 !rv_over(&game.pos);
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
			int id = kui_buttons_hit(buttons, 3, e.x, e.y);
			if (id == BTN_UNDO) {
				undo();
			} else if (id == BTN_HINT) {
				hints = !hints;
				kui_cfg_set_int(cfg_path(), "hints", hints);
				draw_buttons();
				update_board();
			} else if (id == BTN_NEW) {
				if (ui_yes_no(T("Yeni oyun"), T("Bu oyun bitirilsin mi?"), draw_all)) {
					int r = setup(0);
					if (r != 1) return r;
					start();
				} else {
					draw_all();
				}
			} else if (kui_rect_contains((KuiRect){ UI_BOARD_X, UI_BOARD_Y, UI_BOARD_W, UI_BOARD_W }, e.x, e.y)) {
				int col = (e.x - UI_BOARD_X) / CELL, row = (e.y - UI_BOARD_Y) / CELL;
				play(row * 8 + col);
			}
			break;
		}
		default: break;
		}
		if (kui_quit_pending()) { save(); return UI_QUIT; }
	}
}

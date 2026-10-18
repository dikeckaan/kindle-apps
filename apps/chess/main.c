/* Chess for Kindle: single player against Stockfish (6 levels) or two players on one device.
 * Each square remembers what was last drawn on it; after a move only squares whose look changed are redrawn. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "chess_game.h"
#include "kui.h"
#include "levels.h"
#include "uci.h"

#define SQ 128
#define ENGINE_TIMEOUT_MS 10000

enum { BTN_UNDO = 1, BTN_NEW };

static Game game;
static Uci engine;
static int engine_ok, thinking, view_flip, over;
static long think_started;
static char save_path[300], engine_path[300];
static KuiRect board_r, status_r;
static KuiButton buttons[2];
static int selected = -1, last_from = -1, last_to = -1;
static int drawn[64]; /* look of each square as drawn; -1 = unknown */
static char status_drawn[96];

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ---- geometry ---- */

static int bottom_side(void)
{
	int s = game.mode == 1 ? game.human_side : (game.flip ? game.pos.side : 0);
	return view_flip ? !s : s;
}

static KuiRect square_rect(int sq)
{
	int f = sq & 7, r = sq >> 3;
	int col = bottom_side() == 0 ? f : 7 - f, row = bottom_side() == 0 ? 7 - r : r;
	return (KuiRect){ board_r.x + col * SQ, board_r.y + row * SQ, SQ, SQ };
}

static int square_at(int x, int y)
{
	if (!kui_rect_contains(board_r, x, y)) return -1;
	int col = (x - board_r.x) / SQ, row = (y - board_r.y) / SQ;
	int f = bottom_side() == 0 ? col : 7 - col, r = bottom_side() == 0 ? 7 - row : row;
	return r * 8 + f;
}

/* ---- drawing ---- */

static const char *glyph_solid[7] = { "", "♟", "♞", "♝", "♜", "♛", "♚" };
static const char *glyph_outline[7] = { "", "♙", "♘", "♗", "♖", "♕", "♔" };

static int is_target(int sq)
{
	if (selected < 0) return 0;
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&game.pos, ms);
	for (int i = 0; i < n; i++)
		if (ms[i].from == selected && ms[i].to == sq) return 1;
	return 0;
}

/* piece | selected<<5 | target<<6 | last-move<<7 | bottom-side<<8 encodes everything that affects a square's look */
static int square_look(int sq) { return game.pos.sq[sq] | (sq == selected) << 5 | is_target(sq) << 6 | (sq == last_from || sq == last_to) << 7 | bottom_side() << 8; }

static void draw_square(int sq)
{
	KuiRect r = square_rect(sq);
	int dark = ((sq & 7) + (sq >> 3)) % 2 == 0;
	kui_fill(r, dark ? 0xBB : 0xFF);
	if (sq == last_from || sq == last_to) kui_frame((KuiRect){ r.x + 4, r.y + 4, r.w - 8, r.h - 8 }, 3, 0x55);
	int p = game.pos.sq[sq];
	if (p != EMPTY) {
		int c = chess_piece_color(p), t = c ? p - 6 : p;
		if (c == 0) kui_text_on(r, glyph_solid[t], KUI_FONT_REG, 104, KUI_ALIGN_CENTER, 0xFF, dark ? 0xBB : 0xFF); /* white body */
		kui_text(r, c == 0 ? glyph_outline[t] : glyph_solid[t], KUI_FONT_REG, 104, KUI_ALIGN_CENTER, 0);
	}
	if (is_target(sq)) {
		int d = p != EMPTY ? 0 : 28;
		if (d) kui_fill((KuiRect){ r.x + (SQ - d) / 2, r.y + (SQ - d) / 2, d, d }, 0x33);
		else kui_frame((KuiRect){ r.x + 2, r.y + 2, r.w - 4, r.h - 4 }, 6, 0x33); /* capture target */
	}
	if (sq == selected) kui_frame(r, 8, 0);
	kui_mark_dirty(r);
	drawn[sq] = square_look(sq);
}

static void status_text(char *out, int cap)
{
	Result res = game_result(&game);
	if (res != RES_ONGOING) snprintf(out, (size_t) cap, "%s", result_text(res, game.pos.side));
	else if (thinking) snprintf(out, (size_t) cap, "Düşünüyor… (%s)", level_get(game.level)->name);
	else if (chess_in_check(&game.pos, game.pos.side)) snprintf(out, (size_t) cap, "Şah! Sıra: %s", game.pos.side ? "Siyah" : "Beyaz");
	else snprintf(out, (size_t) cap, "Sıra: %s", game.pos.side ? "Siyah" : "Beyaz");
}

static void draw_status(int force)
{
	char s[96];
	status_text(s, sizeof s);
	if (!force && strcmp(s, status_drawn) == 0) return;
	kui_fill(status_r, 255);
	kui_text(status_r, s, KUI_FONT_BOLD, 46, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(status_r);
	snprintf(status_drawn, sizeof status_drawn, "%s", s);
}

/* redraw only squares whose look changed */
static void update_board(void)
{
	for (int sq = 0; sq < 64; sq++)
		if (drawn[sq] != square_look(sq)) draw_square(sq);
	draw_status(0);
	kui_flush(0);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar("Satranç", "Menü");
	board_r = (KuiRect){ (w - 8 * SQ) / 2, KUI_TOPBAR_H + 30, 8 * SQ, 8 * SQ };
	kui_frame((KuiRect){ board_r.x - 4, board_r.y - 4, board_r.w + 8, board_r.h + 8 }, 4, 0);
	status_r = (KuiRect){ 20, board_r.y + board_r.h + 14, w - 40, 80 };
	int by = h - 24 - 120, bw = (w - 3 * 40) / 2;
	buttons[0] = (KuiButton){ { 40, by, bw, 120 }, "Geri al", BTN_UNDO };
	buttons[1] = (KuiButton){ { 80 + bw, by, bw, 120 }, "Yeni oyun", BTN_NEW };
	for (int i = 0; i < 2; i++) kui_button(&buttons[i], 0);
	for (int sq = 0; sq < 64; sq++) draw_square(sq);
	draw_status(1);
	kui_flush(1);
}

/* ---- persistence ---- */

static void save(void)
{
	static char buf[8192];
	int n = game_save(&game, buf, sizeof buf);
	if (n > 0) kui_write_atomic(save_path, buf, n);
}

static void sync_last_move(void)
{
	last_from = game.n ? game.hist[game.n - 1].from : -1;
	last_to = game.n ? game.hist[game.n - 1].to : -1;
}

/* ---- engine ---- */

static void engine_shutdown(void)
{
	if (engine_ok) uci_close(&engine);
	engine_ok = thinking = 0;
}

static int engine_ensure(void)
{
	if (engine_ok) return 1;
	if (uci_start(&engine, engine_path) != 0) return 0;
	if (uci_configure(&engine, level_get(game.level)) != 0) {
		uci_close(&engine);
		return 0;
	}
	engine_ok = 1;
	return 1;
}

static void engine_failed(void);

static void maybe_start_engine(void)
{
	if (game.mode != 1 || over || thinking || game.pos.side == game.human_side) return;
	if (game_result(&game) != RES_ONGOING) return;
	char pos[GAME_MAX_PLIES * 6 + 128];
	game_uci_position(&game, pos, sizeof pos);
	if (!engine_ensure() || uci_go(&engine, pos, level_get(game.level)) != 0) {
		engine_failed();
		return;
	}
	thinking = 1;
	think_started = now_ms();
	draw_status(0);
	kui_flush(0);
}

/* stop a running search and discard its answer */
static void engine_cancel(void)
{
	if (!thinking) return;
	char m[6];
	uci_stop(&engine);
	if (uci_wait_bestmove(&engine, m, 2000) != 1) engine_shutdown();
	thinking = 0;
}

static void engine_failed(void)
{
	engine_shutdown();
	const char *ch[] = { "Tekrar dene", "İki oyunculu devam" };
	int k = kui_dialog("Motor yanıt vermedi", "Satranç motoru çalışmadı.", ch, 2, draw_all);
	draw_all();
	if (k == 1) {
		game.mode = 2;
		save();
		draw_status(1);
		kui_flush(0);
	} else {
		maybe_start_engine();
	}
}

/* ---- game flow ---- */

static void check_end(void)
{
	Result r = game_result(&game);
	if (r == RES_ONGOING) return;
	over = 1;
	const char *ch[] = { "Yeni oyun", "Tahtaya bak" };
	int k = kui_dialog("Oyun bitti", result_text(r, game.pos.side), ch, 2, draw_all);
	draw_all();
	if (k == 0) {
		game_new(&game, game.mode, game.level, game.human_side);
		over = 0;
		sync_last_move();
		save();
		draw_all();
		maybe_start_engine();
	}
}

static void after_move(void)
{
	selected = -1;
	sync_last_move();
	save();
	if (game.mode == 2 && game.flip) for (int i = 0; i < 64; i++) drawn[i] = -1; /* board turns: redraw all squares */
	update_board();
	check_end();
	maybe_start_engine();
}

static int choose_promotion(void)
{
	const char *ch[] = { "Vezir", "Kale", "Fil", "At" };
	static const int promo[] = { WQ, WR, WB, WN };
	int k = kui_dialog("Terfi", "Piyon hangi taşa dönüşsün?", ch, 4, draw_all);
	draw_all();
	return k < 0 ? 0 : promo[k];
}

static void tap_square(int sq)
{
	if (over || thinking || sq < 0) return;
	if (game.mode == 1 && game.pos.side != game.human_side) return;
	int p = game.pos.sq[sq];
	if (selected >= 0 && is_target(sq)) {
		int promo = 0;
		int piece = game.pos.sq[selected];
		if ((piece == WP && (sq >> 3) == 7) || (piece == BP && (sq >> 3) == 0)) {
			promo = choose_promotion();
			if (!promo) return;
		}
		if (game_play(&game, (Move){ (unsigned char) selected, (unsigned char) sq, (unsigned char) promo, 0 }) == 0) {
			after_move();
			return;
		}
	}
	if (p != EMPTY && chess_piece_color(p) == game.pos.side && sq != selected) selected = sq;
	else selected = -1;
	update_board();
}

static void undo(void)
{
	engine_cancel();
	int plies = game.mode == 1 ? (game.pos.side == game.human_side ? 2 : 1) : 1;
	if (game_undo(&game, plies) == 0) return;
	over = 0;
	selected = -1;
	sync_last_move();
	save();
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	update_board();
	maybe_start_engine();
}

/* ---- setup screens ---- */

/* full-screen list of big buttons; returns the chosen index or -1 for ✕ */
static int choose(const char *title, const char **labels, int n)
{
	int w = kui_width();
	KuiButton bs[8];
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

/* returns 1 when a game is ready, 0 to quit */
static int setup(int can_resume)
{
	for (;;) {
		const char *start[3];
		int n = 0, resume_idx = -1;
		if (can_resume) { resume_idx = n; start[n++] = "Devam et"; }
		start[n++] = "Tek oyunculu";
		start[n++] = "İki oyunculu";
		int k = choose("Satranç", start, n);
		if (k < 0) return 0;
		if (k == resume_idx) return 1;
		if (strcmp(start[k], "İki oyunculu") == 0) {
			const char *fl[] = { "Tahta sabit", "Her hamlede tahtayı çevir" };
			int f = choose("İki oyunculu", fl, 2);
			if (f == -2) return 0;
			if (f < 0) continue;
			game_new(&game, 2, 3, 0);
			game.flip = f == 1;
			return 1;
		}
		const char *lv[LEVEL_COUNT];
		for (int i = 0; i < LEVEL_COUNT; i++) lv[i] = level_get(i + 1)->name;
		int l = choose("Zorluk", lv, LEVEL_COUNT);
		if (l == -2) return 0;
		if (l < 0) continue;
		const char *cl[] = { "Beyaz", "Siyah", "Rastgele" };
		int c = choose("Renginiz", cl, 3);
		if (c == -2) return 0;
		if (c < 0) continue;
		if (c == 2) c = (int) (time(NULL) & 1);
		game_new(&game, 1, l + 1, c);
		return 1;
	}
}

int main(void)
{
	if (kui_init("chess") != 0) return 1;
	const char *root = getenv("KUI_ROOT");
	snprintf(save_path, sizeof save_path, "%s/save.txt", kui_data_dir("chess"));
	snprintf(engine_path, sizeof engine_path, "%s/apps/chess/stockfish", root && *root ? root : "/mnt/us/kindle-apps");

	static char buf[8192];
	int can_resume = kui_read_file(save_path, buf, sizeof buf) > 0 && game_load(&game, buf) == 0 &&
			 game_result(&game) == RES_ONGOING;
	if (!setup(can_resume)) {
		kui_quit();
		return 0;
	}
	sync_last_move();
	save();
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	draw_all();
	maybe_start_engine();

	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(thinking ? 100 : -1);
		if (thinking) {
			char m[6];
			int r = uci_poll_bestmove(&engine, m);
			if (r == 1) {
				thinking = 0;
				if (game_play_uci(&game, m) == 0) after_move();
				else engine_failed();
			} else if (r < 0 || now_ms() - think_started > ENGINE_TIMEOUT_MS) {
				engine_failed();
			}
		}
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_WAKE:
			for (int i = 0; i < 64; i++) drawn[i] = -1;
			draw_all();
			break;
		case KUI_EV_TAP: {
			int t = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (t == KUI_BTN_EXIT) { quit = 1; break; }
			if (t == KUI_BTN_MENU) {
				const char *ch[] = { "Tahtayı çevir", "Yeni oyun", "Kapat" };
				int k = kui_dialog("Menü", "", ch, 3, draw_all);
				if (k == 0) view_flip = !view_flip;
				if (k == 1) {
					engine_cancel();
					engine_shutdown();
					over = 0;
					selected = -1;
					if (!setup(0)) { quit = 1; break; }
					sync_last_move();
					save();
				}
				for (int i = 0; i < 64; i++) drawn[i] = -1;
				draw_all();
				maybe_start_engine();
				break;
			}
			int id = kui_buttons_hit(buttons, 2, e.x, e.y);
			if (id == BTN_UNDO) undo();
			else if (id == BTN_NEW) {
				const char *ch[] = { "Evet", "Hayır" };
				if (kui_dialog("Yeni oyun", "Bu oyun bitirilsin mi?", ch, 2, draw_all) == 0) {
					engine_cancel();
					engine_shutdown();
					game_new(&game, game.mode, game.level, game.human_side);
					over = 0;
					selected = -1;
					sync_last_move();
					save();
				}
				for (int i = 0; i < 64; i++) drawn[i] = -1;
				draw_all();
				maybe_start_engine();
			} else {
				tap_square(square_at(e.x, e.y));
			}
			break;
		}
		default: break;
		}
	}
	engine_cancel();
	engine_shutdown();
	save();
	kui_quit();
	return 0;
}

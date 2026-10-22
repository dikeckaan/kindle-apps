/* Chess for Kindle: single player against Stockfish (6 levels), two players on one device, or Lichess puzzles
 * (Bulmacalar, puzzle_ui.c). Each square remembers what was last drawn on it; after a move only squares whose look changed are redrawn.
 * Under the status line a strip shows the latest moves in figurine SAN (♞f3: the same in every language, and no clash
 * between letters such as K = king / Kale); a tap on it opens Hamle geçmişi (history_ui.c). A move tapped there is
 * shown on this board read-only ("inceleme"), stepped with ◀ ▶ or swipes, until "Oyuna dön". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "chess_game.h"
#include "history.h"
#include "history_ui.h"
#include "kui.h"
#include "levels.h"
#include "puzzle_ui.h"
#include "uci.h"

#define SQ 128
#define ENGINE_TIMEOUT_MS 10000

enum { BTN_UNDO = 1, BTN_NEW, BTN_PREV, BTN_NEXT, BTN_LIST, BTN_BACK };

static Game game;
static Uci engine;
static int engine_ok, thinking, view_flip, over;
static long think_started;
static char save_path[300], engine_path[300];
static KuiRect board_r, status_r, strip_r;
static KuiButton buttons[4];
static int n_buttons;
static int review = -1;   /* inceleme: the plies of the position shown (0..game.n); -1 = playing */
static Board review_pos;
static char strip_drawn[420];
static char sans[GAME_MAX_PLIES][HIST_SAN_MAX];
static int selected = -1, last_from = -1, last_to = -1;
static int drawn[64]; /* look of each square as drawn; -1 = unknown */
static char status_drawn[160];

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ---- geometry ---- */

/* the position on the board: the game, or the reviewed one */
static const Board *shown(void) { return review >= 0 ? &review_pos : &game.pos; }

static int bottom_side(void)
{
	int s = game.mode == 1 ? game.human_side : (game.flip && review < 0 ? game.pos.side : 0);
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

/* a filled circle of diameter d centred in r (row by row: kui has no circle primitive) */
static void target_dot(KuiRect r, int d, unsigned char g)
{
	int cx = r.x + r.w / 2, cy = r.y + r.h / 2, R = d / 2;
	for (int dy = -R; dy <= R; dy++) {
		int half = (int) sqrt((double) (R * R - dy * dy));
		kui_fill((KuiRect){ cx - half, cy + dy, 2 * half + 1, 1 }, g);
	}
}

/* Menü → Hamle ipuçları (Satranç and its puzzles share it): dots on the squares the selected piece can reach */
static int hints_on(void)
{
	char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("chess"));
	return kui_cfg_get_int(p, "move_hints", 1);
}

static int show_hints = 1;

static int is_target(int sq)
{
	if (selected < 0 || review >= 0 || !show_hints) return 0;
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&game.pos, ms);
	for (int i = 0; i < n; i++)
		if (ms[i].from == selected && ms[i].to == sq) return 1;
	return 0;
}

/* piece | selected<<5 | target<<6 | last-move<<7 | bottom-side<<8 encodes everything that affects a square's look */
static int square_look(int sq) { return shown()->sq[sq] | (sq == selected) << 5 | is_target(sq) << 6 | (sq == last_from || sq == last_to) << 7 | bottom_side() << 8; }

static void draw_square(int sq)
{
	KuiRect r = square_rect(sq);
	int dark = ((sq & 7) + (sq >> 3)) % 2 == 0;
	kui_fill(r, dark ? 0xBB : 0xFF);
	if (sq == last_from || sq == last_to) kui_frame((KuiRect){ r.x + 4, r.y + 4, r.w - 8, r.h - 8 }, 3, 0x55);
	int p = shown()->sq[sq];
	if (p != EMPTY) {
		int c = chess_piece_color(p), t = c ? p - 6 : p;
		if (c == 0) kui_text_on(r, glyph_solid[t], KUI_FONT_REG, 104, KUI_ALIGN_CENTER, 0xFF, dark ? 0xBB : 0xFF); /* white body */
		kui_text(r, c == 0 ? glyph_outline[t] : glyph_solid[t], KUI_FONT_REG, 104, KUI_ALIGN_CENTER, 0);
	}
	if (is_target(sq)) {
		if (p == EMPTY) target_dot(r, 46, 0x22);
		else kui_frame((KuiRect){ r.x + 3, r.y + 3, r.w - 6, r.h - 6 }, 9, 0x22); /* capture target */
	}
	if (sq == selected) kui_frame(r, 8, 0);
	kui_mark_dirty(r);
	drawn[sq] = square_look(sq);
}

/* "12. ♞f3" or "12… ♞c6" for ply i */
static void move_label(int i, char *out, int cap)
{
	char fig[HIST_SAN_MAX];
	san_figurine(sans[i], fig, sizeof fig);
	snprintf(out, (size_t) cap, history_is_black(&game, i) ? "%d… %s" : "%d. %s", history_move_no(&game, i), fig);
}

static void status_text(char *out, int cap)
{
	if (review >= 0) {
		char mv[48];
		history_sans(&game, sans);
		if (review == 0) snprintf(mv, sizeof mv, "%s", T("başlangıç"));
		else move_label(review - 1, mv, sizeof mv);
		snprintf(out, (size_t) cap, T("İnceleme: %s (%d/%d)"), mv, review, game.n);
		return;
	}
	Result res = game_result(&game);
	const char *color = game.pos.side ? T("Siyah") : T("Beyaz");
	if (res != RES_ONGOING) snprintf(out, (size_t) cap, "%s", T(result_text(res, game.pos.side)));
	else if (thinking) snprintf(out, (size_t) cap, T("Düşünüyor… (%s)"), T(level_get(game.level)->name));
	else if (chess_in_check(&game.pos, game.pos.side)) snprintf(out, (size_t) cap, T("Şah! Sıra: %s"), color);
	else snprintf(out, (size_t) cap, T("Sıra: %s"), color);
}

static void draw_status(int force)
{
	char s[160];
	status_text(s, sizeof s);
	if (!force && strcmp(s, status_drawn) == 0) return;
	kui_fill(status_r, 255);
	kui_text(status_r, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 46, status_r.w), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(status_r);
	snprintf(status_drawn, sizeof status_drawn, "%s", s);
}

/* the moves [from, upto) as one line: "… 12. ♞f3 ♞c6 13. e4" */
static int strip_line(int from, int upto, char *out, int cap)
{
	int o = snprintf(out, (size_t) cap, "%s", from > 0 ? "… " : "");
	for (int i = from; i < upto && o < cap; i++) {
		char fig[HIST_SAN_MAX];
		san_figurine(sans[i], fig, sizeof fig);
		if (!history_is_black(&game, i)) o += snprintf(out + o, (size_t) (cap - o), "%d. ", history_move_no(&game, i));
		else if (i == from) o += snprintf(out + o, (size_t) (cap - o), "%d… ", history_move_no(&game, i));
		if (o < cap) o += snprintf(out + o, (size_t) (cap - o), i + 1 < upto ? "%s " : "%s", fig);
	}
	return o < cap ? o : cap - 1;
}

#define STRIP_PX 42
#define STRIP_MORE_W 150

/* the latest moves up to the shown position; in review the last one (the reviewed move) is underlined */
static void draw_strip(int force)
{
	char s[400], key[420];
	int upto = review >= 0 ? review : game.n, maxw = strip_r.w - 32 - STRIP_MORE_W;
	history_sans(&game, sans);
	int from = upto;
	while (from > 0) {
		strip_line(from - 1, upto, s, sizeof s);
		if (kui_text_width(s, KUI_FONT_REG, STRIP_PX) > maxw) break;
		from--;
	}
	if (from > 0 && from < upto - 1 && history_is_black(&game, from)) from++; /* start with a full move */
	if (upto == 0) snprintf(s, sizeof s, "%s", review >= 0 ? T("Başlangıç konumu") : T("Hamleler burada görünecek"));
	else strip_line(from, upto, s, sizeof s);
	snprintf(key, sizeof key, "%d|%s", review >= 0, s);
	if (!force && strcmp(key, strip_drawn) == 0) return;
	snprintf(strip_drawn, sizeof strip_drawn, "%s", key);
	kui_fill(strip_r, 255);
	kui_frame(strip_r, 2, 0x88);
	KuiRect tr = { strip_r.x + 16, strip_r.y, maxw, strip_r.h };
	kui_text(tr, s, KUI_FONT_REG, STRIP_PX, KUI_ALIGN_LEFT, upto ? 0 : 0x55);
	if (review > 0) {
		char fig[HIST_SAN_MAX];
		san_figurine(sans[review - 1], fig, sizeof fig);
		int fw = kui_text_width(fig, KUI_FONT_REG, STRIP_PX), sw = kui_text_width(s, KUI_FONT_REG, STRIP_PX);
		kui_fill((KuiRect){ tr.x + sw - fw, strip_r.y + strip_r.h - 12, fw, 4 }, 0);
	}
	KuiRect more = { strip_r.x + strip_r.w - STRIP_MORE_W - 16, strip_r.y, STRIP_MORE_W, strip_r.h };
	kui_text(more, T("Tümü ›"), KUI_FONT_BOLD, kui_fit_px(T("Tümü ›"), KUI_FONT_BOLD, 36, STRIP_MORE_W), KUI_ALIGN_RIGHT, 0);
	kui_mark_dirty(strip_r);
}

/* redraw only squares whose look changed */
static void update_board(void)
{
	for (int sq = 0; sq < 64; sq++)
		if (drawn[sq] != square_look(sq)) draw_square(sq);
	draw_status(0);
	draw_strip(0);
	kui_flush(0);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Satranç"), T("Menü"));
	board_r = (KuiRect){ (w - 8 * SQ) / 2, KUI_TOPBAR_H + 30, 8 * SQ, 8 * SQ };
	kui_frame((KuiRect){ board_r.x - 4, board_r.y - 4, board_r.w + 8, board_r.h + 8 }, 4, 0);
	status_r = (KuiRect){ 20, board_r.y + board_r.h + 10, w - 40, 60 };
	strip_r = (KuiRect){ 24, status_r.y + status_r.h + 4, w - 48, 66 };
	int by = h - 20 - 104;
	if (review < 0) {
		int bw = (w - 3 * 40) / 2;
		buttons[0] = (KuiButton){ { 40, by, bw, 104 }, T("Geri al"), BTN_UNDO };
		buttons[1] = (KuiButton){ { 80 + bw, by, bw, 104 }, T("Yeni oyun"), BTN_NEW };
		n_buttons = 2;
	} else {
		buttons[0] = (KuiButton){ { 40, by, 170, 104 }, "◀", BTN_PREV };
		buttons[1] = (KuiButton){ { 230, by, 170, 104 }, "▶", BTN_NEXT };
		buttons[2] = (KuiButton){ { 420, by, 250, 104 }, T("Liste"), BTN_LIST };
		buttons[3] = (KuiButton){ { 690, by, w - 730, 104 }, T("Oyuna dön"), BTN_BACK };
		n_buttons = 4;
	}
	for (int i = 0; i < n_buttons; i++) kui_button(&buttons[i], 0);
	for (int sq = 0; sq < 64; sq++) draw_square(sq);
	draw_status(1);
	draw_strip(1);
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
	const char *ram = kui_ram_copy(engine_path, "stockfish"); /* an update must not kill the engine mid-game */
	if (uci_start(&engine, ram ? ram : engine_path) != 0) return 0;
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
	if (game.mode != 1 || over || thinking || review >= 0 || game.pos.side == game.human_side) return;
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
	const char *ch[] = { T("Tekrar dene"), T("İki oyunculu devam") };
	int k = kui_dialog(T("Motor yanıt vermedi"), T("Satranç motoru çalışmadı."), ch, 2, draw_all);
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
	const char *ch[] = { T("Yeni oyun"), T("Tahtaya bak") };
	int k = kui_dialog(T("Oyun bitti"), T(result_text(r, game.pos.side)), ch, 2, draw_all);
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
	const char *ch[] = { T("Vezir"), T("Kale"), T("Fil"), T("At") };
	static const int promo[] = { WQ, WR, WB, WN };
	int k = kui_dialog(T("Terfi"), T("Piyon hangi taşa dönüşsün?"), ch, 4, draw_all);
	draw_all();
	return k < 0 ? 0 : promo[k];
}

static void tap_square(int sq)
{
	if (over || thinking || review >= 0 || sq < 0) return;
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

/* ---- inceleme (review): a past position, read-only ---- */

static void review_show(int plies)
{
	if (plies < 0) plies = 0;
	if (plies > game.n) plies = game.n;
	review = plies;
	chess_from_fen(&review_pos, game.start_fen);
	for (int i = 0; i < plies; i++) chess_make(&review_pos, game.hist[i]);
	last_from = plies ? game.hist[plies - 1].from : -1;
	last_to = plies ? game.hist[plies - 1].to : -1;
}

static void review_step(int d)
{
	if (review + d < 0 || review + d > game.n) return;
	review_show(review + d);
	update_board();
}

static void review_end(void)
{
	review = -1;
	sync_last_move();
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	draw_all();
	maybe_start_engine();
}

/* Hamle geçmişi, then the tapped move in review; returns 1 when the app should quit */
static int open_history(void)
{
	engine_cancel(); /* restarts when play resumes */
	selected = -1;
	int k = history_ui_run(&game, review > 0 ? review - 1 : -1);
	if (k == -2) return 1;
	if (k >= 0) review_show(k);
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	draw_all();
	if (review < 0) maybe_start_engine();
	return 0;
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
		const char *start[4];
		int n = 0, resume_idx = -1;
		if (can_resume) { resume_idx = n; start[n++] = T("Devam et"); }
		start[n++] = T("Tek oyunculu");
		int two_idx = n;
		start[n++] = T("İki oyunculu");
		int puzzle_idx = n;
		start[n++] = T("Bulmacalar");
		int k = choose(T("Satranç"), start, n);
		if (k < 0) return 0;
		if (k == resume_idx) return 1;
		if (k == puzzle_idx) {
			if (puzzle_run()) return 0;
			continue;
		}
		if (k == two_idx) {
			const char *fl[] = { T("Tahta sabit"), T("Her hamlede tahtayı çevir") };
			int f = choose(T("İki oyunculu"), fl, 2);
			if (f == -2) return 0;
			if (f < 0) continue;
			game_new(&game, 2, 3, 0);
			game.flip = f == 1;
			return 1;
		}
		const char *lv[LEVEL_COUNT];
		for (int i = 0; i < LEVEL_COUNT; i++) lv[i] = T(level_get(i + 1)->name);
		int l = choose(T("Zorluk"), lv, LEVEL_COUNT);
		if (l == -2) return 0;
		if (l < 0) continue;
		const char *cl[] = { T("Beyaz"), T("Siyah"), T("Rastgele") };
		int c = choose(T("Renginiz"), cl, 3);
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
	show_hints = hints_on();
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
		case KUI_EV_SWIPE:
			if (review >= 0 && e.dir == KUI_DIR_LEFT) review_step(1);
			if (review >= 0 && e.dir == KUI_DIR_RIGHT) review_step(-1);
			break;
		case KUI_EV_WAKE:
			for (int i = 0; i < 64; i++) drawn[i] = -1;
			draw_all();
			break;
		case KUI_EV_TAP: {
			int t = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (t == KUI_BTN_EXIT) { quit = 1; break; }
			if (t == KUI_BTN_MENU) {
				const char *ch[] = { T("Tahtayı çevir"), T("Yeni oyun"), T("Bulmacalar"),
						     show_hints ? T("İpuçları: Açık") : T("İpuçları: Kapalı") };
				/* the full move list (Hamle geçmişi) opens with a tap on the move strip */
				int k = kui_dialog(T("Menü"), T("İpuçları: seçtiğiniz taşın gidebileceği kareler"), ch, 4, draw_all);
				if (k >= 0 && review >= 0) {
					review = -1; /* every choice ends the review */
					sync_last_move();
				}
				if (k == 0) view_flip = !view_flip;
				if (k == 3) { /* ✕ closes the menu */
					char sp[300];
					show_hints = !show_hints;
					snprintf(sp, sizeof sp, "%s/settings", kui_data_dir("chess"));
					kui_cfg_set_int(sp, "move_hints", show_hints);
				}
				if (k == 2) {
					engine_cancel(); /* the search would time out meanwhile; it starts again on return */
					selected = -1;
					if (puzzle_run()) { quit = 1; break; }
					show_hints = hints_on();
				}
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
			if (kui_rect_contains(strip_r, e.x, e.y)) {
				if (open_history()) quit = 1;
				break;
			}
			int id = kui_buttons_hit(buttons, n_buttons, e.x, e.y);
			if (id == BTN_PREV) review_step(-1);
			else if (id == BTN_NEXT) review_step(1);
			else if (id == BTN_LIST) {
				if (open_history()) quit = 1;
			} else if (id == BTN_BACK) review_end();
			else if (review >= 0) break;
			else if (id == BTN_UNDO) undo();
			else if (id == BTN_NEW) {
				const char *ch[] = { T("Evet"), T("Hayır") };
				if (kui_dialog(T("Yeni oyun"), T("Bu oyun bitirilsin mi?"), ch, 2, draw_all) == 0) {
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

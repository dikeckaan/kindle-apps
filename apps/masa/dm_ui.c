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

/* one square: piece (DM_*) and UM_* marks; shared by the game and the lessons */
static void draw_square(KuiRect r, int p, int marks)
{
	int cx = r.x + r.w / 2, cy = r.y + r.h / 2, under = -1;
	kui_fill(r, UI_BOARD_GRAY);
	kui_frame(r, 1, 0x44);
	if (marks & UM_LAST) kui_frame((KuiRect){ r.x + 5, r.y + 5, r.w - 10, r.h - 10 }, 2, 0x77); /* thin: the last move */
	if (marks & UM_MUST) kui_frame((KuiRect){ r.x + 3, r.y + 3, r.w - 6, r.h - 6 }, 9, 0x44); /* thick: must capture */
	if (p) {
		int white = dm_color(p) == 0;
		under = white ? 255 : 0;
		ui_disc(cx, cy, r.w / 2 - r.w * 14 / 124, white);
		if (dm_is_king(p)) {
			int gs = r.w * 60 / 124;
			ui_ring(cx, cy, r.w / 2 - r.w * 24 / 124, 4, white ? 0 : 255);
			KuiRect g = { cx - gs / 2, cy - gs / 2, gs, gs };
			if (white) kui_text(g, "♛", KUI_FONT_REG, r.w * 46 / 124, KUI_ALIGN_CENTER, 0);
			else kui_text_on(g, "♛", KUI_FONT_REG, r.w * 46 / 124, KUI_ALIGN_CENTER, 255, 0);
		}
	}
	ui_cell_marks(r, marks & ~(UM_LAST | UM_MUST), under);
}

static void draw_cell(int sq)
{
	int lm = last_mark(sq);
	int marks = (lm == 1 ? UM_LAST : 0) | (lm == 2 && !game.pos.b[sq] ? UM_X : 0) | (must_capture(sq) ? UM_MUST : 0) |
		    (is_via(sq) ? UM_VIA : 0) | (is_target(sq) ? UM_DOT : 0) | (sq == sel ? UM_FRAME : 0);
	draw_square(cell_rect(sq), game.pos.b[sq], marks);
	kui_mark_dirty(cell_rect(sq));
	drawn[sq] = look(sq);
}


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
	char counts[80];
	snprintf(counts, sizeof counts, T("Beyaz %d – Siyah %d"), cnt[0], cnt[1]);
	if (!human_turn())
		snprintf(out, (size_t) cap, "%s\n%s", T("Kindle düşünüyor…"), counts);
	else if (npre)
		snprintf(out, (size_t) cap, "%s\n%s", T("Almaya devam: sıradaki noktalı kareye dokunun"),
			 T("Baştan seçmek için taşın kendisine dokunun"));
	else if (sel >= 0)
		snprintf(out, (size_t) cap, "%s\n%s · %s", T("Taş seçildi: gideceği noktalı kareye dokunun"),
			 T("Başka taş seçmek için ona dokunun"), counts);
	else if (nlegal && legal[0].ncap) {
		char need[80];
		snprintf(need, sizeof need, T("En çok taş alan yol zorunlu: %d taş"), legal[0].ncap);
		snprintf(out, (size_t) cap, "%s\n%s · %s", T("Zorunlu alma var: çerçeveli taşlardan birini seçin"), need, counts);
	} else {
		snprintf(out, (size_t) cap, "%s: %s\n%s", ui_turn_text(game.mode == 1, game.pos.side == 1), T("bir taş seçin"), counts);
	}
}

static void update_board(void)
{
	for (int sq = 0; sq < 64; sq++)
		if (drawn[sq] != look(sq)) draw_cell(sq);
	char s[512];
	status_text(s, sizeof s);
	ui_status(s, 0);
	kui_flush(0);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Dama"), T("Menü"));
	kui_frame((KuiRect){ UI_BOARD_X - 5, UI_BOARD_Y - 5, UI_BOARD_W + 10, UI_BOARD_W + 10 }, 5, 0);
	for (int sq = 0; sq < 64; sq++) draw_cell(sq);
	const char *labels[2] = { T("Geri al"), T("Yeni oyun") };
	static const int ids[2] = { BTN_UNDO, BTN_NEW };
	ui_layout_buttons(buttons, 2, labels, ids);
	for (int i = 0; i < 2; i++) kui_button(&buttons[i], 0);
	char s[512];
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
	char s[512];
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

/* ---- lessons: "Nasıl oynanır?" pages and the scripted "Örnek oyun" ---- */

static int lesson_piece(char c)
{
	const char *k = c ? strchr("wWbB", c) : NULL;
	return k ? DM_WMAN + (int) (k - "wWbB") : DM_EMPTY;
}

static void lesson_cell(const UiPic *pic, int i, KuiRect r) { draw_square(r, pic->piece[i], pic->marks[i]); }

static void howto_page(int k, UiPic *pic) { ui_pic_from_page(&dm_pages[k], 8, lesson_piece, pic); }

/* the position after k scripted moves: start and end of the last move framed, its path ringed, the taken crossed */
static void demo_page(int k, UiPic *pic)
{
	Dm d;
	DmMove m = { 0 };
	dm_init(&d);
	for (int i = 0; i < k && i < dm_ndemo; i++)
		if (dm_parse_move(&d, dm_demo[i].move, &m) == 0) dm_apply(&d, &m);
	memset(pic, 0, sizeof *pic);
	int cnt[2] = { 0, 0 };
	for (int q = 0; q < 64; q++) {
		pic->piece[q] = d.b[q];
		if (d.b[q]) cnt[dm_color(d.b[q])]++;
	}
	if (k > 0) {
		pic->marks[m.from] |= UM_LAST;
		pic->marks[m.path[m.n - 1]] |= UM_LAST;
		for (int i = 0; i + 1 < m.n; i++) pic->marks[m.path[i]] |= UM_VIA;
		for (int i = 0; i < m.ncap; i++) pic->marks[m.cap[i]] |= UM_X;
	}
	snprintf(pic->head, sizeof pic->head, T("Beyaz %d – Siyah %d"), cnt[0], cnt[1]);
	snprintf(pic->text, sizeof pic->text, "%s",
		 k == 0 ? T("Kindle kısa bir oyun oynayacak. Her dokunuşta bir hamle yapılır ve burada anlatılır. Taşın "
			    "çıktığı ve vardığı kare çerçeveli, alınan taşların yeri çarpı ile gösterilir.")
			: T(dm_demo[k - 1].caption));
}

static const UiLesson *howto(void)
{
	static char title[80];
	static UiLesson l;
	snprintf(title, sizeof title, "%s · %s", T("Dama"), T("Nasıl oynanır?"));
	l = (UiLesson){ title, 8, 0, dm_npages, 0, howto_page, lesson_cell };
	return &l;
}

static const UiLesson *demo(void)
{
	static char title[80];
	static UiLesson l;
	snprintf(title, sizeof title, "%s · %s", T("Dama"), T("Örnek oyun"));
	l = (UiLesson){ title, 8, 0, dm_ndemo + 1, 1, demo_page, lesson_cell };
	return &l;
}

static int setup(int can_resume)
{
	for (;;) {
		const char *opts[5];
		int n = 0, resume = -1;
		if (can_resume) { resume = n; opts[n++] = T("Devam et"); }
		int one = n;
		opts[n++] = T("Kindle'a karşı");
		opts[n++] = T("İki oyunculu");
		int learn = n;
		opts[n++] = T("Nasıl oynanır?");
		opts[n++] = T("Örnek oyun");
		int k = ui_choose(T("Dama"), T("Türk daması · almak zorunlu, en çok taşı alan yol seçilir"), opts, n);
		if (k < 0) return k == UI_QUIT ? UI_QUIT : 0;
		if (k == resume) return 1;
		if (k >= learn) {
			if (ui_lesson(k == learn ? howto() : demo()) == UI_QUIT) return UI_QUIT;
			continue;
		}
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
	if (ui_offer_howto("dama", T("Dama"), howto()) == UI_QUIT) return UI_QUIT;
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
			int top = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (top == KUI_BTN_EXIT) { save(); return 0; }
			if (top == KUI_BTN_MENU) {
				if (ui_game_menu(howto(), demo()) == UI_QUIT) { save(); return UI_QUIT; }
				for (int i = 0; i < 64; i++) drawn[i] = -1;
				draw_all();
				break;
			}
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

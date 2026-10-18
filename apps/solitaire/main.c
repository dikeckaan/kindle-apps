/* Klondike solitaire for Kindle: tap a card to select its run, tap a pile to move it there; long-press sends a card
 * to its best place. Only the piles whose picture changed are redrawn and refreshed.
 * Red suits are drawn as outline glyphs (♡ ♢) and black suits filled (♠ ♣): grayscale e-ink has no red. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "solitaire/klondike.h"

#define CW 140          /* card size */
#define CH 200
#define GAP 12
#define FAN 40          /* waste fan offset in draw-3 mode */
#define DOWN_STEP 30    /* tableau offsets */
#define UP_STEP 60
#define HEAD_H 52       /* rank/suit strip at the top of a face-up card */
#define SAVE_MAX (4 << 20)

static Klondike game;
static char save_path[300];
static int sel_pile = -1, sel_idx = -1;
static int auto_offered;
static int x0, top_y, tab_y;

/* what is on screen for each pile, so only changed piles get redrawn */
typedef struct { unsigned char n, down, c[KL_MAXPILE]; int sel, extra; } PileView;
static PileView shown[KL_PILES];
static int shown_valid;

static const char *RANK_TXT[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
static const char *SUIT_TXT[4] = { "♠", "♡", "♢", "♣" }; /* hollow = red */

static int col_x(int i) { return x0 + i * (CW + GAP); }

static void layout(void)
{
	x0 = (kui_width() - (7 * CW + 6 * GAP)) / 2;
	top_y = KUI_TOPBAR_H + 16;
	tab_y = top_y + CH + 24;
}

static KuiRect pile_rect(int p)
{
	if (p == KL_STOCK) return (KuiRect){ col_x(0), top_y, CW, CH };
	if (p == KL_WASTE) return (KuiRect){ col_x(1), top_y, CW + 2 * FAN, CH };
	if (p < KL_T0) return (KuiRect){ col_x(3 + p - KL_F0), top_y, CW, CH };
	return (KuiRect){ col_x(p - KL_T0), tab_y, CW, kui_height() - tab_y };
}

/* tableau offsets for column t, shrunk when the column would run off the bottom */
static void col_steps(int t, int *ds, int *us)
{
	const KlState *s = &game.s;
	int n = s->n[KL_T0 + t], nd = s->down[t], nu = n - nd;
	int avail = kui_height() - 8 - tab_y - CH;
	*ds = DOWN_STEP;
	*us = UP_STEP;
	if (nu > 1 && nd * *ds + (nu - 1) * *us > avail) {
		*us = (avail - nd * *ds) / (nu - 1);
		if (*us < HEAD_H - 6) {
			*us = HEAD_H - 6;
			if (nd) *ds = (avail - (nu - 1) * *us) / nd;
			if (*ds < 10) *ds = 10;
		}
	}
}

static int card_y(int t, int i)
{
	int ds, us, y = tab_y, nd = game.s.down[t];
	col_steps(t, &ds, &us);
	for (int k = 0; k < i; k++) y += k < nd ? ds : us;
	return y;
}

static void draw_back(int x, int y)
{
	KuiRect r = { x, y, CW, CH };
	kui_fill(r, 255);
	kui_frame(r, 3, 0);
	KuiRect in = { x + 10, y + 10, CW - 20, CH - 20 }; /* 120 x 180: whole 20 px squares */
	kui_fill(in, 0xCC);
	for (int yy = 0; yy * 20 < in.h; yy++) /* checkerboard back */
		for (int xx = yy & 1; xx * 20 < in.w; xx += 2) {
			int w = in.w - xx * 20 < 20 ? in.w - xx * 20 : 20, h = in.h - yy * 20 < 20 ? in.h - yy * 20 : 20;
			kui_fill((KuiRect){ in.x + xx * 20, in.y + yy * 20, w, h }, 0x77);
		}
}

/* full: the whole card is visible (big suit in the middle); fanned: only a FAN-wide strip shows (suit under the rank) */
static void draw_face(int x, int y, int c, int selected, int full, int fanned)
{
	KuiRect r = { x, y, CW, CH };
	kui_fill(r, 255);
	kui_frame(r, selected ? 6 : 3, 0);
	unsigned char fg = selected ? 255 : 0, bg = selected ? 0 : 255;
	KuiRect head = { x + 3, y + 3, CW - 6, HEAD_H - 3 };
	if (selected) kui_fill(head, 0);
	const char *rank = RANK_TXT[kl_rank(c) - 1], *suit = SUIT_TXT[kl_suit(c)];
	kui_text_on((KuiRect){ head.x + 6, head.y, 80, head.h }, rank, KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, fg, bg);
	kui_text_on((KuiRect){ head.x + head.w - 56, head.y, 50, head.h }, suit, KUI_FONT_BOLD, 40, KUI_ALIGN_RIGHT, fg, bg);
	if (fanned) kui_text_on((KuiRect){ x + 4, y + HEAD_H, FAN - 6, 44 }, suit, KUI_FONT_BOLD, 34, KUI_ALIGN_LEFT, 0, 255);
	if (full) kui_text_on((KuiRect){ x + 6, y + HEAD_H + 6, CW - 12, CH - HEAD_H - 18 }, suit, KUI_FONT_REG, 96, KUI_ALIGN_CENTER, 0, 255);
}

static void draw_slot(KuiRect r, const char *label)
{
	kui_frame(r, 3, 0xAA);
	if (label) kui_text_on((KuiRect){ r.x + 6, r.y + 6, r.w - 12, r.h - 12 }, label, KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, 0xAA, 255);
}

static PileView view_of(int p)
{
	PileView v;
	memset(&v, 0, sizeof v);
	v.n = game.s.n[p];
	memcpy(v.c, game.s.c[p], v.n);
	if (p >= KL_T0) v.down = game.s.down[p - KL_T0];
	v.sel = sel_pile == p ? sel_idx : -1;
	if (p == KL_STOCK) v.extra = game.s.n[KL_WASTE] > 0;
	if (p == KL_WASTE) v.extra = game.draw;
	return v;
}

static void draw_pile(int p)
{
	const KlState *s = &game.s;
	KuiRect r = pile_rect(p);
	int n = s->n[p];
	kui_fill(r, 255);
	if (p == KL_STOCK) {
		if (n) draw_back(r.x, r.y);
		else draw_slot(r, s->n[KL_WASTE] ? "↻" : NULL);
	} else if (p == KL_WASTE) {
		int k = game.draw == 3 ? (n < 3 ? n : 3) : (n ? 1 : 0);
		if (!k) draw_slot((KuiRect){ r.x, r.y, CW, CH }, NULL);
		for (int i = 0; i < k; i++) {
			int idx = n - k + i;
			draw_face(r.x + i * FAN, r.y, s->c[p][idx], sel_pile == p && idx == n - 1, i == k - 1, i < k - 1);
		}
	} else if (p < KL_T0) {
		if (n) draw_face(r.x, r.y, s->c[p][n - 1], sel_pile == p, 1, 0);
		else draw_slot(r, "A");
	} else {
		int t = p - KL_T0;
		if (!n) draw_slot((KuiRect){ r.x, r.y, CW, CH }, "K");
		for (int i = 0; i < n; i++) {
			int y = card_y(t, i);
			if (i < s->down[t]) draw_back(r.x, y);
			else draw_face(r.x, y, s->c[p][i], sel_pile == p && i >= sel_idx, i == n - 1, 0);
		}
	}
	kui_mark_dirty(r);
	shown[p] = view_of(p);
}

/* redraws only the piles whose picture changed, then a partial refresh */
static void refresh(void)
{
	for (int p = 0; p < KL_PILES; p++) {
		PileView v = view_of(p);
		if (!shown_valid || memcmp(&v, &shown[p], sizeof v) != 0) draw_pile(p);
	}
	shown_valid = 1;
	kui_flush(0);
}

static void paint_all(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar("Solitaire", "Menü");
	for (int p = 0; p < KL_PILES; p++) draw_pile(p);
	shown_valid = 1;
}

static void draw_all(int flash)
{
	paint_all();
	kui_mark_dirty((KuiRect){ 0, 0, kui_width(), kui_height() });
	kui_flush(flash);
}

static void save(void)
{
	int cap = 128 + 120 * (game.nhist + 2);
	if (cap > SAVE_MAX) cap = SAVE_MAX;
	char *buf = malloc((size_t) cap);
	if (!buf) return;
	int n = kl_save(&game, buf, cap);
	if (n > 0) kui_write_atomic(save_path, buf, n);
	free(buf);
}

static void drop_save(void) { remove(save_path); }

/* full-screen list of big buttons; index, -1 for ✕, -2 on quit */
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

/* start screen: 1 = a game is ready, 0 = back to the current game (✕ with a game open), -1 = quit */
static int setup(int can_resume)
{
	const char *start[3];
	int n = 0, resume_idx = -1;
	if (can_resume) { resume_idx = n; start[n++] = "Devam et"; }
	int one = n;
	start[n++] = "Tek kart çek";
	start[n++] = "Üç kart çek";
	int k = choose("Solitaire", start, n);
	if (k == -2) return -1;
	if (k == -1) return 0;
	if (k != resume_idx) {
		kl_new(&game, (unsigned) time(NULL) ^ (unsigned) clock(), k == one ? 1 : 3);
		save();
	}
	sel_pile = sel_idx = -1;
	auto_offered = 0;
	return 1;
}

/* hit test: pile under (x, y) or -1; *idx = card index in it (-1 if none) */
static int hit(int x, int y, int *idx)
{
	const KlState *s = &game.s;
	*idx = -1;
	if (y >= top_y && y < top_y + CH + 12) {
		for (int p = KL_STOCK; p < KL_T0; p++) {
			KuiRect r = pile_rect(p);
			if (p == KL_WASTE) r.w = game.draw == 3 ? CW + 2 * FAN : CW;
			if (x >= r.x - GAP / 2 && x < r.x + r.w + GAP / 2) {
				if (s->n[p]) *idx = s->n[p] - 1;
				return p;
			}
		}
		return -1;
	}
	if (y < tab_y - 12) return -1;
	int t = (x - x0 + GAP / 2) / (CW + GAP);
	if (x < x0 - GAP || t < 0 || t > 6) return -1;
	int p = KL_T0 + t;
	for (int i = s->n[p] - 1; i >= 0; i--) {
		int cy = card_y(t, i);
		if (y >= cy && (y < cy + CH || i < s->n[p] - 1)) {
			*idx = i;
			break;
		}
	}
	return p;
}

static void redraw_bg(void) { paint_all(); }

static int finish_won(void) /* 1 to quit */
{
	drop_save();
	const char *ch[] = { "Yeni oyun", "Çıkış" };
	int k = kui_dialog("Tebrikler!", "Bütün kartları yerleştirdin.", ch, 2, redraw_bg);
	if (k != 0) return 1;
	int r = setup(0);
	if (r <= 0) return 1;
	draw_all(1);
	return 0;
}

static void run_autocomplete(void)
{
	sel_pile = sel_idx = -1;
	while (kl_autocomplete_step(&game)) refresh();
	save();
}

/* after any change: save, then win / auto-complete offers; returns 1 to quit */
static int after_change(void)
{
	save();
	if (!kl_can_autocomplete(&game)) auto_offered = 0;
	else if (!auto_offered) {
		auto_offered = 1;
		const char *ch[] = { "Evet", "Hayır" };
		int k = kui_dialog("Otomatik tamamla", "Bütün kartlar açık. Oyun tamamlansın mı?", ch, 2, redraw_bg);
		draw_all(0);
		if (k == 0) run_autocomplete();
	}
	return kl_won(&game) ? finish_won() : 0;
}

static int menu(void) /* 1 to quit */
{
	const char *ch[3] = { "Yeni oyun", "Geri al", "Tamamla" };
	int n = kl_can_autocomplete(&game) ? 3 : 2;
	int k = kui_dialog("Menü", "", ch, n, redraw_bg);
	sel_pile = sel_idx = -1;
	if (k == 0) {
		save();
		int r = setup(1);
		if (r < 0) return 1;
		draw_all(1);
		return 0;
	}
	draw_all(0);
	if (k == 1 && kl_undo(&game)) {
		refresh();
		save();
	}
	if (k == 2) {
		run_autocomplete();
		if (kl_won(&game)) return finish_won();
	}
	return 0;
}

static int on_tap(int x, int y) /* 1 to quit */
{
	int t = kui_topbar_hit(kui_width(), 1, x, y);
	if (t == KUI_BTN_EXIT) return 1;
	if (t == KUI_BTN_MENU) return menu();
	int idx, p = hit(x, y, &idx);
	if (sel_pile >= 0) {
		int from = sel_pile, from_idx = sel_idx;
		sel_pile = sel_idx = -1;
		int moved = p >= 0 && p != from && kl_move(&game, from, from_idx, p);
		refresh();
		return moved ? after_change() : 0;
	}
	if (p == KL_STOCK) {
		if (!kl_draw(&game)) return 0;
		refresh();
		return after_change();
	}
	if (p < 0 || idx < 0) return 0;
	if (p >= KL_T0 && idx < game.s.down[p - KL_T0]) return 0;
	sel_pile = p;
	sel_idx = idx;
	refresh();
	return 0;
}

static int on_long(int x, int y)
{
	int idx, p = hit(x, y, &idx);
	sel_pile = sel_idx = -1;
	int changed = 0;
	if (p == KL_STOCK) changed = kl_draw(&game);
	else if (p >= 0 && idx >= 0 && (p < KL_T0 || idx >= game.s.down[p - KL_T0])) changed = kl_auto_move(&game, p, idx) >= 0;
	refresh();
	return changed ? after_change() : 0;
}

int main(void)
{
	if (kui_init("solitaire") != 0) return 1;
	snprintf(save_path, sizeof save_path, "%s/save.txt", kui_data_dir("solitaire"));
	layout();
	kl_init(&game);
	int can_resume = 0;
	char *buf = malloc(SAVE_MAX);
	if (buf && kui_read_file(save_path, buf, SAVE_MAX) > 0 && kl_load(&game, buf) == 0 && !kl_won(&game)) can_resume = 1;
	free(buf);
	if (setup(can_resume) != 1) {
		kl_free(&game);
		kui_quit();
		return 0;
	}
	draw_all(1);

	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(-1);
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_WAKE: draw_all(1); break;
		case KUI_EV_TAP: quit = on_tap(e.x, e.y); break;
		case KUI_EV_LONGPRESS: quit = on_long(e.x, e.y); break;
		case KUI_EV_SWIPE:
			if (e.dir == KUI_DIR_LEFT && kl_undo(&game)) { /* swipe left = undo */
				sel_pile = sel_idx = -1;
				refresh();
				save();
			}
			break;
		default: break;
		}
	}
	if (!kl_won(&game)) save();
	kl_free(&game);
	kui_quit();
	return 0;
}

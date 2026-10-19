/* Masa Oyunları: Reversi, Türk daması and Go against the Kindle (Kolay/Orta/Zor) or two players on one device.
 * Rules and AI are pure logic in reversi.c, dama.c and go.c; the screens are in rv_ui.c, dm_ui.c and go_ui.c.
 * Each board cell remembers how it was last drawn and only changed cells are redrawn with a partial refresh. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

/* ---- discs ---- */

static void span(int x0, int x1, int y, unsigned char g)
{
	if (x1 >= x0) kui_fill((KuiRect){ x0, y, x1 - x0 + 1, 1 }, g);
}

static int half_width(int r, int dy) { return (int) sqrt((double) r * r - (double) dy * dy + r * 0.8); }

void ui_dot(int cx, int cy, int r, unsigned char g)
{
	for (int dy = -r; dy <= r; dy++) {
		int dx = half_width(r, dy);
		span(cx - dx, cx + dx, cy + dy, g);
	}
}

void ui_ring(int cx, int cy, int r, int t, unsigned char g)
{
	int ri = r - t;
	for (int dy = -r; dy <= r; dy++) {
		int dx = half_width(r, dy);
		if (abs(dy) > ri) {
			span(cx - dx, cx + dx, cy + dy, g);
		} else {
			int di = half_width(ri, dy);
			span(cx - dx, cx - di - 1, cy + dy, g);
			span(cx + di + 1, cx + dx, cy + dy, g);
		}
	}
}

void ui_disc(int cx, int cy, int r, int white)
{
	ui_dot(cx, cy, r, 0);
	if (white) ui_dot(cx, cy, r - 5, 255);
}

/* ---- screens ---- */

int ui_choose(const char *title, const char *subtitle, const char **labels, int n)
{
	int w = kui_width();
	KuiButton bs[8];
	if (n > 6) n = 6;
	int top = KUI_TOPBAR_H + (subtitle && *subtitle ? 150 : 70);
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(title, NULL);
		if (subtitle && *subtitle)
			kui_text((KuiRect){ 60, KUI_TOPBAR_H + 20, w - 120, 110 }, subtitle, KUI_FONT_REG,
				 kui_fit_px(subtitle, KUI_FONT_REG, 40, w - 120), KUI_ALIGN_CENTER, 0);
		for (int i = 0; i < n; i++) {
			bs[i] = (KuiButton){ { 120, top + i * 180, w - 240, 150 }, labels[i], i + 1 };
			kui_button(&bs[i], 0);
		}
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return UI_QUIT;
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

void ui_layout_buttons(KuiButton *bs, int n, const char **labels, const int *ids)
{
	int w = kui_width(), gap = 30, bw = (w - 2 * 40 - (n - 1) * gap) / n, y = kui_height() - 24 - UI_BTN_H;
	for (int i = 0; i < n; i++) bs[i] = (KuiButton){ { 40 + i * (bw + gap), y, bw, UI_BTN_H }, labels[i], ids[i] };
}

static char status_drawn[512];

void ui_status_reset(void) { status_drawn[0] = '\0'; }

void ui_status(const char *s, int force)
{
	if (!force && strcmp(s, status_drawn) == 0) return;
	KuiRect r = { 20, UI_STATUS_Y, kui_width() - 40, UI_STATUS_H };
	kui_fill(r, 255);
	const char *nl = strchr(s, '\n');
	if (!nl) {
		kui_text(r, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 44, r.w), KUI_ALIGN_CENTER, 0);
	} else {
		char first[200];
		snprintf(first, sizeof first, "%.*s", (int) (nl - s), s);
		KuiRect a = { r.x, r.y, r.w, 74 }, b = { r.x, r.y + 74, r.w, r.h - 74 };
		kui_text(a, first, KUI_FONT_BOLD, kui_fit_px(first, KUI_FONT_BOLD, 44, r.w), KUI_ALIGN_CENTER, 0);
		kui_text(b, nl + 1, KUI_FONT_REG, kui_fit_px(nl + 1, KUI_FONT_REG, 36, r.w), KUI_ALIGN_CENTER, 0x22);
	}
	kui_mark_dirty(r);
	snprintf(status_drawn, sizeof status_drawn, "%s", s);
}

const char *ui_turn_text(int vs_kindle, int black)
{
	static char s[80];
	if (vs_kindle) snprintf(s, sizeof s, T("Sıra sizde (%s)"), black ? T("Siyah") : T("Beyaz"));
	else snprintf(s, sizeof s, "%s", black ? T("Sıra Siyah'ta") : T("Sıra Beyaz'da"));
	return s;
}

/* ---- board cell marks ---- */

int ui_mark_bits(char c)
{
	switch (c) {
	case '*': return UM_FRAME;
	case '@': return UM_FRAME | UM_DOT;
	case 'o': return UM_DOT;
	case 'n': return UM_LAST;
	case 'c': return UM_HL;
	case 'x': return UM_X;
	case '#': return UM_MUST;
	case 'v': return UM_VIA;
	case 'l': return UM_LIB;
	case 't': return UM_TERR_B;
	case 'u': return UM_TERR_W;
	default: return 0;
	}
}

/* a diagonal cross of square dots: works on any background */
static void cross(int cx, int cy, int half, int t, unsigned char g)
{
	for (int i = -half; i <= half; i++) {
		kui_fill((KuiRect){ cx + i - t / 2, cy + i - t / 2, t, t }, g);
		kui_fill((KuiRect){ cx + i - t / 2, cy - i - t / 2, t, t }, g);
	}
}

void ui_cell_marks(KuiRect r, int m, int under)
{
	int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
	if (m & UM_HL) kui_frame((KuiRect){ r.x + 6, r.y + 6, r.w - 12, r.h - 12 }, 5, 0x11);
	if (m & UM_X) {
		int half = r.w * 27 / 100, t = r.w / 11;
		if (under < 0) {
			cross(cx, cy, half + 2, t + 4, UI_BOARD_GRAY); /* a gap around it so it stands off grid lines */
			cross(cx, cy, half, t, 0x11);
		} else {
			cross(cx, cy, half, t, under ? 0 : 255);
		}
	}
	if (m & UM_VIA) ui_ring(cx, cy, r.w * 26 / 124, 6, 0x22);
	if (m & UM_LIB) {
		ui_dot(cx, cy, r.w / 6, 255);
		ui_ring(cx, cy, r.w / 6, 4, 0x22);
	}
	if (m & UM_DOT) ui_dot(cx, cy, r.w * 15 / 124, 0x22);
	if (m & UM_FRAME) kui_frame(r, 8, 0);
}

/* ---- lessons ---- */

void ui_pic_from_page(const MasaPage *p, int n, int (*piece)(char), UiPic *pic)
{
	memset(pic, 0, sizeof *pic);
	for (int i = 0; i < n * n; i++) {
		pic->piece[i] = (unsigned char) piece(p->board[i / n][i % n]);
		pic->marks[i] = (unsigned short) (p->marks[0] ? ui_mark_bits(p->marks[i / n][i % n]) : 0);
	}
	snprintf(pic->head, sizeof pic->head, "%s", T(p->head));
	snprintf(pic->text, sizeof pic->text, "%s", T(p->text));
}

enum { LS_PREV = 1, LS_MID, LS_NEXT };
#define LS_HEAD_Y (KUI_TOPBAR_H + 8)
#define LS_HEAD_H 70
#define LS_BOARD_Y (LS_HEAD_Y + LS_HEAD_H + 10)
#define LS_LABEL 44
#define LS_TEXT_X 50

typedef struct {
	const UiLesson *l;
	int k, cell, bx, by, bw;
	int drawn[UI_PIC_MAX];
	UiPic pic;
	KuiButton bs[3];
} Lesson;

static KuiRect ls_cell(const Lesson *s, int i)
{
	return (KuiRect){ s->bx + (i % s->l->n) * s->cell, s->by + (i / s->l->n) * s->cell, s->cell, s->cell };
}

static void ls_labels(const Lesson *s)
{
	static const char go_letters[] = "ABCDEFGHJKLMN"; /* Go boards skip I */
	int n = s->l->n;
	for (int i = 0; i < n; i++) {
		char t[12];
		snprintf(t, sizeof t, "%c", s->l->go ? go_letters[i] : 'a' + i);
		KuiRect below = { s->bx + i * s->cell, s->by + s->bw + 6, s->cell, LS_LABEL - 6 };
		kui_text(below, t, KUI_FONT_BOLD, 32, KUI_ALIGN_CENTER, 0x33);
		snprintf(t, sizeof t, "%d", n - i);
		KuiRect left = { s->bx - LS_LABEL - 4, s->by + i * s->cell, LS_LABEL, s->cell };
		kui_text(left, t, KUI_FONT_BOLD, 32, KUI_ALIGN_CENTER, 0x33);
	}
}

static int ls_text_bottom(void) { return kui_height() - 24 - UI_BTN_H - 24; }

static void ls_text(const Lesson *s)
{
	int w = kui_width() - 2 * LS_TEXT_X, top = s->by + s->bw + LS_LABEL + 10, h = ls_text_bottom() - top;
	KuiRect box = { LS_TEXT_X - 10, top, w + 20, h };
	kui_fill(box, 255);
	KuiLine lines[16];
	int px = 40, n = 0, lh = 0;
	for (; px >= 28; px -= 2) {
		n = kui_wrap_text(s->pic.text, w, KUI_FONT_REG, px, lines, 16);
		lh = kui_line_height(KUI_FONT_REG, px);
		if (n * lh <= h) break;
	}
	int y = top + 6; /* top-aligned: the eye finds the text at the same place on every page */
	for (int i = 0; i < n; i++) {
		char line[400];
		snprintf(line, sizeof line, "%.*s", lines[i].len, s->pic.text + lines[i].off);
		kui_text((KuiRect){ LS_TEXT_X, y + i * lh, w, lh }, line, KUI_FONT_REG, px, KUI_ALIGN_LEFT, 0);
	}
	kui_mark_dirty(box);
}

static void ls_head(const Lesson *s)
{
	KuiRect r = { 20, LS_HEAD_Y, kui_width() - 40, LS_HEAD_H };
	char h[320];
	if (s->l->demo) {
		char step[80];
		if (s->k == 0) snprintf(step, sizeof step, "%s", T("Başlangıç"));
		else snprintf(step, sizeof step, T("Hamle %d / %d"), s->k, s->l->count - 1);
		if (s->pic.head[0]) snprintf(h, sizeof h, "%s · %s", step, s->pic.head);
		else snprintf(h, sizeof h, "%s", step);
	} else {
		snprintf(h, sizeof h, "%d/%d · %s", s->k + 1, s->l->count, s->pic.head);
	}
	kui_fill(r, 255);
	kui_text(r, h, KUI_FONT_BOLD, kui_fit_px(h, KUI_FONT_BOLD, 46, r.w), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
}

static void ls_buttons(Lesson *s)
{
	int w = kui_width(), y = kui_height() - 24 - UI_BTN_H, last = s->k == s->l->count - 1;
	KuiRect row = { 0, y, w, UI_BTN_H };
	kui_fill(row, 255);
	const char *mid = s->l->demo ? T("Baştan") : T("Kapat");
	const char *next = last ? T("Bitti") : s->l->demo ? T("Sonraki hamle ▶") : T("Sonraki ▶");
	s->bs[0] = (KuiButton){ { 40, y, 230, UI_BTN_H }, T("◀ Geri"), LS_PREV };
	s->bs[1] = (KuiButton){ { 300, y, 230, UI_BTN_H }, mid, LS_MID };
	s->bs[2] = (KuiButton){ { 560, y, w - 600, UI_BTN_H }, next, LS_NEXT };
	if (s->k > 0) kui_button(&s->bs[0], 0);
	if (!s->l->demo || s->k > 0) kui_button(&s->bs[1], 0);
	kui_button(&s->bs[2], 0);
	kui_mark_dirty(row);
}

static void ls_draw(Lesson *s, int full)
{
	const UiLesson *l = s->l;
	l->page(s->k, &s->pic);
	if (full) {
		kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
		kui_topbar(l->title, NULL);
		kui_frame((KuiRect){ s->bx - 5, s->by - 5, s->bw + 10, s->bw + 10 }, 5, 0);
		ls_labels(s);
		for (int i = 0; i < UI_PIC_MAX; i++) s->drawn[i] = -1;
	}
	for (int i = 0; i < l->n * l->n; i++) {
		int look = s->pic.piece[i] | s->pic.marks[i] << 8;
		if (s->drawn[i] == look) continue;
		KuiRect r = ls_cell(s, i);
		l->cell(&s->pic, i, r);
		kui_mark_dirty(r);
		s->drawn[i] = look;
	}
	ls_head(s);
	ls_text(s);
	ls_buttons(s);
	kui_flush(full);
}

int ui_lesson(const UiLesson *l)
{
	static Lesson s;
	memset(&s, 0, sizeof s);
	s.l = l;
	s.cell = (l->n >= 9 ? 765 : 768) / l->n;
	s.bw = s.cell * l->n;
	s.bx = (kui_width() - s.bw) / 2 + LS_LABEL / 2;
	s.by = LS_BOARD_Y;
	ls_draw(&s, 1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		int step = 0;
		if (e.type == KUI_EV_QUIT) return UI_QUIT;
		if (e.type == KUI_EV_WAKE) { ls_draw(&s, 1); continue; }
		if (e.type == KUI_EV_SWIPE) step = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
			int id = kui_buttons_hit(s.bs, 3, e.x, e.y);
			if (id == LS_PREV && s.k > 0) step = -1;
			if (id == LS_NEXT || kui_rect_contains((KuiRect){ s.bx, s.by, s.bw, s.bw }, e.x, e.y)) step = 1;
			if (id == LS_MID) {
				if (!l->demo) return 0;
				if (s.k > 0) step = -s.k; /* Baştan */
			}
		}
		if (!step) continue;
		if (s.k + step >= l->count) return 0; /* Bitti */
		if (s.k + step < 0) continue;
		s.k += step;
		ls_draw(&s, 0);
		if (kui_quit_pending()) return UI_QUIT;
	}
}

static const char *learn_path(void)
{
	static char p[320];
	snprintf(p, sizeof p, "%s/learn", kui_data_dir("masa"));
	return p;
}

static void blank_screen(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar(T("Masa Oyunları"), NULL);
}

int ui_offer_howto(const char *game, const char *name, const UiLesson *howto)
{
	if (kui_cfg_get_int(learn_path(), game, 0)) return 0;
	kui_cfg_set_int(learn_path(), game, 1);
	blank_screen();
	kui_flush(1);
	char msg[300];
	snprintf(msg, sizeof msg, T("%s oyununu ilk kez açıyorsunuz. Nasıl oynandığını resimlerle kısaca gösterelim mi?"), name);
	const char *ch[] = { T("Göster"), T("Gerek yok") };
	if (kui_dialog(name, msg, ch, 2, blank_screen) != 0) return kui_quit_pending() ? UI_QUIT : 0;
	return ui_lesson(howto);
}

int ui_game_menu(const UiLesson *howto, const UiLesson *demo)
{
	const char *items[] = { T("Oyuna dön"), T("Nasıl oynanır?"), T("Örnek oyun") };
	int k = ui_choose(T("Menü"), NULL, items, 3);
	if (k == UI_QUIT) return UI_QUIT;
	if (k == 1) return ui_lesson(howto);
	if (k == 2) return ui_lesson(demo);
	return 0;
}

int ui_yes_no(const char *title, const char *msg, void (*redraw)(void))
{
	const char *ch[] = { T("Evet"), T("Hayır") };
	return kui_dialog(title, msg, ch, 2, redraw) == 0;
}

void ui_thinking(void)
{
	ui_status(T("Kindle düşünüyor…"), 0);
	kui_flush(0);
}

void ui_after_thinking(void)
{
	if (!getenv("KUI_SIM")) kui_drain_input(1); /* taps queued during the search (the simulator's script must stay) */
}

const char *ui_save_path(const char *game)
{
	static char p[4][320];
	static int k;
	k = (k + 1) % 4;
	snprintf(p[k], sizeof p[k], "%s/%s.txt", kui_data_dir("masa"), game);
	return p[k];
}

/* ---- levels and statistics ---- */

static const char *level_names[3] = { N_("Kolay"), N_("Orta"), N_("Zor") };

const char *ui_level_name(int level) { return T(level_names[(level < 1 || level > 3 ? 2 : level) - 1]); }

int ui_choose_level(const char *title)
{
	const char *lv[3] = { T("Kolay"), T("Orta"), T("Zor") };
	int k = ui_choose(title, T("Kindle ne kadar iyi oynasın?"), lv, 3);
	return k < 0 ? k : k + 1;
}

static const char *stats_path(void)
{
	static char p[320];
	snprintf(p, sizeof p, "%s/stats", kui_data_dir("masa"));
	return p;
}

void ui_stats_record(const char *game, int level, int outcome)
{
	char key[64];
	snprintf(key, sizeof key, "%s.%d.%c", game, level, outcome > 0 ? 'w' : outcome < 0 ? 'l' : 'd');
	kui_cfg_set_int(stats_path(), key, kui_cfg_get_int(stats_path(), key, 0) + 1);
}

static void stats_screen(void)
{
	static const char *games[3] = { "reversi", "dama", "go" };
	static const char *names[3] = { N_("Reversi"), N_("Dama"), N_("Go") };
	int w = kui_width();
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(T("İstatistikler"), NULL);
		int y = KUI_TOPBAR_H + 30;
		kui_text((KuiRect){ 60, y, w - 120, 70 }, T("Kindle'a karşı: kazanma · kaybetme · beraberlik"), KUI_FONT_REG,
			 kui_fit_px(T("Kindle'a karşı: kazanma · kaybetme · beraberlik"), KUI_FONT_REG, 36, w - 120), KUI_ALIGN_CENTER, 0);
		y += 100;
		for (int g = 0; g < 3; g++) {
			kui_text((KuiRect){ 60, y, w - 120, 80 }, T(names[g]), KUI_FONT_BOLD, 52, KUI_ALIGN_LEFT, 0);
			kui_fill((KuiRect){ 60, y + 84, w - 120, 3 }, 0);
			y += 100;
			for (int l = 1; l <= 3; l++) {
				char kw[64], kl[64], kd[64], line[160];
				snprintf(kw, sizeof kw, "%s.%d.w", games[g], l);
				snprintf(kl, sizeof kl, "%s.%d.l", games[g], l);
				snprintf(kd, sizeof kd, "%s.%d.d", games[g], l);
				int nw = kui_cfg_get_int(stats_path(), kw, 0), nl = kui_cfg_get_int(stats_path(), kl, 0),
				    nd = kui_cfg_get_int(stats_path(), kd, 0);
				kui_text((KuiRect){ 100, y, 300, 70 }, ui_level_name(l), KUI_FONT_REG, 42, KUI_ALIGN_LEFT, 0);
				snprintf(line, sizeof line, T("%d kazanma · %d kaybetme · %d berabere"), nw, nl, nd);
				kui_text((KuiRect){ 360, y, w - 420, 70 }, line, KUI_FONT_REG, kui_fit_px(line, KUI_FONT_REG, 40, w - 420),
					 KUI_ALIGN_RIGHT, 0);
				y += 76;
			}
			y += 30;
		}
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type == KUI_EV_TAP && kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		}
	}
}

int main(void)
{
	if (kui_init("masa") != 0) return 1;
	for (;;) {
		const char *items[] = { T("Reversi"), T("Dama (Türk daması)"), T("Go"), T("İstatistikler") };
		int k = ui_choose(T("Masa Oyunları"), T("Kindle'a karşı ya da iki kişi aynı cihazda"), items, 4);
		int r = 0;
		if (k < 0) break;
		if (k == 0) r = reversi_screen();
		if (k == 1) r = dama_screen();
		if (k == 2) r = go_screen();
		if (k == 3) stats_screen();
		if (r == UI_QUIT || kui_quit_pending()) break;
	}
	kui_quit();
	return 0;
}

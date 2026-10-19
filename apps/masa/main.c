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

static char status_drawn[200];

void ui_status_reset(void) { status_drawn[0] = '\0'; }

void ui_status(const char *s, int force)
{
	if (!force && strcmp(s, status_drawn) == 0) return;
	KuiRect r = { 20, UI_STATUS_Y, kui_width() - 40, UI_STATUS_H };
	kui_fill(r, 255);
	kui_text(r, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 44, r.w), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
	snprintf(status_drawn, sizeof status_drawn, "%s", s);
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

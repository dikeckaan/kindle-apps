/* Şiir Defteri: classic Turkish poetry, typeset in serif — a poem a day, poets and their poems, a random poem and
 * favourites. Fully offline (the anthology ships with the app); it only waits for touches. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "siir.h"

#define MARGIN 80
#define BAR_Y (kui_height() - 115) /* bottom button row */
#define BAR_H 95
#define MAX_ITEMS 1600

static SiirDb db;
static SiirFavs favs;
static unsigned rnd_count;

/* ---- files ---- */

static const char *favs_path(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/favs", kui_data_dir("siir"));
	return p;
}

static void load_favs(void)
{
	static char buf[200000];
	siir_favs_parse(kui_read_file(favs_path(), buf, sizeof buf) > 0 ? buf : "", &favs);
}

static void save_favs(void)
{
	static char buf[200000];
	int n = siir_favs_format(&favs, buf, sizeof buf);
	if (n >= 0) kui_write_atomic(favs_path(), buf, n);
}

static int load_db(void)
{
	const char *root = getenv("KUI_ROOT");
	char p[300];
	snprintf(p, sizeof p, "%s/apps/siir/poems.txt", root && *root ? root : "/mnt/us/kindle-apps");
	if (siir_load(p, &db) > 0) return 0;
	return siir_load("apps/siir/data/poems.txt", &db) > 0 ? 0 : -1; /* development tree (simulator) */
}

static int today_index(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return siir_daily(siir_days(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday), db.n_poems);
}

static int random_index(int avoid) { return siir_random((unsigned) time(NULL) ^ (++rnd_count * 0x9E3779B9u), db.n_poems, avoid); }

/* ---- drawing helpers ---- */

static void draw_n(KuiRect r, const char *s, int len, KuiFont f, int px, KuiAlign a, unsigned char fg)
{
	char buf[1024];
	if (len > (int) sizeof buf - 1) len = (int) sizeof buf - 1;
	memcpy(buf, s, (size_t) len);
	buf[len] = '\0';
	kui_text(r, buf, f, px, a, fg);
}

/* wraps text to w and (when draw) draws at most max lines from y; returns the height used */
static int draw_block(int x, int y, int w, const char *text, KuiFont f, int px, KuiAlign a, int max, unsigned char fg, int draw)
{
	KuiLine ls[12];
	int n = kui_wrap_text(text, w, f, px, ls, 12), lh = kui_line_height(f, px);
	if (n > max) n = max;
	for (int i = 0; i < n && draw; i++) draw_n((KuiRect){ x, y + i * lh, w, lh }, text + ls[i].off, ls[i].len, f, px, a, fg);
	return n * lh;
}

static void flush_screen(int full)
{
	if (full) {
		kui_flush(1);
	} else {
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, kui_width(), kui_height() - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

static void page_label(int pg, int pages, KuiRect r)
{
	char s[32];
	snprintf(s, sizeof s, "%d / %d", pg + 1, pages);
	kui_text(r, s, KUI_FONT_SERIF, 32, KUI_ALIGN_CENTER, 0);
}

/* ---- poem layout ---- */

typedef struct { const char *s; int len, x; } Piece; /* drawing data for an item */
static SiirItem items[MAX_ITEMS];
static Piece pieces[MAX_ITEMS];
static int n_items, body_px, body_x;

/* lays the poem's lines out at px; returns how many verse lines had to wrap */
static int layout(const char *text, int px, int tw)
{
	static KuiLine ls[64];
	int lh = kui_line_height(KUI_FONT_SERIF, px), gap = lh * 3 / 5, wrapped = 0, indent = px + 10, widest = 0;
	n_items = 0;
	for (const char *p = text; *p && n_items < MAX_ITEMS - 64;) {
		const char *e = strchr(p, '\n');
		int len = e ? (int) (e - p) : (int) strlen(p);
		if (!len) {
			items[n_items] = (SiirItem){ SIIR_GAP, gap };
			pieces[n_items++] = (Piece){ "", 0, 0 };
		} else {
			char line[1024];
			int l = len < (int) sizeof line - 1 ? len : (int) sizeof line - 1;
			memcpy(line, p, (size_t) l);
			line[l] = '\0';
			int k = kui_wrap_text(line, tw, KUI_FONT_SERIF, px, ls, 64);
			if (k > 1) { /* continuation lines get a hanging indent: re-wrap the rest narrower */
				wrapped++;
				int first = ls[0].len;
				items[n_items] = (SiirItem){ SIIR_LINE, lh };
				pieces[n_items++] = (Piece){ p, first, 0 };
				const char *rest = line + ls[1].off;
				k = kui_wrap_text(rest, tw - indent, KUI_FONT_SERIF, px, ls, 64);
				for (int i = 0; i < k && n_items < MAX_ITEMS; i++) {
					items[n_items] = (SiirItem){ SIIR_CONT, lh };
					pieces[n_items++] = (Piece){ p + (rest - line) + ls[i].off, ls[i].len, indent };
				}
				widest = tw;
			} else {
				int wd = kui_text_width(line, KUI_FONT_SERIF, px);
				if (wd > widest) widest = wd;
				items[n_items] = (SiirItem){ SIIR_LINE, lh };
				pieces[n_items++] = (Piece){ p, len, 0 };
			}
		}
		p = e ? e + 1 : p + len;
	}
	/* centre the block of verse on the page by its widest line (an anthology page), never past the margins */
	body_x = (kui_width() - widest) / 2;
	if (body_x < MARGIN) body_x = MARGIN;
	if (body_x > kui_width() / 4) body_x = kui_width() / 4;
	return wrapped;
}

/* picks the largest size at which few lines wrap, then lays the poem out at it */
static void layout_fit(const char *text, int tw)
{
	static const int sizes[] = { 40, 37, 34, 31 };
	int verse = 1;
	for (const char *p = text; *p; p++) verse += *p == '\n' && p[1] && p[1] != '\n';
	for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
		body_px = sizes[i];
		if (layout(text, body_px, tw) * 12 <= verse) return; /* at most one line in twelve wraps */
	}
}

/* the header above the poem on its first page; draws it when draw != 0, returns its height */
static int header(const SiirPoem *p, int y0, int draw)
{
	int w = kui_width(), tw = w - 2 * MARGIN, y = y0;
	const SiirAuthor *a = &db.authors[p->author];
	KuiLine ls[4];
	int tpx = 52, n = kui_wrap_text(p->title, tw, KUI_FONT_SERIF_BOLD, tpx, ls, 4);
	if (n > 2) n = kui_wrap_text(p->title, tw, KUI_FONT_SERIF_BOLD, tpx = 44, ls, 4);
	if (n > 3) n = 3;
	int lh = kui_line_height(KUI_FONT_SERIF_BOLD, tpx);
	for (int i = 0; i < n; i++, y += lh)
		if (draw) draw_n((KuiRect){ MARGIN, y, tw, lh }, p->title + ls[i].off, ls[i].len, KUI_FONT_SERIF_BOLD, tpx, KUI_ALIGN_CENTER, 0);
	y += 12;
	char up[160], by[240];
	kui_tr_upper_str(a->name, up, sizeof up);
	snprintf(by, sizeof by, a->years[0] ? "%s  ·  %s" : "%s", up, a->years);
	int bpx = kui_fit_px(by, KUI_FONT_SERIF, 28, tw);
	if (draw) kui_text((KuiRect){ MARGIN, y, tw, 44 }, by, KUI_FONT_SERIF, bpx, KUI_ALIGN_CENTER, 0x30);
	y += 44 + 26;
	if (draw) kui_fill((KuiRect){ w / 2 - 70, y, 140, 2 }, 0);
	return y + 2 + 44 - y0;
}

enum { PV_BACK, PV_MENU, PV_QUIT };

/* Shows poem *idx. main_view: the opening screen (Menü at the top right, a Rastgele button, ✕ leaves the app).
 * *label is the top bar title and changes to "Rastgele" when a random poem is drawn. */
static int poem_view(int *idx, const char **label, int main_view)
{
	int w = kui_width(), tw = w - 2 * MARGIN, top = KUI_TOPBAR_H + 46, bottom = BAR_Y - 44;
	static int starts[64];
	for (;;) {
		const SiirPoem *p = &db.poems[*idx];
		const SiirAuthor *a = &db.authors[p->author];
		layout_fit(p->text, tw);
		int run_h = 64, head_h = header(p, top, 0);
		int pages = siir_paginate(items, n_items, bottom - top - head_h, bottom - top - run_h, starts, 64);
		if (pages < 1) pages = 1, starts[0] = 0;
		KuiButton prev = { { 20, BAR_Y, 130, BAR_H }, "◀", 1 }, next = { { w - 150, BAR_Y, 130, BAR_H }, "▶", 2 };
		KuiButton fav = { { 170, BAR_Y, 130, BAR_H }, "", 3 }, rnd = { { w - 170 - 270, BAR_Y, 270, BAR_H }, T("Rastgele"), 4 };
		if (pages == 1 && !main_view) fav.r.x = (w - fav.r.w) / 2; /* alone on the bar: centred */
		int pg = 0, full = 1, redraw = 1, next_poem = 0;
		while (!next_poem) {
			if (redraw) {
				kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
				kui_topbar(*label, main_view ? T("Menü") : NULL);
				int y = top;
				if (pg == 0) {
					y += header(p, top, 1);
				} else { /* running head */
					char s[400];
					snprintf(s, sizeof s, "%s  —  %s", p->title, a->name);
					kui_text((KuiRect){ MARGIN, top - 16, tw, 40 }, s, KUI_FONT_SERIF, kui_fit_px(s, KUI_FONT_SERIF, 26, tw), KUI_ALIGN_CENTER, 0x50);
					y += run_h;
				}
				int end = pg + 1 < pages ? starts[pg + 1] : n_items;
				for (int i = starts[pg]; i < end; i++) {
					if (items[i].kind == SIIR_GAP && i == starts[pg]) continue;
					if (items[i].kind != SIIR_GAP)
						draw_n((KuiRect){ body_x + pieces[i].x, y, w - MARGIN - body_x - pieces[i].x, items[i].h }, pieces[i].s, pieces[i].len,
						       KUI_FONT_SERIF, body_px, KUI_ALIGN_LEFT, 0);
					y += items[i].h;
				}
				kui_fill((KuiRect){ MARGIN, BAR_Y - 16, tw, 1 }, 0);
				fav.label = siir_favs_has(&favs, p->url) ? "★" : "☆";
				kui_button(&fav, 0);
				if (main_view) kui_button(&rnd, 0);
				if (pages > 1) {
					kui_button(&prev, 0);
					kui_button(&next, 0);
					int x0 = fav.r.x + fav.r.w, x1 = main_view ? rnd.r.x : next.r.x;
					page_label(pg, pages, (KuiRect){ x0, BAR_Y, x1 - x0, BAR_H });
				}
				flush_screen(full);
				full = redraw = 0;
			}
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return PV_QUIT;
			if (e.type == KUI_EV_WAKE) {
				full = redraw = 1;
				continue;
			}
			int dlt = 0;
			if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
			if (e.type == KUI_EV_TAP) {
				int hit = kui_topbar_hit(w, main_view, e.x, e.y);
				if (hit == KUI_BTN_EXIT) return PV_BACK;
				if (hit == KUI_BTN_MENU) return PV_MENU;
				KuiButton bs[4] = { prev, next, fav, rnd };
				int id = kui_buttons_hit(bs, main_view ? 4 : 3, e.x, e.y);
				if ((id == 1 || id == 2) && pages > 1) dlt = id == 1 ? -1 : 1;
				if (id == 3) { /* favourite toggle: only the button changes (partial refresh) */
					if (siir_favs_toggle(&favs, p->url) >= 0) save_favs();
					fav.label = siir_favs_has(&favs, p->url) ? "★" : "☆";
					kui_button(&fav, 0);
					kui_flush(0);
				}
				if (id == 4) {
					*idx = random_index(*idx);
					*label = T("Rastgele");
					next_poem = 1;
				}
			}
			if (dlt && pg + dlt >= 0 && pg + dlt < pages) {
				pg += dlt;
				redraw = 1; /* page turn: partial refresh */
			}
		}
	}
}

/* ---- lists: poets, a poet's poems, favourites ---- */

typedef struct {
	const char *title;
	int n, row_h;
	void (*row)(int i, KuiRect r); /* draws row i */
	int (*head)(int y, int draw);  /* optional block above the rows (every page); returns its height */
} ListDef;

/* returns the tapped row, -1 on ✕, -2 on quit; *page is kept between calls */
static int list_screen(const ListDef *L, int *page, int full)
{
	int w = kui_width(), h = kui_height();
	int head_h = L->head ? L->head(KUI_TOPBAR_H + 20, 0) : 0, y0 = KUI_TOPBAR_H + 20 + head_h;
	int per = (BAR_Y - 20 - y0) / L->row_h;
	if (per < 1) per = 1;
	int pages = L->n ? (L->n + per - 1) / per : 1;
	if (*page >= pages) *page = pages - 1;
	KuiButton prev = { { 20, BAR_Y, 130, BAR_H }, "◀", 1 }, next = { { w - 150, BAR_Y, 130, BAR_H }, "▶", 2 };
	for (int redraw = 1;;) {
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(L->title, NULL);
			if (L->head) L->head(KUI_TOPBAR_H + 20, 1);
			for (int i = 0; i < per; i++) {
				int k = *page * per + i;
				if (k >= L->n) break;
				KuiRect r = { MARGIN - 20, y0 + i * L->row_h, w - 2 * (MARGIN - 20), L->row_h };
				L->row(k, r);
				kui_fill((KuiRect){ r.x, r.y + r.h - 2, r.w, 1 }, 0x80);
			}
			if (!L->n)
				kui_text((KuiRect){ MARGIN, y0 + 120, w - 2 * MARGIN, 200 }, T("Henüz favori şiir yok.\nBir şiirde ☆ düğmesine dokunun."), KUI_FONT_SERIF, 38,
					 KUI_ALIGN_CENTER, 0);
			if (pages > 1) {
				kui_button(&prev, 0);
				kui_button(&next, 0);
				page_label(*page, pages, (KuiRect){ 170, BAR_Y, w - 340, BAR_H });
			}
			flush_screen(full);
			full = redraw = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return -2;
		if (e.type == KUI_EV_WAKE) {
			full = redraw = 1;
			continue;
		}
		int dlt = 0;
		if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
			KuiButton bs[2] = { prev, next };
			int id = pages > 1 ? kui_buttons_hit(bs, 2, e.x, e.y) : 0;
			if (id) dlt = id == 1 ? -1 : 1;
			else if (e.y >= y0 && e.y < y0 + per * L->row_h) {
				int k = *page * per + (e.y - y0) / L->row_h;
				if (k < L->n) return k;
			}
		}
		if (dlt && *page + dlt >= 0 && *page + dlt < pages) {
			*page += dlt;
			redraw = 1;
		}
	}
}

static void author_row(int i, KuiRect r)
{
	const SiirAuthor *a = &db.authors[i];
	int yw = kui_text_width(a->years, KUI_FONT_SERIF, 28) + 10, nw = r.w - 40 - yw - 20;
	kui_text((KuiRect){ r.x + 20, r.y + 12, nw, 56 }, a->name, KUI_FONT_SERIF_BOLD, kui_fit_px(a->name, KUI_FONT_SERIF_BOLD, 40, nw), KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ r.x + r.w - 20 - yw, r.y + 12, yw, 56 }, a->years, KUI_FONT_SERIF, 28, KUI_ALIGN_RIGHT, 0x30);
	draw_block(r.x + 20, r.y + 72, r.w - 40, a->note, KUI_FONT_SERIF, 26, KUI_ALIGN_LEFT, 2, 0x30, 1);
}

static int cur_author;

/* the poet's dates and note above the list of their poems */
static int author_head(int y, int draw)
{
	const SiirAuthor *a = &db.authors[cur_author];
	int w = kui_width(), tw = w - 2 * MARGIN, used = 0;
	char s[160];
	snprintf(s, sizeof s, a->years[0] ? "%s  ·  " : "%s", a->years);
	size_t n = strlen(s);
	snprintf(s + n, sizeof s - n, T("%d şiir"), a->count);
	if (draw) kui_text((KuiRect){ MARGIN, y, tw, 44 }, s, KUI_FONT_SERIF_BOLD, 30, KUI_ALIGN_CENTER, 0);
	used += 50;
	used += draw_block(MARGIN, y + used, tw, a->note, KUI_FONT_SERIF, 30, KUI_ALIGN_CENTER, 3, 0x20, draw) + 20;
	if (draw) kui_fill((KuiRect){ w / 2 - 70, y + used, 140, 2 }, 0);
	return used + 22;
}

/* first verse line of a poem, for lists */
static void incipit(const char *text, char *out, int cap)
{
	const char *e = strchr(text, '\n');
	int len = e ? (int) (e - text) : (int) strlen(text);
	if (len > cap - 1) len = cap - 1;
	memcpy(out, text, (size_t) len);
	out[len] = '\0';
}

static void poem_row(int k, KuiRect r, const char *sub)
{
	const SiirPoem *p = &db.poems[k];
	kui_text((KuiRect){ r.x + 20, r.y + 8, r.w - 40, 54 }, p->title, KUI_FONT_SERIF_BOLD, kui_fit_px(p->title, KUI_FONT_SERIF_BOLD, 36, r.w - 40), KUI_ALIGN_LEFT,
		 0);
	char s[300];
	snprintf(s, sizeof s, "%s", sub);
	/* cut long first lines to the row with "…" */
	while (kui_text_width(s, KUI_FONT_SERIF, 27) > r.w - 40) {
		int len = (int) strlen(s) - 3;
		if (len < 0) break;
		if (strlen(s) >= 3 && !strcmp(s + strlen(s) - 3, "…")) len -= 1;
		while (len > 0 && ((unsigned char) s[len] & 0xC0) == 0x80) len--;
		strcpy(s + (len > 0 ? len : 0), "…");
	}
	kui_text((KuiRect){ r.x + 20, r.y + 62, r.w - 40, 40 }, s, KUI_FONT_SERIF, 27, KUI_ALIGN_LEFT, 0x40);
}

static void author_poem_row(int i, KuiRect r)
{
	int k = db.authors[cur_author].first + i;
	char inc[300];
	incipit(db.poems[k].text, inc, sizeof inc);
	poem_row(k, r, inc);
}

static int fav_list[SIIR_MAX_FAVS], n_fav_list;

static void fav_row(int i, KuiRect r)
{
	const SiirPoem *p = &db.poems[fav_list[i]];
	poem_row(fav_list[i], r, db.authors[p->author].name);
}

static int author_poems(int a)
{
	cur_author = a;
	ListDef L = { db.authors[a].name, db.authors[a].count, 110, author_poem_row, author_head };
	for (int page = 0;;) {
		int k = list_screen(&L, &page, 1);
		if (k < 0) return k;
		int idx = db.authors[a].first + k;
		const char *label = db.authors[a].name;
		if (poem_view(&idx, &label, 0) == PV_QUIT) return -2;
	}
}

static int authors_screen(void)
{
	ListDef L = { T("Şairler"), db.n_authors, 160, author_row, NULL };
	for (int page = 0;;) {
		int k = list_screen(&L, &page, 1);
		if (k < 0) return k;
		if (author_poems(k) == -2) return -2;
	}
}

static int favourites_screen(void)
{
	for (int page = 0;;) {
		n_fav_list = 0;
		for (int i = favs.n - 1; i >= 0; i--) { /* newest first; keys of poems no longer in the anthology are kept but not shown */
			int k = siir_find_url(&db, favs.key[i]);
			if (k >= 0) fav_list[n_fav_list++] = k;
		}
		ListDef L = { T("Favoriler"), n_fav_list, 110, fav_row, NULL };
		int k = list_screen(&L, &page, 1);
		if (k < 0) return k;
		int idx = fav_list[k];
		const char *label = T("Favoriler");
		if (poem_view(&idx, &label, 0) == PV_QUIT) return -2;
	}
}

/* ---- menu ---- */

enum { M_BACK, M_TODAY, M_RANDOM, M_QUIT };

static int menu_screen(void)
{
	int w = kui_width(), h = kui_height();
	for (int full = 1;;) {
		const char *labels[4] = { T("Günün şiiri"), T("Rastgele"), T("Şairler"), T("Favoriler") };
		char sub[4][300], date[96];
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		kui_tr_date(&tm, date, sizeof date);
		const SiirPoem *tp = &db.poems[today_index()];
		snprintf(sub[0], sizeof sub[0], "%s — %s", date, tp->title);
		snprintf(sub[1], sizeof sub[1], "%s", T("Bütün şiirlerden biri"));
		snprintf(sub[2], sizeof sub[2], T("%d şair, yüzyıllara göre"), db.n_authors);
		int nf = 0;
		for (int i = 0; i < favs.n; i++) nf += siir_find_url(&db, favs.key[i]) >= 0;
		snprintf(sub[3], sizeof sub[3], T("%d şiir yıldızlı"), nf);
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Şiir Defteri"), NULL);
		KuiRect rows[4];
		int y = KUI_TOPBAR_H + 70;
		for (int i = 0; i < 4; i++) {
			rows[i] = (KuiRect){ MARGIN, y, w - 2 * MARGIN, 190 };
			kui_frame(rows[i], 3, 0);
			kui_text((KuiRect){ rows[i].x + 40, y + 28, rows[i].w - 80, 70 }, labels[i], KUI_FONT_SERIF_BOLD, 52, KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ rows[i].x + 40, y + 108, rows[i].w - 80, 50 }, sub[i], KUI_FONT_SERIF,
				 kui_fit_px(sub[i], KUI_FONT_SERIF, 30, rows[i].w - 80), KUI_ALIGN_LEFT, 0x30);
			y += 190 + 34;
		}
		char foot[200];
		snprintf(foot, sizeof foot, T("%d şiir · %d şair · kaynak: Vikikaynak (kamu malı)"), db.n_poems, db.n_authors);
		kui_text((KuiRect){ MARGIN, h - 110, w - 2 * MARGIN, 60 }, foot, KUI_FONT_SERIF, kui_fit_px(foot, KUI_FONT_SERIF, 28, w - 2 * MARGIN), KUI_ALIGN_CENTER,
			 0x40);
		flush_screen(full);
		full = 1; /* every way back into the menu is a new screen */
		for (int again = 1; again;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return M_QUIT;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return M_BACK;
			if (kui_rect_contains(rows[0], e.x, e.y)) return M_TODAY;
			if (kui_rect_contains(rows[1], e.x, e.y)) return M_RANDOM;
			int r = 0;
			if (kui_rect_contains(rows[2], e.x, e.y)) r = authors_screen(), again = 0;
			else if (kui_rect_contains(rows[3], e.x, e.y)) r = favourites_screen(), again = 0;
			if (r == -2) return M_QUIT; /* a tap elsewhere waits for the next one */
		}
	}
}

int main(void)
{
	if (kui_init("siir") != 0) return 1;
	load_favs();
	if (load_db() != 0) {
		kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
		kui_topbar(T("Şiir Defteri"), NULL);
		kui_text((KuiRect){ MARGIN, 500, kui_width() - 2 * MARGIN, 200 }, T("Şiirler yüklenemedi."), KUI_FONT_SERIF, 44, KUI_ALIGN_CENTER, 0);
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT || (e.type == KUI_EV_TAP && kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT)) break;
		}
		kui_quit();
		return 0;
	}
	int idx = today_index();
	const char *label = T("Günün şiiri");
	for (;;) {
		int r = poem_view(&idx, &label, 1);
		if (r != PV_MENU) break;
		int m = menu_screen();
		if (m == M_QUIT) break;
		if (m == M_TODAY) idx = today_index(), label = T("Günün şiiri");
		if (m == M_RANDOM) idx = random_index(idx), label = T("Rastgele");
	}
	siir_favs_free(&favs);
	siir_free(&db);
	kui_quit();
	return 0;
}

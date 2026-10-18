/* Gazete: headlines from AA and TRT Haber RSS; tap one to read the text the feed provides, with its source link.
 * Feeds are cached so the last news can be read offline. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "kui.h"
#include "net/net.h"
#include "news.h"

#define FRESH_S (15 * 60)
#define PER_PAGE 6
#define ROW_H 140

static const NewsFeed *feeds;
static int n_feeds, feed, page;
static NewsItem items[NEWS_MAX];
static int n_items, offline;
static long fetched;
static KuiButton tabs[2], cats[8], nav[2];
static int cat_feed[8], n_cats;

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("news"));
	return p;
}

static void cache_path(int f, char *out, int cap) { snprintf(out, (size_t) cap, "%s/%s.xml", kui_data_dir("news"), feeds[f].id); }

static int load_file(int f)
{
	char p[300];
	struct stat st;
	cache_path(f, p, sizeof p);
	static char buf[NET_MAX_BODY + 1];
	int len = kui_read_file(p, buf, sizeof buf);
	news_free(items, n_items);
	n_items = len > 0 ? news_parse_rss(buf, len, items, NEWS_MAX) : 0;
	fetched = n_items && stat(p, &st) == 0 ? (long) st.st_mtime : 0;
	return n_items;
}

static void fetch(int f)
{
	char *body = NULL, p[300];
	int len = 0;
	offline = 1;
	if (net_get(feeds[f].url, &body, &len, 20) == 0) {
		static NewsItem probe[NEWS_MAX];
		int n = news_parse_rss(body, len, probe, NEWS_MAX);
		news_free(probe, n);
		if (n > 0) { /* never replace a good cache with an error page */
			cache_path(f, p, sizeof p);
			kui_write_atomic(p, body, len);
			offline = 0;
		}
	}
	free(body);
	load_file(f);
}

/* ---- wrapped text drawing ---- */

/* draws up to max_lines lines of text wrapped to box.w starting at box.y; returns the height used */
static int draw_wrapped(KuiRect box, const char *text, KuiFont f, int px, int max_lines)
{
	static KuiLine lines[400];
	int n = kui_wrap_text(text, box.w, f, px, lines, 400), lh = kui_line_height(f, px);
	if (n > max_lines) n = max_lines;
	for (int i = 0; i < n; i++) {
		char buf[1024];
		int len = lines[i].len < (int) sizeof buf - 4 ? lines[i].len : (int) sizeof buf - 4;
		memcpy(buf, text + lines[i].off, (size_t) len);
		buf[len] = '\0';
		if (i == max_lines - 1 && lines[i].off + lines[i].len < (int) strlen(text)) strcat(buf, "…");
		kui_text((KuiRect){ box.x, box.y + i * lh, box.w, lh }, buf, f, px, KUI_ALIGN_LEFT, 0);
	}
	return n * lh;
}

/* ---- headline list ---- */

static int pages(void) { return n_items ? (n_items + PER_PAGE - 1) / PER_PAGE : 1; }

static void build_cats(void)
{
	n_cats = 0;
	for (int i = 0; i < n_feeds && n_cats < 8; i++)
		if (!strcmp(feeds[i].source, feeds[feed].source)) cat_feed[n_cats++] = i;
}

static void draw_list(int full)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar("Gazete", "Yenile");
	int tw = (w - 120) / 2;
	tabs[0] = (KuiButton){ { 40, KUI_TOPBAR_H + 15, tw, 90 }, "Anadolu Ajansı", 1 };
	tabs[1] = (KuiButton){ { 80 + tw, KUI_TOPBAR_H + 15, tw, 90 }, "TRT Haber", 2 };
	kui_button(&tabs[0], !strcmp(feeds[feed].source, "AA"));
	kui_button(&tabs[1], !strcmp(feeds[feed].source, "TRT"));
	build_cats();
	int cw = (w - 40 - 3 * 15) / 4;
	for (int i = 0; i < n_cats; i++) {
		cats[i] = (KuiButton){ { 20 + (i % 4) * (cw + 15), KUI_TOPBAR_H + 125 + (i / 4) * 95, cw, 80 }, feeds[cat_feed[i]].label, 10 + i };
		kui_button(&cats[i], cat_feed[i] == feed);
	}
	int y0 = KUI_TOPBAR_H + 330;
	kui_fill((KuiRect){ 20, y0 - 10, w - 40, 3 }, 0);
	if (!n_items) {
		kui_text((KuiRect){ 40, y0 + 200, w - 80, 200 }, offline ? "Haberler alınamadı.\nWi-Fi bağlantısını kontrol edin." : "Yükleniyor…",
			 KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
	}
	for (int i = 0; i < PER_PAGE; i++) {
		int k = page * PER_PAGE + i;
		if (k >= n_items) break;
		int y = y0 + i * ROW_H;
		draw_wrapped((KuiRect){ 30, y + 5, w - 60, 0 }, items[k].title, KUI_FONT_BOLD, 36, 2);
		char d[32];
		news_date_short(items[k].date, d, sizeof d);
		kui_text((KuiRect){ 30, y + 98, w - 60, 36 }, d, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0);
		kui_fill((KuiRect){ 30, y + ROW_H - 4, w - 60, 1 }, 0);
	}
	/* bottom: page navigation and freshness */
	char s[96];
	nav[0] = (KuiButton){ { 20, h - 100, 160, 90 }, "◀", 1 };
	nav[1] = (KuiButton){ { w - 180, h - 100, 160, 90 }, "▶", 2 };
	if (pages() > 1) {
		kui_button(&nav[0], 0);
		kui_button(&nav[1], 0);
	}
	if (n_items) {
		time_t t = (time_t) fetched;
		struct tm tm;
		localtime_r(&t, &tm);
		snprintf(s, sizeof s, "%d/%d · %s %02d:%02d", page + 1, pages(), offline ? "çevrimdışı, son veri" : "güncellendi", tm.tm_hour, tm.tm_min);
		kui_text((KuiRect){ 190, h - 100, w - 380, 90 }, s, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0);
	}
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

static void open_feed(int f, int force)
{
	feed = f;
	page = 0;
	offline = 0;
	kui_cfg_set_int(settings(), "feed", feed);
	load_file(f);
	draw_list(0);
	if (force || !n_items || time(NULL) - fetched > FRESH_S) {
		fetch(f);
		draw_list(0);
	}
}

/* ---- article reader: title, date, text and source link, paged ---- */

typedef struct { const char *s; int len; KuiFont f; int px; } RLine;

static void article(const NewsItem *it)
{
	int w = kui_width(), h = kui_height(), top = KUI_TOPBAR_H + 30, bottom = h - 120, tw = w - 80;
	static RLine ls[800];
	static KuiLine tmp[400];
	int n = 0;
	char date[32], src[320];
	news_date_short(it->date, date, sizeof date);
	snprintf(src, sizeof src, "Kaynak: %s", it->link);
	struct { const char *t; KuiFont f; int px; } parts[] = {
		{ it->title, KUI_FONT_BOLD, 46 }, { date, KUI_FONT_REG, 30 }, { "", KUI_FONT_REG, 20 },
		{ it->text, KUI_FONT_REG, 40 },   { "", KUI_FONT_REG, 20 }, { src, KUI_FONT_REG, 28 },
	};
	for (size_t p = 0; p < sizeof parts / sizeof parts[0]; p++) {
		int k = parts[p].t[0] ? kui_wrap_text(parts[p].t, tw, parts[p].f, parts[p].px, tmp, 400) : 1;
		for (int i = 0; i < k && n < 800; i++, n++)
			ls[n] = (RLine){ parts[p].t[0] ? parts[p].t + tmp[i].off : "", parts[p].t[0] ? tmp[i].len : 0, parts[p].f, parts[p].px };
	}
	/* page breaks */
	int starts[100], n_pages = 0;
	for (int i = 0, y = top; i < n; i++) {
		int lh = kui_line_height(ls[i].f, ls[i].px);
		if (i == 0 || y + lh > bottom) {
			if (n_pages == 100) break;
			starts[n_pages++] = i;
			y = top;
		}
		y += lh;
	}
	KuiButton bnav[2] = { { { 20, h - 100, 160, 90 }, "◀", 1 }, { { w - 180, h - 100, 160, 90 }, "▶", 2 } };
	for (int pg = 0, full = 1;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(feeds[feed].source[0] == 'A' ? "Anadolu Ajansı" : "TRT Haber", NULL);
		int end = pg + 1 < n_pages ? starts[pg + 1] : n;
		for (int i = starts[pg], y = top; i < end; i++) {
			int lh = kui_line_height(ls[i].f, ls[i].px);
			if (ls[i].len) {
				char buf[1024];
				int len = ls[i].len < (int) sizeof buf - 1 ? ls[i].len : (int) sizeof buf - 1;
				memcpy(buf, ls[i].s, (size_t) len);
				buf[len] = '\0';
				kui_text((KuiRect){ 40, y, tw, lh }, buf, ls[i].f, ls[i].px, KUI_ALIGN_LEFT, 0);
			}
			y += lh;
		}
		if (n_pages > 1) {
			char s[32];
			snprintf(s, sizeof s, "%d / %d", pg + 1, n_pages);
			kui_button(&bnav[0], 0);
			kui_button(&bnav[1], 0);
			kui_text((KuiRect){ 190, h - 100, w - 380, 90 }, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
		}
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return;
			if (e.type == KUI_EV_WAKE) { full = 1; break; }
			int d = 0;
			if (e.type == KUI_EV_SWIPE) d = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
			if (e.type == KUI_EV_TAP) {
				if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
				int id = n_pages > 1 ? kui_buttons_hit(bnav, 2, e.x, e.y) : 0;
				d = id == 1 ? -1 : id == 2 ? 1 : 0;
			}
			if (d && pg + d >= 0 && pg + d < n_pages) {
				pg += d;
				break;
			}
		}
	}
}

static void turn(int d)
{
	if (page + d < 0 || page + d >= pages()) return;
	page += d;
	draw_list(0);
}

int main(void)
{
	if (kui_init("news") != 0) return 1;
	feeds = news_feeds(&n_feeds);
	feed = kui_cfg_get_int(settings(), "feed", 0);
	if (feed < 0 || feed >= n_feeds) feed = 0;
	load_file(feed);
	draw_list(1);
	if (!n_items || time(NULL) - fetched > FRESH_S) {
		fetch(feed);
		draw_list(0);
	}
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw_list(1); continue; }
		if (e.type == KUI_EV_SWIPE) {
			if (e.dir == KUI_DIR_LEFT) turn(1);
			if (e.dir == KUI_DIR_RIGHT) turn(-1);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) { open_feed(feed, 1); continue; }
		int id = kui_buttons_hit(tabs, 2, e.x, e.y);
		if (id) {
			const char *want = id == 1 ? "AA" : "TRT";
			for (int i = 0; i < n_feeds; i++)
				if (!strcmp(feeds[i].source, want)) {
					if (strcmp(feeds[feed].source, want) != 0) open_feed(i, 0);
					break;
				}
			continue;
		}
		id = kui_buttons_hit(cats, n_cats, e.x, e.y);
		if (id >= 10) {
			open_feed(cat_feed[id - 10], 0);
			continue;
		}
		if (pages() > 1) {
			id = kui_buttons_hit(nav, 2, e.x, e.y);
			if (id) {
				turn(id == 1 ? -1 : 1);
				continue;
			}
		}
		int y0 = KUI_TOPBAR_H + 330;
		if (e.y >= y0 && e.y < y0 + PER_PAGE * ROW_H) {
			int k = page * PER_PAGE + (e.y - y0) / ROW_H;
			if (k < n_items) {
				article(&items[k]);
				draw_list(1);
			}
		}
	}
	news_free(items, n_items);
	kui_quit();
	return 0;
}

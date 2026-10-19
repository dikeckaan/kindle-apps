/* Gazete: a newspaper-style front page built from many Turkish news sources (RSS/Atom), a page per source with its
 * sections, and a serif reader that shows the text the feed provides with its source link. Feeds are cached so
 * the last news can be read offline. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "kui.h"
#include "net/net.h"
#include "news.h"
#include "reader.h"

#define FRESH_S (15 * 60)
#define MAX_SRC 24
#define FRONT_MAX 60
#define PER_SOURCE 5 /* stories each source contributes to the front page */

static const NewsSource *src;
static int n_src, offline;
static NewsItem lists[MAX_SRC][NEWS_MAX]; /* front page: section 0 of each source */
static int counts[MAX_SRC];
static NewsRef front[FRONT_MAX];
static int n_front, fpage;

/* ---- settings and cache ---- */

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("news"));
	return p;
}

static int enabled(int s)
{
	char key[48];
	snprintf(key, sizeof key, "on_%s", src[s].id);
	return kui_cfg_get_int(settings(), key, src[s].default_on);
}

static void set_enabled(int s, int on)
{
	char key[48];
	snprintf(key, sizeof key, "on_%s", src[s].id);
	kui_cfg_set_int(settings(), key, on);
}

static void cache_path(int s, int sec, char *out, int cap)
{
	char id[48];
	news_feed_id(&src[s], sec, id, sizeof id);
	snprintf(out, (size_t) cap, "%s/%s.xml", kui_data_dir("news"), id);
}

/* parses the cached feed into it[]; returns the count and the cache time */
static int load_cached(int s, int sec, NewsItem *it, long *when)
{
	static char buf[NET_MAX_BODY + 1];
	char p[300];
	struct stat st;
	cache_path(s, sec, p, sizeof p);
	int len = kui_read_file(p, buf, sizeof buf);
	int n = len > 0 ? news_parse_rss(buf, len, it, NEWS_MAX) : 0;
	*when = n && stat(p, &st) == 0 ? (long) st.st_mtime : 0;
	return n;
}

/* downloads a feed into the cache; never replaces a good cache with an error page */
static int fetch_feed(int s, int sec)
{
	char *body = NULL, p[300];
	int len = 0, ok = 0;
	if (net_get(src[s].sec[sec].url, &body, &len, 20) == 0) {
		static NewsItem probe[NEWS_MAX];
		int n = news_parse_rss(body, len, probe, NEWS_MAX);
		news_free(probe, n);
		if (n > 0) {
			cache_path(s, sec, p, sizeof p);
			kui_write_atomic(p, body, len);
			ok = 1;
		}
	}
	free(body);
	return ok;
}

/* ---- drawing helpers ---- */

/* How a redraw reaches the panel. Only the first page and a wake (the screensaver covered everything) flash; going
 * to another screen is one partial update of the whole screen, and a change within a screen refreshes only the rects
 * that changed: the topbar, the masthead and the buttons stay untouched (the core still cleans the panel with a full
 * refresh every so many updates). */
enum { UPD_FLASH, UPD_SCREEN, UPD_PAGE, UPD_DATA };

static void show(int upd, const KuiRect *rs, int n)
{
	if (upd == UPD_FLASH) {
		kui_flush(1);
		return;
	}
	if (upd == UPD_SCREEN) kui_mark_dirty((KuiRect){ 0, 0, kui_width(), kui_height() });
	else
		for (int i = 0; i < n; i++) kui_mark_dirty(rs[i]);
	kui_flush(0);
}

static KuiRect grow(KuiRect r, int dx, int dy) { return (KuiRect){ r.x - dx, r.y - dy, r.w + 2 * dx, r.h + 2 * dy }; }

/* one left-aligned line; inv: white on black (a story being opened) */
static void ink(KuiRect r, const char *s, KuiFont f, int px, int inv)
{
	if (inv) kui_text_on(r, s, f, px, KUI_ALIGN_LEFT, 255, 0);
	else kui_text(r, s, f, px, KUI_ALIGN_LEFT, 0);
}

/* draws up to max_lines lines of text wrapped to box.w from box.y; returns the height used */
static int draw_wrapped(KuiRect box, const char *text, KuiFont f, int px, int max_lines, int inv)
{
	static KuiLine lines[400];
	int n = kui_wrap_text(text, box.w, f, px, lines, 400), lh = kui_line_height(f, px);
	if (n > max_lines) n = max_lines;
	for (int i = 0; i < n; i++) {
		char buf[1024];
		int len = lines[i].len < (int) sizeof buf - 4 ? lines[i].len : (int) sizeof buf - 4;
		memcpy(buf, text + lines[i].off, (size_t) len);
		buf[len] = '\0';
		if (i == max_lines - 1 && lines[i].off + lines[i].len < (int) strlen(text)) {
			/* cut more until "…" fits too (an overlong line would wrap onto the next story) */
			strcat(buf, "…");
			while (len > 0 && kui_text_width(buf, f, px) > box.w) {
				do len--;
				while (len > 0 && ((unsigned char) buf[len] & 0xC0) == 0x80);
				while (len > 0 && buf[len - 1] == ' ') len--;
				strcpy(buf + len, "…");
			}
		}
		ink((KuiRect){ box.x, box.y + i * lh, box.w, lh }, buf, f, px, inv);
	}
	return n * lh;
}

/* "HÜRRİYET · 18 Eki 17:54" above a story */
static void kicker(KuiRect r, const char *source, const char *date, int px, int inv)
{
	char up[96], d[32], s[160];
	kui_tr_upper_str(source, up, sizeof up);
	news_show_date(date, d, sizeof d);
	if (d[0]) snprintf(s, sizeof s, "%s · %s", up, d);
	else snprintf(s, sizeof s, "%s", up);
	ink(r, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, px, r.w), inv);
}

static void status_line(const char *msg)
{
	int w = kui_width(), h = kui_height();
	KuiRect r = { 190, h - 105, w - 380, 100 };
	kui_fill(r, 255);
	kui_text(r, msg, KUI_FONT_REG, kui_fit_px(msg, KUI_FONT_REG, 30, r.w), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
	kui_flush(0);
}

/* the bottom bar (◀, the status or Yenile, ▶) and its middle */
static KuiRect bar_rect(void) { return (KuiRect){ 0, kui_height() - 105, kui_width(), 105 }; }
static KuiRect bar_mid(void) { return (KuiRect){ 190, kui_height() - 105, kui_width() - 380, 100 }; }

/* ---- one source: its sections and headlines ---- */

/* a headline row; inv paints it white on black at once, before the article opens */
static void draw_row(const NewsItem *it, int y, int row, int inv)
{
	int w = kui_width();
	char d[32];
	if (inv) kui_fill((KuiRect){ 20, y, w - 40, row - 6 }, 0);
	draw_wrapped((KuiRect){ 30, y + 5, w - 60, 0 }, it->title, KUI_FONT_SERIF_BOLD, 36, 2, inv);
	news_show_date(it->date, d, sizeof d);
	if (d[0]) ink((KuiRect){ 30, y + 98, w - 60, 36 }, d, KUI_FONT_REG, 28, inv);
}

static void source_view(int s)
{
	int w = kui_width(), h = kui_height(), sec = 0, page = 0, n = 0, per = 6, row = 140;
	static NewsItem items[NEWS_MAX];
	long when = 0;
	KuiButton cats[NEWS_MAX_SECTIONS], nav[2];
	int y0 = KUI_TOPBAR_H + (src[s].n_sec > 1 ? 30 + ((src[s].n_sec + 3) / 4) * 95 : 20);
	int reload = 1, upd = UPD_SCREEN, fetched_now = 0, quit = 0;
	/* a page turn refreshes the list and the page number; new items also the sections and the bar */
	KuiRect list = { 0, y0 - 4, w, per * row + 4 }, cat_area = { 0, KUI_TOPBAR_H + 2, w, y0 - 10 - KUI_TOPBAR_H - 2 };
	while (!quit) {
		if (reload) {
			news_free(items, n);
			n = load_cached(s, sec, items, &when);
			fetched_now = 0;
		}
		int pages = n ? (n + per - 1) / per : 1;
		if (page >= pages) page = pages - 1;
		/* kui_topbar and kui_button mark themselves dirty: they are drawn only when their part refreshes */
		int all = upd == UPD_FLASH || upd == UPD_SCREEN, cw = (w - 40 - 3 * 15) / 4;
		if (all) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(src[s].name, T("Yenile"));
			kui_fill((KuiRect){ 20, y0 - 8, w - 40, 3 }, 0);
		}
		for (int i = 0; i < src[s].n_sec && src[s].n_sec > 1; i++) {
			cats[i] = (KuiButton){ { 20 + (i % 4) * (cw + 15), KUI_TOPBAR_H + 20 + (i / 4) * 95, cw, 80 }, T(src[s].sec[i].label), 10 + i };
			if (upd != UPD_PAGE) kui_button(&cats[i], i == sec);
		}
		kui_fill(list, 255);
		kui_fill(upd == UPD_PAGE ? bar_mid() : bar_rect(), 255);
		if (!n)
			kui_text((KuiRect){ 40, y0 + 200, w - 80, 200 }, offline ? T("Haberler alınamadı.\nWi-Fi bağlantısını kontrol edin.") : T("Yükleniyor…"),
				 KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
		for (int i = 0; i < per; i++) {
			int k = page * per + i;
			if (k >= n) break;
			int y = y0 + i * row;
			draw_row(&items[k], y, row, 0);
			kui_fill((KuiRect){ 30, y + row - 4, w - 60, 1 }, 0);
		}
		nav[0] = (KuiButton){ { 20, h - 100, 160, 90 }, "◀", 1 };
		nav[1] = (KuiButton){ { w - 180, h - 100, 160, 90 }, "▶", 2 };
		if (pages > 1 && upd != UPD_PAGE) {
			kui_button(&nav[0], 0);
			kui_button(&nav[1], 0);
		}
		if (n) {
			char st[160];
			time_t t = (time_t) when + news_local_off(); /* the device may have no zone set */
			struct tm tm;
			gmtime_r(&t, &tm);
			snprintf(st, sizeof st, offline ? T("%d/%d · çevrimdışı, son veri %02d:%02d") : T("%d/%d · güncellendi %02d:%02d"), page + 1, pages,
				 tm.tm_hour, tm.tm_min);
			kui_text((KuiRect){ 190, h - 100, w - 380, 90 }, st, KUI_FONT_REG, kui_fit_px(st, KUI_FONT_REG, 30, w - 390), KUI_ALIGN_CENTER, 0);
		}
		KuiRect rs[3] = { list, upd == UPD_PAGE ? bar_mid() : bar_rect(), cat_area };
		show(upd, rs, upd == UPD_PAGE ? 2 : 3);
		if (reload && !fetched_now && (!n || time(NULL) - when > FRESH_S)) { /* the cached page is up; now fetch */
			status_line(T("Güncelleniyor…"));
			offline = !fetch_feed(s, sec);
			fetched_now = 1;
			news_free(items, n);
			n = load_cached(s, sec, items, &when);
			reload = 0;
			upd = UPD_DATA;
			continue; /* redraw with the new items */
		}
		reload = 0;
		for (upd = -1; upd < 0;) { /* until something changed */
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) { quit = 1; break; }
			if (e.type == KUI_EV_WAKE) upd = UPD_FLASH;
			if (e.type == KUI_EV_SWIPE) {
				if (e.dir == KUI_DIR_LEFT && page + 1 < pages) page++, upd = UPD_PAGE;
				else if (e.dir == KUI_DIR_RIGHT && page > 0) page--, upd = UPD_PAGE;
			}
			if (e.type != KUI_EV_TAP) continue;
			int hit = kui_topbar_hit(w, 1, e.x, e.y);
			if (hit == KUI_BTN_EXIT) { quit = 1; break; }
			if (hit == KUI_BTN_MENU) {
				status_line(T("Güncelleniyor…"));
				offline = !fetch_feed(s, sec);
				news_free(items, n);
				n = load_cached(s, sec, items, &when);
				upd = UPD_DATA;
				continue;
			}
			int id = src[s].n_sec > 1 ? kui_buttons_hit(cats, src[s].n_sec, e.x, e.y) : 0;
			if (id >= 10 && id - 10 != sec) {
				sec = id - 10;
				page = 0;
				offline = 0;
				reload = 1;
				upd = UPD_DATA;
				continue;
			}
			id = pages > 1 ? kui_buttons_hit(nav, 2, e.x, e.y) : 0;
			if (id == 1 && page > 0) page--, upd = UPD_PAGE;
			if (id == 2 && page + 1 < pages) page++, upd = UPD_PAGE;
			if (id) continue;
			if (e.y >= y0 && e.y < y0 + per * row) {
				int i = (e.y - y0) / row, k = page * per + i;
				if (k < n) {
					draw_row(&items[k], y0 + i * row, row, 1); /* only this row refreshes, before the reader lays out */
					kui_mark_dirty((KuiRect){ 20, y0 + i * row, w - 40, row - 6 });
					kui_flush(0);
					news_read(&items[k], src[s].name);
					upd = UPD_SCREEN;
				}
			}
		}
	}
	news_free(items, n);
}

/* ---- sources: open one, or choose which appear on the front page ---- */

static void sources_screen(void)
{
	int w = kui_width(), h = kui_height(), cols = 2, rows = (n_src + 1) / 2;
	int cw = (w - 3 * 30) / cols, rh = (h - KUI_TOPBAR_H - 120) / (rows ? rows : 1);
	if (rh > 110) rh = 110;
	for (int upd = UPD_SCREEN;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Kaynaklar"), NULL);
		for (int i = 0; i < n_src; i++) {
			KuiRect r = { 30 + (i / rows) * (cw + 30), KUI_TOPBAR_H + 15 + (i % rows) * rh, cw, rh - 12 };
			kui_frame(r, 2, 0);
			kui_text((KuiRect){ r.x + 20, r.y, r.w - 110, r.h }, src[i].name, KUI_FONT_SERIF_BOLD, kui_fit_px(src[i].name, KUI_FONT_SERIF_BOLD, 38, r.w - 120),
				 KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ r.x + r.w - 90, r.y, 80, r.h }, enabled(i) ? "☑" : "☐", KUI_FONT_REG, 56, KUI_ALIGN_CENTER, 0);
		}
		kui_text((KuiRect){ 30, h - 100, w - 60, 90 }, T("Kaynağa dokunun: haberlerini açar. ☑ / ☐: ön sayfada gösterilsin mi."), KUI_FONT_REG, 28,
			 KUI_ALIGN_CENTER, 0);
		show(upd, NULL, 0);
		for (upd = -1; upd < 0;) { /* until the whole screen changes */
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return;
			if (e.type == KUI_EV_WAKE) upd = UPD_FLASH;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
			for (int i = 0; i < n_src; i++) {
				KuiRect r = { 30 + (i / rows) * (cw + 30), KUI_TOPBAR_H + 15 + (i % rows) * rh, cw, rh - 12 };
				if (!kui_rect_contains(r, e.x, e.y)) continue;
				if (e.x >= r.x + r.w - 110) { /* only the box changes */
					KuiRect box = { r.x + r.w - 90, r.y + 4, 80, r.h - 8 };
					set_enabled(i, !enabled(i));
					kui_fill(box, 255);
					kui_text(box, enabled(i) ? "☑" : "☐", KUI_FONT_REG, 56, KUI_ALIGN_CENTER, 0);
					kui_mark_dirty(box);
					kui_flush(0);
				} else { /* the source white on black at once, then its page */
					kui_fill(r, 0);
					kui_text_on((KuiRect){ r.x + 20, r.y, r.w - 110, r.h }, src[i].name, KUI_FONT_SERIF_BOLD,
						    kui_fit_px(src[i].name, KUI_FONT_SERIF_BOLD, 38, r.w - 120), KUI_ALIGN_LEFT, 255, 0);
					kui_mark_dirty(r);
					kui_flush(0);
					source_view(i);
					upd = UPD_SCREEN;
				}
				break;
			}
		}
	}
}

/* ---- front page ---- */

static int front_src[MAX_SRC], n_front_src; /* list index → source */
static long front_when; /* newest fetch among the front page's feeds */

/* fetch_old: also downloads the feeds older than FRESH_S; returns how many it tried (the bar shows their status) */
static int build_front(int fetch_old)
{
	int tried = 0;
	n_front_src = 0;
	front_when = 0;
	for (int s = 0; s < n_src && n_front_src < MAX_SRC; s++) {
		if (!enabled(s)) continue;
		int l = n_front_src++;
		front_src[l] = s;
		long when = 0;
		news_free(lists[l], counts[l]);
		counts[l] = load_cached(s, 0, lists[l], &when);
		if (fetch_old && (!counts[l] || time(NULL) - when > FRESH_S)) {
			char msg[160];
			snprintf(msg, sizeof msg, T("%s yükleniyor… (%d)"), src[s].name, l + 1);
			status_line(msg);
			tried++;
			if (fetch_feed(s, 0)) {
				news_free(lists[l], counts[l]);
				counts[l] = load_cached(s, 0, lists[l], &when);
			} else {
				offline = 1;
			}
		}
		if (counts[l] && when > front_when) front_when = when;
	}
	for (int l = n_front_src; l < MAX_SRC; l++) { /* sources turned off since the last build */
		news_free(lists[l], counts[l]);
		counts[l] = 0;
	}
	const NewsItem *ptr[MAX_SRC];
	for (int l = 0; l < n_front_src; l++) ptr[l] = lists[l];
	n_front = news_front_page(ptr, counts, n_front_src, PER_SOURCE, (long) time(NULL), news_local_off(), front, FRONT_MAX);
	return tried;
}

/* story slots of a front page: page 0 has a lead story and six below it, later pages ten */
static int slots(int page, KuiRect *r)
{
	int w = kui_width(), top = 330, col = (w - 40 - 40) / 2, n = 0;
	if (page == 0) {
		r[n++] = (KuiRect){ 30, top, w - 60, 300 };
		for (int i = 0; i < 6; i++) r[n++] = (KuiRect){ 30 + (i % 2) * (col + 20), top + 315 + (i / 2) * 230, col, 220 };
	} else {
		for (int i = 0; i < 10; i++) r[n++] = (KuiRect){ 30 + (i % 2) * (col + 20), top + (i / 2) * 200, col, 190 };
	}
	return n;
}

static int front_pages(void) { return n_front <= 7 ? 1 : 1 + (n_front - 7 + 9) / 10; }
static int first_story(int page) { return page == 0 ? 0 : 7 + (page - 1) * 10; }

/* a tapped story turns white on black over its slot and margin (clear of the column and lead rules) */
static KuiRect pressed_rect(KuiRect slot) { return grow(slot, 6, 4); }

/* story i of the current page in its slot r */
static void draw_story(int i, KuiRect r, int inv)
{
	const NewsRef *ref = &front[first_story(fpage) + i];
	const NewsItem *it = &lists[ref->src][ref->idx];
	const char *name = src[front_src[ref->src]].name;
	int lead = fpage == 0 && i == 0;
	if (inv) kui_fill(pressed_rect(r), 0);
	kicker((KuiRect){ r.x, r.y, r.w, 36 }, name, it->date, lead ? 28 : 24, inv);
	int used = draw_wrapped((KuiRect){ r.x, r.y + 40, r.w, 0 }, it->title, KUI_FONT_SERIF_BOLD, lead ? 54 : 32, lead ? 3 : 4, inv);
	if (lead && it->text[0])
		draw_wrapped((KuiRect){ r.x, r.y + 48 + used, r.w, 0 }, it->text, KUI_FONT_SERIF, 32, (r.h - 48 - used) / kui_line_height(KUI_FONT_SERIF, 32),
			     inv);
}

static KuiButton fnav[3];

/* a page turn redraws only the masthead's date line and the stories; new data also the bar (kui_topbar and
 * kui_button mark themselves dirty, so they are drawn only when their part refreshes) */
static void draw_front(int upd)
{
	int w = kui_width(), h = kui_height(), all = upd == UPD_FLASH || upd == UPD_SCREEN;
	char s[200], date[96];
	KuiRect dateline = { 20, KUI_TOPBAR_H + 132, w - 40, 50 }, stories = { 0, KUI_TOPBAR_H + 200, w, h - 116 - KUI_TOPBAR_H - 200 };
	if (all) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Gazete"), T("Kaynaklar"));
		/* masthead */
		const char *title = T("Gazete");
		kui_text((KuiRect){ 20, KUI_TOPBAR_H + 5, w - 40, 120 }, title, KUI_FONT_SERIF_BOLD, kui_fit_px(title, KUI_FONT_SERIF_BOLD, 104, w - 60),
			 KUI_ALIGN_CENTER, 0);
		kui_fill((KuiRect){ 20, KUI_TOPBAR_H + 128, w - 40, 2 }, 0);
		kui_fill((KuiRect){ 20, KUI_TOPBAR_H + 185, w - 40, 6 }, 0);
		kui_fill((KuiRect){ 20, KUI_TOPBAR_H + 195, w - 40, 1 }, 0);
		kui_fill((KuiRect){ 20, h - 114, w - 40, 1 }, 0); /* the foot of the page, above the buttons */
	}
	kui_fill(dateline, 255);
	kui_fill(stories, 255);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	kui_tr_date(&tm, date, sizeof date);
	snprintf(s, sizeof s, T("%s · Manşetler · Sayfa %d/%d"), date, fpage + 1, front_pages());
	if (front_when) { /* when the newest feed was fetched */
		struct tm ft;
		time_t t = (time_t) front_when + news_local_off(); /* as the source pages show it */
		gmtime_r(&t, &ft);
		char u[64];
		snprintf(u, sizeof u, offline ? T("çevrimdışı, son veri %02d:%02d") : T("güncellendi %02d:%02d"), ft.tm_hour, ft.tm_min);
		size_t n = strlen(s);
		snprintf(s + n, sizeof s - n, " · %s", u);
	}
	kui_text(dateline, s, KUI_FONT_SERIF, kui_fit_px(s, KUI_FONT_SERIF, 30, w - 60), KUI_ALIGN_CENTER, 0);
	if (!n_front) {
		kui_text((KuiRect){ 40, 600, w - 80, 300 },
			 offline ? T("Haberler alınamadı.\nWi-Fi bağlantısını kontrol edin.") : T("Ön sayfada kaynak yok.\nKaynaklar'dan seçin."), KUI_FONT_SERIF, 42,
			 KUI_ALIGN_CENTER, 0);
	}
	KuiRect r[12];
	int ns = slots(fpage, r), first = first_story(fpage);
	for (int i = 0; i < ns && first + i < n_front; i++) {
		draw_story(i, r[i], 0);
		if (fpage == 0 && i == 0) kui_fill((KuiRect){ 20, r[i].y + r[i].h + 5, w - 40, 2 }, 0);
		else if (i % 2 == (fpage == 0 ? 1 : 0)) kui_fill((KuiRect){ r[i].x + r[i].w + 9, r[i].y, 2, r[i].h - 10 }, 0); /* column rule */
	}
	fnav[0] = (KuiButton){ { 20, h - 100, 160, 90 }, "◀", 1 };
	fnav[1] = (KuiButton){ { w - 180, h - 100, 160, 90 }, "▶", 2 };
	fnav[2] = (KuiButton){ { w / 2 - 150, h - 100, 300, 90 }, T("Yenile"), 3 };
	if (upd != UPD_PAGE) {
		kui_fill(bar_rect(), 255);
		if (front_pages() > 1) {
			kui_button(&fnav[0], 0);
			kui_button(&fnav[1], 0);
		}
		kui_button(&fnav[2], 0);
	}
	KuiRect rs[3] = { dateline, stories, bar_rect() };
	show(upd, rs, upd == UPD_PAGE ? 2 : 3);
}

int main(void)
{
	if (kui_init("news") != 0) return 1;
	src = news_sources(&n_src);
	if (n_src > MAX_SRC) n_src = MAX_SRC;
	build_front(0); /* cached first: something to read at once */
	draw_front(UPD_FLASH);
	if (build_front(1)) draw_front(UPD_DATA);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw_front(UPD_FLASH); continue; }
		if (e.type == KUI_EV_SWIPE) {
			if (e.dir == KUI_DIR_LEFT && fpage + 1 < front_pages()) fpage++, draw_front(UPD_PAGE);
			else if (e.dir == KUI_DIR_RIGHT && fpage > 0) fpage--, draw_front(UPD_PAGE);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			sources_screen();
			fpage = 0;
			offline = 0;
			build_front(0); /* sources may have changed */
			draw_front(UPD_SCREEN);
			if (build_front(1)) draw_front(UPD_DATA);
			continue;
		}
		int id = kui_buttons_hit(fnav, 3, e.x, e.y);
		if (id == 1 && fpage > 0) { fpage--; draw_front(UPD_PAGE); continue; }
		if (id == 2 && fpage + 1 < front_pages()) { fpage++; draw_front(UPD_PAGE); continue; }
		if (id == 3) {
			offline = 0;
			for (int l = 0; l < n_front_src; l++) {
				char msg[160];
				snprintf(msg, sizeof msg, T("%s yükleniyor… (%d)"), src[front_src[l]].name, l + 1);
				status_line(msg);
				if (!fetch_feed(front_src[l], 0)) offline = 1;
			}
			build_front(0);
			draw_front(UPD_DATA);
			continue;
		}
		KuiRect r[12];
		int ns = slots(fpage, r), first = first_story(fpage);
		for (int i = 0; i < ns && first + i < n_front; i++) {
			if (!kui_rect_contains(r[i], e.x, e.y)) continue;
			const NewsRef *ref = &front[first + i];
			draw_story(i, r[i], 1); /* at once, and only the story's rect refreshes */
			kui_mark_dirty(pressed_rect(r[i]));
			kui_flush(0);
			news_read(&lists[ref->src][ref->idx], src[front_src[ref->src]].name);
			draw_front(UPD_SCREEN);
			break;
		}
	}
	for (int l = 0; l < n_front_src; l++) news_free(lists[l], counts[l]);
	kui_quit();
	return 0;
}

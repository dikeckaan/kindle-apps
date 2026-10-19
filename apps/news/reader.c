/* Article reader shared by Gazete and Masa Panosu: source and time, serif title and text, the feed's link; paged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "news.h"
#include "reader.h"

/* device time zone offset; without a configured zone the news sources' own (Türkiye, UTC+3) */
int news_local_off(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_gmtoff || getenv("TZ") ? (int) tm.tm_gmtoff : 3 * 3600;
}

void news_show_date(const char *date, char *out, int cap) { news_date_local(date, (long) time(NULL), news_local_off(), out, cap); }

/* ---- article reader ---- */

typedef struct { const char *s; int len; KuiFont f; int px; } RLine;

void news_read(const NewsItem *it, const char *source)
{
	int w = kui_width(), h = kui_height(), top = KUI_TOPBAR_H + 30, bottom = h - 120, tw = w - 80;
	static RLine ls[1600];
	static KuiLine tmp[800];
	int n = 0;
	char head[160], d[32], link[320];
	news_show_date(it->date, d, sizeof d);
	if (d[0]) snprintf(head, sizeof head, "%s · %s", source, d);
	else snprintf(head, sizeof head, "%s", source);
	snprintf(link, sizeof link, T("Kaynak: %s"), it->link);
	struct { const char *t; KuiFont f; int px; } parts[] = {
		{ head, KUI_FONT_BOLD, 28 }, { it->title, KUI_FONT_SERIF_BOLD, 48 }, { "", KUI_FONT_REG, 18 },
		{ it->text, KUI_FONT_SERIF, 38 }, { "", KUI_FONT_REG, 20 }, { link, KUI_FONT_REG, 26 },
	};
	for (size_t p = 0; p < sizeof parts / sizeof parts[0]; p++) {
		int k = parts[p].t[0] ? kui_wrap_text(parts[p].t, tw, parts[p].f, parts[p].px, tmp, 800) : 1;
		for (int i = 0; i < k && n < 1600; i++, n++)
			ls[n] = (RLine){ parts[p].t[0] ? parts[p].t + tmp[i].off : "", parts[p].t[0] ? tmp[i].len : 0, parts[p].f, parts[p].px };
	}
	int starts[200], n_pages = 0;
	for (int i = 0, y = top; i < n; i++) {
		int lh = kui_line_height(ls[i].f, ls[i].px);
		if (i == 0 || y + lh > bottom) {
			if (n_pages == 200) break;
			starts[n_pages++] = i;
			y = top;
		}
		y += lh;
	}
	KuiButton bnav[2] = { { { 20, h - 100, 160, 90 }, "◀", 1 }, { { w - 180, h - 100, 160, 90 }, "▶", 2 } };
	/* opening is one partial update of the whole screen (no flash; the caller's screen may have no topbar); a page
	 * turn refreshes only the text, down to where the longer of the two pages ends, and the page number */
	for (int pg = 0, upd = 1, prev_end = bottom;;) { /* upd: 0 flash (wake), 1 whole screen, 2 page turn */
		KuiRect text = { 30, top - 6, w - 60, bottom - (top - 6) }, count = { 190, h - 100, w - 380, 90 };
		if (upd < 2) { /* kui_topbar and kui_button mark themselves dirty: a page turn leaves them alone */
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T("Gazete"), NULL);
			if (n_pages > 1) {
				kui_button(&bnav[0], 0);
				kui_button(&bnav[1], 0);
			}
		}
		kui_fill(text, 255);
		kui_fill(count, 255);
		int end = pg + 1 < n_pages ? starts[pg + 1] : n, y = top;
		for (int i = starts[pg]; i < end; i++) {
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
		if (pg == 0) kui_fill((KuiRect){ 40, top + kui_line_height(KUI_FONT_BOLD, 28) - 4, tw, 2 }, 0);
		if (n_pages > 1) {
			char s[32];
			snprintf(s, sizeof s, "%d / %d", pg + 1, n_pages);
			kui_text(count, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
		}
		if (upd == 0) kui_flush(1);
		else {
			int y1 = (y > prev_end ? y : prev_end) + 6; /* below it both pages are blank */
			if (upd == 1) kui_mark_dirty((KuiRect){ 0, 0, w, h });
			else {
				if (y1 < bottom) text.h = y1 - text.y;
				kui_mark_dirty(text);
				kui_mark_dirty(count);
			}
			kui_flush(0);
		}
		prev_end = y;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return;
			if (e.type == KUI_EV_WAKE) { upd = 0; break; }
			int dlt = 0;
			if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
			if (e.type == KUI_EV_TAP) {
				if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
				int id = n_pages > 1 ? kui_buttons_hit(bnav, 2, e.x, e.y) : 0;
				dlt = id == 1 ? -1 : id == 2 ? 1 : 0;
			}
			if (dlt && pg + dlt >= 0 && pg + dlt < n_pages) {
				pg += dlt;
				upd = 2;
				break;
			}
		}
	}
}


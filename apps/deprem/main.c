/* Son Depremler: recent earthquakes in and around Türkiye from Kandilli (KRDAE), EMSC when Kandilli is down.
 * A map (circle size = magnitude, filled = last 24 h) and a list; tap a quake for its details. The last good
 * list is cached for offline use. Full refresh only when a screen is entered; in-screen changes are partial.
 * The map zooms (+/−, or a long press zooms in around that point) to 2× and 4×, swipes pan it; only the map is redrawn. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "deprem.h"
#include "kui.h"
#include "net/net.h"
#include "weather/wx.h"

#define FRESH_S (5 * 60) /* a cached list younger than this is shown without fetching */
#define URL_KANDILLI "http://www.koeri.boun.edu.tr/scripts/lst0.asp"
#define URL_EMSC "https://www.seismicportal.eu/fdsnws/event/1/query?format=json&limit=200&minlat=34&maxlat=44&minlon=24&maxlon=46"

/* layout (1072 × 1448) */
#define FY (KUI_TOPBAR_H + 18) /* filter row */
#define FH 86
#define CY (FY + FH + 22)      /* content top */
#define ROW_H 124
#define LIST_ROWS 8
#define TOP_ROWS 3            /* "En büyükler" rows under the map */
#define BAR_H 110              /* status bar at the bottom */

static Quake all[DQ_MAX];
static int n_all;
static DqSource src;
static long fetched; /* unix time of the shown list */
static int offline;  /* last fetch failed */
static const char *status_msg;
static int shown[DQ_MAX], n_shown; /* filtered, newest first */
static int top[TOP_ROWS], n_top;   /* largest of the filtered */
static int min_mag_i, period_i, view, page;
static WxCity city;
static DqView mview; /* zoom and centre of the main map */

static const double min_mags[3] = { 0, 3, 4 };
enum { BTN_ALL = 1, BTN_M3, BTN_M4, BTN_24H, BTN_7D, BTN_REFRESH, BTN_PREV, BTN_NEXT, BTN_ZIN, BTN_ZOUT, BTN_ZRESET };
static KuiButton fbtn[5], bar_btn, pnav[2], zbtn[3];
static int n_zbtn; /* zbtn[] drawn on the main map: +, −, and Türkiye when zoomed */

/* ---- data ---- */

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("deprem"));
	return p;
}

static const char *cache_path(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/last.txt", kui_data_dir("deprem"));
	return p;
}

static void load_city(void)
{
	static WxCity c[WX_MAX_CITIES];
	char p[300];
	snprintf(p, sizeof p, "%s/cities.txt", kui_data_dir("weather"));
	wx_cities_load(p, c, WX_MAX_CITIES); /* the weather app's first city (İstanbul / Maltepe when none) */
	city = c[0];
}

static void apply_filter(void)
{
	long now = (long) time(NULL);
	n_shown = dq_filter(all, n_all, min_mags[min_mag_i], now - (period_i ? 7 * 86400L : 86400L), shown, DQ_MAX);
	int tmp[DQ_MAX];
	memcpy(tmp, shown, sizeof(int) * (size_t) n_shown);
	dq_sort_by_mag(all, tmp, n_shown);
	n_top = n_shown < TOP_ROWS ? n_shown : TOP_ROWS;
	memcpy(top, tmp, sizeof(int) * (size_t) n_top);
	int pages = n_shown ? (n_shown + LIST_ROWS - 1) / LIST_ROWS : 1;
	if (page >= pages) page = pages - 1;
}

static int load_cache(void)
{
	static char buf[NET_MAX_BODY + 1];
	struct stat st;
	int len = kui_read_file(cache_path(), buf, sizeof buf);
	n_all = len > 0 ? dq_parse(buf, len, all, DQ_MAX, &src) : 0;
	fetched = n_all && stat(cache_path(), &st) == 0 ? (long) st.st_mtime : 0;
	apply_filter();
	return n_all;
}

/* one source into the cache; never replaces a good cache with an error page */
static int fetch_one(const char *url)
{
	static Quake probe[DQ_MAX];
	char *body = NULL;
	int len = 0, ok = 0;
	DqSource s;
	if (net_get(url, &body, &len, 25) == 0 && dq_parse(body, len, probe, DQ_MAX, &s) > 0) ok = kui_write_atomic(cache_path(), body, len) == 0;
	free(body);
	return ok;
}

/* Kandilli, then EMSC; Wi-Fi is switched on for the fetch when it was off, and off again afterwards */
static void fetch(void)
{
	int was_off = kui_wifi_enabled() == 0;
	if (was_off) {
		kui_wifi_enable(1);
		kui_wifi_wait(40);
	}
	int ok = fetch_one(URL_KANDILLI) || fetch_one(URL_EMSC);
	if (was_off) kui_wifi_enable(0);
	offline = !ok;
	load_cache();
}

/* ---- formatting ---- */

static void utf8_head(const char *s, int n, char *out, int cap) /* first n characters */
{
	int o = 0;
	for (; *s && n > 0 && o < cap - 4; n--) {
		do out[o++] = *s++;
		while ((*s & 0xC0) == 0x80);
	}
	out[o] = '\0';
}

static void tr_tm(long t, struct tm *tm)
{
	time_t lt = (time_t) (t + DQ_TR_OFFSET);
	gmtime_r(&lt, tm);
}

/* Türkiye time: "14:05" today, "18 Eki 23:42" before */
static void short_time(long t, char *out, int cap)
{
	struct tm tm, now;
	tr_tm(t, &tm);
	tr_tm((long) time(NULL), &now);
	if (tm.tm_yday == now.tm_yday && tm.tm_year == now.tm_year) {
		snprintf(out, (size_t) cap, "%02d:%02d", tm.tm_hour, tm.tm_min);
	} else {
		char mon[16];
		utf8_head(kui_tr_month(tm.tm_mon), 3, mon, sizeof mon);
		snprintf(out, (size_t) cap, "%d %s %02d:%02d", tm.tm_mday, mon, tm.tm_hour, tm.tm_min);
	}
}

static void fmt_mag(double m, char *out, int cap) { kui_fmt_num(m, 1, out, cap); }

static int dist_km(const Quake *q) { return (int) lround(dq_distance_km(city.lat, city.lon, q->lat, q->lon)); }

static const char *src_name(void) { return src == DQ_SRC_EMSC ? "EMSC" : "Kandilli"; }

/* ---- drawing primitives (clipped to a rectangle) ---- */

static KuiRect clip;

static void span(int x0, int x1, int y, unsigned char g)
{
	if (y < clip.y || y >= clip.y + clip.h) return;
	if (x0 < clip.x) x0 = clip.x;
	if (x1 >= clip.x + clip.w) x1 = clip.x + clip.w - 1;
	if (x1 >= x0) kui_fill((KuiRect){ x0, y, x1 - x0 + 1, 1 }, g);
}

static int half_width(int r, int dy) { return (int) sqrt((double) r * r - (double) dy * dy + r * 0.8); }

static void disc(int cx, int cy, int r, unsigned char g)
{
	for (int dy = -r; dy <= r; dy++) {
		int dx = half_width(r, dy);
		span(cx - dx, cx + dx, cy + dy, g);
	}
}

static void ring(int cx, int cy, int r, int t, unsigned char g)
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

static void line(int x0, int y0, int x1, int y1, unsigned char g)
{
	int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
	for (;;) {
		span(x0, x0 + 1, y0, g);
		span(x0, x0 + 1, y0 + 1, g);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) err += dy, x0 += sx;
		if (e2 <= dx) err += dx, y0 += sy;
	}
}

/* ---- map ---- */

/* the map fitted to area, magnified by v (NULL: the whole map) */
static void map_proj(KuiRect area, const DqView *v, DqProj *p)
{
	DqProj full;
	dq_proj_fit(&full, area.x, area.y, area.w, area.h, DQ_MAP_LON0, DQ_MAP_LON1, DQ_MAP_LAT0, DQ_MAP_LAT1);
	if (v) dq_proj_view(p, &full, v);
	else *p = full;
}

static void draw_land(const DqProj *p)
{
	static const unsigned char fill[3] = { 228, 196, 255 }; /* other land, Türkiye, lakes */
	for (int i = 0; i < dq_map_n_rings; i++) {
		const DqRing *r = &dq_map_rings[i];
		int ymin = 1 << 30, ymax = -(1 << 30);
		for (int k = 0; k < r->n; k++) {
			int x, y;
			dq_project(p, dq_map_pts[r->start + k][1] / 100.0, dq_map_pts[r->start + k][0] / 100.0, &x, &y);
			if (y < ymin) ymin = y;
			if (y > ymax) ymax = y;
		}
		if (ymin < clip.y) ymin = clip.y;
		if (ymax > clip.y + clip.h - 1) ymax = clip.y + clip.h - 1;
		for (int y = ymin; y <= ymax; y++) {
			int xs[64], n = dq_ring_scan(p, r, y, xs, 64);
			for (int k = 0; k + 1 < n; k += 2) span(xs[k], xs[k + 1], y, fill[r->kind]);
		}
	}
	for (int i = 0; i < dq_map_n_rings; i++) {
		const DqRing *r = &dq_map_rings[i];
		int px, py;
		dq_project(p, dq_map_pts[r->start + r->n - 1][1] / 100.0, dq_map_pts[r->start + r->n - 1][0] / 100.0, &px, &py);
		for (int k = 0; k < r->n; k++) {
			int x, y;
			dq_project(p, dq_map_pts[r->start + k][1] / 100.0, dq_map_pts[r->start + k][0] / 100.0, &x, &y);
			line(px, py, x, y, r->kind == 1 ? 40 : 110);
			px = x, py = y;
		}
	}
}

static int quake_r(const Quake *q) { return dq_radius(q->mag, 15); }

static void draw_quake(const DqProj *p, const Quake *q, int recent)
{
	int x, y, r = quake_r(q);
	dq_project(p, q->lat, q->lon, &x, &y);
	if (recent) {
		disc(x, y, r + 2, 255);
		disc(x, y, r, 0);
	} else {
		ring(x, y, r + 1, 5, 255);
		ring(x, y, r, 3, 0);
	}
}

/* the city's marker and name box; 0 when it is off the map */
static int city_rects(const DqProj *p, KuiRect *mark, KuiRect *b)
{
	int x, y;
	dq_project(p, city.lat, city.lon, &x, &y);
	if (!kui_rect_contains(clip, x, y)) return 0;
	*mark = (KuiRect){ x - 12, y - 12, 24, 24 };
	int tw = kui_text_width(city.name, KUI_FONT_BOLD, 26) + 16;
	*b = (KuiRect){ x + 18, y - 20, tw, 40 };
	if (b->x + b->w > clip.x + clip.w) b->x = x - 18 - tw;
	if (b->y < clip.y + 2) b->y = clip.y + 2;
	if (b->y + b->h > clip.y + clip.h - 2) b->y = clip.y + clip.h - 2 - b->h;
	return 1;
}

static void draw_city(const DqProj *p, int label)
{
	KuiRect m, b;
	if (!city_rects(p, &m, &b)) return;
	kui_fill(m, 255);
	kui_frame(m, 4, 0);
	kui_fill((KuiRect){ m.x + 8, m.y + 8, 8, 8 }, 0);
	if (!label) return;
	kui_fill(b, 255);
	kui_frame(b, 2, 0);
	kui_text(b, city.name, KUI_FONT_BOLD, 26, KUI_ALIGN_CENTER, 0);
}

static int overlaps(KuiRect a, KuiRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

/* province names for the zoom (more of them when magnified), each where it fits: inside the map, clear of the
 * zoom buttons, the city box and the names already placed (dq_cities is ordered by importance) */
static void draw_places(const DqProj *p, int zoom)
{
	static KuiRect taken[100];
	int nt = 0;
	KuiRect m, b;
	for (int i = 0; i < n_zbtn; i++) taken[nt++] = zbtn[i].r;
	if (n_zbtn) taken[nt++] = kui_rect_union(zbtn[0].r, zbtn[1].r); /* the zoom level between + and − */
	if (city_rects(p, &m, &b)) taken[nt++] = m, taken[nt++] = b;
	KuiRect inner = { clip.x + 4, clip.y + 4, clip.w - 8, clip.h - 8 };
	for (int i = 0; i < dq_n_cities && nt < 100; i++) {
		const DqCity *c = &dq_cities[i];
		if (c->zoom > zoom || !strcmp(c->name, city.name)) continue;
		int x, y, tw = kui_text_width(c->name, KUI_FONT_REG, 24);
		dq_project(p, c->lat, c->lon, &x, &y);
		KuiRect dot = { x - 4, y - 4, 8, 8 }, t = { x + 10, y - 16, tw + 4, 32 };
		if (t.x + t.w > inner.x + inner.w) t.x = x - 10 - t.w;
		KuiRect both = kui_rect_union(dot, t);
		if (both.x < inner.x || both.y < inner.y || both.x + both.w > inner.x + inner.w || both.y + both.h > inner.y + inner.h) continue;
		int free = 1;
		for (int k = 0; k < nt && free; k++)
			if (overlaps((KuiRect){ both.x - 6, both.y - 2, both.w + 12, both.h + 4 }, taken[k])) free = 0;
		if (!free) continue;
		taken[nt++] = both;
		kui_fill(dot, 0);
		kui_text(t, c->name, KUI_FONT_REG, 24, KUI_ALIGN_LEFT, 0);
	}
}

/* +, the zoom level, − down the right edge of the map; Türkiye (back to 1×) beside + when zoomed */
static void layout_zoom(KuiRect area, int zoom)
{
	int s = 84, x = area.x + area.w - s - 12, y = area.y + 12;
	zbtn[0] = (KuiButton){ { x, y, s, s }, "+", BTN_ZIN };
	zbtn[1] = (KuiButton){ { x, y + s + 50, s, s }, "−", BTN_ZOUT };
	n_zbtn = 2;
	if (zoom > 1) {
		const char *l = T("Türkiye");
		int w = kui_text_width(l, KUI_FONT_BOLD, 34) + 40;
		zbtn[n_zbtn++] = (KuiButton){ { x - 12 - w, y, w, s }, l, BTN_ZRESET };
	}
}

static void draw_zoom(int zoom)
{
	char s[8];
	KuiRect lv = { zbtn[0].r.x, zbtn[0].r.y + zbtn[0].r.h, zbtn[0].r.w, zbtn[1].r.y - zbtn[0].r.y - zbtn[0].r.h };
	for (int i = 0; i < n_zbtn; i++) kui_button(&zbtn[i], 0);
	snprintf(s, sizeof s, "%d×", zoom);
	kui_fill(lv, 255);
	kui_fill((KuiRect){ lv.x, lv.y, 4, lv.h }, 0);
	kui_fill((KuiRect){ lv.x + lv.w - 4, lv.y, 4, lv.h }, 0);
	kui_text(lv, s, KUI_FONT_BOLD, 30, KUI_ALIGN_CENTER, 0);
}

/* the map with the quakes idx[0..n) (only `one` when >= 0); v: the main map's zoom (with names and zoom buttons),
 * NULL for the detail screen's whole map */
static void draw_map(KuiRect area, const int *idx, int n, int one, const DqView *v)
{
	DqProj p;
	map_proj(area, v, &p);
	area = (KuiRect){ p.x, p.y, p.w, p.h };
	clip = area;
	n_zbtn = 0;
	if (v) layout_zoom(area, v->zoom);
	kui_fill(area, 255);
	draw_land(&p);
	if (v) draw_places(&p, v->zoom);
	long day_ago = (long) time(NULL) - 86400;
	if (one >= 0) {
		draw_quake(&p, &all[one], 1);
		int x, y;
		dq_project(&p, all[one].lat, all[one].lon, &x, &y);
		int r = quake_r(&all[one]) + 14;
		ring(x, y, r, 4, 0); /* a target ring around it */
	} else {
		/* older (hollow) under recent (filled); within each, large under small so every dot stays visible */
		static int order[DQ_MAX];
		memcpy(order, idx, sizeof(int) * (size_t) n);
		dq_sort_by_mag(all, order, n);
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < n; i++) {
				const Quake *q = &all[order[i]];
				int recent = q->t >= day_ago;
				if (recent == pass) draw_quake(&p, q, recent);
			}
	}
	draw_city(&p, 1);
	if (v) draw_zoom(v->zoom);
	kui_frame(area, 2, 0);
	kui_mark_dirty(area);
}

static KuiRect map_rect(void)
{
	int w = kui_width();
	return (KuiRect){ 20, CY, w - 40, dq_proj_height(w - 40, DQ_MAP_LON0, DQ_MAP_LON1, DQ_MAP_LAT0, DQ_MAP_LAT1) };
}

/* nearest drawn quake to a tap on the map, -1 if none is close */
static int map_hit(int tx, int ty)
{
	DqProj p;
	map_proj(map_rect(), &mview, &p);
	KuiRect area = { p.x, p.y, p.w, p.h };
	int best = -1;
	double bd = 1e9;
	for (int i = 0; i < n_shown; i++) {
		int x, y;
		dq_project(&p, all[shown[i]].lat, all[shown[i]].lon, &x, &y);
		if (!kui_rect_contains(area, x, y)) continue; /* off the zoomed map */
		double d = hypot(x - tx, y - ty) - quake_r(&all[shown[i]]);
		if (d < bd) bd = d, best = shown[i];
	}
	return bd <= 36 ? best : -1;
}

/* ---- screen parts ---- */

static void draw_filters(void)
{
	int w = kui_width(), bw = 150, pw = 200;
	const char *labels[5] = { T("Tümü"), "≥ 3", "≥ 4", T("24 saat"), T("7 gün") };
	for (int i = 0; i < 3; i++) fbtn[i] = (KuiButton){ { 20 + i * (bw + 12), FY, bw, FH }, labels[i], BTN_ALL + i };
	for (int i = 0; i < 2; i++) fbtn[3 + i] = (KuiButton){ { w - 20 - 2 * pw - 12 + i * (pw + 12), FY, pw, FH }, labels[3 + i], BTN_24H + i };
	for (int i = 0; i < 5; i++) kui_button(&fbtn[i], i < 3 ? i == min_mag_i : i - 3 == period_i);
}

static void draw_status(int with_btn)
{
	int w = kui_width(), h = kui_height();
	KuiRect bar = { 0, h - BAR_H, w, BAR_H };
	KuiRect txt = { 24, bar.y + 6, w - 330, BAR_H - 12 };
	char l1[200], t[64], a[48];
	kui_fill(bar, 255);
	kui_fill((KuiRect){ 20, bar.y, w - 40, 2 }, 0);
	const char *l2 = n_all ? (src == DQ_SRC_EMSC ? T("Kaynak: EMSC") : T("Kaynak: Boğaziçi Üni. Kandilli Rasathanesi (KRDAE)")) : "";
	if (status_msg) {
		snprintf(l1, sizeof l1, "%s", status_msg);
	} else if (n_all) {
		short_time(fetched, t, sizeof t);
		if (offline) {
			dq_ago((long) time(NULL), fetched, a, sizeof a);
			char ta[120];
			snprintf(ta, sizeof ta, "%s (%s)", t, a);
			snprintf(l1, sizeof l1, T("Çevrimdışı — son veri %s · %s"), ta, src_name());
		} else {
			snprintf(l1, sizeof l1, T("Güncellendi %s · %s"), t, src_name());
		}
	} else {
		snprintf(l1, sizeof l1, "%s", offline ? T("Bağlantı yok, veri alınamadı") : T("Yükleniyor…"));
	}
	kui_text((KuiRect){ txt.x, txt.y, txt.w, 52 }, l1, KUI_FONT_BOLD, kui_fit_px(l1, KUI_FONT_BOLD, 30, txt.w), KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ txt.x, txt.y + 50, txt.w, 46 }, l2, KUI_FONT_REG, kui_fit_px(l2, KUI_FONT_REG, 26, txt.w), KUI_ALIGN_LEFT, 0);
	bar_btn = (KuiButton){ { w - 280, h - 100, 256, 88 }, T("Yenile"), BTN_REFRESH };
	if (with_btn) kui_button(&bar_btn, 0);
	kui_mark_dirty(bar);
}

/* a list row: magnitude, place, "12 dk önce · 00:49 · derinlik 7 km", distance from the city */
static void draw_row(KuiRect r, const Quake *q)
{
	char s[240], m[16], ago[48], tm[48], dep[24];
	kui_fill(r, 255);
	fmt_mag(q->mag, m, sizeof m);
	KuiRect mb = { r.x, r.y + 12, 140, r.h - 24 };
	if (q->mag >= 4) {
		kui_fill(mb, 0);
		kui_text_on(mb, m, KUI_FONT_BOLD, 60, KUI_ALIGN_CENTER, 255, 0);
	} else {
		kui_frame(mb, 2, 0);
		kui_text(mb, m, KUI_FONT_BOLD, q->mag >= 3 ? 60 : 50, KUI_ALIGN_CENTER, 0);
	}
	int tx = r.x + 165, rw = 190, tw = r.w - 165 - rw;
	kui_text((KuiRect){ tx, r.y + 10, tw, 56 }, q->place, KUI_FONT_BOLD, kui_fit_px(q->place, KUI_FONT_BOLD, 38, tw), KUI_ALIGN_LEFT, 0);
	dq_ago((long) time(NULL), q->t, ago, sizeof ago);
	short_time(q->t, tm, sizeof tm);
	kui_fmt_num(q->depth, q->depth < 10 ? 1 : 0, dep, sizeof dep);
	char d2[64];
	snprintf(d2, sizeof d2, T("derinlik %s km"), dep);
	snprintf(s, sizeof s, "%s · %s · %s", ago, tm, d2);
	kui_text((KuiRect){ tx, r.y + 66, tw, 46 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, tw), KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, "%d km", dist_km(q));
	kui_text((KuiRect){ r.x + r.w - rw, r.y + 10, rw, 56 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 36, rw), KUI_ALIGN_RIGHT, 0);
	kui_text((KuiRect){ r.x + r.w - rw, r.y + 66, rw, 46 }, city.name, KUI_FONT_REG, kui_fit_px(city.name, KUI_FONT_REG, 26, rw), KUI_ALIGN_RIGHT, 0);
	kui_fill((KuiRect){ r.x, r.y + r.h - 1, r.w, 1 }, 0);
	kui_mark_dirty(r);
}

static void empty_note(KuiRect r)
{
	const char *s = n_all ? T("Bu süzgeçle deprem yok.") : offline ? T("Deprem listesi alınamadı.\nWi-Fi bağlantısını kontrol edip Yenile'ye basın.") : T("Yükleniyor…");
	kui_fill(r, 255);
	kui_text(r, s, KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
}

/* legend under the map: ● son 24 saat  ○ daha eski  ▣ city  ·  n deprem */
static KuiRect legend_rect(void)
{
	KuiRect m = map_rect();
	return (KuiRect){ 20, m.y + m.h + 8, kui_width() - 40, 56 };
}

static void draw_legend(void)
{
	KuiRect r = legend_rect();
	char s[64];
	kui_fill(r, 255);
	clip = r;
	int x = r.x + 16, cy = r.y + r.h / 2;
	const char *a = T("son 24 saat"), *b = T("daha eski");
	disc(x, cy, 12, 0);
	kui_text((KuiRect){ x + 22, r.y, 300, r.h }, a, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0);
	x += 22 + kui_text_width(a, KUI_FONT_REG, 28) + 40;
	ring(x, cy, 12, 3, 0);
	kui_text((KuiRect){ x + 22, r.y, 300, r.h }, b, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0);
	x += 22 + kui_text_width(b, KUI_FONT_REG, 28) + 40;
	kui_frame((KuiRect){ x - 12, cy - 12, 24, 24 }, 4, 0);
	kui_fill((KuiRect){ x - 4, cy - 4, 8, 8 }, 0);
	kui_text((KuiRect){ x + 22, r.y, 300, r.h }, city.name, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, T("%d deprem"), n_shown);
	kui_text((KuiRect){ r.x + r.w - 260, r.y, 250, r.h }, s, KUI_FONT_BOLD, 30, KUI_ALIGN_RIGHT, 0);
	kui_mark_dirty(r);
}

static KuiRect top_rect(int i)
{
	KuiRect l = legend_rect();
	return (KuiRect){ 20, l.y + l.h + 70 + i * ROW_H, kui_width() - 40, ROW_H };
}

static void draw_map_view(void)
{
	draw_map(map_rect(), shown, n_shown, -1, &mview);
	draw_legend();
	KuiRect l = legend_rect();
	KuiRect hdr = { 20, l.y + l.h + 8, kui_width() - 40, 62 };
	kui_fill(hdr, 255);
	kui_fill((KuiRect){ hdr.x, hdr.y, hdr.w, 3 }, 0);
	kui_text((KuiRect){ hdr.x, hdr.y + 4, hdr.w, hdr.h - 4 }, T("En büyükler"), KUI_FONT_SERIF_BOLD, 34, KUI_ALIGN_LEFT, 0);
	kui_mark_dirty(hdr);
	KuiRect all_rows = kui_rect_union(top_rect(0), top_rect(TOP_ROWS - 1));
	if (!n_top) {
		empty_note(all_rows);
		return;
	}
	for (int i = 0; i < TOP_ROWS; i++) {
		if (i < n_top) draw_row(top_rect(i), &all[top[i]]);
		else {
			kui_fill(top_rect(i), 255);
			kui_mark_dirty(top_rect(i));
		}
	}
}

static int pages(void) { return n_shown ? (n_shown + LIST_ROWS - 1) / LIST_ROWS : 1; }

static KuiRect list_row_rect(int i) { return (KuiRect){ 20, CY + i * ROW_H, kui_width() - 40, ROW_H }; }

static void draw_pagenav(void)
{
	int w = kui_width(), y = CY + LIST_ROWS * ROW_H + 4;
	KuiRect r = { 20, y, w - 40, 84 };
	char s[48];
	kui_fill(r, 255);
	pnav[0] = (KuiButton){ { 20, y + 2, 160, 80 }, "◀", BTN_PREV };
	pnav[1] = (KuiButton){ { w - 180, y + 2, 160, 80 }, "▶", BTN_NEXT };
	if (pages() > 1) {
		kui_button(&pnav[0], 0);
		kui_button(&pnav[1], 0);
		snprintf(s, sizeof s, T("Sayfa %d/%d"), page + 1, pages());
		kui_text((KuiRect){ 200, y, w - 400, 84 }, s, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty(r);
}

static void draw_list_rows(void)
{
	if (!n_shown) {
		empty_note(kui_rect_union(list_row_rect(0), list_row_rect(LIST_ROWS - 1)));
		return;
	}
	for (int i = 0; i < LIST_ROWS; i++) {
		int k = page * LIST_ROWS + i;
		if (k < n_shown) draw_row(list_row_rect(i), &all[shown[k]]);
		else {
			kui_fill(list_row_rect(i), 255);
			kui_mark_dirty(list_row_rect(i));
		}
	}
}

static void draw_content(void)
{
	if (view == 0) draw_map_view();
	else {
		draw_list_rows();
		draw_pagenav();
	}
}

static void draw_screen(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Son Depremler"), view == 0 ? T("Liste") : T("Harita"));
	draw_filters();
	draw_content();
	draw_status(1);
	kui_flush(1);
}

/* ---- detail ---- */

static void detail_row(int y, const char *label, const char *value)
{
	int w = kui_width();
	kui_text((KuiRect){ 30, y, 330, 64 }, label, KUI_FONT_REG, kui_fit_px(label, KUI_FONT_REG, 32, 320), KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ 370, y, w - 400, 64 }, value, KUI_FONT_BOLD, kui_fit_px(value, KUI_FONT_BOLD, 36, w - 400), KUI_ALIGN_LEFT, 0);
	kui_fill((KuiRect){ 30, y + 66, w - 60, 1 }, 0);
}

static void draw_detail(int k)
{
	const Quake *q = &all[k];
	int w = kui_width(), h = kui_height();
	char s[200], a[64], b[64];
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Deprem"), NULL);
	int y = KUI_TOPBAR_H + 30;
	/* magnitude block and place */
	KuiRect mb = { 30, y, 250, 200 };
	fmt_mag(q->mag, a, sizeof a);
	kui_fill(mb, 0);
	kui_text_on((KuiRect){ mb.x, mb.y + 10, mb.w, 140 }, a, KUI_FONT_BOLD, 120, KUI_ALIGN_CENTER, 255, 0);
	kui_text_on((KuiRect){ mb.x, mb.y + 145, mb.w, 45 }, q->mtype, KUI_FONT_BOLD, 32, KUI_ALIGN_CENTER, 255, 0);
	int tx = 310, tw = w - tx - 30;
	static KuiLine lines[8];
	int nl = kui_wrap_text(q->place, tw, KUI_FONT_SERIF_BOLD, 52, lines, 8), lh = kui_line_height(KUI_FONT_SERIF_BOLD, 52);
	if (nl > 2) nl = 2;
	for (int i = 0; i < nl; i++) {
		char ln[128];
		int len = lines[i].len < (int) sizeof ln - 1 ? lines[i].len : (int) sizeof ln - 1;
		memcpy(ln, q->place + lines[i].off, (size_t) len);
		ln[len] = '\0';
		kui_text((KuiRect){ tx, y + 10 + i * lh, tw, lh }, ln, KUI_FONT_SERIF_BOLD, 52, KUI_ALIGN_LEFT, 0);
	}
	dq_ago((long) time(NULL), q->t, a, sizeof a);
	kui_text((KuiRect){ tx, y + 145, tw, 50 }, a, KUI_FONT_REG, 36, KUI_ALIGN_LEFT, 0);
	y += 230;
	/* facts */
	struct tm tm;
	char date[96];
	tr_tm(q->t, &tm);
	kui_tr_date(&tm, date, sizeof date);
	snprintf(s, sizeof s, "%s · %02d:%02d:%02d", date, tm.tm_hour, tm.tm_min, tm.tm_sec);
	detail_row(y, T("Türkiye saati"), s);
	kui_fmt_num(q->depth, 1, a, sizeof a);
	snprintf(s, sizeof s, "%s km", a);
	detail_row(y + 72, T("Derinlik"), s);
	kui_fmt_num(q->lat, 4, a, sizeof a);
	kui_fmt_num(q->lon, 4, b, sizeof b);
	snprintf(s, sizeof s, T("%s° K, %s° D"), a, b);
	detail_row(y + 144, T("Koordinatlar"), s);
	snprintf(s, sizeof s, T("%d km (%s)"), dist_km(q), city.name);
	detail_row(y + 216, T("Uzaklık"), s);
	snprintf(s, sizeof s, "%s · %s", src == DQ_SRC_EMSC ? "EMSC" : "Kandilli (KRDAE)", q->mtype);
	detail_row(y + 288, T("Kaynak"), s);
	y += 380;
	KuiRect m = { 20, y, w - 40, h - BAR_H - 20 - y };
	draw_map(m, NULL, 0, k, NULL);
	draw_status(0); /* no Yenile here: the bar only says when the data was fetched */
	kui_flush(1);
}

static void detail_screen(int k)
{
	draw_detail(k);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type == KUI_EV_WAKE) { draw_detail(k); continue; }
		if (e.type == KUI_EV_SWIPE && e.dir == KUI_DIR_RIGHT) return;
		if (e.type == KUI_EV_TAP && kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) return;
	}
}

/* ---- main ---- */

static void refresh(void)
{
	status_msg = T("Güncelleniyor…");
	draw_status(1);
	kui_flush(0);
	fetch();
	status_msg = NULL;
	draw_content();
	draw_status(1);
	kui_flush(0);
}

static void save_settings(void)
{
	kui_cfg_set_int(settings(), "min_mag", min_mag_i);
	kui_cfg_set_int(settings(), "period", period_i);
	kui_cfg_set_int(settings(), "view", view);
}

/* zoom / pan: only the map rectangle changes (partial refresh) */
static void redraw_map(void)
{
	draw_map(map_rect(), shown, n_shown, -1, &mview);
	kui_flush(0);
}

/* a long press on the map: zoom in around that point (at 4× it only moves there) */
static void zoom_at(int x, int y)
{
	DqProj p;
	map_proj(map_rect(), &mview, &p);
	dq_unproject(&p, x, y, &mview.lat, &mview.lon);
	dq_view_zoom(&mview, 1);
	redraw_map();
}

static void turn_page(int d)
{
	if (page + d < 0 || page + d >= pages()) return;
	page += d;
	draw_list_rows();
	draw_pagenav();
	kui_flush(0);
}

int main(void)
{
	if (kui_init("deprem") != 0) return 1;
	min_mag_i = kui_cfg_get_int(settings(), "min_mag", 0) % 3;
	period_i = kui_cfg_get_int(settings(), "period", 0) % 2;
	view = kui_cfg_get_int(settings(), "view", 0) % 2;
	dq_view_reset(&mview);
	load_city();
	load_cache();
	draw_screen();
	if (!n_all || time(NULL) - fetched > FRESH_S) refresh();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) {
			apply_filter(); /* "24 saat" moves on while asleep */
			draw_screen();
			if (time(NULL) - fetched > FRESH_S) refresh();
			continue;
		}
		if (e.type == KUI_EV_SWIPE && view == 1) {
			if (e.dir == KUI_DIR_LEFT) turn_page(1);
			else if (e.dir == KUI_DIR_RIGHT) turn_page(-1);
			continue;
		}
		if (e.type == KUI_EV_SWIPE && view == 0) { /* pan: the map follows the finger, half a view at a time */
			double fx = e.dir == KUI_DIR_LEFT ? 0.5 : e.dir == KUI_DIR_RIGHT ? -0.5 : 0;
			double fy = e.dir == KUI_DIR_UP ? 0.5 : e.dir == KUI_DIR_DOWN ? -0.5 : 0;
			if (dq_view_pan(&mview, fx, fy)) redraw_map();
			continue;
		}
		if (e.type == KUI_EV_LONGPRESS && view == 0 && kui_rect_contains(map_rect(), e.x, e.y) && !kui_buttons_hit(zbtn, n_zbtn, e.x, e.y)) {
			zoom_at(e.x, e.y);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) { /* Liste ⇄ Harita: a new screen */
			view = !view;
			page = 0;
			save_settings();
			draw_screen();
			continue;
		}
		int id = kui_buttons_hit(fbtn, 5, e.x, e.y);
		if (id >= BTN_ALL && id <= BTN_7D) {
			int m = id <= BTN_M4 ? id - BTN_ALL : min_mag_i, p = id >= BTN_24H ? id - BTN_24H : period_i;
			if (m == min_mag_i && p == period_i) continue;
			min_mag_i = m;
			period_i = p;
			page = 0;
			save_settings();
			apply_filter();
			draw_filters();
			draw_content();
			kui_flush(0);
			continue;
		}
		if (kui_buttons_hit(&bar_btn, 1, e.x, e.y) == BTN_REFRESH) {
			offline = 0;
			refresh();
			continue;
		}
		int k = -1;
		if (view == 0) {
			int z = kui_buttons_hit(zbtn, n_zbtn, e.x, e.y);
			if (z) {
				int changed = z == BTN_ZRESET || dq_view_zoom(&mview, z == BTN_ZIN ? 1 : -1);
				if (z == BTN_ZRESET) dq_view_reset(&mview);
				if (changed) redraw_map();
				continue;
			}
			if (kui_rect_contains(map_rect(), e.x, e.y)) k = map_hit(e.x, e.y);
			for (int i = 0; i < n_top; i++)
				if (kui_rect_contains(top_rect(i), e.x, e.y)) k = top[i];
		} else {
			if (pages() > 1) {
				int b = kui_buttons_hit(pnav, 2, e.x, e.y);
				if (b == BTN_PREV) { turn_page(-1); continue; }
				if (b == BTN_NEXT) { turn_page(1); continue; }
			}
			for (int i = 0; i < LIST_ROWS; i++)
				if (kui_rect_contains(list_row_rect(i), e.x, e.y) && page * LIST_ROWS + i < n_shown) k = shown[page * LIST_ROWS + i];
		}
		if (k >= 0) {
			detail_screen(k);
			draw_screen();
		}
	}
	kui_quit();
	return 0;
}

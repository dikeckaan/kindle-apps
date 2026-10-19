/* Vakitler: prayer times (Diyanet method by default), sunrise/sunset, the Moon, the Hijri date, a monthly imsakiye
 * and the qibla — all computed offline (apps/vakit/vakit.c), no network.
 * Main screen: the next prayer's countdown is redrawn once a minute with a partial refresh of only its box; the table
 * row moves with a partial refresh when a time passes; a full refresh happens on entering a screen and at midnight.
 * Battery saver (as Masa Saati): after the idle time without a touch, or at once with the power button, the Kindle
 * suspends between minutes (Wi-Fi off; the battery is read rarely). On the charger it stays awake.
 * The place is saved as data/vakit/place ("name|lat|lon|tz_min|dst", vakit.h) so other apps can read it; until one
 * is chosen the first Hava Durumu city is used. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "vakit.h"

#define W kui_width()
#define H kui_height()

static VkPlace place;
static VkOpts opts;
static long skew; /* simulator: each "none" step moves the clock a minute on */
static int deep;  /* in the battery saver's sleep loop */
static int quit_requested;

/* ---- time ---- */

static int sim(void) { return getenv("KUI_SIM") != NULL; }

static long now_s(void)
{
	const char *f = getenv("VAKIT_TIME"); /* simulator and screenshots: a fixed Unix time */
	return (f && *f ? atol(f) : (long) time(NULL)) + skew;
}

static int device_offset(void)
{
	time_t t = (time_t) now_s();
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_gmtoff || getenv("TZ") ? (int) tm.tm_gmtoff : 3 * 3600; /* no zone on the device: Türkiye */
}

static int utc_off(void) { return place.dst == VK_DST_DEVICE ? device_offset() : vk_utc_offset(&place, now_s()); }

static void fmt_min(int min, char out[8])
{
	if (min < 0) {
		snprintf(out, 8, "—");
		return;
	}
	min = ((min % 1440) + 1440) % 1440;
	snprintf(out, 8, "%02d:%02d", min / 60, min % 60);
}

static void fmt_hours(double h, char out[8]) { fmt_min(isnan(h) ? -1 : (int) floor(h * 60 + 0.5), out); }

static void fmt_hijri(const VkHijri *h, char *out, int cap) { snprintf(out, (size_t) cap, "%d %s %d", h->d, T(vk_hijri_months[h->m - 1]), h->y); }

static void fmt_date(int y, int m, int d, char *out, int cap)
{
	struct tm tm;
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_wday = vk_wday(vk_jdn(y, m, d));
	kui_tr_date(&tm, out, cap);
}

/* ---- files ---- */

static const char *path(const char *name)
{
	static char p[4][320];
	static int k;
	k = (k + 1) % 4;
	snprintf(p[k], sizeof p[k], "%s/%s", kui_data_dir("vakit"), name);
	return p[k];
}
#define CFG path("settings")
static int cfg(const char *key, int def) { return kui_cfg_get_int(CFG, key, def); }
static void cfg_set(const char *key, int v) { kui_cfg_set_int(CFG, key, v); }
#define IDLE_S (kui_saver_get(CFG, KUI_SAVER_IDLE_MIN) * 60L)

static int in_turkey(double lat, double lon) { return lat > 35.8 && lat < 42.2 && lon > 25.6 && lon < 44.9; }

/* the first Hava Durumu city ("name|admin|lat|lon"), 0 = found */
static int weather_city(VkPlace *p)
{
	char buf[2048], path_w[320];
	snprintf(path_w, sizeof path_w, "%s/cities.txt", kui_data_dir("weather"));
	if (kui_read_file(path_w, buf, sizeof buf) <= 0) return -1;
	buf[strcspn(buf, "\n")] = '\0';
	char *a = strchr(buf, '|'), *b = a ? strchr(a + 1, '|') : NULL;
	if (!a || !b || a == buf) return -1;
	double lat, lon;
	if (sscanf(b + 1, "%lf|%lf", &lat, &lon) != 2 || fabs(lat) > 90 || fabs(lon) > 180) return -1;
	memset(p, 0, sizeof *p);
	*a = '\0';
	snprintf(p->name, sizeof p->name, "%.63s", buf);
	p->lat = lat;
	p->lon = lon;
	p->tz_min = 180;
	p->dst = in_turkey(lat, lon) ? VK_DST_NONE : VK_DST_DEVICE;
	return 0;
}

static void load_place(void)
{
	char buf[256];
	if (kui_read_file(path("place"), buf, sizeof buf) > 0 && vk_place_parse(buf, &place) == 0) return;
	if (weather_city(&place) == 0) return;
	vk_place_from_def(&vk_tr_cities[33], &place); /* İstanbul */
}

static void save_place(void)
{
	char buf[256];
	vk_place_format(&place, buf, sizeof buf);
	if (kui_write_atomic(path("place"), buf, (int) strlen(buf)) != 0) fprintf(stderr, "vakit: place not saved\n");
}

static void load_opts(void)
{
	opts.method = cfg("method", VK_DIYANET);
	opts.hanafi = cfg("hanafi", 0);
	opts.ramadan = 0;
}

/* ---- drawing helpers ---- */

static void hline(int x, int y, int w, int t, unsigned char g) { kui_fill((KuiRect){ x, y, w, t }, g); }

/* a ring (thick > 0) or a disc (thick = 0) */
static void circle(int cx, int cy, int r, int thick, unsigned char g)
{
	for (int dy = -r; dy <= r; dy++) {
		int xo = (int) sqrt((double) r * r - (double) dy * dy);
		int ri = r - thick;
		if (!thick || abs(dy) >= ri) {
			kui_fill((KuiRect){ cx - xo, cy + dy, 2 * xo + 1, 1 }, g);
			continue;
		}
		int xi = (int) sqrt((double) ri * ri - (double) dy * dy);
		kui_fill((KuiRect){ cx - xo, cy + dy, xo - xi, 1 }, g);
		kui_fill((KuiRect){ cx + xi + 1, cy + dy, xo - xi, 1 }, g);
	}
}

static void line(double x0, double y0, double x1, double y1, int t, unsigned char g)
{
	double dx = x1 - x0, dy = y1 - y0;
	int n = (int) (fmax(fabs(dx), fabs(dy))) + 1;
	for (int i = 0; i <= n; i++) {
		double x = x0 + dx * i / n, y = y0 + dy * i / n;
		kui_fill((KuiRect){ (int) (x - t / 2.0), (int) (y - t / 2.0), t, t }, g);
	}
}

static void triangle(double ax, double ay, double bx, double by, double cx, double cy, unsigned char g)
{
	double ys[3] = { ay, by, cy }, xs[3] = { ax, bx, cx };
	int y0 = (int) floor(fmin(ay, fmin(by, cy))), y1 = (int) ceil(fmax(ay, fmax(by, cy)));
	for (int y = y0; y <= y1; y++) {
		double yc = y + 0.5, lo = 1e9, hi = -1e9;
		for (int i = 0, j = 2; i < 3; j = i++) {
			if ((ys[i] <= yc) == (ys[j] <= yc)) continue;
			double x = xs[j] + (yc - ys[j]) * (xs[i] - xs[j]) / (ys[i] - ys[j]);
			lo = fmin(lo, x);
			hi = fmax(hi, x);
		}
		if (hi >= lo) kui_fill((KuiRect){ (int) lo, y, (int) (hi - lo) + 1, 1 }, g);
	}
}

static const char *cardinal(double az)
{
	static const char *const n[8] = { N_("K"), N_("KD"), N_("D"), N_("GD"), N_("G"), N_("GB"), N_("B"), N_("KB") };
	return T(n[(int) floor(az / 45 + 0.5) % 8]);
}

/* ---- the main screen ---- */

static VkNow now_v;
static VkDay today;
static VkHijri hijri_today;
static int shown_min = -1, shown_cur = -1, shown_next = -1, shown_d = -1;

static void compute(void)
{
	VkNow n;
	VkDay d;
	opts.ramadan = 0;
	vk_now(now_s(), place.lat, place.lon, utc_off(), &opts, &n, &d);
	vk_hijri(vk_jdn(n.y, n.m, n.d), &hijri_today);
	if (hijri_today.m == 9 && opts.method == VK_UMMALQURA) {
		opts.ramadan = 1;
		vk_now(now_s(), place.lat, place.lon, utc_off(), &opts, &n, &d);
	}
	now_v = n;
	today = d;
}

static KuiRect r_city(void) { return (KuiRect){ 140, KUI_TOPBAR_H + 6, W - 280, 76 }; }
static KuiRect r_date(void) { return (KuiRect){ 20, KUI_TOPBAR_H + 84, W - 40, 50 }; }
static KuiRect r_count(void) { return (KuiRect){ 30, 270, W - 60, 250 }; }
#define TABLE_Y 545
#define ROW_H 96
static KuiRect r_row(int i) { return (KuiRect){ 30, TABLE_Y + i * ROW_H, W - 60, ROW_H - 6 }; }
static KuiRect r_info(void) { return (KuiRect){ 30, TABLE_Y + VK_N * ROW_H + 10, W - 60, 170 }; }

enum { B_IMSAKIYE = 1, B_KIBLE, B_SEHIR };
static KuiButton home_btn[3];

static void draw_header(void)
{
	char s[256], h[80];
	KuiRect c = r_city(), d = r_date();
	kui_fill(c, 255);
	snprintf(s, sizeof s, "%s ▾", place.name);
	kui_text(c, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 50, c.w), KUI_ALIGN_CENTER, 0);
	kui_fill(d, 255);
	char ds[80];
	fmt_date(now_v.y, now_v.m, now_v.d, ds, sizeof ds);
	fmt_hijri(&hijri_today, h, sizeof h);
	snprintf(s, sizeof s, "%s · %s", ds, h);
	kui_text(d, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 32, d.w), KUI_ALIGN_CENTER, 60);
	kui_mark_dirty(c);
	kui_mark_dirty(d);
}

static void draw_countdown(void)
{
	KuiRect r = r_count();
	char left[48], s[96], t[8], sub[160];
	kui_fill(r, 255);
	kui_frame(r, 4, 0);
	if (now_v.next < 0) {
		kui_text(r, T("Bugün vakit hesaplanamıyor"), KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0);
	} else {
		vk_fmt_left(now_v.next_at - now_s(), left, sizeof left, T("%d sa %d dk"), T("%d sa"), T("%d dk"));
		snprintf(s, sizeof s, T(vk_until_fmt[now_v.next]), left);
		KuiRect big = { r.x + 20, r.y + 20, r.w - 40, 150 };
		kui_text(big, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 112, big.w), KUI_ALIGN_CENTER, 0);
		long at_local = now_v.next_at + utc_off();
		fmt_min((int) ((at_local % 86400 + 86400) % 86400 / 60), t);
		int tomorrow = (now_v.next_at + utc_off()) / 86400 != (now_s() + utc_off()) / 86400;
		snprintf(sub, sizeof sub, tomorrow ? T("Yarın %s %s") : T("%s vakti %s"), T(vk_names[now_v.next]), t);
		kui_text((KuiRect){ r.x + 20, r.y + 170, r.w - 40, 64 }, sub, KUI_FONT_REG, kui_fit_px(sub, KUI_FONT_REG, 44, r.w - 40), KUI_ALIGN_CENTER, 0);
	}
	if (deep) {
		const char *z = T("☾ uykuda");
		kui_text((KuiRect){ r.x + r.w - 220, r.y + r.h - 50, 200, 40 }, z, KUI_FONT_REG, 26, KUI_ALIGN_RIGHT, 90);
	}
	kui_mark_dirty(r);
	shown_min = now_v.now_min;
}

static void draw_row(int i)
{
	KuiRect r = r_row(i);
	int cur = i == now_v.cur;
	unsigned char bg = cur ? 0 : 255, fg = cur ? 255 : 0;
	char t[8];
	kui_fill((KuiRect){ r.x, r.y, r.w, ROW_H }, 255);
	kui_fill(r, bg);
	fmt_min(today.min[i], t);
	const char *name = T(vk_names[i]);
	kui_text_on((KuiRect){ r.x + 30, r.y, r.w / 2, r.h }, name, cur ? KUI_FONT_BOLD : KUI_FONT_REG, 54, KUI_ALIGN_LEFT, fg, bg);
	kui_text_on((KuiRect){ r.x + r.w / 2, r.y, r.w / 2 - 30, r.h }, t, KUI_FONT_BOLD, 62, KUI_ALIGN_RIGHT, fg, bg);
	if (!cur && i + 1 != now_v.cur && i < VK_N - 1) hline(r.x + 20, r.y + ROW_H - 4, r.w - 40, 2, 190);
	kui_mark_dirty((KuiRect){ r.x, r.y, r.w, ROW_H });
}

static void draw_table(void)
{
	for (int i = 0; i < VK_N; i++) draw_row(i);
	shown_cur = now_v.cur;
	shown_next = now_v.next;
}

static void draw_info(void)
{
	KuiRect r = r_info();
	char a[8], b[8], c[8], s[200];
	kui_fill(r, 255);
	fmt_hours(today.sunrise, a);
	fmt_hours(today.sunset, b);
	fmt_hours(today.noon, c);
	int dl = (int) floor(today.day_len * 60 + 0.5);
	snprintf(s, sizeof s, T("Güneş doğuşu %s · batışı %s · gün %d sa %d dk"), a, b, dl / 60, dl % 60);
	kui_text((KuiRect){ r.x, r.y, r.w, 54 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, r.w), KUI_ALIGN_CENTER, 0);
	double illum;
	int ph = vk_moon_phase((double) now_s(), &illum, NULL);
	snprintf(s, sizeof s, T("Zeval %s · Ay: %s, %%%d aydınlık"), c, T(vk_moon_names[ph]), (int) floor(illum * 100 + 0.5));
	kui_text((KuiRect){ r.x, r.y + 56, r.w, 54 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, r.w), KUI_ALIGN_CENTER, 0);
	const VkMethod *me = vk_method(opts.method);
	snprintf(s, sizeof s, "%s%s%s", T(me->name), opts.hanafi ? T(" · Hanefi ikindi") : "", today.high_lat ? T(" · yüksek enlem kuralı") : "");
	kui_text((KuiRect){ r.x, r.y + 112, r.w, 48 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 28, r.w), KUI_ALIGN_CENTER, 90);
	kui_mark_dirty(r);
}

static void draw_home(void)
{
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	kui_topbar(T("Vakitler"), T("Ayarlar"));
	draw_header();
	draw_countdown();
	draw_table();
	draw_info();
	const char *lb[3] = { T("İmsakiye"), T("Kıble"), T("Şehir") };
	int bw = (W - 4 * 30) / 3, y = H - 120;
	for (int i = 0; i < 3; i++) {
		home_btn[i] = (KuiButton){ { 30 + i * (bw + 30), y, bw, 100 }, lb[i], B_IMSAKIYE + i };
		kui_button(&home_btn[i], 0);
	}
	shown_d = now_v.d;
}

static void full_redraw(void)
{
	compute();
	draw_home();
	kui_flush(1);
}

static void soft_redraw(void)
{
	compute();
	draw_home();
	kui_mark_dirty((KuiRect){ 0, 0, W, H });
	kui_flush(0);
}

/* once a minute: only what changed */
static void tick(void)
{
	compute();
	if (now_v.d != shown_d) { /* midnight: a new day's table, with a full refresh against ghosting */
		draw_home();
		kui_flush(1);
		return;
	}
	if (now_v.cur != shown_cur || now_v.next != shown_next) {
		draw_table();
		draw_countdown();
		kui_flush(0);
		return;
	}
	if (now_v.now_min != shown_min) {
		draw_countdown();
		kui_flush(0);
	}
}

/* ---- imsakiye: a month's times ---- */

static void month_step(int *y, int *m, int k)
{
	int t = *y * 12 + (*m - 1) + k;
	*y = t / 12;
	*m = t % 12 + 1;
}

enum { I_PREV = 1, I_NEXT, I_RAMAZAN, I_TODAY };

static void draw_imsakiye(int y, int m)
{
	char s[160], a[80], b[80];
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H }, 255);
	snprintf(s, sizeof s, "%s %d · %s", kui_tr_month(m - 1), y, place.name);
	kui_text((KuiRect){ 20, KUI_TOPBAR_H + 4, W - 40, 60 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 44, W - 40), KUI_ALIGN_CENTER, 0);
	int nd = vk_month_days(y, m);
	VkHijri h0, h1;
	vk_hijri(vk_jdn(y, m, 1), &h0);
	vk_hijri(vk_jdn(y, m, nd), &h1);
	snprintf(a, sizeof a, "%s %d", T(vk_hijri_months[h0.m - 1]), h0.y);
	snprintf(b, sizeof b, "%s %d", T(vk_hijri_months[h1.m - 1]), h1.y);
	if (h0.m == h1.m) snprintf(s, sizeof s, T("Hicri: %s"), a);
	else snprintf(s, sizeof s, T("Hicri: %s – %s"), a, b);
	kui_text((KuiRect){ 20, KUI_TOPBAR_H + 64, W - 40, 44 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, W - 40), KUI_ALIGN_CENTER, 60);
	/* columns: day, Hijri day, six times */
	int x0 = 16, cw_day = 136, cw_h = 86, cw = (W - 2 * x0 - cw_day - cw_h) / VK_N, top = KUI_TOPBAR_H + 116, rh = 33;
	const char *hd[2] = { T("Gün"), T("Hicri") };
	kui_text((KuiRect){ x0, top, cw_day, 40 }, hd[0], KUI_FONT_BOLD, 26, KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ x0 + cw_day, top, cw_h, 40 }, hd[1], KUI_FONT_BOLD, kui_fit_px(hd[1], KUI_FONT_BOLD, 26, cw_h - 4), KUI_ALIGN_CENTER, 0);
	for (int i = 0; i < VK_N; i++) {
		const char *n = T(vk_names[i]);
		kui_text((KuiRect){ x0 + cw_day + cw_h + i * cw, top, cw, 40 }, n, KUI_FONT_BOLD, kui_fit_px(n, KUI_FONT_BOLD, 26, cw - 6), KUI_ALIGN_CENTER, 0);
	}
	hline(x0, top + 42, W - 2 * x0, 3, 0);
	int ty, tm, td, any_ramadan = 0;
	{
		VkNow n;
		vk_now(now_s(), place.lat, place.lon, utc_off(), &opts, &n, NULL);
		ty = n.y;
		tm = n.m;
		td = n.d;
	}
	for (int d = 1; d <= nd; d++) {
		int ry = top + 48 + (d - 1) * rh;
		VkHijri hj;
		vk_hijri(vk_jdn(y, m, d), &hj);
		VkOpts o = opts;
		o.ramadan = hj.m == 9;
		long t_noon = (vk_jdn(y, m, d) - 2440588L) * 86400L + 12 * 3600L;
		int off = place.dst == VK_DST_DEVICE ? device_offset() : vk_utc_offset(&place, t_noon);
		VkDay day;
		vk_day(y, m, d, place.lat, place.lon, off / 3600.0, &o, &day);
		int is_today = y == ty && m == tm && d == td, ram = hj.m == 9;
		any_ramadan |= ram;
		unsigned char bg = is_today ? 0 : ram ? 215 : 255, fg = is_today ? 255 : 0;
		if (bg != 255) kui_fill((KuiRect){ x0, ry, W - 2 * x0, rh - 2 }, bg);
		else if (d % 2 == 0) kui_fill((KuiRect){ x0, ry, W - 2 * x0, rh - 2 }, 242);
		unsigned char rb = bg != 255 ? bg : d % 2 == 0 ? 242 : 255;
		snprintf(s, sizeof s, "%d %s", d, kui_tr_weekday_short(vk_wday(vk_jdn(y, m, d))));
		KuiFont f = is_today || hj.d == 1 ? KUI_FONT_BOLD : KUI_FONT_REG;
		kui_text_on((KuiRect){ x0 + 6, ry, cw_day - 6, rh - 2 }, s, f, 26, KUI_ALIGN_LEFT, fg, rb);
		snprintf(s, sizeof s, "%d", hj.d);
		if (hj.d == 1 && d > 1) hline(x0, ry - 2, W - 2 * x0, 3, 0); /* a new Hijri month (named above) */
		kui_text_on((KuiRect){ x0 + cw_day, ry, cw_h, rh - 2 }, s, f, kui_fit_px(s, f, 26, cw_h - 4), KUI_ALIGN_CENTER, fg, rb);
		for (int i = 0; i < VK_N; i++) {
			char t[8];
			fmt_min(day.min[i], t);
			KuiFont tf = (ram && (i == VK_IMSAK || i == VK_AKSAM)) || is_today ? KUI_FONT_BOLD : KUI_FONT_REG;
			kui_text_on((KuiRect){ x0 + cw_day + cw_h + i * cw, ry, cw, rh - 2 }, t, tf, 27, KUI_ALIGN_CENTER, fg, rb);
		}
	}
	int by = H - 104, bw = (W - 5 * 20) / 4;
	static KuiButton ib[4];
	const char *lb[4] = { "◀", T("Bugün"), T("Ramazan"), "▶" };
	int ids[4] = { I_PREV, I_TODAY, I_RAMAZAN, I_NEXT };
	for (int i = 0; i < 4; i++) {
		ib[i] = (KuiButton){ { 20 + i * (bw + 20), by, bw, 90 }, lb[i], ids[i] };
		kui_button(&ib[i], 0);
	}
	if (any_ramadan) {
		const char *n = T("Gri satırlar: Ramazan (imsak ve iftar kalın)");
		kui_text((KuiRect){ 20, by - 38, W - 40, 34 }, n, KUI_FONT_REG, kui_fit_px(n, KUI_FONT_REG, 26, W - 40), KUI_ALIGN_CENTER, 60);
	}
}

static int imsakiye_hit(int x, int y)
{
	if (y < H - 104) return 0;
	int bw = (W - 5 * 20) / 4, i = (x - 20) / (bw + 20);
	static const int ids[4] = { I_PREV, I_TODAY, I_RAMAZAN, I_NEXT };
	return i >= 0 && i < 4 ? ids[i] : 0;
}

/* the Gregorian month of the next 1 Ramazan (or this one, while it lasts) */
static void ramadan_month(int *y, int *m)
{
	long j = vk_jdn(now_v.y, now_v.m, now_v.d);
	for (int k = 0; k < 400; k++) {
		VkHijri h;
		vk_hijri(j + k, &h);
		if (h.m == 9) {
			int d;
			vk_from_jdn(j + k, y, m, &d);
			return;
		}
	}
}

static void imsakiye_screen(void)
{
	compute();
	int y = now_v.y, m = now_v.m;
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, W, H }, 255);
			kui_topbar(T("İmsakiye"), NULL);
		}
		draw_imsakiye(y, m);
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) {
				quit_requested = 1;
				return;
			}
			if (e.type == KUI_EV_WAKE) {
				full = 1;
				break;
			}
			int id = 0;
			if (e.type == KUI_EV_SWIPE) id = e.dir == KUI_DIR_LEFT ? I_NEXT : e.dir == KUI_DIR_RIGHT ? I_PREV : 0;
			if (e.type == KUI_EV_TAP) {
				if (kui_topbar_hit(W, 0, e.x, e.y) == KUI_BTN_EXIT) return;
				id = imsakiye_hit(e.x, e.y);
			}
			if (!id) continue;
			if (id == I_PREV) month_step(&y, &m, -1);
			else if (id == I_NEXT) month_step(&y, &m, 1);
			else if (id == I_TODAY) {
				y = now_v.y;
				m = now_v.m;
			} else if (id == I_RAMAZAN) ramadan_month(&y, &m);
			break;
		}
	}
}

/* ---- kıble ---- */

#define DIAL_R 360
#define DIAL_CY 690
static KuiRect r_dial(void) { return (KuiRect){ W / 2 - DIAL_R - 60, DIAL_CY - DIAL_R - 60, 2 * DIAL_R + 120, 2 * DIAL_R + 120 }; }
static KuiRect r_kible_text(void) { return (KuiRect){ 30, DIAL_CY + DIAL_R + 66, W - 60, H - (DIAL_CY + DIAL_R + 66) - 10 }; }

static void dial_xy(double az, double r, double *x, double *y)
{
	*x = W / 2 + r * sin(az * M_PI / 180);
	*y = DIAL_CY - r * cos(az * M_PI / 180);
}

static void draw_dial(double qb, double sun_az, double sun_alt)
{
	KuiRect d = r_dial();
	int cx = W / 2;
	kui_fill(d, 255);
	circle(cx, DIAL_CY, DIAL_R, 6, 0);
	for (int a = 0; a < 360; a += 5) {
		double x0, y0, x1, y1, len = a % 45 == 0 ? 36 : a % 15 == 0 ? 22 : 12;
		dial_xy(a, DIAL_R - 6, &x0, &y0);
		dial_xy(a, DIAL_R - 6 - len, &x1, &y1);
		line(x0, y0, x1, y1, a % 45 == 0 ? 5 : 3, a % 45 == 0 ? 0 : 100);
	}
	static const char *const card[4] = { N_("K"), N_("D"), N_("G"), N_("B") };
	for (int i = 0; i < 4; i++) {
		double x, y;
		dial_xy(i * 90, DIAL_R - 82, &x, &y);
		const char *c = T(card[i]);
		kui_text((KuiRect){ (int) x - 50, (int) y - 34, 100, 68 }, c, KUI_FONT_BOLD, i == 0 ? 60 : 48, KUI_ALIGN_CENTER, i == 0 ? 0 : 80);
	}
	/* the sun on the rim, where it stands now */
	if (sun_alt > -0.83) {
		double x, y;
		dial_xy(sun_az, DIAL_R + 2, &x, &y);
		for (int k = 0; k < 8; k++) {
			double a = k * M_PI / 4;
			line(x + 30 * cos(a), y + 30 * sin(a), x + 46 * cos(a), y + 46 * sin(a), 5, 0);
		}
		circle((int) x, (int) y, 24, 0, 0);
		circle((int) x, (int) y, 17, 0, 255);
		circle((int) x, (int) y, 8, 0, 0);
	}
	/* the qibla arrow */
	double tx, ty, bx, by, lx, ly;
	dial_xy(qb, DIAL_R - 50, &tx, &ty);
	dial_xy(qb, DIAL_R - 150, &bx, &by);
	dial_xy(qb + 180, 90, &lx, &ly);
	line(lx, ly, bx, by, 16, 0);
	double px = cos(qb * M_PI / 180), py = sin(qb * M_PI / 180); /* perpendicular to the arrow */
	triangle(tx, ty, bx + 52 * px, by + 52 * py, bx - 52 * px, by - 52 * py, 0);
	circle(cx, DIAL_CY, 22, 0, 0);
	circle(cx, DIAL_CY, 12, 0, 255);
	/* the Kaaba on the rim where the arrow points: a black cube with its band */
	double kx, ky;
	dial_xy(qb, DIAL_R + 30, &kx, &ky);
	kui_fill((KuiRect){ (int) kx - 25, (int) ky - 25, 50, 50 }, 255);
	kui_fill((KuiRect){ (int) kx - 22, (int) ky - 22, 44, 44 }, 0);
	hline((int) kx - 22, (int) ky - 11, 44, 5, 255);
	kui_mark_dirty(d);
}

static void kible_screen(void)
{
	double qb, km;
	vk_qibla(place.lat, place.lon, &qb, &km);
	for (int full = 1;;) {
		long t = now_s();
		double az, alt;
		vk_sun_azalt((double) t, place.lat, place.lon, &az, &alt);
		char s[200], kms[32], q[8];
		if (full) {
			kui_fill((KuiRect){ 0, 0, W, H }, 255);
			kui_topbar(T("Kıble"), NULL);
			snprintf(s, sizeof s, T("Kıble %.0f° %s · %s"), qb, cardinal(qb), place.name);
			kui_text((KuiRect){ 20, KUI_TOPBAR_H + 10, W - 40, 80 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 56, W - 40), KUI_ALIGN_CENTER, 0);
			kui_fmt_num(km, 0, kms, sizeof kms);
			snprintf(s, sizeof s, T("Kâbe'ye uzaklık: %s km · gerçek kuzeye göre"), kms);
			kui_text((KuiRect){ 20, KUI_TOPBAR_H + 92, W - 40, 50 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, W - 40), KUI_ALIGN_CENTER, 0);
		}
		draw_dial(qb, az, alt);
		KuiRect tr = r_kible_text();
		kui_fill(tr, 255);
		if (alt > -0.83) snprintf(s, sizeof s, T("☀ Güneş şu an %.0f° %s yönünde, %.0f° yüksekte"), az, cardinal(az), alt);
		else snprintf(s, sizeof s, "%s", T("Güneş şu an ufkun altında"));
		kui_text((KuiRect){ tr.x, tr.y, tr.w, 50 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 36, tr.w), KUI_ALIGN_CENTER, 0);
		{
			int ty, tm, td;
			long lt = t + utc_off(), days = lt / 86400 - (lt < 0 && lt % 86400 ? 1 : 0);
			vk_from_jdn(days + 2440588L, &ty, &tm, &td);
			double kt = vk_qibla_time(ty, tm, td, place.lat, place.lon, utc_off() / 3600.0);
			fmt_hours(kt, q);
			if (isnan(kt)) snprintf(s, sizeof s, "%s", T("Bugün Güneş kıble yönünden geçmiyor."));
			else snprintf(s, sizeof s, T("Kıble saati %s: Güneş o an tam kıble yönündedir."), q);
		}
		kui_text((KuiRect){ tr.x, tr.y + 54, tr.w, 46 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 32, tr.w), KUI_ALIGN_CENTER, 0);
		const char *ex = T("Kindle'da pusula yok: cihazı, ☀ işareti gökteki Güneş'i gösterecek biçimde çevirin; ok o zaman kıbleyi gösterir.");
		KuiLine ln[4];
		int nl = kui_wrap_text(ex, tr.w, KUI_FONT_REG, 28, ln, 4), lh = kui_line_height(KUI_FONT_REG, 28);
		for (int i = 0; i < nl; i++) {
			char b[200];
			snprintf(b, sizeof b, "%.*s", ln[i].len, ex + ln[i].off);
			kui_text((KuiRect){ tr.x, tr.y + 106 + i * lh, tr.w, lh }, b, KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 70);
		}
		kui_mark_dirty(tr);
		if (full) kui_flush(1);
		else kui_flush(0);
		full = 0;
		KuiEvent e;
		for (;;) {
			e = kui_next_event(kui_ms_to_next_minute() + 4 * 60000); /* the Sun moves ≈ 1° in 5 minutes */
			if (e.type == KUI_EV_QUIT) {
				quit_requested = 1;
				return;
			}
			if (e.type == KUI_EV_WAKE) {
				full = 1;
				break;
			}
			if (e.type == KUI_EV_NONE) {
				if (sim()) skew += 300;
				break;
			}
			if (e.type == KUI_EV_TAP && kui_topbar_hit(W, 0, e.x, e.y) == KUI_BTN_EXIT) return;
			if (e.type == KUI_EV_TAP) break; /* a tap anywhere: the Sun now */
		}
	}
}

/* ---- city ---- */

enum { TAB_TR, TAB_WORLD, TAB_OTHER };
#define CITY_COLS 3
#define CITY_ROWS 14
#define CITY_PER (CITY_COLS * CITY_ROWS)
#define CITY_Y 250
enum { C_TAB = 10, C_PREV = 20, C_NEXT, C_WX = 30, C_COORD, C_ITEM = 100 };

static KuiButton cbtn[CITY_PER + 16];
static int n_cbtn;

static int same_place(const VkCityDef *c) { return fabs(c->lat - place.lat) < 1e-3 && fabs(c->lon - place.lon) < 1e-3; }

static void draw_city(int tab, int page, const VkPlace *wx, int have_wx)
{
	char s[200];
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H }, 255);
	n_cbtn = 0;
	const char *tl[3] = { T("Türkiye (81 il)"), T("Dünya"), T("Diğer") };
	int tw = (W - 4 * 20) / 3;
	for (int i = 0; i < 3; i++) {
		cbtn[n_cbtn] = (KuiButton){ { 20 + i * (tw + 20), KUI_TOPBAR_H + 16, tw, 96 }, tl[i], C_TAB + i };
		kui_button(&cbtn[n_cbtn++], i == tab);
	}
	if (tab == TAB_OTHER) {
		int y = CITY_Y;
		snprintf(s, sizeof s, T("Şu an: %s (%.2f, %.2f)"), place.name, place.lat, place.lon);
		kui_text((KuiRect){ 30, y, W - 60, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, W - 60), KUI_ALIGN_LEFT, 0);
		y += 90;
		static char wl[120];
		if (have_wx) {
			snprintf(wl, sizeof wl, T("Hava Durumu şehri: %s"), wx->name);
			cbtn[n_cbtn] = (KuiButton){ { 30, y, W - 60, 120 }, wl, C_WX };
			kui_button(&cbtn[n_cbtn++], 0);
			y += 150;
		}
		cbtn[n_cbtn] = (KuiButton){ { 30, y, W - 60, 120 }, T("Enlem ve boylam gir"), C_COORD };
		kui_button(&cbtn[n_cbtn++], 0);
		y += 160;
		const char *note = T("Seçilen yer Vakitler'e kaydedilir (data/vakit/place); Hava Durumu'nun şehir listesi değişmez.");
		KuiLine ln[4];
		int nl = kui_wrap_text(note, W - 60, KUI_FONT_REG, 30, ln, 4), lh = kui_line_height(KUI_FONT_REG, 30);
		for (int i = 0; i < nl; i++) {
			char b[200];
			snprintf(b, sizeof b, "%.*s", ln[i].len, note + ln[i].off);
			kui_text((KuiRect){ 30, y + i * lh, W - 60, lh }, b, KUI_FONT_REG, 30, KUI_ALIGN_LEFT, 80);
		}
		return;
	}
	const VkCityDef *list = tab == TAB_TR ? vk_tr_cities : vk_world_cities;
	int n = tab == TAB_TR ? 81 : vk_n_world_cities, pages = (n + CITY_PER - 1) / CITY_PER;
	int cw = (W - 2 * 20 - (CITY_COLS - 1) * 12) / CITY_COLS, chh = 72;
	static char lbl[CITY_PER][80];
	for (int k = 0; k < CITY_PER; k++) {
		int i = page * CITY_PER + k;
		if (i >= n) break;
		int col = k / CITY_ROWS, row = k % CITY_ROWS; /* down the columns, like a printed list */
		if (tab == TAB_TR) snprintf(lbl[k], sizeof lbl[k], "%02d %s", list[i].plate, list[i].name);
		else snprintf(lbl[k], sizeof lbl[k], "%s", list[i].name);
		KuiRect r = { 20 + col * (cw + 12), CITY_Y + row * (chh + 6), cw, chh };
		cbtn[n_cbtn] = (KuiButton){ r, lbl[k], C_ITEM + i };
		int sel = same_place(&list[i]);
		kui_fill(r, sel ? 0 : 255);
		kui_frame(r, 2, 0);
		kui_text_on((KuiRect){ r.x + 14, r.y, r.w - 20, r.h }, lbl[k], sel ? KUI_FONT_BOLD : KUI_FONT_REG, kui_fit_px(lbl[k], KUI_FONT_REG, 34, r.w - 24), KUI_ALIGN_LEFT, sel ? 255 : 0, sel ? 0 : 255);
		n_cbtn++;
	}
	if (pages > 1) {
		int by = H - 110;
		cbtn[n_cbtn] = (KuiButton){ { 20, by, 260, 96 }, "◀", C_PREV };
		kui_button(&cbtn[n_cbtn++], 0);
		cbtn[n_cbtn] = (KuiButton){ { W - 280, by, 260, 96 }, "▶", C_NEXT };
		kui_button(&cbtn[n_cbtn++], 0);
		snprintf(s, sizeof s, T("Sayfa %d / %d"), page + 1, pages);
		kui_text((KuiRect){ 300, by, W - 600, 96 }, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
	}
}

static void redraw_none(void) {}

/* the time zone for typed coordinates */
static int ask_zone(VkPlace *p)
{
	const char *ch[3] = { T("Türkiye (UTC+3)"), T("Kindle saati"), T("UTC farkı gir") };
	int c = kui_dialog(T("Saat dilimi"), T("Vakitler hangi saate göre gösterilsin?"), ch, 3, redraw_none);
	if (c == 0) {
		p->tz_min = 180;
		p->dst = VK_DST_NONE;
		return 0;
	}
	if (c == 1) {
		p->tz_min = device_offset() / 60;
		p->dst = VK_DST_DEVICE;
		return 0;
	}
	if (c != 2) return -1;
	char buf[16];
	snprintf(buf, sizeof buf, "%g", device_offset() / 3600.0);
	if (!kui_input_text(T("UTC farkı (saat, ör. 3 veya -5 ya da 5,5)"), buf, sizeof buf, 0, 1, redraw_none)) return -1;
	for (char *k = buf; *k; k++)
		if (*k == ',') *k = '.';
	char *end;
	double h = strtod(buf, &end);
	if (end == buf || fabs(h) > 14) return -1;
	p->tz_min = (int) lround(h * 60);
	p->dst = VK_DST_NONE;
	return 0;
}

static int enter_coords(void)
{
	char buf[64];
	snprintf(buf, sizeof buf, "%.4f, %.4f", place.lat, place.lon);
	for (;;) {
		if (!kui_input_text(T("Enlem, boylam (ör. 41.01, 28.98)"), buf, sizeof buf, 0, 3, redraw_none)) return 0;
		VkPlace p;
		memset(&p, 0, sizeof p);
		if (vk_parse_latlon(buf, &p.lat, &p.lon) == 0) {
			char name[64];
			snprintf(name, sizeof name, "%.2f, %.2f", p.lat, p.lon);
			if (!kui_input_text(T("Yerin adı"), name, sizeof name, 0, 1, redraw_none)) return 0;
			for (char *k = name; *k; k++)
				if (*k == '|') *k = ' ';
			snprintf(p.name, sizeof p.name, "%s", name);
			if (in_turkey(p.lat, p.lon)) {
				p.tz_min = 180;
				p.dst = VK_DST_NONE;
			} else if (ask_zone(&p) != 0) {
				return 0;
			}
			place = p;
			save_place();
			return 1;
		}
		const char *ok[1] = { T("Tamam") };
		kui_dialog(T("Koordinat"), T("Enlem −90…90, boylam −180…180 olmalı, ör. 41.01, 28.98"), ok, 1, redraw_none);
		if (kui_quit_pending()) return 0;
	}
}

static void city_screen(void)
{
	int tab = TAB_OTHER, page = 0;
	for (int i = 0; i < 81; i++)
		if (same_place(&vk_tr_cities[i])) {
			tab = TAB_TR;
			page = i / CITY_PER;
		}
	for (int i = 0; i < vk_n_world_cities; i++)
		if (same_place(&vk_world_cities[i])) {
			tab = TAB_WORLD;
			page = i / CITY_PER;
		}
	VkPlace wx;
	int have_wx = weather_city(&wx) == 0;
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, W, H }, 255);
			kui_topbar(T("Şehir"), NULL);
		}
		draw_city(tab, page, &wx, have_wx);
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		int n = tab == TAB_TR ? 81 : tab == TAB_WORLD ? vk_n_world_cities : 0, pages = (n + CITY_PER - 1) / CITY_PER;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) {
				quit_requested = 1;
				return;
			}
			if (e.type == KUI_EV_WAKE) {
				full = 1;
				break;
			}
			int id = 0;
			if (e.type == KUI_EV_SWIPE) id = e.dir == KUI_DIR_LEFT ? C_NEXT : e.dir == KUI_DIR_RIGHT ? C_PREV : 0;
			if (e.type == KUI_EV_TAP) {
				if (kui_topbar_hit(W, 0, e.x, e.y) == KUI_BTN_EXIT) return;
				id = kui_buttons_hit(cbtn, n_cbtn, e.x, e.y);
			}
			if (!id) continue;
			if (id >= C_TAB && id < C_TAB + 3) {
				if (tab == id - C_TAB) continue;
				tab = id - C_TAB;
				page = 0;
			} else if (id == C_PREV || id == C_NEXT) {
				if (pages < 2) continue;
				page = (page + (id == C_NEXT ? 1 : pages - 1)) % pages;
			} else if (id >= C_ITEM) {
				const VkCityDef *list = tab == TAB_TR ? vk_tr_cities : vk_world_cities;
				vk_place_from_def(&list[id - C_ITEM], &place);
				save_place();
				return;
			} else if (id == C_WX) {
				place = wx;
				save_place();
				return;
			} else if (id == C_COORD) {
				int done = enter_coords();
				if (done || kui_quit_pending()) return;
				full = 1;
			}
			break;
		}
	}
}

/* ---- settings ---- */

enum { S_METHOD = 10, S_SHAFI = 30, S_HANAFI, S_POWER };
static KuiButton sb[VK_N_METHODS + 3];

static void draw_settings(void)
{
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H }, 255);
	kui_text((KuiRect){ 30, KUI_TOPBAR_H + 10, W - 60, 56 }, T("Hesaplama yöntemi"), KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
	int y = KUI_TOPBAR_H + 74;
	for (int i = 0; i < VK_N_METHODS; i++, y += 88) {
		KuiRect r = { 30, y, W - 60, 78 };
		int sel = i == opts.method;
		kui_fill(r, sel ? 0 : 255);
		kui_frame(r, 3, 0);
		const char *n = T(vk_method(i)->name);
		kui_text_on((KuiRect){ r.x + 24, r.y, r.w - 48, r.h }, n, sel ? KUI_FONT_BOLD : KUI_FONT_REG, kui_fit_px(n, KUI_FONT_REG, 38, r.w - 48), KUI_ALIGN_LEFT, sel ? 255 : 0, sel ? 0 : 255);
		sb[i] = (KuiButton){ r, n, S_METHOD + i };
	}
	y += 10;
	kui_text((KuiRect){ 30, y, W - 60, 56 }, T("İkindi"), KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
	y += 62;
	int bw = (W - 60 - 20) / 2;
	sb[VK_N_METHODS] = (KuiButton){ { 30, y, bw, 90 }, T("Asr-ı evvel (Şafii)"), S_SHAFI };
	sb[VK_N_METHODS + 1] = (KuiButton){ { 50 + bw, y, bw, 90 }, T("Asr-ı sani (Hanefi)"), S_HANAFI };
	kui_button(&sb[VK_N_METHODS], !opts.hanafi);
	kui_button(&sb[VK_N_METHODS + 1], opts.hanafi);
	y += 110;
	const VkMethod *me = vk_method(opts.method);
	char s[400], fa[16], is[16];
	kui_fmt_num(me->fajr, me->fajr == floor(me->fajr) ? 0 : 1, fa, sizeof fa);
	kui_fmt_num(me->isha, me->isha == floor(me->isha) ? 0 : 1, is, sizeof is);
	if (opts.method == VK_DIYANET)
		snprintf(s, sizeof s, "%s", T("Diyanet: imsak güneş ufkun 18° altındayken, yatsı 17°. Temkin: güneş −7, öğle +5, ikindi +4, akşam +7 dk. Diyanet'in yayımladığı vakitlerle en çok 1 dk fark."));
	else if (me->isha_min)
		snprintf(s, sizeof s, T("İmsak %s°, yatsı akşamdan %d dk sonra (Ramazan'da %d dk); temkin yok."), fa, me->isha_min, me->isha_min + 30);
	else
		snprintf(s, sizeof s, T("İmsak %s°, yatsı %s°; temkin yok."), fa, is);
	KuiLine ln[6];
	int nl = kui_wrap_text(s, W - 60, KUI_FONT_REG, 30, ln, 6), lh = kui_line_height(KUI_FONT_REG, 30);
	for (int i = 0; i < nl; i++) {
		char b[400];
		snprintf(b, sizeof b, "%.*s", ln[i].len, s + ln[i].off);
		kui_text((KuiRect){ 30, y + i * lh, W - 60, lh }, b, KUI_FONT_REG, 30, KUI_ALIGN_LEFT, 60);
	}
	const char *hj = T("Hicri tarih: 2016 Hicri Takvim Birliği ölçütüyle (Diyanet'in kullandığı) hesaplanır; nadiren bir gün farklı olabilir.");
	nl = kui_wrap_text(hj, W - 60, KUI_FONT_REG, 26, ln, 6);
	int lh2 = kui_line_height(KUI_FONT_REG, 26);
	int y2 = H - 130 - nl * lh2 - 10;
	for (int i = 0; i < nl; i++) {
		char b[400];
		snprintf(b, sizeof b, "%.*s", ln[i].len, hj + ln[i].off);
		kui_text((KuiRect){ 30, y2 + i * lh2, W - 60, lh2 }, b, KUI_FONT_REG, 26, KUI_ALIGN_LEFT, 90);
	}
	sb[VK_N_METHODS + 2] = (KuiButton){ { 30, H - 120, W - 60, 100 }, T("Pil ve ekran"), S_POWER };
	kui_button(&sb[VK_N_METHODS + 2], 0);
}

static void settings_screen(void)
{
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, W, H }, 255);
			kui_topbar(T("Ayarlar"), NULL);
		}
		draw_settings();
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) {
				quit_requested = 1;
				return;
			}
			if (e.type == KUI_EV_WAKE) {
				full = 1;
				break;
			}
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(W, 0, e.x, e.y) == KUI_BTN_EXIT) return;
			int id = kui_buttons_hit(sb, VK_N_METHODS + 3, e.x, e.y);
			if (id >= S_METHOD && id < S_METHOD + VK_N_METHODS) {
				if (opts.method == id - S_METHOD) continue;
				cfg_set("method", id - S_METHOD);
			} else if (id == S_SHAFI || id == S_HANAFI) {
				if (opts.hanafi == (id == S_HANAFI)) continue;
				cfg_set("hanafi", id == S_HANAFI);
			} else if (id == S_POWER) {
				kui_power_screen(CFG, KUI_PS_SAVER);
				if (kui_quit_pending()) {
					quit_requested = 1;
					return;
				}
				full = 1;
				break;
			} else {
				continue;
			}
			load_opts();
			break;
		}
	}
}

/* ---- battery saver ---- */

static void deep_sleep_loop(void)
{
	fprintf(stderr, "deep: enter (battery %d%%)\n", kui_battery_level());
	deep = 1;
	kui_drain_power_key();
	if (kui_wifi_enabled() == 1) kui_wifi_enable(0);
	int light = kui_light_get();
	if (kui_saver_get(CFG, KUI_SAVER_LIGHT_OFF) && light > 0) kui_light_set(0);
	compute();
	draw_countdown(); /* the ☾ mark */
	kui_flush(0);
	kui_wait_refresh();
	for (;;) {
		if (kui_quit_pending()) break;
		compute();
		if (now_v.now_min == shown_min) {
			int r = kui_sleep_until_every(kui_saver_get(CFG, KUI_SAVER_CLOCK_MIN));
			if (r != KUI_SLEEP_MINUTE) break;
		}
		if (kui_battery_charging()) break;
		tick();
		kui_wait_refresh();
	}
	deep = 0;
	if (light > 0 && kui_light_get() == 0) kui_light_set(light);
	kui_battery_refresh();
	fprintf(stderr, "deep: leave (battery %d%%)\n", kui_battery_level());
	kui_drain_input(700); /* the touch that woke the Kindle must not press anything */
	soft_redraw();
}

/* ---- home ---- */

static void home(void)
{
	full_redraw();
	long last_input = now_s();
	for (;;) {
		if (kui_quit_pending() || quit_requested) break;
		if (!sim() && now_s() - last_input >= IDLE_S && !kui_input_asleep() && !kui_battery_charging()) {
			deep_sleep_loop();
			last_input = now_s();
			continue;
		}
		KuiEvent e = kui_next_event(kui_ms_to_next_minute());
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_NONE) {
			if (sim()) skew += 60;
			if (!kui_input_asleep()) tick();
			continue;
		}
		if (e.type == KUI_EV_WAKE) {
			full_redraw();
			continue;
		}
		if (e.type == KUI_EV_POWER) {
			if (!kui_battery_charging()) deep_sleep_loop();
			last_input = now_s();
			continue;
		}
		last_input = now_s();
		int screen = 0;
		if (e.type == KUI_EV_SWIPE && e.dir == KUI_DIR_LEFT) {
			imsakiye_screen();
			screen = 1;
		} else if (e.type == KUI_EV_TAP) {
			int top = kui_topbar_hit(W, 1, e.x, e.y);
			if (top == KUI_BTN_EXIT) break;
			int id = kui_buttons_hit(home_btn, 3, e.x, e.y);
			if (top == KUI_BTN_MENU) settings_screen();
			else if (kui_rect_contains(r_city(), e.x, e.y) || id == B_SEHIR) city_screen();
			else if (id == B_IMSAKIYE) imsakiye_screen();
			else if (id == B_KIBLE) kible_screen();
			else {
				tick();
				continue;
			}
			screen = 1;
		}
		if (screen) {
			if (quit_requested || kui_quit_pending()) break;
			kui_drain_input(300);
			full_redraw();
			last_input = now_s();
		}
	}
}

int main(void)
{
	if (kui_init("vakit") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes only for new screens and at midnight */
	kui_battery_max_age(30 * 60); /* the battery is not read every minute */
	load_place();
	load_opts();
	kui_prevent_sleep(1);
	int wifi_was = kui_wifi_enabled();
	if (!sim()) fprintf(stderr, "power key grab: %d\n", kui_grab_power_key(1));
	home();
	kui_grab_power_key(0);
	kui_prevent_sleep(0);
	if (wifi_was == 1 && kui_wifi_enabled() == 0) kui_wifi_enable(1); /* leave Wi-Fi as we found it */
	kui_quit();
	return 0;
}

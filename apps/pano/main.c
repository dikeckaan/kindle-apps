/* Masa Panosu: a desk dashboard. Big clock and date, the first saved city's weather, breaking news from a chosen
 * source, currency and gold, and extras — on this day in history, prayer times (Diyanet), moon phase, battery.
 * The clock is redrawn every minute without flashing; data refreshes on its own schedule; a full refresh once an hour.
 * Keeps the Kindle awake while open. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "currency/fx.h"
#include "kui.h"
#include "net/net.h"
#include "news/news.h"
#include "pano.h"
#include "weather/wx.h"
#include "weather/wxdraw.h"

enum { OPT_WEATHER, OPT_NEWS, OPT_FX, OPT_HISTORY, OPT_PRAYER, OPT_MOON, N_OPT };
static const char *const opt_key[N_OPT] = { "weather", "news", "fx", "history", "prayer", "moon" };
static const char *const opt_label[N_OPT] = { /* N_ */ "Hava durumu", "Son dakika haberleri", "Döviz ve altın", "Tarihte bugün",
					      "Namaz vakitleri", "Ay evresi" };
static const int opt_default[N_OPT] = { 1, 1, 1, 1, 0, 1 };

static WxCity city;
static Wx wx;
static int have_wx;
static NewsItem news[NEWS_MAX];
static int n_news, news_first, news_shown = 3;
static FxRate rates[FX_MAX];
static int n_rates;
static FxGold gold[FX_GOLD_MAX];
static int n_gold;
static PanoEvent events[8];
static int n_events;
static PanoPrayer prayer;
static int have_prayer;
static long t_news, t_wx, t_fx, t_otd, t_prayer; /* last fetch attempts: failures are retried after 30 min at most */
static int quit_requested;
static int last_day = -1, shown_min = -1, shown_hour = -1;
static KuiRect r_clock, r_news, r_prayer, r_settings;

/* ---- settings ---- */

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("pano"));
	return p;
}

static int opt(int i) { return kui_cfg_get_int(settings(), opt_key[i], opt_default[i]); }

static int news_source(void)
{
	int n;
	news_sources(&n);
	int s = kui_cfg_get_int(settings(), "news_source", 4); /* NTV: full texts, frequent updates */
	return s >= 0 && s < n ? s : 0;
}

/* ---- time ---- */

static void now_local(struct tm *tm)
{
	time_t t = time(NULL);
	localtime_r(&t, tm);
	if (tm->tm_gmtoff == 0 && !getenv("TZ") && have_wx && wx.utc_offset) { /* no zone on the device: the city's */
		t += wx.utc_offset;
		gmtime_r(&t, tm);
	}
}

static int local_off(void)
{
	struct tm tm;
	now_local(&tm);
	return (int) tm.tm_gmtoff ? (int) tm.tm_gmtoff : have_wx ? wx.utc_offset : 3 * 3600;
}

/* ---- data: cached files of the other apps plus our own; fetched when old ---- */

static int read_cache(const char *app, const char *name, char *buf, int cap, long *mtime)
{
	char p[400];
	struct stat st;
	snprintf(p, sizeof p, "%s/%s", kui_data_dir(app), name);
	int n = kui_read_file(p, buf, cap);
	if (mtime) *mtime = n > 0 && stat(p, &st) == 0 ? (long) st.st_mtime : 0;
	return n;
}

static int fetch_to(const char *url, const char *app, const char *name, int (*valid)(const char *, int))
{
	char *body = NULL, p[400];
	int len = 0, ok = 0;
	if (net_get(url, &body, &len, 20) == 0 && valid(body, len)) {
		snprintf(p, sizeof p, "%s/%s", kui_data_dir(app), name);
		ok = kui_write_atomic(p, body, len) == 0;
	}
	free(body);
	return ok;
}

static int valid_wx(const char *b, int n) { static Wx w; (void) n; return wx_parse(b, &w) == 0; }
static int valid_news(const char *b, int n)
{
	static NewsItem it[NEWS_MAX];
	int k = news_parse_rss(b, n, it, NEWS_MAX);
	news_free(it, k);
	return k > 0;
}
static int valid_tcmb(const char *b, int n) { static FxRate r[FX_MAX]; char d[11]; (void) n; return fx_parse_tcmb(b, r, FX_MAX, d) > 0; }
static int valid_gold(const char *b, int n) { static FxGold g[FX_GOLD_MAX]; (void) n; return fx_parse_gold(b, g, FX_GOLD_MAX) > 0; }
static int valid_otd(const char *b, int n) { static PanoEvent e[8]; (void) n; return pano_parse_onthisday(b, e, 8) > 0; }
static int valid_prayer(const char *b, int n) { static PanoPrayer p; (void) n; return pano_parse_prayer(b, &p) == 0; }

static void load_weather(int fetch)
{
	static char buf[65536];
	char cpath[400], name[64];
	long mt;
	WxCity cs[WX_MAX_CITIES];
	snprintf(cpath, sizeof cpath, "%s/cities.txt", kui_data_dir("weather"));
	wx_cities_load(cpath, cs, WX_MAX_CITIES);
	city = cs[0];
	wx_cache_name(&city, name, sizeof name);
	have_wx = read_cache("weather", name, buf, sizeof buf, &mt) > 0 && wx_parse(buf, &wx) == 0;
	if (fetch && (!have_wx || time(NULL) - mt > 30 * 60) && time(NULL) - t_wx > 30 * 60) {
		char url[700];
		t_wx = (long) time(NULL);
		wx_forecast_url(&city, url, sizeof url);
		if (fetch_to(url, "weather", name, valid_wx)) load_weather(0);
	}
}

static void load_news(int fetch)
{
	static char buf[NET_MAX_BODY + 1];
	int n;
	const NewsSource *src = news_sources(&n), *s = &src[news_source()];
	char id[48], name[64];
	long mt;
	news_feed_id(s, 0, id, sizeof id);
	snprintf(name, sizeof name, "%s.xml", id);
	int len = read_cache("news", name, buf, sizeof buf, &mt);
	news_free(news, n_news);
	n_news = len > 0 ? news_parse_rss(buf, len, news, NEWS_MAX) : 0;
	if (fetch && (!n_news || time(NULL) - mt > 10 * 60) && time(NULL) - t_news > 10 * 60) {
		t_news = (long) time(NULL);
		if (fetch_to(s->sec[0].url, "news", name, valid_news)) load_news(0);
	}
}

static void load_fx(int fetch)
{
	static char buf[65536];
	char d[11];
	long mt;
	n_rates = read_cache("currency", "tcmb.xml", buf, sizeof buf, &mt) > 0 ? fx_parse_tcmb(buf, rates, FX_MAX, d) : 0;
	n_gold = read_cache("currency", "gold.json", buf, sizeof buf, NULL) > 0 ? fx_parse_gold(buf, gold, FX_GOLD_MAX) : 0;
	if (fetch && (!n_rates || time(NULL) - mt > 60 * 60) && time(NULL) - t_fx > 60 * 60) {
		t_fx = (long) time(NULL);
		fetch_to("https://www.tcmb.gov.tr/kurlar/today.xml", "currency", "tcmb.xml", valid_tcmb);
		fetch_to("https://finans.truncgil.com/today.json", "currency", "gold.json", valid_gold);
		load_fx(0);
	}
}

/* daily: on this day (in the UI language) and prayer times for the city */
static void load_daily(int fetch)
{
	static char buf[65536];
	struct tm tm;
	now_local(&tm);
	char name[96], url[300];
	snprintf(name, sizeof name, "otd_%s_%02d%02d.json", kui_lang(), tm.tm_mon + 1, tm.tm_mday);
	n_events = read_cache("pano", name, buf, sizeof buf, NULL) > 0 ? pano_parse_onthisday(buf, events, 8) : 0;
	if (fetch && !n_events && opt(OPT_HISTORY) && time(NULL) - t_otd > 30 * 60) {
		t_otd = (long) time(NULL);
		pano_onthisday_url(kui_lang(), tm.tm_mon + 1, tm.tm_mday, url, sizeof url);
		if (fetch_to(url, "pano", name, valid_otd))
			n_events = read_cache("pano", name, buf, sizeof buf, NULL) > 0 ? pano_parse_onthisday(buf, events, 8) : 0;
	}
	snprintf(name, sizeof name, "prayer_%.4f_%.4f_%04d%02d%02d.json", city.lat, city.lon, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	have_prayer = read_cache("pano", name, buf, sizeof buf, NULL) > 0 && pano_parse_prayer(buf, &prayer) == 0;
	if (fetch && !have_prayer && opt(OPT_PRAYER) && time(NULL) - t_prayer > 30 * 60) {
		t_prayer = (long) time(NULL);
		pano_prayer_url(city.lat, city.lon, tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, url, sizeof url);
		if (fetch_to(url, "pano", name, valid_prayer))
			have_prayer = read_cache("pano", name, buf, sizeof buf, NULL) > 0 && pano_parse_prayer(buf, &prayer) == 0;
	}
}

/* ---- drawing ---- */

static int draw_wrapped(KuiRect box, const char *text, KuiFont f, int px, int max_lines)
{
	static KuiLine lines[200];
	int n = kui_wrap_text(text, box.w, f, px, lines, 200), lh = kui_line_height(f, px);
	if (n > max_lines) n = max_lines;
	for (int i = 0; i < n; i++) {
		char buf[1024];
		int len = lines[i].len < (int) sizeof buf - 4 ? lines[i].len : (int) sizeof buf - 4;
		memcpy(buf, text + lines[i].off, (size_t) len);
		buf[len] = '\0';
		if (i == max_lines - 1 && lines[i].off + lines[i].len < (int) strlen(text)) {
			strcat(buf, "…");
			while (len > 0 && kui_text_width(buf, f, px) > box.w) {
				do len--;
				while (len > 0 && ((unsigned char) buf[len] & 0xC0) == 0x80);
				strcpy(buf + len, "…");
			}
		}
		kui_text((KuiRect){ box.x, box.y + i * lh, box.w, lh }, buf, f, px, KUI_ALIGN_LEFT, 0);
	}
	return n * lh;
}

static void rule(int y) { kui_fill((KuiRect){ 30, y, kui_width() - 60, 2 }, 0); }

static void draw_header(void)
{
	int w = kui_width();
	KuiRect r = { 0, 0, w, 90 };
	kui_fill(r, 255);
	kui_text((KuiRect){ 0, 0, 120, 90 }, "✕", KUI_FONT_BOLD, 56, KUI_ALIGN_CENTER, 0);
	kui_battery((KuiRect){ w / 2 - 110, 5, 260, 80 });
	const char *lbl = T("Ayarlar");
	r_settings = (KuiRect){ w - 260, 0, 260, 90 };
	kui_text((KuiRect){ r_settings.x, 0, r_settings.w - 30, 90 }, lbl, KUI_FONT_REG, kui_fit_px(lbl, KUI_FONT_REG, 38, 220), KUI_ALIGN_RIGHT, 0);
	kui_mark_dirty(r);
}

static void draw_clock(const struct tm *tm)
{
	int w = kui_width();
	KuiRect r = r_clock;
	char s[96];
	kui_fill(r, 255);
	snprintf(s, sizeof s, "%02d:%02d", tm->tm_hour, tm->tm_min);
	int tw = w * 62 / 100;
	kui_text((KuiRect){ r.x + 20, r.y, tw, r.h }, s, KUI_FONT_BOLD, kui_fit_px("00:00", KUI_FONT_BOLD, 300, tw - 20), KUI_ALIGN_CENTER, 0);
	int x = r.x + tw + 20, cw = w - x - 30;
	const char *wd = kui_tr_weekday(tm->tm_wday);
	kui_text((KuiRect){ x, r.y + 40, cw, 70 }, wd, KUI_FONT_BOLD, kui_fit_px(wd, KUI_FONT_BOLD, 50, cw), KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, "%d %s %d", tm->tm_mday, kui_tr_month(tm->tm_mon), tm->tm_year + 1900);
	kui_text((KuiRect){ x, r.y + 115, cw, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 38, cw), KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, T("Hafta %d · Gün %d"), pano_iso_week(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday),
		 pano_day_of_year(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday));
	kui_text((KuiRect){ x, r.y + 180, cw, 50 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, cw), KUI_ALIGN_LEFT, 0);
	if (opt(OPT_MOON)) {
		double age = pano_moon_age((long) time(NULL));
		snprintf(s, sizeof s, T("Ay: %s, %%%d"), T(pano_moon_phase_name(age)), pano_moon_lit_percent(age));
		kui_text((KuiRect){ x, r.y + 230, cw, 50 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 28, cw), KUI_ALIGN_LEFT, 0);
	}
	kui_mark_dirty(r);
}

static int draw_weather(int y)
{
	int w = kui_width();
	char s[160], a[32], b[32];
	if (!have_wx) {
		kui_text((KuiRect){ 30, y, w - 60, 60 }, T("Hava durumu yok (Wi-Fi kapalı olabilir)"), KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0);
		return 70;
	}
	wx_draw_icon((KuiRect){ 30, y + 10, 150, 150 }, wx_code_icon(wx.code, wx.is_day));
	wx_fmt_temp(wx.temp, s, sizeof s);
	kui_text((KuiRect){ 190, y, 230, 120 }, s, KUI_FONT_BOLD, 100, KUI_ALIGN_LEFT, 0);
	const char *cond = T(wx_code_text(wx.code));
	kui_text((KuiRect){ 190, y + 115, 300, 50 }, cond, KUI_FONT_REG, kui_fit_px(cond, KUI_FONT_REG, 32, 300), KUI_ALIGN_LEFT, 0);
	if (wx.n_days) {
		wx_fmt_temp(wx.day[0].tmax, a, sizeof a);
		wx_fmt_temp(wx.day[0].tmin, b, sizeof b);
		snprintf(s, sizeof s, "%s · %s / %s", city.name, a, b);
	} else {
		snprintf(s, sizeof s, "%s", city.name);
	}
	kui_text((KuiRect){ 190, y + 160, 330, 40 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 26, 330), KUI_ALIGN_LEFT, 0);
	/* the next hours, every second hour */
	int x0 = 530, cw = (w - 30 - x0) / 4;
	for (int k = 0; k < 4; k++) {
		int i = 1 + k * 2;
		if (i >= wx.n_hours) break;
		int x = x0 + k * cw;
		kui_text((KuiRect){ x, y, cw, 40 }, wx.hour[i].hh, KUI_FONT_REG, 26, KUI_ALIGN_CENTER, 0);
		int day = !wx.n_days || (strcmp(wx.hour[i].hh, wx.day[0].sunrise) >= 0 && strcmp(wx.hour[i].hh, wx.day[0].sunset) < 0);
		wx_draw_icon((KuiRect){ x + cw / 2 - 45, y + 45, 90, 90 }, wx_code_icon(wx.hour[i].code, day));
		wx_fmt_temp(wx.hour[i].t, s, sizeof s);
		kui_text((KuiRect){ x, y + 140, cw, 45 }, s, KUI_FONT_BOLD, 32, KUI_ALIGN_CENTER, 0);
	}
	return 210;
}

static void draw_news(void)
{
	int n, w = kui_width();
	const NewsSource *src = news_sources(&n), *s = &src[news_source()];
	KuiRect r = r_news;
	kui_fill(r, 255);
	char head[160], up[96];
	kui_tr_upper_str(s->name, up, sizeof up);
	snprintf(head, sizeof head, T("SON DAKİKA · %s"), up);
	kui_text((KuiRect){ 30, r.y, w - 60, 44 }, head, KUI_FONT_BOLD, kui_fit_px(head, KUI_FONT_BOLD, 28, w - 60), KUI_ALIGN_LEFT, 0);
	if (!n_news) {
		kui_text((KuiRect){ 30, r.y + 60, w - 60, 60 }, T("Haber alınamadı."), KUI_FONT_SERIF, 32, KUI_ALIGN_LEFT, 0);
	}
	/* as many headlines as fit, each up to two lines, packed by their real height */
	int y = r.y + 50, lh = kui_line_height(KUI_FONT_SERIF_BOLD, 32), shown = 0;
	static KuiLine tmp[64];
	for (int k = 0; k < n_news && k < 8; k++) {
		const NewsItem *it = &news[(news_first + k) % n_news];
		int lines = kui_wrap_text(it->title, w - 160, KUI_FONT_SERIF_BOLD, 32, tmp, 64);
		if (lines > 2) lines = 2;
		if (y + lines * lh > r.y + r.h) break;
		char d[32];
		news_date_local(it->date, (long) time(NULL), local_off(), d, sizeof d);
		const char *hh = strchr(d, ':') ? strchr(d, ':') - 2 : d; /* only the time */
		kui_text((KuiRect){ 30, y + 4, 100, 40 }, hh, KUI_FONT_BOLD, 28, KUI_ALIGN_LEFT, 0);
		y += draw_wrapped((KuiRect){ 130, y, w - 160, 0 }, it->title, KUI_FONT_SERIF_BOLD, 32, 2) + 16;
		shown++;
	}
	news_shown = shown ? shown : 1;
	kui_mark_dirty(r);
}

static int draw_fx(int y)
{
	int w = kui_width();
	char s[256] = "", v[32];
	static const char *const codes[] = { "USD", "EUR", "GBP" };
	for (size_t c = 0; c < sizeof codes / sizeof codes[0]; c++)
		for (int i = 0; i < n_rates; i++)
			if (!strcmp(rates[i].code, codes[c])) {
				kui_fmt_num(rates[i].sell, 2, v, sizeof v);
				snprintf(s + strlen(s), sizeof s - strlen(s), "%s%s %s", s[0] ? " · " : "", codes[c], v);
			}
	if (n_gold) {
		kui_fmt_num(gold[0].sell, 0, v, sizeof v);
		snprintf(s + strlen(s), sizeof s - strlen(s), "%s%s %s", s[0] ? " · " : "", T(gold[0].label), v);
	}
	if (!s[0]) snprintf(s, sizeof s, "%s", T("Kur bilgisi yok"));
	kui_text((KuiRect){ 30, y, w - 60, 60 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 34, w - 60), KUI_ALIGN_CENTER, 0);
	return 70;
}

static int draw_history(int y, int h)
{
	int w = kui_width();
	if (!n_events) return 0;
	struct tm tm;
	now_local(&tm);
	const PanoEvent *e = &events[(tm.tm_hour * 6 + tm.tm_min / 10) % n_events]; /* a different one every 10 minutes */
	char head[64];
	snprintf(head, sizeof head, T("Tarihte bugün · %d"), e->year);
	kui_text((KuiRect){ 30, y, w - 60, 44 }, head, KUI_FONT_BOLD, 28, KUI_ALIGN_LEFT, 0);
	int lines = (h - 50) / kui_line_height(KUI_FONT_SERIF, 30);
	return 50 + draw_wrapped((KuiRect){ 30, y + 46, w - 60, 0 }, e->text, KUI_FONT_SERIF, 30, lines > 0 ? lines : 1);
}

static void draw_prayer(void)
{
	int w = kui_width();
	KuiRect r = r_prayer;
	kui_fill(r, 255);
	if (!have_prayer) {
		kui_text((KuiRect){ 30, r.y, w - 60, 50 }, T("Namaz vakitleri alınamadı"), KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0);
		kui_mark_dirty(r);
		return;
	}
	struct tm tm;
	now_local(&tm);
	int left, next = pano_next_prayer(&prayer, tm.tm_hour * 60 + tm.tm_min, &left);
	char s[160];
	snprintf(s, sizeof s, T("Sıradaki: %s %s · %d sa %d dk"), T(pano_prayer_name(next)), prayer.t[next], left / 60, left % 60);
	kui_text((KuiRect){ 30, r.y, w - 60, 46 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 30, w - 60), KUI_ALIGN_LEFT, 0);
	int cw = (w - 60) / PANO_N_PRAYER;
	for (int i = 0; i < PANO_N_PRAYER; i++) {
		KuiRect c = { 30 + i * cw, r.y + 50, cw, 70 };
		if (i == next) kui_fill(c, 0);
		char t[48];
		snprintf(t, sizeof t, "%s\n%s", T(pano_prayer_name(i)), prayer.t[i]);
		if (i == next) kui_text_on(c, t, KUI_FONT_BOLD, kui_fit_px(T(pano_prayer_name(i)), KUI_FONT_BOLD, 26, cw - 8), KUI_ALIGN_CENTER, 255, 0);
		else kui_text(c, t, KUI_FONT_REG, kui_fit_px(T(pano_prayer_name(i)), KUI_FONT_REG, 26, cw - 8), KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty(r);
}

static void draw_all(int full)
{
	int w = kui_width(), h = kui_height();
	struct tm tm;
	now_local(&tm);
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	draw_header();
	r_clock = (KuiRect){ 0, 90, w, 290 };
	draw_clock(&tm);
	int y = 90 + 295;
	rule(y);
	y += 12;
	if (opt(OPT_WEATHER)) {
		y += draw_weather(y);
		rule(y);
		y += 12;
	}
	if (opt(OPT_FX)) {
		y += draw_fx(y);
		rule(y);
		y += 12;
	}
	int bottom = h - 10;
	if (opt(OPT_PRAYER)) {
		r_prayer = (KuiRect){ 0, bottom - 125, w, 125 };
		bottom -= 135;
		draw_prayer();
		rule(bottom + 2);
	} else {
		r_prayer = (KuiRect){ 0, 0, 0, 0 };
	}
	if (opt(OPT_HISTORY) && n_events) {
		int hh = 190;
		draw_history(bottom - hh, hh);
		bottom -= hh + 10;
		rule(bottom + 2);
	}
	if (opt(OPT_NEWS)) {
		r_news = (KuiRect){ 0, y, w, bottom - y - 8 };
		draw_news();
	} else {
		r_news = (KuiRect){ 0, 0, 0, 0 };
	}
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, w, h });
		kui_flush(0); /* data changed: in place, no flash */
	}
	shown_min = tm.tm_min;
	shown_hour = tm.tm_hour;
}

/* ---- settings screen ---- */

static void settings_screen(void)
{
	int w = kui_width(), h = kui_height();
	for (int full = 1;;) {
		int ns;
		const NewsSource *src = news_sources(&ns);
		KuiRect rows[N_OPT + 1];
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Ayarlar"), NULL);
		for (int i = 0; i < N_OPT; i++) {
			rows[i] = (KuiRect){ 40, KUI_TOPBAR_H + 30 + i * 120, w - 80, 105 };
			kui_frame(rows[i], 2, 0);
			const char *l = T(opt_label[i]);
			kui_text((KuiRect){ rows[i].x + 25, rows[i].y, rows[i].w - 140, rows[i].h }, l, KUI_FONT_BOLD, kui_fit_px(l, KUI_FONT_BOLD, 38, rows[i].w - 150),
				 KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ rows[i].x + rows[i].w - 110, rows[i].y, 100, rows[i].h }, opt(i) ? "☑" : "☐", KUI_FONT_REG, 60, KUI_ALIGN_CENTER, 0);
		}
		rows[N_OPT] = (KuiRect){ 40, KUI_TOPBAR_H + 30 + N_OPT * 120 + 30, w - 80, 120 };
		char s[160];
		snprintf(s, sizeof s, T("Haber kaynağı: %s"), src[news_source()].name);
		KuiButton b = { rows[N_OPT], s, 1 };
		kui_button(&b, 0);
		kui_text((KuiRect){ 40, h - 160, w - 80, 140 },
			 T("Hava durumu ve namaz vakitleri Hava Durumu uygulamasındaki ilk şehre göredir. Namaz vakitleri Diyanet yöntemiyle hesaplanır."),
			 KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 0);
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) {
			quit_requested = 1;
			return;
		}
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		for (int i = 0; i < N_OPT; i++)
			if (kui_rect_contains(rows[i], e.x, e.y)) kui_cfg_set_int(settings(), opt_key[i], !opt(i));
		if (kui_rect_contains(rows[N_OPT], e.x, e.y)) { /* pick the news source */
			KuiButton bs[32];
			int cols = 2, rws = (ns + 1) / 2, cw = (w - 3 * 30) / cols, rh = (h - KUI_TOPBAR_H - 60) / rws;
			if (rh > 110) rh = 110;
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T("Haber kaynağı"), NULL);
			for (int i = 0; i < ns && i < 32; i++) {
				bs[i] = (KuiButton){ { 30 + (i / rws) * (cw + 30), KUI_TOPBAR_H + 20 + (i % rws) * rh, cw, rh - 12 }, src[i].name, i + 1 };
				kui_button(&bs[i], i == news_source());
			}
			kui_flush(1);
			for (;;) {
				KuiEvent f = kui_next_event(-1);
				if (f.type == KUI_EV_QUIT) {
					quit_requested = 1;
					return;
				}
				if (f.type != KUI_EV_TAP) continue;
				if (kui_topbar_hit(w, 0, f.x, f.y) == KUI_BTN_EXIT) break;
				int id = kui_buttons_hit(bs, ns < 32 ? ns : 32, f.x, f.y);
				if (id) {
					kui_cfg_set_int(settings(), "news_source", id - 1);
					t_news = 0;
					break;
				}
			}
			full = 1;
		}
	}
}

/* ---- main loop ---- */

static void load_all(int fetch)
{
	load_weather(fetch && opt(OPT_WEATHER));
	if (opt(OPT_NEWS)) load_news(fetch);
	if (opt(OPT_FX)) load_fx(fetch);
	load_daily(fetch);
}

/* every minute: clock (and the prayer countdown) without flashing; data on its own schedule; full refresh hourly */
static void tick(void)
{
	struct tm tm;
	now_local(&tm);
	if (tm.tm_mday != last_day) { /* a new day: daily data again */
		last_day = tm.tm_mday;
		load_daily(1);
		draw_all(1);
		return;
	}
	if (tm.tm_hour != shown_hour) { /* hourly: a full refresh clears e-ink ghosting */
		load_all(1);
		draw_all(1);
		return;
	}
	if (tm.tm_min == shown_min) return;
	shown_min = tm.tm_min;
	if (tm.tm_min % 10 == 0) { /* news, weather and history change: redraw everything without a flash */
		load_all(1);
		draw_all(0);
		return;
	}
	draw_clock(&tm);
	if (r_prayer.h) draw_prayer();
	draw_header(); /* battery */
	kui_flush(0);
}

static int ms_to_next_minute(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int) (60000 - (tv.tv_sec % 60) * 1000 - tv.tv_usec / 1000) + 200;
}

int main(void)
{
	if (kui_init("pano") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes are scheduled hourly */
	kui_prevent_sleep(1);
	load_all(0); /* cached first: something to show at once */
	struct tm tm;
	now_local(&tm);
	last_day = tm.tm_mday;
	draw_all(1);
	load_all(1);
	draw_all(0);
	for (;;) {
		KuiEvent e = kui_next_event(ms_to_next_minute());
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_NONE) {
			if (!kui_input_asleep()) tick();
			continue;
		}
		if (e.type == KUI_EV_WAKE) {
			draw_all(1);
			continue;
		}
		if (e.type != KUI_EV_TAP) {
			if (!kui_input_asleep()) tick(); /* swipes etc. reset the timeout: keep the minute current */
			continue;
		}
		if (e.x < 120 && e.y < 90) break;
		if (kui_rect_contains(r_settings, e.x, e.y)) {
			settings_screen();
			if (quit_requested) break; /* no network waits on the way out */
			t_otd = t_prayer = 0;      /* options may have been switched on */
			load_all(1);
			draw_all(1);
			continue;
		}
		if (r_news.h && kui_rect_contains(r_news, e.x, e.y) && n_news) { /* the next three headlines */
			news_first = (news_first + news_shown) % n_news;
			draw_news();
			kui_flush(0);
		}
		if (!kui_input_asleep()) tick(); /* a tap near the minute boundary must not delay the clock a minute */
	}
	kui_prevent_sleep(0);
	news_free(news, n_news);
	kui_quit();
	return 0;
}

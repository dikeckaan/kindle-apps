/* Hava Durumu: Open-Meteo forecast for saved cities; the last forecast is cached for offline use. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "kui.h"
#include "net/net.h"
#include "wx.h"
#include "wxdraw.h"

#define FRESH_S (30 * 60) /* a cached forecast younger than this is shown without fetching */

static WxCity cities[WX_MAX_CITIES];
static int n_cities, cur;
static Wx wx;
static int have_wx;
static long fetched;     /* unix time of the shown forecast */
static int offline;      /* last fetch failed */
static const char *status_msg;
static KuiButton nav[3];
enum { BTN_PREV = 1, BTN_NEXT, BTN_REFRESH };

static const char *cities_path(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/cities.txt", kui_data_dir("weather"));
	return p;
}

static void cache_path(const WxCity *c, char *out, int cap)
{
	char name[64];
	wx_cache_name(c, name, sizeof name);
	snprintf(out, (size_t) cap, "%s/%s", kui_data_dir("weather"), name);
}

static int load_cache(void)
{
	static char buf[65536];
	char path[400];
	struct stat st;
	cache_path(&cities[cur], path, sizeof path);
	have_wx = kui_read_file(path, buf, sizeof buf) > 0 && wx_parse(buf, &wx) == 0;
	if (have_wx) { /* a cached forecast: start at the current hour */
		char iso[17];
		wx_local_iso(&wx, iso);
		wx_advance(&wx, iso);
	}
	fetched = have_wx && stat(path, &st) == 0 ? (long) st.st_mtime : 0;
	return have_wx;
}

static int fetch(void)
{
	char url[700], path[400];
	char *body = NULL;
	int len = 0;
	static Wx fresh;
	wx_forecast_url(&cities[cur], url, sizeof url);
	if (net_get(url, &body, &len, 20) != 0 || wx_parse(body, &fresh) != 0) {
		free(body);
		offline = 1;
		return -1;
	}
	cache_path(&cities[cur], path, sizeof path);
	kui_write_atomic(path, body, len);
	free(body);
	wx = fresh;
	have_wx = 1;
	fetched = (long) time(NULL);
	offline = 0;
	return 0;
}

/* city-local wall time of a unix timestamp, from the forecast's UTC offset */
/* first n characters (not bytes) of a UTF-8 string, so short month names never end in half a letter */
static void utf8_head(const char *s, int n, char *out, int cap)
{
	int o = 0;
	for (; *s && n > 0 && o < cap - 4; n--) {
		do out[o++] = *s++;
		while ((*s & 0xC0) == 0x80);
	}
	out[o] = '\0';
}

static void local_hhmm(long t, char *out, int cap)
{
	time_t lt = (time_t) (t + (have_wx ? wx.utc_offset : 0));
	struct tm tm;
	gmtime_r(&lt, &tm);
	if (time(NULL) - t > 20 * 3600) {
		char mon[16];
		utf8_head(kui_tr_month(tm.tm_mon), 3, mon, sizeof mon);
		snprintf(out, (size_t) cap, "%d %s %02d:%02d", tm.tm_mday, mon, tm.tm_hour, tm.tm_min);
	} else snprintf(out, (size_t) cap, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* percentage in the language's style: "%73" in Turkish, "73 %" in German … */
static void fmt_pct(int v, char *out, int cap)
{
	if (v < 0) snprintf(out, (size_t) cap, "—");
	else snprintf(out, (size_t) cap, T("%%%d"), v);
}

static void draw_status(void)
{
	int w = kui_width(), h = kui_height();
	KuiRect bar = { 0, h - 110, w - 300, 110 };
	char s[160], t[48];
	kui_fill(bar, 255);
	if (status_msg) {
		snprintf(s, sizeof s, "%s", status_msg);
	} else if (have_wx) {
		local_hhmm(fetched, t, sizeof t);
		snprintf(s, sizeof s, offline ? T("Çevrimdışı — son veri %s · Open-Meteo") : T("Güncellendi %s · Open-Meteo"), t);
	} else {
		snprintf(s, sizeof s, "%s", offline ? T("Bağlantı yok, veri alınamadı") : "");
	}
	kui_text((KuiRect){ 30, bar.y, bar.w - 30, bar.h }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, bar.w - 40), KUI_ALIGN_LEFT, 0);
	nav[2] = (KuiButton){ { w - 280, h - 100, 250, 90 }, T("Yenile"), BTN_REFRESH };
	kui_button(&nav[2], 0);
	kui_mark_dirty((KuiRect){ 0, h - 110, w, 110 });
}

static void draw(int full)
{
	int w = kui_width(), h = kui_height();
	char s[160], a[32], b[32];
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Hava Durumu"), T("Şehirler"));
	const WxCity *c = &cities[cur];
	int y = KUI_TOPBAR_H;
	/* city row with ◀ ▶ when there is more than one city */
	snprintf(s, sizeof s, "%s, %s", c->name, c->admin);
	kui_text((KuiRect){ 130, y, w - 260, 100 }, s, KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0);
	nav[0] = (KuiButton){ { 10, y + 5, 110, 90 }, "◀", BTN_PREV };
	nav[1] = (KuiButton){ { w - 120, y + 5, 110, 90 }, "▶", BTN_NEXT };
	if (n_cities > 1) {
		kui_button(&nav[0], 0);
		kui_button(&nav[1], 0);
	}
	y += 110;
	kui_fill((KuiRect){ 30, y, w - 60, 2 }, 0);
	if (!have_wx) {
		kui_text((KuiRect){ 40, y + 200, w - 80, 300 }, offline ? T("Hava durumu alınamadı.\nWi-Fi bağlantısını kontrol edip Yenile'ye basın.")
									: T("Yükleniyor…"),
			 KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
	} else {
		/* now: icon, temperature, condition; details on the right */
		wx_draw_icon((KuiRect){ 30, y + 30, 260, 260 }, wx_code_icon(wx.code, wx.is_day));
		wx_fmt_temp(wx.temp, s, sizeof s);
		kui_text((KuiRect){ 290, y + 20, 360, 200 }, s, KUI_FONT_BOLD, 170, KUI_ALIGN_LEFT, 0);
		const char *cond = T(wx_code_text(wx.code));
		kui_text((KuiRect){ 290, y + 220, 380, 70 }, cond, KUI_FONT_REG, kui_fit_px(cond, KUI_FONT_REG, 46, 380), KUI_ALIGN_LEFT, 0);
		int dx = 680, dy = y + 40;
		wx_fmt_temp(wx.feels, a, sizeof a);
		snprintf(s, sizeof s, T("Hissedilen %s"), a);
		kui_text((KuiRect){ dx, dy, w - dx - 20, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - dx - 20), KUI_ALIGN_LEFT, 0);
		if (isnan(wx.wind)) snprintf(s, sizeof s, "%s", T("Rüzgâr —"));
		else snprintf(s, sizeof s, T("Rüzgâr %ld km/s %s"), lround(wx.wind), wx.wind_dir < 0 ? "" : T(wx_wind_dir(wx.wind_dir)));
		kui_text((KuiRect){ dx, dy + 65, w - dx - 20, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - dx - 20), KUI_ALIGN_LEFT, 0);
		fmt_pct(wx.hum, a, sizeof a);
		snprintf(s, sizeof s, T("Nem %s"), a);
		kui_text((KuiRect){ dx, dy + 130, w - dx - 20, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - dx - 20), KUI_ALIGN_LEFT, 0);
		fmt_pct(wx.n_hours ? wx.hour[0].pop : (wx.n_days ? wx.day[0].pop : -1), a, sizeof a);
		snprintf(s, sizeof s, T("Yağış %s"), a);
		kui_text((KuiRect){ dx, dy + 195, w - dx - 20, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - dx - 20), KUI_ALIGN_LEFT, 0);
		y += 310;
		if (wx.n_days) {
			wx_fmt_temp(wx.day[0].tmax, a, sizeof a);
			wx_fmt_temp(wx.day[0].tmin, b, sizeof b);
			snprintf(s, sizeof s, T("Bugün  en yüksek %s · en düşük %s · ☼ %s – %s"), a, b, wx.day[0].sunrise, wx.day[0].sunset);
			kui_text((KuiRect){ 30, y, w - 60, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 32, w - 60), KUI_ALIGN_CENTER, 0);
		}
		y += 75;
		kui_fill((KuiRect){ 30, y, w - 60, 2 }, 0);
		/* 12-hour strip */
		if (wx.n_hours) {
			int cw = (w - 40) / 12;
			for (int i = 0; i < wx.n_hours && i < 12; i++) {
				int x = 20 + i * cw;
				snprintf(s, sizeof s, "%.2s", wx.hour[i].hh);
				kui_text((KuiRect){ x, y + 10, cw, 45 }, s, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0);
				int day = !wx.n_days || (strcmp(wx.hour[i].hh, wx.day[0].sunrise) >= 0 && strcmp(wx.hour[i].hh, wx.day[0].sunset) < 0);
				wx_draw_icon((KuiRect){ x + 4, y + 60, cw - 8, cw - 8 }, wx_code_icon(wx.hour[i].code, day));
				wx_fmt_temp(wx.hour[i].t, s, sizeof s);
				kui_text((KuiRect){ x, y + 60 + cw, cw, 45 }, s, KUI_FONT_BOLD, 30, KUI_ALIGN_CENTER, 0);
				fmt_pct(wx.hour[i].pop, s, sizeof s);
				kui_text((KuiRect){ x, y + 105 + cw, cw, 40 }, wx.hour[i].pop > 0 ? s : "", KUI_FONT_REG, 24, KUI_ALIGN_CENTER, 0);
			}
			y += 160 + cw;
		}
		kui_fill((KuiRect){ 30, y, w - 60, 2 }, 0);
		/* 7 days */
		int rh = (h - 120 - y - 10) / 7;
		if (rh > 80) rh = 80;
		for (int i = 0; i < wx.n_days; i++) {
			int ry = y + 10 + i * rh;
			const char *dn = i == 0 ? T("Bugün") : kui_tr_weekday_short(wx.day[i].wday);
			kui_text((KuiRect){ 30, ry, 160, rh }, dn, KUI_FONT_BOLD, kui_fit_px(dn, KUI_FONT_BOLD, 36, 155), KUI_ALIGN_LEFT, 0);
			wx_draw_icon((KuiRect){ 190, ry + 4, rh - 8, rh - 8 }, wx_code_icon(wx.day[i].code, 1));
			const char *dc = T(wx_code_text(wx.day[i].code));
			kui_text((KuiRect){ 200 + rh, ry, 640 - 210 - rh, rh }, dc, KUI_FONT_REG, kui_fit_px(dc, KUI_FONT_REG, 34, 640 - 210 - rh), KUI_ALIGN_LEFT, 0);
			wx_fmt_temp(wx.day[i].tmax, a, sizeof a);
			wx_fmt_temp(wx.day[i].tmin, b, sizeof b);
			snprintf(s, sizeof s, "%s / %s", a, b);
			kui_text((KuiRect){ 640, ry, 230, rh }, s, KUI_FONT_BOLD, 36, KUI_ALIGN_RIGHT, 0);
			fmt_pct(wx.day[i].pop, s, sizeof s);
			kui_text((KuiRect){ 880, ry, w - 910, rh }, wx.day[i].pop > 0 ? s : "", KUI_FONT_REG, 32, KUI_ALIGN_RIGHT, 0);
		}
	}
	draw_status();
	if (full) {
		kui_flush(1);
	} else { /* content update in place: no flash */
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

static void redraw_full(void) { draw(1); }

static void refresh(void)
{
	status_msg = T("Güncelleniyor…");
	draw_status();
	kui_flush(0);
	fetch();
	status_msg = NULL;
	draw(0);
}

static void show_city(int i, int full)
{
	cur = (i + n_cities) % n_cities;
	offline = 0;
	load_cache();
	draw(full);
	if (!have_wx || time(NULL) - fetched > FRESH_S) refresh();
}

/* ---- cities screen ---- */

static void message(const char *title, const char *msg)
{
	const char *ok[] = { T("Tamam") };
	kui_dialog(title, msg, ok, 1, NULL);
}

/* list of names with an optional "Sil" column; returns the tapped row (>= 0), -2 - row for Sil, -1 for ✕,
 * -100 for the top-right button */
static int pick_list(const char *title, const char *right, const WxCity *list, int n, int deletable, const char *empty)
{
	int w = kui_width();
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
			kui_topbar(title, right);
			if (!n) kui_text((KuiRect){ 40, 400, w - 80, 200 }, empty, KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
			for (int i = 0; i < n; i++) {
				KuiRect r = { 30, KUI_TOPBAR_H + 20 + i * 120, w - 60, 110 };
				char s[160];
				snprintf(s, sizeof s, "%s, %s", list[i].name, list[i].admin);
				kui_frame(r, 2, 0);
				kui_text((KuiRect){ r.x + 20, r.y, r.w - (deletable ? 220 : 40), r.h }, s, i == cur && deletable ? KUI_FONT_BOLD : KUI_FONT_REG,
					 40, KUI_ALIGN_LEFT, 0);
				if (deletable && n > 1) {
					KuiButton del = { { r.x + r.w - 180, r.y + 15, 160, r.h - 30 }, T("Sil"), 1 };
					kui_button(&del, 0);
				}
			}
			kui_flush(1);
			full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return -1;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(w, right != NULL, e.x, e.y);
		if (hit == KUI_BTN_EXIT) return -1;
		if (hit == KUI_BTN_MENU) return -100;
		for (int i = 0; i < n; i++) {
			KuiRect r = { 30, KUI_TOPBAR_H + 20 + i * 120, w - 60, 110 };
			if (!kui_rect_contains(r, e.x, e.y)) continue;
			if (deletable && n > 1 && e.x >= r.x + r.w - 180) return -2 - i;
			return i;
		}
	}
}

static void add_city(void)
{
	if (n_cities >= WX_MAX_CITIES) {
		message(T("Şehirler"), T("En fazla 10 şehir kaydedilebilir."));
		return;
	}
	char q[96] = "";
	if (!kui_input_text(T("Şehir veya ilçe ara"), q, sizeof q, 0, 2, NULL)) return;
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar(T("Aranıyor…"), NULL);
	kui_text((KuiRect){ 40, 500, kui_width() - 80, 120 }, q, KUI_FONT_BOLD, 48, KUI_ALIGN_CENTER, 0);
	kui_flush(1);
	char url[600];
	char *body = NULL;
	int len = 0;
	WxCity found[10];
	int n = -1;
	wx_geocode_url(q, kui_lang(), url, sizeof url);
	if (net_get(url, &body, &len, 20) == 0) n = wx_geocode_parse(body, q, found, 10);
	free(body);
	if (n < 0) {
		message(T("Şehir ara"), T("Bağlantı kurulamadı. Wi-Fi açık mı?"));
		return;
	}
	int i = pick_list(T("Sonuçlar"), NULL, found, n, 0, T("Sonuç bulunamadı"));
	if (i < 0) return;
	cities[n_cities++] = found[i];
	if (wx_cities_save(cities_path(), cities, n_cities) != 0) message(T("Şehirler"), T("Kaydedilemedi."));
	cur = n_cities - 1;
}

static void cities_screen(void)
{
	for (;;) {
		int r = pick_list(T("Şehirler"), T("+ Ekle"), cities, n_cities, 1, "");
		if (r == -1) return;
		if (r == -100) {
			int before = n_cities;
			add_city();
			if (n_cities > before) return; /* show the new city */
			continue;
		}
		if (r >= 0) {
			cur = r;
			return;
		}
		int d = -2 - r;
		char msg[160];
		snprintf(msg, sizeof msg, T("%s silinsin mi?"), cities[d].name);
		const char *yn[] = { T("Sil"), T("Vazgeç") };
		if (kui_dialog(T("Şehri sil"), msg, yn, 2, NULL) != 0) continue;
		memmove(&cities[d], &cities[d + 1], sizeof cities[0] * (size_t) (n_cities - d - 1));
		n_cities--;
		if (cur >= n_cities) cur = n_cities - 1;
		else if (cur > d) cur--;
		wx_cities_save(cities_path(), cities, n_cities);
	}
}

int main(void)
{
	if (kui_init("weather") != 0) return 1;
	n_cities = wx_cities_load(cities_path(), cities, WX_MAX_CITIES);
	show_city(0, 1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { redraw_full(); continue; }
		if (e.type == KUI_EV_SWIPE && n_cities > 1) {
			if (e.dir == KUI_DIR_LEFT) show_city(cur + 1, 0);
			if (e.dir == KUI_DIR_RIGHT) show_city(cur - 1, 0);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			cities_screen();
			show_city(cur, 1);
			continue;
		}
		int id = kui_buttons_hit(nav, 3, e.x, e.y);
		if (id == BTN_REFRESH) refresh();
		else if (id == BTN_PREV && n_cities > 1) show_city(cur - 1, 0);
		else if (id == BTN_NEXT && n_cities > 1) show_city(cur + 1, 0);
	}
	kui_quit();
	return 0;
}

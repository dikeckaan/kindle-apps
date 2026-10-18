/* Masa Saati: big clock, Turkish date and the first saved city's weather. Only the clock digits are
 * refreshed each minute (no flash); the whole screen is refreshed once an hour. Keeps the Kindle awake. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "kui.h"
#include "net/net.h"
#include "weather/wx.h"
#include "weather/wxdraw.h"

#define WX_EVERY_S (30 * 60)

static WxCity city;
static Wx wx;
static int have_wx;
static long wx_time;
static int shown_min = -1, shown_hour = -1, shown_mday = -1;

static void now_local(struct tm *tm)
{
	time_t t = time(NULL);
	localtime_r(&t, tm);
	/* no time zone configured on the device: use the city's offset from the forecast */
	if (tm->tm_gmtoff == 0 && !getenv("TZ") && have_wx && wx.utc_offset) {
		t += wx.utc_offset;
		gmtime_r(&t, tm);
	}
}

static void cache_path(char *out, int cap)
{
	char name[64];
	wx_cache_name(&city, name, sizeof name);
	snprintf(out, (size_t) cap, "%s/%s", kui_data_dir("weather"), name);
}

static void load_weather(void)
{
	static char buf[65536];
	char path[400], cpath[400];
	struct stat st;
	WxCity cs[WX_MAX_CITIES];
	snprintf(cpath, sizeof cpath, "%s/cities.txt", kui_data_dir("weather"));
	wx_cities_load(cpath, cs, WX_MAX_CITIES);
	city = cs[0];
	cache_path(path, sizeof path);
	have_wx = kui_read_file(path, buf, sizeof buf) > 0 && wx_parse(buf, &wx) == 0;
	wx_time = have_wx && stat(path, &st) == 0 ? (long) st.st_mtime : 0;
}

/* quietly refreshes the forecast when it is old; keeps the old one on failure */
static int update_weather(void)
{
	if (have_wx && time(NULL) - wx_time < WX_EVERY_S) return 0;
	char url[700], path[400];
	char *body = NULL;
	int len = 0;
	static Wx fresh;
	wx_forecast_url(&city, url, sizeof url);
	int ok = net_get(url, &body, &len, 20) == 0 && wx_parse(body, &fresh) == 0;
	if (ok) {
		cache_path(path, sizeof path);
		kui_write_atomic(path, body, len);
		wx = fresh;
		have_wx = 1;
	}
	wx_time = (long) time(NULL); /* also after a failure: retry in WX_EVERY_S, not every minute */
	free(body);
	return ok;
}

static KuiRect time_rect(void) { return (KuiRect){ 0, 150, kui_width(), 430 }; }

static void draw_time(const struct tm *tm)
{
	KuiRect r = time_rect();
	char s[16];
	snprintf(s, sizeof s, "%02d:%02d", tm->tm_hour, tm->tm_min);
	kui_fill(r, 255);
	kui_text(r, s, KUI_FONT_BOLD, kui_fit_px("00:00", KUI_FONT_BOLD, 380, r.w - 80), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
}

static void draw_all(const struct tm *tm)
{
	int w = kui_width(), h = kui_height();
	char s[128], a[32], b[32];
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_text(kui_topbar_exit_rect(), "✕", KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 0);
	draw_time(tm);
	kui_tr_date(tm, s, sizeof s);
	kui_text((KuiRect){ 20, 590, w - 40, 100 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 64, w - 60), KUI_ALIGN_CENTER, 0);
	kui_fill((KuiRect){ 60, 720, w - 120, 3 }, 0);
	if (!have_wx) {
		kui_text((KuiRect){ 40, 800, w - 80, 200 }, "Hava durumu yok (Wi-Fi kapalı olabilir)", KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
		return;
	}
	wx_draw_icon((KuiRect){ 70, 760, 230, 230 }, wx_code_icon(wx.code, wx.is_day));
	wx_fmt_temp(wx.temp, s, sizeof s);
	kui_text((KuiRect){ 330, 750, 380, 170 }, s, KUI_FONT_BOLD, 140, KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ 330, 920, w - 360, 70 }, wx_code_text(wx.code), KUI_FONT_REG, 46, KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ 700, 760, w - 740, 70 }, city.name, KUI_FONT_BOLD, kui_fit_px(city.name, KUI_FONT_BOLD, 44, w - 740), KUI_ALIGN_RIGHT, 0);
	if (wx.n_days) {
		wx_fmt_temp(wx.day[0].tmax, a, sizeof a);
		wx_fmt_temp(wx.day[0].tmin, b, sizeof b);
		snprintf(s, sizeof s, "%s / %s", a, b);
		kui_text((KuiRect){ 700, 840, w - 740, 70 }, s, KUI_FONT_REG, 44, KUI_ALIGN_RIGHT, 0);
	}
	/* next three days */
	int cols = wx.n_days > 3 ? 3 : wx.n_days - 1, cw = (w - 120) / 3;
	if (cols > 0) kui_fill((KuiRect){ 60, 1030, w - 120, 2 }, 0);
	for (int i = 0; i < cols; i++) {
		int x = 60 + i * cw, d = i + 1;
		kui_text((KuiRect){ x, 1060, cw, 70 }, kui_tr_weekday(wx.day[d].wday), KUI_FONT_BOLD, 42, KUI_ALIGN_CENTER, 0);
		wx_draw_icon((KuiRect){ x + cw / 2 - 70, 1140, 140, 140 }, wx_code_icon(wx.day[d].code, 1));
		wx_fmt_temp(wx.day[d].tmax, a, sizeof a);
		wx_fmt_temp(wx.day[d].tmin, b, sizeof b);
		snprintf(s, sizeof s, "%s / %s", a, b);
		kui_text((KuiRect){ x, 1300, cw, 70 }, s, KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
	}
}

static void full_redraw(void)
{
	struct tm tm;
	now_local(&tm);
	draw_all(&tm);
	kui_flush(1);
	shown_min = tm.tm_min;
	shown_hour = tm.tm_hour;
	shown_mday = tm.tm_mday;
}

/* called on every timeout: redraws only what changed */
static void tick(void)
{
	struct tm tm;
	now_local(&tm);
	if (tm.tm_hour != shown_hour || tm.tm_mday != shown_mday) { /* hourly: full refresh clears ghosting */
		update_weather();
		full_redraw();
		return;
	}
	if (tm.tm_min != shown_min) {
		draw_time(&tm);
		kui_flush(0);
		shown_min = tm.tm_min;
	}
	if (tm.tm_min % 30 == 0 && update_weather()) full_redraw();
}

static int ms_to_next_minute(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int) (60000 - (tv.tv_sec % 60) * 1000 - tv.tv_usec / 1000) + 200;
}

int main(void)
{
	if (kui_init("clock") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes are scheduled hourly by tick() */
	kui_prevent_sleep(1);
	load_weather();
	full_redraw();
	if (update_weather()) full_redraw();
	for (;;) {
		KuiEvent e = kui_next_event(ms_to_next_minute());
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_NONE) {
			if (!kui_input_asleep()) tick(); /* nothing is drawn or fetched behind the screensaver */
			continue;
		}
		if (e.type == KUI_EV_WAKE) { full_redraw(); continue; }
		if (e.type == KUI_EV_TAP && kui_rect_contains(kui_topbar_exit_rect(), e.x, e.y)) break;
	}
	kui_prevent_sleep(0);
	kui_quit();
	return 0;
}

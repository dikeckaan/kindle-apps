/* Masa Saati: big clock, Turkish date and the first saved city's weather. Only the clock digits are
 * refreshed each minute (no flash); the whole screen is refreshed once an hour.
 * Battery saver: after IDLE_S without a touch (or at once with the power button) the Kindle suspends between
 * minutes; Wi-Fi is on only while the forecast is fetched (every 6 h). On the charger it stays awake. */
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


static WxCity city;
static Wx wx;
static int have_wx;
static long wx_time;
static int shown_min = -1, shown_hour = -1, shown_mday = -1;
static int deep; /* in the battery saver's sleep loop */

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("clock"));
	return p;
}
#define WX_EVERY_S (kui_saver_get(settings(), KUI_SAVER_WX_HOURS) * 3600L)
#define IDLE_S (kui_saver_get(settings(), KUI_SAVER_IDLE_MIN) * 60L)

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
	kui_battery_max_age(WX_EVERY_S); /* the battery is read together with the forecast */
	if (have_wx && time(NULL) - wx_time < WX_EVERY_S) return 0;
	kui_battery_refresh();
	char url[700], path[400];
	char *body = NULL;
	int len = 0;
	static Wx fresh;
	wx_forecast_url(&city, url, sizeof url);
	int was_on = kui_wifi_enabled() == 1;
	if (!was_on) { /* the saver keeps Wi-Fi off: on just for this fetch */
		kui_wifi_enable(1);
		kui_wifi_wait(45);
	}
	int ok = net_get(url, &body, &len, 20) == 0 && wx_parse(body, &fresh) == 0;
	kui_wifi_enable(0);
	fprintf(stderr, "weather: %s\n", ok ? "updated" : "failed");
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

static KuiRect battery_rect(void) { return (KuiRect){ kui_width() - 300, 20, 280, 80 }; }

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
	if (have_wx) { /* up to 6 h old: "now" is the current hour's forecast */
		char iso[17];
		wx_local_iso(&wx, iso);
		wx_advance(&wx, iso);
	}
	int w = kui_width(), h = kui_height();
	char s[128], a[32], b[32];
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_text(kui_topbar_exit_rect(), "✕", KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 0);
	kui_battery(battery_rect());
	if (deep) {
		int every = kui_saver_get(settings(), KUI_SAVER_CLOCK_MIN);
		char z[64];
		if (every > 1) snprintf(z, sizeof z, T("☾ uykuda · %d dk"), every);
		else snprintf(z, sizeof z, "%s", T("☾ uykuda"));
		kui_text((KuiRect){ 140, 20, 400, 80 }, z, KUI_FONT_REG, kui_fit_px(z, KUI_FONT_REG, 36, 400), KUI_ALIGN_LEFT, 90);
	}
	draw_time(tm);
	kui_tr_date(tm, s, sizeof s);
	kui_text((KuiRect){ 20, 590, w - 40, 100 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 64, w - 60), KUI_ALIGN_CENTER, 0);
	kui_fill((KuiRect){ 60, 720, w - 120, 3 }, 0);
	if (!have_wx) {
		kui_text((KuiRect){ 40, 800, w - 80, 200 }, T("Hava durumu yok (Wi-Fi kapalı olabilir)"), KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
		return;
	}
	wx_draw_icon((KuiRect){ 70, 760, 230, 230 }, wx_code_icon(wx.code, wx.is_day));
	wx_fmt_temp(wx.temp, s, sizeof s);
	kui_text((KuiRect){ 330, 750, 380, 170 }, s, KUI_FONT_BOLD, 140, KUI_ALIGN_LEFT, 0);
	const char *cond = T(wx_code_text(wx.code));
	kui_text((KuiRect){ 330, 920, w - 360, 70 }, cond, KUI_FONT_REG, kui_fit_px(cond, KUI_FONT_REG, 46, w - 360), KUI_ALIGN_LEFT, 0);
	if (wx_time) { /* when the forecast was fetched */
		struct tm ft;
		time_t t = (time_t) wx_time;
		localtime_r(&t, &ft);
		if (ft.tm_gmtoff == 0 && !getenv("TZ") && wx.utc_offset) { /* no zone on the device: the city's */
			t += wx.utc_offset;
			gmtime_r(&t, &ft);
		}
		char ts[16];
		snprintf(ts, sizeof ts, "%02d:%02d", ft.tm_hour, ft.tm_min);
		snprintf(s, sizeof s, T("Güncellendi %s · Open-Meteo"), ts);
		kui_text((KuiRect){ 60, 1385, w - 120, 50 }, s, KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 90);
	}
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
		const char *wd = kui_tr_weekday(wx.day[d].wday);
		kui_text((KuiRect){ x, 1060, cw, 70 }, wd, KUI_FONT_BOLD, kui_fit_px(wd, KUI_FONT_BOLD, 42, cw - 10), KUI_ALIGN_CENTER, 0);
		wx_draw_icon((KuiRect){ x + cw / 2 - 70, 1140, 140, 140 }, wx_code_icon(wx.day[d].code, 1));
		wx_fmt_temp(wx.day[d].tmax, a, sizeof a);
		wx_fmt_temp(wx.day[d].tmin, b, sizeof b);
		snprintf(s, sizeof s, "%s / %s", a, b);
		kui_text((KuiRect){ x, 1300, cw, 70 }, s, KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
	}
}

/* the same screen without a flash (entering/leaving the saver only changes the header) */
static void soft_redraw(void)
{
	struct tm tm;
	now_local(&tm);
	draw_all(&tm);
	kui_flush(0);
	shown_min = tm.tm_min;
	shown_hour = tm.tm_hour;
	shown_mday = tm.tm_mday;
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
		kui_request_flash(); /* the minutes' partial updates leave ghosts only a flash clears */
		full_redraw();
		return;
	}
	if (tm.tm_min != shown_min) {
		draw_time(&tm); /* only the digits: the battery is redrawn with the hourly/forecast refreshes */
		kui_flush(0);
		shown_min = tm.tm_min;
	}
	if (tm.tm_min % 30 == 0 && update_weather()) full_redraw(); /* only when 6 h old */
}

static int charging(void) { return kui_battery_charging(); } /* read with the forecast, not every minute */

/* suspend between minutes until a touch, the power button or the charger */
static void deep_sleep_loop(void)
{
	fprintf(stderr, "deep: enter (battery %d%%)\n", kui_lipc_get_int("com.lab126.powerd", "battLevel", -1));
	deep = 1;
	kui_drain_power_key();
	if (kui_wifi_enabled() == 1) kui_wifi_enable(0);
	int light = kui_light_get();
	if (kui_saver_get(settings(), KUI_SAVER_LIGHT_OFF) && light > 0) kui_light_set(0); /* e-ink keeps the picture in the dark */
	soft_redraw();
	kui_wait_refresh();
	for (;;) {
		if (kui_quit_pending()) break;
		struct tm tm;
		now_local(&tm);
		if (tm.tm_min == shown_min) {
			int r = kui_sleep_until_every(kui_saver_get(settings(), KUI_SAVER_CLOCK_MIN));
			if (r != KUI_SLEEP_MINUTE) break;
		}
		if (charging()) break; /* plugged in: no need to sleep */
		tick();
		kui_wait_refresh();
	}
	deep = 0;
	if (light > 0 && kui_light_get() == 0) kui_light_set(light);
	kui_battery_refresh(); /* woken by hand: a fresh level (and the charger) */
	fprintf(stderr, "deep: leave (battery %d%%)\n", kui_lipc_get_int("com.lab126.powerd", "battLevel", -1));
	kui_drain_input(700); /* the touch that woke the Kindle must not press anything (e.g. ✕) */
	soft_redraw();
}

int main(void)
{
	if (kui_init("clock") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes are scheduled hourly by tick() */
	kui_prevent_sleep(1);
	int wifi_was = kui_wifi_enabled();
	fprintf(stderr, "power key grab: %d\n", kui_grab_power_key(1)); /* the power button sleeps/wakes the clock */
	load_weather();
	full_redraw();
	if (update_weather()) full_redraw();
	kui_wifi_enable(0); /* off until the next forecast */
	long last_input = (long) time(NULL);
	for (;;) {
		if (kui_quit_pending()) break;
		if (time(NULL) - last_input >= IDLE_S && !kui_input_asleep() && !charging()) {
			deep_sleep_loop();
			last_input = (long) time(NULL);
			continue;
		}
		KuiEvent e = kui_next_event(kui_ms_to_next_minute());
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_NONE) {
			if (!kui_input_asleep()) tick(); /* nothing is drawn or fetched behind the screensaver */
			continue;
		}
		if (e.type == KUI_EV_WAKE) { full_redraw(); continue; }
		if (e.type == KUI_EV_POWER) {
			if (!charging()) deep_sleep_loop();
			last_input = (long) time(NULL);
			continue;
		}
		last_input = (long) time(NULL);
		if (e.type == KUI_EV_TAP && kui_rect_contains(kui_topbar_exit_rect(), e.x, e.y)) break;
		if (e.type == KUI_EV_TAP && kui_rect_contains(battery_rect(), e.x, e.y)) { /* estimate, light, saver intervals */
			kui_power_screen(settings(), KUI_PS_SAVER);
			if (kui_quit_pending()) break;
			full_redraw();
			last_input = (long) time(NULL);
			continue;
		}
		if (!kui_input_asleep()) tick();
	}
	kui_grab_power_key(0);
	kui_prevent_sleep(0);
	if (wifi_was == 1 && kui_wifi_enabled() == 0) kui_wifi_enable(1); /* leave Wi-Fi as we found it */
	kui_quit();
	return 0;
}

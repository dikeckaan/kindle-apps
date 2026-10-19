/* Uyku Ekranı: a calm info screen for the night stand — today's date (and the Hijri date, the moon), the weather of
 * Hava Durumu's first city, today's and tomorrow's agenda from Ajanda (read only), Şiir Defteri's poem of the day (or
 * a proverb). Between updates the Kindle suspends to RAM (RTC alarm, Wi-Fi off) and wakes every 30 / 60 / 120
 * minutes for one quiet update; the power button returns to the normal Kindle.
 *
 * Masa Panosu's battery saver is a dashboard that wakes every minute for its clock and goes back to the dashboard on
 * the power button; this app shares its machinery, not its screen: the suspend, alarm and EBUSY retry are kui's
 * (core/kui/power.c), the weather cache and its fetch are Hava Durumu's (apps/weather/wx.c, the same files Masa
 * Panosu reads), the moon is Masa Panosu's pano.c, the agenda is Ajanda's agenda.c and the poem Şiir Defteri's
 * siir.c — all linked unchanged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "ajanda/agenda.h"
#include "kui.h"
#include "net/net.h"
#include "pano/pano.h"
#include "siir/siir.h"
#include "uyku.h"
#include "weather/wx.h"
#include "weather/wxdraw.h"

#define FOOT_H 110

enum { OPT_WX, OPT_AGENDA, OPT_POEM, OPT_LIGHT, N_OPT };
static const char *const opt_key[N_OPT] = { "weather", "agenda", "poem", "light_off" };
static const char *const opt_label[N_OPT] = { /* N_ */ "Hava durumu", "Ajanda: bugün ve yarın", "Günün şiiri", "Uykudayken ön ışığı kapat" };
static const char *const opt_sub[N_OPT] = { N_("Hava Durumu'ndaki ilk şehir; Wi-Fi üç saatte bir kısa süre açılır"),
					    N_("Ajanda'daki notlar, bayramlar ve özel günler"),
					    N_("Şiir Defteri kuruluysa onun günün şiiri, değilse bir atasözü"),
					    N_("Çıkınca ışık eski düzeyine döner") };

static WxCity city;
static Wx wx;
static int have_wx, wifi_was = -1, siir_ok;
static long wx_time;
static SiirDb db;

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("uyku"));
	return p;
}

static int opt(int i) { return kui_cfg_get_int(settings(), opt_key[i], 1); }
static int every_min(void) { return uyku_interval(kui_cfg_get_int(settings(), "every", 60)); }

/* ---- time: the device's zone, or the city's when none is set (as Masa Panosu does) ---- */

static int local_off(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	if (tm.tm_gmtoff || getenv("TZ")) return (int) tm.tm_gmtoff;
	return have_wx && wx.utc_offset ? wx.utc_offset : 3 * 3600;
}

static void local_tm(long t, struct tm *tm)
{
	time_t tt = (time_t) (t + local_off());
	gmtime_r(&tt, tm);
}

/* ---- data ---- */

static void load_weather(int fetch)
{
	static char buf[65536];
	char p[400], name[64];
	WxCity cs[WX_MAX_CITIES];
	snprintf(p, sizeof p, "%s/cities.txt", kui_data_dir("weather"));
	wx_cities_load(p, cs, WX_MAX_CITIES);
	city = cs[0];
	wx_cache_name(&city, name, sizeof name);
	snprintf(p, sizeof p, "%s/%s", kui_data_dir("weather"), name);
	struct stat st;
	have_wx = kui_read_file(p, buf, sizeof buf) > 0 && wx_parse(buf, &wx) == 0;
	wx_time = have_wx && stat(p, &st) == 0 ? (long) st.st_mtime : 0;
	if (have_wx) { /* an older forecast: start at the current hour */
		char iso[17];
		wx_local_iso(&wx, iso);
		wx_advance(&wx, iso);
	}
	if (!fetch) return;
	char url[700], *body = NULL;
	int len = 0;
	wx_forecast_url(&city, url, sizeof url);
	if (net_get(url, &body, &len, 20) == 0) {
		static Wx test;
		if (wx_parse(body, &test) == 0) kui_write_atomic(p, body, len); /* Hava Durumu and Masa Panosu read it too */
	}
	free(body);
	load_weather(0);
}

/* Wi-Fi only when the forecast is due; off again before the next suspend (its wakeup source blocks suspend) */
static void update_data(void)
{
	static long tried; /* a failed fetch (no network) is not retried on every wake: Wi-Fi costs more than the screen */
	load_weather(0);
	long now = (long) time(NULL);
	if (!opt(OPT_WX) || !uyku_weather_due(now, wx_time > tried ? wx_time : tried)) return;
	tried = now;
	if (kui_wifi_enabled() != 1) kui_wifi_enable(1);
	int up = kui_wifi_wait(45);
	fprintf(stderr, "uyku: wifi %s\n", up ? "connected" : "did not connect");
	if (up) load_weather(1);
	kui_wifi_enable(0);
}

static void load_siir(void)
{
	const char *root = getenv("KUI_ROOT");
	char p[300];
	snprintf(p, sizeof p, "%s/apps/siir/poems.txt", root && *root ? root : "/mnt/us/kindle-apps");
	siir_ok = siir_load(p, &db) > 0 || siir_load("apps/siir/data/poems.txt", &db) > 0; /* development tree (simulator) */
}

/* ---- drawing ---- */

/* centred wrapped text from y; returns the height used */
static int draw_centered(int y, const char *text, KuiFont f, int px, int max_lines, unsigned char fg)
{
	static KuiLine l[64];
	int w = kui_width(), tw = w - 160, n = kui_wrap_text(text, tw, f, px, l, 64), lh = kui_line_height(f, px);
	if (n > max_lines) n = max_lines;
	for (int i = 0; i < n; i++) {
		char buf[512];
		int len = l[i].len < (int) sizeof buf - 4 ? l[i].len : (int) sizeof buf - 4;
		memcpy(buf, text + l[i].off, (size_t) len);
		strcpy(buf + len, i == max_lines - 1 && l[i].off + l[i].len < (int) strlen(text) ? "…" : "");
		kui_text((KuiRect){ 80, y + i * lh, tw, lh }, buf, f, px, KUI_ALIGN_CENTER, fg);
	}
	return n * lh;
}

static void label(int y, const char *s)
{
	char up[96];
	kui_tr_upper_str(s, up, sizeof up);
	kui_text((KuiRect){ 80, y, kui_width() - 160, 40 }, up, KUI_FONT_BOLD, 26, KUI_ALIGN_CENTER, 90);
}

static void rule(int y, int half) { kui_fill((KuiRect){ kui_width() / 2 - half, y, 2 * half, 2 }, 120); }

typedef struct {
	char weekday[40], date[64], sub[160];
	char temp[16], cond[96], place[160], range[96];
	AgendaItem today[8], tomorrow[8];
	int n_today, n_tomorrow, poem;
	const char *proverb;
} View;

static void build_view(View *v, long now)
{
	struct tm tm;
	local_tm(now, &tm);
	int y = tm.tm_year + 1900, m = tm.tm_mon + 1, d = tm.tm_mday;
	memset(v, 0, sizeof *v);
	kui_tr_upper_str(kui_tr_weekday(tm.tm_wday), v->weekday, sizeof v->weekday);
	snprintf(v->date, sizeof v->date, "%s %d", kui_tr_month(tm.tm_mon), y);
	int hy, hm, hd;
	agenda_hijri(y, m, d, &hy, &hm, &hd);
	double age = pano_moon_age(now);
	snprintf(v->sub, sizeof v->sub, "%d %s %d · %s", hd, T(agenda_hijri_month_name(hm)), hy, T(pano_moon_phase_name(age)));
	if (opt(OPT_WX) && have_wx) {
		wx_fmt_temp(wx.temp, v->temp, sizeof v->temp);
		snprintf(v->cond, sizeof v->cond, "%s", T(wx_code_text(wx.code)));
		snprintf(v->place, sizeof v->place, "%s", city.name);
		if (wx.n_days) {
			char a[16], b[16];
			wx_fmt_temp(wx.day[0].tmax, a, sizeof a);
			wx_fmt_temp(wx.day[0].tmin, b, sizeof b);
			if (wx.day[0].pop > 0) snprintf(v->range, sizeof v->range, T("%s / %s · yağış %%%d"), a, b, wx.day[0].pop);
			else snprintf(v->range, sizeof v->range, "%s / %s", a, b);
		}
	}
	if (opt(OPT_AGENDA)) {
		const char *dir = kui_data_dir("ajanda");
		v->n_today = agenda_upcoming(dir, y, m, d, 1, v->today, 8);
		int ty = y, tmo = m, td = d;
		agenda_add_days(&ty, &tmo, &td, 1);
		v->n_tomorrow = agenda_upcoming(dir, ty, tmo, td, 1, v->tomorrow, 8);
	}
	long day = siir_days(y, m, d);
	v->poem = opt(OPT_POEM) && siir_ok ? siir_daily(day, db.n_poems) : -1;
	v->proverb = opt(OPT_POEM) && v->poem < 0 ? uyku_proverb(day) : NULL;
}

static unsigned long long view_hash(const View *v)
{
	unsigned long long h = uyku_hash(0, v->weekday);
	h = uyku_hash(h, v->date);
	h = uyku_hash(h, v->sub);
	h = uyku_hash(h, v->temp);
	h = uyku_hash(h, v->cond);
	h = uyku_hash(h, v->place);
	h = uyku_hash(h, v->range);
	for (int i = 0; i < v->n_today; i++) h = uyku_hash(h, v->today[i].text);
	h = uyku_hash(h, "|");
	for (int i = 0; i < v->n_tomorrow; i++) h = uyku_hash(h, v->tomorrow[i].text);
	char k[24];
	snprintf(k, sizeof k, "%d", v->poem);
	h = uyku_hash(h, k);
	return uyku_hash(h, v->proverb);
}

static const char *item_text(const AgendaItem *it) { return it->kind == AGENDA_NOTE ? it->text : T(it->text); }

/* agenda lines from y; returns the height used */
static int draw_agenda(int y, const char *head, const AgendaItem *it, int n, int max_lines)
{
	int w = kui_width(), lh = kui_line_height(KUI_FONT_SERIF, 34), y0 = y;
	label(y, head);
	y += 46;
	if (!n) {
		kui_text((KuiRect){ 80, y, w - 160, lh }, T("Kayıtlı bir şey yok."), KUI_FONT_SERIF, 32, KUI_ALIGN_CENTER, 110);
		return y + lh - y0;
	}
	for (int i = 0; i < n && i < max_lines; i++) {
		char s[AGENDA_TEXT_MAX + 16];
		const char *t = item_text(&it[i]);
		if (i == max_lines - 1 && n > max_lines) snprintf(s, sizeof s, T("%s (+%d)"), t, n - max_lines);
		else snprintf(s, sizeof s, "%s", t);
		KuiFont f = it[i].kind == AGENDA_HOLIDAY || it[i].kind == AGENDA_NOTE ? KUI_FONT_SERIF_BOLD : KUI_FONT_SERIF;
		kui_text((KuiRect){ 80, y, w - 160, lh }, s, f, kui_fit_px(s, f, 34, w - 160), KUI_ALIGN_CENTER, 0);
		y += lh;
	}
	return y - y0;
}

static void draw_body(const View *v)
{
	int w = kui_width(), bottom = kui_height() - FOOT_H, y;
	kui_fill((KuiRect){ 0, 0, w, bottom }, 255);
	struct tm tm;
	local_tm((long) time(NULL), &tm);
	char s[64];
	kui_text((KuiRect){ 0, 36, w, 54 }, v->weekday, KUI_FONT_BOLD, 40, KUI_ALIGN_CENTER, 0);
	snprintf(s, sizeof s, "%d", tm.tm_mday);
	kui_text((KuiRect){ 0, 88, w, 245 }, s, KUI_FONT_SERIF_BOLD, 230, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 0, 335, w, 76 }, v->date, KUI_FONT_SERIF, 60, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 60, 412, w - 120, 46 }, v->sub, KUI_FONT_REG, kui_fit_px(v->sub, KUI_FONT_REG, 30, w - 120), KUI_ALIGN_CENTER, 80);
	y = 476;
	rule(y, 90);
	y += 26;
	if (opt(OPT_WX)) {
		if (v->temp[0]) {
			/* icon and temperature on the left half, condition, city and the day's range on the right */
			int cx = w / 2;
			wx_draw_icon((KuiRect){ cx - 400, y + 6, 160, 160 }, wx_code_icon(wx.code, wx.is_day));
			kui_text((KuiRect){ cx - 230, y, 230, 170 }, v->temp, KUI_FONT_BOLD, kui_fit_px(v->temp, KUI_FONT_BOLD, 120, 220), KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ cx + 30, y + 12, w / 2 - 110, 54 }, v->cond, KUI_FONT_BOLD, kui_fit_px(v->cond, KUI_FONT_BOLD, 40, w / 2 - 110),
				 KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ cx + 30, y + 70, w / 2 - 110, 46 }, v->place, KUI_FONT_REG, kui_fit_px(v->place, KUI_FONT_REG, 32, w / 2 - 110),
				 KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ cx + 30, y + 120, w / 2 - 110, 44 }, v->range, KUI_FONT_REG, kui_fit_px(v->range, KUI_FONT_REG, 30, w / 2 - 110),
				 KUI_ALIGN_LEFT, 60);
		} else {
			kui_text((KuiRect){ 80, y, w - 160, 170 }, T("Hava durumu yok: Hava Durumu'nda bir şehir seçin; Wi-Fi ile güncellenir."), KUI_FONT_REG, 30,
				 KUI_ALIGN_CENTER, 80);
		}
		y += 172;
		rule(y, 90);
		y += 26;
	}
	if (opt(OPT_AGENDA)) {
		int room_poem = opt(OPT_POEM) ? 314 : 0;
		int lh = kui_line_height(KUI_FONT_SERIF, 34), avail = bottom - y - room_poem - 30;
		int max_today = (avail - 46) / lh;
		if (max_today > 3) max_today = 3;
		if (max_today < 1) max_today = 1;
		y += draw_agenda(y, T("Bugün"), v->today, v->n_today, max_today);
		int left = bottom - room_poem - y - 30;
		if (v->n_tomorrow && left >= 46 + lh + 16) {
			int mt = (left - 46 - 16) / lh;
			if (mt > 2) mt = 2;
			y += 16;
			y += draw_agenda(y, T("Yarın"), v->tomorrow, v->n_tomorrow, mt);
		}
		y += 20;
		if (opt(OPT_POEM)) {
			rule(y, 90);
			y += 26;
		}
	}
	if (v->poem >= 0) {
		const SiirPoem *p = &db.poems[v->poem];
		const SiirAuthor *a = &db.authors[p->author];
		label(y, T("Günün şiiri"));
		y += 46;
		y += draw_centered(y, p->title, KUI_FONT_SERIF_BOLD, 40, 2, 0);
		char by[160];
		snprintf(by, sizeof by, "%s · %s", a->name, a->years);
		kui_text((KuiRect){ 80, y, w - 160, 40 }, by, KUI_FONT_REG, 26, KUI_ALIGN_CENTER, 80);
		y += 54;
		/* the verse in a centred block, as many whole lines (to a stanza end) as fit */
		int px = 32, lh = kui_line_height(KUI_FONT_SERIF, px), max_lines = (bottom - 20 - y) / lh, more = 0;
		static char ex[4096];
		if (max_lines > 0) {
			int n = uyku_poem_excerpt(p->text, max_lines, ex, sizeof ex, &more);
			int widest = 0;
			for (char *q = ex; n && *q;) {
				char *e = strchr(q, '\n');
				int len = e ? (int) (e - q) : (int) strlen(q);
				char line[512];
				snprintf(line, sizeof line, "%.*s", len, q);
				int tw = kui_text_width(line, KUI_FONT_SERIF, px);
				if (tw > widest) widest = tw;
				q = e ? e + 1 : q + len;
			}
			if (widest > w - 160) widest = w - 160;
			int x0 = (w - widest) / 2, i = 0;
			for (char *q = ex; n && *q; i++) {
				char *e = strchr(q, '\n');
				int len = e ? (int) (e - q) : (int) strlen(q);
				char line[512];
				snprintf(line, sizeof line, "%.*s%s", len, q, !e && more ? " …" : "");
				if (len) kui_text((KuiRect){ x0, y + i * lh, w - x0 - 60, lh }, line, KUI_FONT_SERIF, kui_fit_px(line, KUI_FONT_SERIF, px, w - x0 - 60), KUI_ALIGN_LEFT, 0);
				if (!e) break;
				q = e + 1;
			}
		}
	} else if (v->proverb) {
		label(y, T("Günün sözü"));
		y += 70;
		y += draw_centered(y, v->proverb, KUI_FONT_SERIF, 44, 4, 0);
		kui_text((KuiRect){ 80, y + 10, w - 160, 40 }, T("Atasözü"), KUI_FONT_REG, 26, KUI_ALIGN_CENTER, 80);
	}
	kui_mark_dirty((KuiRect){ 0, 0, w, bottom });
}

static void draw_footer(long updated, long next)
{
	int w = kui_width(), h = kui_height();
	KuiRect r = { 0, h - FOOT_H, w, FOOT_H };
	kui_fill(r, 255);
	kui_fill((KuiRect){ 40, r.y + 4, w - 80, 2 }, 160);
	struct tm a, b;
	local_tm(updated, &a);
	local_tm(next, &b);
	char s[160];
	snprintf(s, sizeof s, T("☾ %02d:%02d'de güncellendi · sonraki %02d:%02d"), a.tm_hour, a.tm_min, b.tm_hour, b.tm_min);
	kui_text((KuiRect){ 40, r.y + 14, w - 340, 46 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 28, w - 340), KUI_ALIGN_LEFT, 40);
	const char *hint = T("Güç tuşu: Kindle'a dön");
	kui_text((KuiRect){ 40, r.y + 58, w - 340, 40 }, hint, KUI_FONT_REG, kui_fit_px(hint, KUI_FONT_REG, 24, w - 340), KUI_ALIGN_LEFT, 100);
	kui_battery((KuiRect){ w - 280, r.y + 18, 240, 76 });
	kui_mark_dirty(r);
}

/* ---- the sleep loop ---- */

enum { W_TIME, W_EXIT, W_SETTINGS, W_RESUME };

/* woken by a touch (or another wakeup source): a small box over the screen for a minute */
static int paused_box(void)
{
	int w = kui_width(), h = kui_height();
	KuiRect box = { 40, h - FOOT_H - 330, w - 80, 320 };
	int bw = (box.w - 4 * 30) / 3;
	KuiButton bs[3] = { { { box.x + 30, box.y + 180, bw, 110 }, T("Kindle'a dön"), 1 },
			    { { box.x + 60 + bw, box.y + 180, bw, 110 }, T("Ayarlar"), 2 },
			    { { box.x + 90 + 2 * bw, box.y + 180, bw, 110 }, T("Uykuya devam"), 3 } };
	kui_fill(box, 255);
	kui_frame(box, 4, 0);
	kui_text((KuiRect){ box.x + 20, box.y + 20, box.w - 40, 60 }, T("Uyku ekranı duraklatıldı"), KUI_FONT_BOLD, 42, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ box.x + 20, box.y + 90, box.w - 40, 70 }, T("Bir dakika içinde seçilmezse uykuya devam eder."), KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 60);
	for (int i = 0; i < 3; i++) kui_button(&bs[i], 0);
	kui_mark_dirty(box);
	kui_flush(0);
	time_t until = time(NULL) + 60;
	while (time(NULL) < until) {
		KuiEvent e = kui_next_event((int) (until - time(NULL)) * 1000);
		if (e.type == KUI_EV_QUIT || e.type == KUI_EV_POWER) return W_EXIT;
		if (e.type == KUI_EV_NONE) break;
		if (e.type != KUI_EV_TAP) continue;
		int id = kui_buttons_hit(bs, 3, e.x, e.y);
		if (id == 1) return W_EXIT;
		if (id == 2) return W_SETTINGS;
		if (id == 3) break;
	}
	return W_RESUME;
}

/* Waits until `target` (Unix time): suspended when it can be (power key ours, not charging), otherwise awake.
 * The power button — pressed while awake, or the press that woke the Kindle — means "back to the Kindle". */
static int wait_until(long target, int grabbed)
{
	for (;;) {
		KuiEvent e = kui_next_event(0); /* anything pressed while we were updating */
		if (e.type == KUI_EV_QUIT || e.type == KUI_EV_POWER) return W_EXIT;
		if (e.type == KUI_EV_WAKE) return W_EXIT; /* the Kindle's own screensaver was up (power key not ours) */
		if (e.type == KUI_EV_TAP) return paused_box();
		long left = target - (long) time(NULL);
		if (left <= 0) return W_TIME;
		if (grabbed && left > 4 && !kui_battery_charging()) {
			kui_wait_refresh(); /* the panel finished drawing (the kernel refuses suspend meanwhile) */
			int slept = kui_suspend((int) left);
			fprintf(stderr, "uyku: asked %ld s, slept %d s\n", left, slept);
			if (slept >= 0 && kui_woke_early((int) left, slept)) {
				/* was it the power button? its press arrives right after the resume */
				KuiEvent f = kui_next_event(1500);
				if (f.type == KUI_EV_QUIT || f.type == KUI_EV_POWER) return W_EXIT;
				kui_drain_input(500); /* the rest of a waking touch must not press a button */
				if (kui_quit_pending()) return W_EXIT;
				return paused_box();
			}
			if (slept >= 0) continue; /* the alarm: wait out the last seconds */
		}
		/* awake: on the charger, without the power key, or the suspend failed (retried within a minute) */
		e = kui_next_event(left > 60 ? 60000 : (int) left * 1000);
		if (e.type == KUI_EV_QUIT || e.type == KUI_EV_POWER || e.type == KUI_EV_WAKE) return W_EXIT;
		if (e.type == KUI_EV_TAP) return paused_box();
	}
}

/* runs the sleep screen; returns 1 to leave the app (back to the Kindle), 0 for the settings */
static int sleep_screen(void)
{
	int grabbed = kui_grab_power_key(1) == 0; /* power button: ours while the sleep screen runs */
	fprintf(stderr, "uyku: start, every %d min, power key %s, battery %d%%\n", every_min(), grabbed ? "grabbed" : "not available",
		kui_battery_level());
	kui_prevent_sleep(1); /* the Kindle's screensaver must not cover the screen while we are awake */
	int light = kui_light_get();
	if (opt(OPT_LIGHT) && light > 0) kui_light_set(0);
	kui_drain_power_key();
	unsigned long long prev = 0;
	int first = 1, partials = 0, last_day = -1, rc = 1;
	for (;;) {
		kui_battery_refresh();
		update_data();
		if (kui_wifi_enabled() == 1) kui_wifi_enable(0); /* off before the suspend: its wakeup source blocks it */
		long now = (long) time(NULL), next = uyku_next_wake(now, local_off(), every_min());
		struct tm tm;
		local_tm(now, &tm);
		static View v;
		build_view(&v, now);
		unsigned long long hsh = view_hash(&v);
		UykuUpd k = uyku_update_kind(first, prev, hsh, last_day >= 0 && tm.tm_mday != last_day, partials);
		if (!kui_input_asleep()) {
			if (k != UYKU_UPD_FOOTER) draw_body(&v);
			draw_footer(now, next);
			if (k == UYKU_UPD_FULL) {
				kui_flush(1);
				partials = 0;
			} else {
				kui_flush(0); /* in place: no flash */
				if (k == UYKU_UPD_PARTIAL) partials++;
			}
		}
		fprintf(stderr, "uyku: update %s, next in %ld s\n", k == UYKU_UPD_FULL ? "full" : k == UYKU_UPD_PARTIAL ? "partial" : "footer", next - now);
		first = 0;
		prev = hsh;
		last_day = tm.tm_mday;
		int r = wait_until(next, grabbed);
		if (r == W_EXIT) break;
		if (r == W_SETTINGS) {
			rc = 0;
			break;
		}
		if (r == W_RESUME) prev = 0; /* the box covered the screen: draw the body again (in place) */
	}
	if (light > 0 && kui_light_get() == 0) kui_light_set(light);
	kui_prevent_sleep(0);
	kui_grab_power_key(0);
	kui_battery_refresh();
	fprintf(stderr, "uyku: leave (battery %d%%)\n", kui_battery_level());
	return rc;
}

/* ---- settings (the first screen) ---- */

static int settings_screen(void)
{
	int w = kui_width(), h = kui_height();
	for (int full = 1;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Uyku Ekranı"), NULL);
		int y = KUI_TOPBAR_H + 30;
		const char *t = T("Yenileme aralığı");
		kui_text((KuiRect){ 40, y, w - 80, 50 }, t, KUI_FONT_BOLD, 36, KUI_ALIGN_LEFT, 0);
		y += 60;
		static const int mins[3] = { 30, 60, 120 };
		KuiButton seg[3];
		char sl[3][32];
		int sw = (w - 80 - 40) / 3;
		for (int i = 0; i < 3; i++) {
			snprintf(sl[i], sizeof sl[i], T("%d dakika"), mins[i]);
			seg[i] = (KuiButton){ { 40 + i * (sw + 20), y, sw, 100 }, sl[i], i + 1 };
			kui_button(&seg[i], every_min() == mins[i]);
		}
		y += 130;
		KuiRect rows[N_OPT];
		for (int i = 0; i < N_OPT; i++) {
			rows[i] = (KuiRect){ 40, y, w - 80, 118 };
			kui_frame(rows[i], 2, 0);
			const char *l = T(opt_label[i]), *s = T(opt_sub[i]);
			kui_text((KuiRect){ rows[i].x + 25, rows[i].y + 10, rows[i].w - 150, 56 }, l, KUI_FONT_BOLD, kui_fit_px(l, KUI_FONT_BOLD, 36, rows[i].w - 150),
				 KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ rows[i].x + 25, rows[i].y + 66, rows[i].w - 150, 40 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 24, rows[i].w - 150),
				 KUI_ALIGN_LEFT, 70);
			kui_text((KuiRect){ rows[i].x + rows[i].w - 110, rows[i].y, 100, rows[i].h }, opt(i) ? "☑" : "☐", KUI_FONT_REG, 60, KUI_ALIGN_CENTER, 0);
			y += 132;
		}
		y += 10;
		KuiButton start = { { 40, y, w - 80, 130 }, T("Uyku ekranını başlat"), 10 };
		kui_button(&start, 0);
		char info[600];
		snprintf(info, sizeof info,
			 T("Kindle her %d dakikada bir birkaç saniyeliğine uyanır, ekranı sessizce tazeler ve yeniden derin uykuya geçer; Wi-Fi yalnızca "
			   "hava durumu için birkaç saatte bir açılır. Güç tuşuna basınca normal Kindle'a dönülür."),
			 every_min());
		kui_text((KuiRect){ 50, y + 150, w - 100, h - y - 170 }, info, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 40);
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return 0;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
		int id = kui_buttons_hit(seg, 3, e.x, e.y);
		if (id) kui_cfg_set_int(settings(), "every", mins[id - 1]);
		for (int i = 0; i < N_OPT; i++)
			if (kui_rect_contains(rows[i], e.x, e.y)) kui_cfg_set_int(settings(), opt_key[i], !opt(i));
		if (kui_rect_contains(start.r, e.x, e.y)) return 1;
	}
}

int main(void)
{
	if (kui_init("uyku") != 0) return 1;
	kui_set_refresh_every(0); /* our own refresh plan: full only when entering a screen or on a new day */
	wifi_was = kui_wifi_enabled();
	load_siir();
	int to_kindle = 0;
	while (!kui_quit_pending() && settings_screen()) {
		if (sleep_screen()) {
			to_kindle = 1;
			break;
		}
	}
	if (wifi_was == 1 && kui_wifi_enabled() == 0) kui_wifi_enable(1); /* leave Wi-Fi as we found it */
	if (siir_ok) siir_free(&db);
	kui_quit();
	/* the power button: past the Uygulamalar menu, straight back to the Kindle (the hub exits on this code) */
	return to_kindle && !kui_quit_pending() ? KUI_EXIT_TO_KINDLE : 0;
}

/* Odak: a focus timer (Pomodoro 25/5, 50/10 or custom) with a session label, today's total, a 7-day chart and stats.
 * Battery: while a session runs the Kindle is suspended and wakes only when the shown minutes change (or at the
 * phase's end second); Wi-Fi is off for the session, the front light optionally too. Each wake redraws only the
 * digits and the ring segment that changed, without a flash; phase changes get a full refresh so they are noticed.
 * A touch wakes it and shows Duraklat / +5 dk / Bitir; the power button pauses. On the charger it stays awake. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "kui.h"
#include "odak.h"

#define LOG_CAP (128 * 1024)

static char log_buf[LOG_CAP];
static int log_len;
static OdakEntry entries[ODAK_LOG_MAX];
static int n_entries;
static OdakStats stats;
static char labels[ODAK_MAX_LABELS][ODAK_LABEL_MAX];
static int n_labels;
static OdakSession S;
static int quit_requested;
static long skew; /* simulator only: time jumps to each wake instead of waiting */

/* ---- time ---- */

static long now_s(void) { return (long) time(NULL) + skew; }

static int utc_off(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_gmtoff || getenv("TZ") ? (int) tm.tm_gmtoff : 3 * 3600; /* no zone on the device: Türkiye (UTC+3) */
}

static void fmt_hm(long t, char out[8])
{
	long l = t + utc_off();
	l %= 86400;
	if (l < 0) l += 86400;
	snprintf(out, 8, "%02d:%02d", (int) (l / 3600), (int) (l / 60 % 60));
}

static void fmt_dur(int min, char *out, int cap)
{
	if (min < 60) snprintf(out, (size_t) cap, T("%d dk"), min);
	else if (min % 60 == 0) snprintf(out, (size_t) cap, T("%d sa"), min / 60);
	else snprintf(out, (size_t) cap, T("%d sa %d dk"), min / 60, min % 60);
}

/* ---- files ---- */

static const char *path(const char *name)
{
	static char p[4][320];
	static int k;
	k = (k + 1) % 4;
	snprintf(p[k], sizeof p[k], "%s/%s", kui_data_dir("odak"), name);
	return p[k];
}
#define CFG path("settings")
static int cfg(const char *key, int def) { return kui_cfg_get_int(CFG, key, def); }
static void cfg_set(const char *key, int v) { kui_cfg_set_int(CFG, key, v); }

static void load_log(void)
{
	log_len = kui_read_file(path("log"), log_buf, sizeof log_buf);
	if (log_len < 0) log_len = 0;
	log_buf[log_len] = '\0';
	n_entries = odak_log_parse(log_buf, entries, ODAK_LOG_MAX);
	odak_stats(entries, n_entries, now_s(), utc_off(), &stats);
}

static void log_focus(long start, int min)
{
	if (min <= 0) return;
	OdakEntry e = { start, min, "" };
	int li = cfg("label", 0);
	memcpy(e.label, labels[li >= 0 && li < n_labels ? li : 0], sizeof e.label); /* same size, NUL-terminated */
	char line[128];
	if (odak_log_format(&e, line, sizeof line)) {
		log_len = odak_log_append(log_buf, log_len, sizeof log_buf, line);
		if (kui_write_atomic(path("log"), log_buf, log_len) != 0) fprintf(stderr, "odak: log write failed\n");
	}
	n_entries = odak_log_parse(log_buf, entries, ODAK_LOG_MAX);
	odak_stats(entries, n_entries, now_s(), utc_off(), &stats);
}

static void load_labels(void)
{
	char buf[1024];
	if (kui_read_file(path("labels"), buf, sizeof buf) < 0) buf[0] = '\0';
	n_labels = odak_labels_parse(buf, labels, ODAK_MAX_LABELS);
}

static void save_labels(void)
{
	char buf[1024];
	int n = odak_labels_format(labels, n_labels, buf, sizeof buf);
	kui_write_atomic(path("labels"), buf, n);
}

static const char *cur_label(void)
{
	int li = cfg("label", 0);
	return T(labels[li >= 0 && li < n_labels ? li : 0]);
}

static void cur_plan(OdakPlan *p) { odak_plan(cfg("preset", ODAK_PRESET_POMODORO), cfg("cfocus", 40), cfg("cbreak", 10), p); }

/* ---- waiting: suspend to RAM until a moment (or a touch / the power button) ---- */

static int sim(void) { return getenv("KUI_SIM") != NULL; }

/* woken from suspend by hand: what woke it (the power button arrives as an event); later touches are dropped */
static KuiEvent woken_by_hand(void)
{
	KuiEvent r = { KUI_EV_TAP, -1, -1, KUI_DIR_NONE }; /* a tap that hits nothing */
	struct timespec t0, t;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	kui_battery_refresh(); /* the charger may have been plugged in meanwhile */
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &t);
		int left = 700 - (int) ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
		if (left <= 0) break;
		KuiEvent e = kui_next_event(left);
		if (e.type == KUI_EV_QUIT) return e;
		if (e.type == KUI_EV_POWER) r.type = KUI_EV_POWER;
	}
	return r;
}

/* Until epoch second `until` (0: no limit): KUI_EV_NONE once it is reached, otherwise the event that came first.
 * may_suspend: sleep in suspend-to-RAM (not on the charger); a wake by hand gives a tap at (-1, -1) or KUI_EV_POWER. */
static KuiEvent wait_until(long until, int may_suspend)
{
	for (;;) {
		if (kui_quit_pending()) return (KuiEvent){ KUI_EV_QUIT, 0, 0, KUI_DIR_NONE };
		long now = now_s();
		if (until && now >= until) return (KuiEvent){ KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
		long secs = until ? until - now : 3600;
		if (may_suspend && secs > 4 && !kui_battery_charging()) { /* a few seconds are not worth a suspend */
			kui_wait_refresh();
			int slept = kui_suspend((int) secs);
			if (slept >= 0) {
				int early = kui_woke_early((int) secs, slept);
				fprintf(stderr, "deep: asked %ld s, slept %d s%s\n", secs, slept, early ? " (woken)" : "");
				if (early) return woken_by_hand();
				continue; /* the RTC (whole seconds) may wake it a moment early: the rest awake */
			}
			may_suspend = 0; /* suspend failed: wait awake this time */
		}
		int ms = 3600 * 1000;
		if (until) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			long m = (until - skew - (long) tv.tv_sec) * 1000 - tv.tv_usec / 1000 + 50;
			ms = m < 1 ? 1 : m > 60000 ? 60000 : (int) m;
		}
		KuiEvent e = kui_next_event(ms);
		if (e.type == KUI_EV_NONE) {
			if (sim() && until) skew += until - now_s(); /* the simulator does not wait */
			continue;
		}
		return e;
	}
}

/* ---- drawing helpers ---- */

#define W kui_width()
#define H kui_height()
#define RING_R 320
#define RING_T 36
#define RING_CY 480
#define RING_GRAY 205
static int ring_cx(void) { return W / 2; }
static KuiRect ring_box(void) { return (KuiRect){ ring_cx() - RING_R, RING_CY - RING_R, 2 * RING_R, 2 * RING_R }; }
static KuiRect phase_rect(void) { return (KuiRect){ ring_cx() - 200, RING_CY - 185, 400, 72 }; }
static KuiRect digits_rect(void) { return (KuiRect){ ring_cx() - 240, RING_CY - 112, 480, 230 }; }
static KuiRect unit_rect(void) { return (KuiRect){ ring_cx() - 150, RING_CY + 120, 300, 70 }; }
static KuiRect info_rect(void) { return (KuiRect){ 30, 822, W - 60, 62 }; }
static KuiRect today_rect(void) { return (KuiRect){ 30, 888, W - 60, 56 }; }
static KuiRect chart_rect(void) { return (KuiRect){ 40, 962, W - 80, 310 }; }
static KuiRect ctrl_rect(void) { return (KuiRect){ 0, 1288, W, H - 1288 }; }
static KuiRect battery_rect(void) { return (KuiRect){ W - 300, 20, 280, 80 }; }

/* The progress ring, only the pixels inside clip: elapsed (from 12 o'clock, clockwise) black, the rest gray. */
static void ring_draw(KuiRect clip, int prog, int scale)
{
	KuiRect b = ring_box();
	int x0 = clip.x > b.x ? clip.x : b.x, x1 = clip.x + clip.w < b.x + b.w ? clip.x + clip.w : b.x + b.w;
	int y0 = clip.y > b.y ? clip.y : b.y, y1 = clip.y + clip.h < b.y + b.h ? clip.y + clip.h : b.y + b.h;
	double ro2 = (double) RING_R * RING_R, ri2 = (double) (RING_R - RING_T) * (RING_R - RING_T), f = (double) prog / scale;
	int cx = ring_cx();
	for (int y = y0; y < y1; y++) {
		double dy = y + 0.5 - RING_CY;
		int run_x = -1, run_c = -1;
		for (int x = x0; x <= x1; x++) {
			int c = -1;
			if (x < x1) {
				double dx = x + 0.5 - cx, d2 = dx * dx + dy * dy;
				if (d2 <= ro2 && d2 >= ri2) {
					double a = atan2(dx, -dy); /* 0 at the top, clockwise */
					if (a < 0) a += 2 * M_PI;
					c = a / (2 * M_PI) < f ? 0 : RING_GRAY;
				}
			}
			if (c != run_c) {
				if (run_c >= 0) kui_fill((KuiRect){ run_x, y, x - run_x, 1 }, (unsigned char) run_c);
				run_x = x;
				run_c = c;
			}
		}
	}
}

/* the part of the ring between two progress values (whole ring if it went back) */
static KuiRect ring_part(int p0, int p1, int scale)
{
	if (p1 < p0 || p0 <= 0 || p1 - p0 > scale / 2) return ring_box();
	int cx = ring_cx(), minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1;
	for (int i = 0; i <= 64; i++) {
		double a = 2 * M_PI * (p0 + (double) (p1 - p0) * i / 64) / scale;
		for (int k = 0; k < 2; k++) {
			double r = k ? RING_R : RING_R - RING_T;
			int x = cx + (int) (r * sin(a)), y = RING_CY - (int) (r * cos(a));
			if (x < minx) minx = x;
			if (x > maxx) maxx = x;
			if (y < miny) miny = y;
			if (y > maxy) maxy = y;
		}
	}
	return (KuiRect){ minx - 3, miny - 3, maxx - minx + 7, maxy - miny + 7 };
}

static void draw_chart(KuiRect r, const int day_min[7])
{
	kui_fill(r, 255);
	const char *ttl = T("Son 7 gün");
	kui_text((KuiRect){ r.x, r.y, r.w, 44 }, ttl, KUI_FONT_BOLD, 32, KUI_ALIGN_LEFT, 0);
	int top = r.y + 92, base = r.y + r.h - 50, mx = 30;
	for (int i = 0; i < 7; i++)
		if (day_min[i] > mx) mx = day_min[i];
	int cw = r.w / 7;
	long today = odak_day(now_s(), utc_off());
	kui_fill((KuiRect){ r.x, base, r.w, 3 }, 0);
	for (int i = 0; i < 7; i++) {
		int x = r.x + i * cw, bh = day_min[i] * (base - top) / mx;
		if (day_min[i] > 0 && bh < 6) bh = 6;
		kui_fill((KuiRect){ x + cw / 2 - 40, base - bh, 80, bh }, i == 6 ? 0 : 110);
		if (day_min[i] > 0) {
			char s[16];
			snprintf(s, sizeof s, "%d", day_min[i]);
			kui_text((KuiRect){ x, base - bh - 44, cw, 40 }, s, KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 0);
		}
		int wd = (int) (((today - (6 - i)) % 7 + 7 + 4) % 7); /* day 0 was a Thursday; 0 = Sunday */
		const char *n = kui_tr_weekday_short(wd);
		kui_text((KuiRect){ x, base + 6, cw, 42 }, n, i == 6 ? KUI_FONT_BOLD : KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 0);
	}
	if (stats.total_min == 0)
		kui_text((KuiRect){ r.x, top, r.w, base - top }, T("Henüz oturum yok"), KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 90);
}

/* ---- the session screen ---- */

enum { M_RUN, M_CTRL, M_PAUSE, M_NOTICE };
static int mode;
static int shown_min = -1, shown_prog; /* what is on screen */
#define PROG_SCALE 3600
static KuiButton ctrl_btn[3];
enum { B_PAUSE = 1, B_EXTEND, B_STOP, B_START };

static const char *phase_name(void)
{
	if (S.state == ODAK_PAUSED) return T("DURAKLATILDI");
	return S.phase == ODAK_FOCUS ? T("ODAK") : S.phase == ODAK_LONG_BREAK ? T("UZUN MOLA") : T("MOLA");
}

static void draw_digits(long now)
{
	KuiRect r = digits_rect();
	char s[16];
	snprintf(s, sizeof s, "%d", odak_left_min(&S, now));
	kui_fill(r, 255);
	kui_text(r, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 240, r.w), KUI_ALIGN_CENTER, 0);
	shown_min = odak_left_min(&S, now);
}

static void draw_center(long now)
{
	KuiRect p = phase_rect(), u = unit_rect();
	const char *ph = phase_name();
	kui_fill(p, 255);
	kui_text(p, ph, KUI_FONT_BOLD, kui_fit_px(ph, KUI_FONT_BOLD, 46, p.w), KUI_ALIGN_CENTER, S.state == ODAK_PAUSED ? 0 : 60);
	draw_digits(now);
	kui_fill(u, 255);
	kui_text(u, T("dk"), KUI_FONT_REG, 60, KUI_ALIGN_CENTER, 0);
}

static void draw_info(long now)
{
	KuiRect r = info_rect(), t = today_rect();
	char s[200], d[48], hm[8];
	kui_fill(r, 255);
	int round = S.phase == ODAK_FOCUS ? S.done_focus + 1 : S.done_focus;
	if (S.state == ODAK_RUNNING) {
		fmt_hm(S.end, hm);
		snprintf(s, sizeof s, T("%s · Tur %d · Bitiş %s"), cur_label(), round, hm);
	} else {
		snprintf(s, sizeof s, T("%s · Tur %d"), cur_label(), round);
	}
	kui_text(r, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 40, r.w), KUI_ALIGN_CENTER, 0);
	(void) now;
	kui_fill(t, 255);
	fmt_dur(stats.today_min, d, sizeof d);
	snprintf(s, sizeof s, T("Bugün %s · %d oturum · Seri %d gün"), d, stats.today_n, stats.streak);
	kui_text(t, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, t.w), KUI_ALIGN_CENTER, 0);
}

static void draw_controls(void)
{
	KuiRect r = ctrl_rect();
	kui_fill(r, 255);
	kui_fill((KuiRect){ 30, r.y, W - 60, 2 }, 0);
	if (mode == M_RUN) {
		const char *h = T("Dokunun: Duraklat · +5 dk · Bitir");
		kui_text((KuiRect){ 30, r.y + 10, W - 60, 70 }, h, KUI_FONT_REG, kui_fit_px(h, KUI_FONT_REG, 34, W - 60), KUI_ALIGN_CENTER, 90);
		const char *z = kui_battery_charging() ? T("Şarjda: uyanık kalıyor") : T("☾ Kindle dakikalar arasında uyuyor");
		kui_text((KuiRect){ 30, r.y + 80, W - 60, 60 }, z, KUI_FONT_REG, kui_fit_px(z, KUI_FONT_REG, 28, W - 60), KUI_ALIGN_CENTER, 90);
		return;
	}
	int bw = (W - 4 * 30) / 3;
	const char *first = mode == M_PAUSE ? T("Devam") : T("Duraklat");
	ctrl_btn[0] = (KuiButton){ { 30, r.y + 20, bw, 120 }, first, B_PAUSE };
	ctrl_btn[1] = (KuiButton){ { 60 + bw, r.y + 20, bw, 120 }, T("+5 dk"), B_EXTEND };
	ctrl_btn[2] = (KuiButton){ { 90 + 2 * bw, r.y + 20, bw, 120 }, T("Bitir"), B_STOP };
	for (int i = 0; i < 3; i++) kui_button(&ctrl_btn[i], i == 0 && mode == M_PAUSE);
}

static void draw_session(int full)
{
	long now = now_s();
	if (full) {
		kui_fill((KuiRect){ 0, 0, W, H }, 255);
		kui_topbar(T("Odak"), NULL);
		kui_battery(battery_rect());
	}
	kui_fill(ring_box(), 255);
	shown_prog = odak_progress(&S, now, PROG_SCALE);
	ring_draw(ring_box(), shown_prog, PROG_SCALE);
	draw_center(now);
	draw_info(now);
	if (full) {
		kui_fill((KuiRect){ 30, 950, W - 60, 2 }, 0);
		draw_chart(chart_rect(), stats.day_min);
	}
	draw_controls();
	if (full) {
		kui_flush(1);
		return;
	}
	kui_mark_dirty((KuiRect){ 0, ring_box().y, W, today_rect().y + today_rect().h - ring_box().y });
	kui_mark_dirty(ctrl_rect());
	kui_flush(0);
}

/* once a minute: only the digits and the ring segment that changed, in one partial refresh */
static void minute_update(void)
{
	long now = now_s();
	int p = odak_progress(&S, now, PROG_SCALE);
	if (odak_left_min(&S, now) == shown_min && p == shown_prog) return;
	KuiRect part = ring_part(shown_prog, p, PROG_SCALE);
	ring_draw(part, p, PROG_SCALE);
	shown_prog = p;
	draw_digits(now);
	kui_mark_dirty(kui_rect_union(part, digits_rect()));
	kui_flush(0);
}

static void set_controls(int m)
{
	mode = m;
	draw_controls();
	kui_mark_dirty(ctrl_rect());
	kui_flush(0);
}

/* ---- phase-change notice: full screen, full refresh ---- */

static KuiButton notice_btn[2];
static int n_notice_btn;

static void draw_notice(int logged)
{
	long now = now_s();
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	kui_topbar(T("Odak"), NULL);
	KuiRect band = { 0, 260, W, 520 };
	kui_fill(band, 0);
	const char *title = S.state == ODAK_FINISHED ? T("Oturum bitti") : odak_is_break(S.phase) ? T("Mola zamanı") : T("Odak zamanı");
	kui_text_on((KuiRect){ 40, 330, W - 80, 220 }, title, KUI_FONT_BOLD, kui_fit_px(title, KUI_FONT_BOLD, 150, W - 80), KUI_ALIGN_CENTER, 255, 0);
	char s[160], d[48];
	if (S.state == ODAK_FINISHED) snprintf(s, sizeof s, "%s", T("Hiç mola yok: oturum tamam."));
	else {
		fmt_dur((int) (S.length / 60), d, sizeof d);
		snprintf(s, sizeof s, S.phase == ODAK_LONG_BREAK ? T("Uzun mola: %s") : odak_is_break(S.phase) ? T("Mola: %s") : T("Odak: %s"), d);
	}
	kui_text_on((KuiRect){ 40, 580, W - 80, 120 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 64, W - 80), KUI_ALIGN_CENTER, 255, 0);
	if (logged > 0) {
		fmt_dur(logged, d, sizeof d);
		snprintf(s, sizeof s, T("Tamamlanan odak: %s · %s"), d, cur_label());
	} else {
		snprintf(s, sizeof s, "%s", T("Mola bitti."));
	}
	kui_text((KuiRect){ 40, 830, W - 80, 70 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 44, W - 80), KUI_ALIGN_CENTER, 0);
	fmt_dur(stats.today_min, d, sizeof d);
	snprintf(s, sizeof s, T("Bugün %s · %d oturum · Seri %d gün"), d, stats.today_n, stats.streak);
	kui_text((KuiRect){ 40, 910, W - 80, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, W - 80), KUI_ALIGN_CENTER, 0);
	int bw = (W - 3 * 40) / 2;
	n_notice_btn = 0;
	if (S.state == ODAK_RUNNING) { /* started on its own */
		const char *a = T("Kendiliğinden başladı. Zamanlayıcı için dokunun.");
		kui_text((KuiRect){ 40, 1060, W - 80, 70 }, a, KUI_FONT_REG, kui_fit_px(a, KUI_FONT_REG, 38, W - 80), KUI_ALIGN_CENTER, 0);
		(void) now;
	} else if (S.state == ODAK_WAITING) {
		notice_btn[n_notice_btn++] = (KuiButton){ { 40, 1100, bw, 180 }, T("Başla"), B_START };
		notice_btn[n_notice_btn++] = (KuiButton){ { 80 + bw, 1100, bw, 180 }, T("Bitir"), B_STOP };
	} else {
		notice_btn[n_notice_btn++] = (KuiButton){ { W / 2 - bw / 2, 1100, bw, 180 }, T("Tamam"), B_STOP };
	}
	for (int i = 0; i < n_notice_btn; i++) kui_button(&notice_btn[i], 0);
	kui_flush(1);
}

/* ---- session ---- */

static int wifi_off_by_us, light_saved = -1;

/* the front light: off for focus phases when asked, back for breaks and afterwards */
static void focus_light(int focus)
{
	if (focus && cfg("light_off", 0)) {
		int l = kui_light_get();
		if (l > 0) {
			light_saved = l;
			kui_light_set(0);
		}
	} else if (light_saved > 0) {
		if (kui_light_get() == 0) kui_light_set(light_saved);
		light_saved = -1;
	}
}

static void session_enter(void)
{
	kui_prevent_sleep(1);
	fprintf(stderr, "power key grab: %d\n", kui_grab_power_key(1)); /* the power button pauses */
	kui_battery_max_age(30 * 60); /* two lipc calls: not on every wake */
	kui_battery_refresh();
	wifi_off_by_us = kui_wifi_enabled() == 1;
	if (wifi_off_by_us) kui_wifi_enable(0); /* its wakeup source would block suspend, and it costs power */
	fprintf(stderr, "odak: session (battery %d%%)\n", kui_battery_level());
}

static void session_leave(void)
{
	focus_light(0);
	if (wifi_off_by_us && kui_wifi_enabled() == 0) kui_wifi_enable(1); /* as we found it */
	wifi_off_by_us = 0;
	kui_grab_power_key(0);
	kui_battery_max_age(60);
	kui_prevent_sleep(0);
	fprintf(stderr, "odak: session over (battery %d%%)\n", kui_battery_level());
}

static void phase_over(void)
{
	long now = now_s(), started = S.phase_start;
	int logged = odak_complete(&S, now);
	log_focus(started, logged);
	if (S.state == ODAK_WAITING && cfg("auto", 0)) odak_begin_next(&S, now);
	focus_light(S.state == ODAK_RUNNING && S.phase == ODAK_FOCUS);
	mode = M_NOTICE;
	draw_notice(logged);
}

static void stop_session(void)
{
	long now = now_s(), started = S.phase_start;
	log_focus(started, odak_stop(&S, now));
}

/* runs a session until Bitir (or quit) */
static void session(void)
{
	OdakPlan p;
	cur_plan(&p);
	session_enter();
	odak_start(&S, &p, now_s());
	focus_light(1);
	mode = M_RUN;
	draw_session(1);
	long ctrl_until = 0, awake_until = 0;
	for (;;) {
		long now = now_s();
		if (odak_due(&S, now) && mode != M_NOTICE) {
			phase_over();
			awake_until = now_s() + 60;
			continue;
		}
		if (odak_due(&S, now) && mode == M_NOTICE) { /* an automatic phase ran out while its notice was up */
			phase_over();
			continue;
		}
		long until;
		int may_suspend;
		switch (mode) {
		case M_RUN: until = odak_next_wake(&S, now); may_suspend = 1; break;
		case M_CTRL: {
			long w = odak_next_wake(&S, now);
			until = w && w < ctrl_until ? w : ctrl_until;
			may_suspend = 0;
			break;
		}
		default: /* paused or a notice: awake a minute for taps, then asleep until a touch */
			if (S.state == ODAK_RUNNING) { /* the next phase started on its own: its notice stays until the minute */
				until = odak_next_wake(&S, now);
				may_suspend = 1;
			} else if (now < awake_until) {
				until = awake_until;
				may_suspend = 0;
			} else {
				until = 0;
				may_suspend = 1;
			}
			break;
		}
		KuiEvent e = wait_until(until, may_suspend);
		now = now_s();
		if (e.type == KUI_EV_QUIT) {
			quit_requested = 1;
			stop_session();
			break;
		}
		if (e.type == KUI_EV_WAKE) { /* the screensaver covered everything */
			if (mode == M_NOTICE) draw_notice(0);
			else draw_session(1);
			continue;
		}
		if (e.type == KUI_EV_NONE) {
			if (mode == M_NOTICE && S.state == ODAK_RUNNING) {
				mode = M_RUN;
				draw_session(1); /* a new screen */
			} else if (mode == M_CTRL && now >= ctrl_until) {
				mode = M_RUN;
				minute_update();
				set_controls(M_RUN);
			} else if (mode == M_RUN || mode == M_CTRL) {
				minute_update();
			}
			continue;
		}
		if (e.type == KUI_EV_POWER) {
			if (S.state == ODAK_RUNNING && mode != M_NOTICE) {
				odak_pause(&S, now);
				mode = M_PAUSE;
				draw_session(0);
			} else if (S.state == ODAK_PAUSED) {
				odak_resume(&S, now);
				mode = M_RUN;
				draw_session(0);
			} else if (S.state == ODAK_WAITING) {
				odak_begin_next(&S, now);
				focus_light(S.phase == ODAK_FOCUS);
				mode = M_RUN;
				draw_session(1);
			} else if (mode == M_NOTICE && S.state == ODAK_RUNNING) {
				mode = M_RUN;
				draw_session(1);
			}
			awake_until = now + 60;
			continue;
		}
		if (e.type != KUI_EV_TAP && e.type != KUI_EV_LONGPRESS && e.type != KUI_EV_SWIPE) continue;
		awake_until = now + 60;
		int exit_hit = e.type == KUI_EV_TAP && kui_topbar_hit(W, 0, e.x, e.y) == KUI_BTN_EXIT;
		if (mode == M_NOTICE) {
			int id = e.type == KUI_EV_TAP ? kui_buttons_hit(notice_btn, n_notice_btn, e.x, e.y) : 0;
			if (S.state == ODAK_RUNNING) { /* auto-started: back to the timer, with the controls */
				if (exit_hit) break;
				mode = M_CTRL;
				ctrl_until = now + 20;
				draw_session(1);
			} else if (id == B_START) {
				odak_begin_next(&S, now);
				focus_light(S.phase == ODAK_FOCUS);
				mode = M_RUN;
				draw_session(1);
			} else if (id == B_STOP || exit_hit) {
				break;
			}
			continue;
		}
		if (mode == M_RUN) { /* the first touch only shows the controls */
			kui_battery_refresh();
			ctrl_until = now + 20;
			set_controls(M_CTRL);
			continue;
		}
		if (exit_hit) {
			stop_session();
			break;
		}
		int id = e.type == KUI_EV_TAP ? kui_buttons_hit(ctrl_btn, 3, e.x, e.y) : 0;
		ctrl_until = now + 20;
		if (id == B_PAUSE && mode == M_CTRL) {
			odak_pause(&S, now);
			mode = M_PAUSE;
			draw_session(0);
		} else if (id == B_PAUSE && mode == M_PAUSE) {
			odak_resume(&S, now);
			mode = M_CTRL;
			draw_session(0);
		} else if (id == B_EXTEND) {
			odak_extend(&S, now, 5);
			draw_session(0);
		} else if (id == B_STOP) {
			stop_session();
			break;
		}
	}
	session_leave();
}

/* ---- stats screen ---- */

static void stats_screen(void)
{
	for (int full = 1;;) {
		odak_stats(entries, n_entries, now_s(), utc_off(), &stats);
		kui_fill((KuiRect){ 0, 0, W, H }, 255);
		kui_topbar(T("İstatistik"), NULL);
		char v[48], s[96];
		const char *cap[3] = { T("Bugün"), T("Bu hafta"), T("Seri") };
		int tw = (W - 4 * 30) / 3;
		for (int i = 0; i < 3; i++) {
			KuiRect t = { 30 + i * (tw + 30), KUI_TOPBAR_H + 30, tw, 200 };
			kui_frame(t, 3, 0);
			if (i == 0) fmt_dur(stats.today_min, v, sizeof v);
			else if (i == 1) fmt_dur(stats.week_min, v, sizeof v);
			else snprintf(v, sizeof v, T("%d gün"), stats.streak);
			kui_text((KuiRect){ t.x + 10, t.y + 20, t.w - 20, 100 }, v, KUI_FONT_BOLD, kui_fit_px(v, KUI_FONT_BOLD, 64, t.w - 30), KUI_ALIGN_CENTER, 0);
			kui_text((KuiRect){ t.x + 10, t.y + 125, t.w - 20, 55 }, cap[i], KUI_FONT_REG, kui_fit_px(cap[i], KUI_FONT_REG, 36, t.w - 30), KUI_ALIGN_CENTER, 0);
		}
		snprintf(s, sizeof s, T("Bugün %d oturum"), stats.today_n);
		kui_text((KuiRect){ 30, 360, W - 60, 50 }, s, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 90);
		draw_chart((KuiRect){ 40, 430, W - 80, 400 }, stats.day_min);
		int y = 870;
		kui_fill((KuiRect){ 30, y, W - 60, 2 }, 0);
		fmt_dur(stats.total_min, v, sizeof v);
		snprintf(s, sizeof s, T("Etiketler · toplam %s"), v);
		kui_text((KuiRect){ 40, y + 12, W - 80, 50 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 34, W - 80), KUI_ALIGN_LEFT, 0);
		y += 74;
		int mx = stats.n_labels ? stats.label_min[0] : 1, rowh = 62;
		for (int i = 0; i < stats.n_labels && y + rowh <= H - 20; i++, y += rowh) {
			const char *l = stats.label[i][0] ? T(stats.label[i]) : T("Etiketsiz");
			kui_text((KuiRect){ 40, y, 260, rowh - 8 }, l, KUI_FONT_REG, kui_fit_px(l, KUI_FONT_REG, 34, 250), KUI_ALIGN_LEFT, 0);
			int bw = (W - 40 - 310 - 200) * stats.label_min[i] / (mx ? mx : 1);
			kui_fill((KuiRect){ 310, y + 10, bw > 4 ? bw : 4, rowh - 28 }, 80);
			fmt_dur(stats.label_min[i], v, sizeof v);
			kui_text((KuiRect){ W - 230, y, 190, rowh - 8 }, v, KUI_FONT_BOLD, kui_fit_px(v, KUI_FONT_BOLD, 32, 190), KUI_ALIGN_RIGHT, 0);
		}
		if (!stats.n_labels)
			kui_text((KuiRect){ 40, y, W - 80, 60 }, T("Henüz oturum yok"), KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 90);
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, 0, W, H });
			kui_flush(0);
		}
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) {
			quit_requested = 1;
			return;
		}
		if (e.type == KUI_EV_WAKE) {
			full = 1;
			continue;
		}
		if (e.type == KUI_EV_TAP && kui_topbar_hit(W, 0, e.x, e.y) == KUI_BTN_EXIT) return;
	}
}

/* ---- home: plan, label, options, start ---- */

enum { H_PRESET = 10, H_FOCUS_DN = 20, H_FOCUS_UP, H_BREAK_DN, H_BREAK_UP, H_LABEL = 30, H_ADD = 50, H_AUTO, H_LIGHT, H_START };
static KuiButton hb[32];
static int n_hb;

static void draw_home(void)
{
	int preset = cfg("preset", ODAK_PRESET_POMODORO), cf = cfg("cfocus", 40), cb = cfg("cbreak", 10);
	OdakPlan cp;
	odak_plan(ODAK_PRESET_CUSTOM, cf, cb, &cp);
	kui_fill((KuiRect){ 0, 0, W, H }, 255);
	kui_topbar(T("Odak"), T("İstatistik"));
	n_hb = 0;
	kui_text((KuiRect){ 40, 135, W - 80, 50 }, T("Süre"), KUI_FONT_BOLD, 34, KUI_ALIGN_LEFT, 0);
	static char pl[3][96];
	snprintf(pl[0], sizeof pl[0], "%s", T("Pomodoro · 25 / 5 dk (4. mola 15)"));
	snprintf(pl[1], sizeof pl[1], "%s", T("Uzun · 50 / 10 dk"));
	snprintf(pl[2], sizeof pl[2], T("Özel · %d / %d dk"), cp.focus_min, cp.break_min);
	for (int i = 0; i < 3; i++) {
		hb[n_hb] = (KuiButton){ { 40, 190 + i * 110, W - 80, 98 }, pl[i], H_PRESET + i };
		kui_button(&hb[n_hb++], i == preset);
	}
	/* number pickers for the custom plan */
	int y = 190 + 3 * 110, half = (W - 80 - 20) / 2;
	static char fv[48], bv[48];
	snprintf(fv, sizeof fv, T("Odak %d dk"), cp.focus_min);
	snprintf(bv, sizeof bv, T("Mola %d dk"), cp.break_min);
	for (int k = 0; k < 2; k++) {
		int x = 40 + k * (half + 20);
		hb[n_hb] = (KuiButton){ { x, y, 100, 96 }, "−", k ? H_BREAK_DN : H_FOCUS_DN };
		kui_button(&hb[n_hb++], 0);
		hb[n_hb] = (KuiButton){ { x + half - 100, y, 100, 96 }, "+", k ? H_BREAK_UP : H_FOCUS_UP };
		kui_button(&hb[n_hb++], 0);
		const char *t = k ? bv : fv;
		kui_text((KuiRect){ x + 100, y, half - 200, 96 }, t, KUI_FONT_BOLD, kui_fit_px(t, KUI_FONT_BOLD, 38, half - 210), KUI_ALIGN_CENTER, 0);
	}
	y += 118;
	kui_text((KuiRect){ 40, y, W - 160, 50 }, T("Etiket (silmek için basılı tutun)"), KUI_FONT_BOLD, kui_fit_px(T("Etiket (silmek için basılı tutun)"), KUI_FONT_BOLD, 34, W - 80), KUI_ALIGN_LEFT, 0);
	y += 56;
	int li = cfg("label", 0), cols = 3, cw = (W - 80 - (cols - 1) * 16) / cols;
	for (int i = 0; i <= n_labels && i < ODAK_MAX_LABELS + 1; i++) {
		if (i == n_labels && n_labels >= ODAK_MAX_LABELS) break;
		KuiRect r = { 40 + (i % cols) * (cw + 16), y + (i / cols) * 92, cw, 80 };
		hb[n_hb] = (KuiButton){ r, i < n_labels ? T(labels[i]) : T("+ Yeni"), i < n_labels ? H_LABEL + i : H_ADD };
		kui_button(&hb[n_hb++], i == li);
	}
	y += ((ODAK_MAX_LABELS + cols) / cols) * 92 + 8;
	const char *ol[2] = { T("Sonraki aşama kendiliğinden başlasın"), T("Odakta ön ışığı kapat") };
	int ov[2] = { cfg("auto", 0), cfg("light_off", 0) };
	for (int k = 0; k < 2; k++) {
		KuiRect r = { 40, y + k * 86, W - 80, 76 };
		hb[n_hb++] = (KuiButton){ r, ol[k], k ? H_LIGHT : H_AUTO };
		kui_frame(r, 2, 0);
		kui_text((KuiRect){ r.x + 24, r.y, r.w - 140, r.h }, ol[k], KUI_FONT_REG, kui_fit_px(ol[k], KUI_FONT_REG, 36, r.w - 150), KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ r.x + r.w - 110, r.y, 100, r.h }, ov[k] ? "☑" : "☐", KUI_FONT_REG, 56, KUI_ALIGN_CENTER, 0);
	}
	y += 2 * 86 + 10;
	hb[n_hb] = (KuiButton){ { 40, y, W - 80, 130 }, T("Başla"), H_START };
	kui_fill(hb[n_hb].r, 0);
	kui_text_on(hb[n_hb].r, hb[n_hb].label, KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 255, 0);
	n_hb++;
	char s[160], d[48];
	fmt_dur(stats.today_min, d, sizeof d);
	snprintf(s, sizeof s, T("Bugün %s · %d oturum · Seri %d gün"), d, stats.today_n, stats.streak);
	kui_text((KuiRect){ 40, y + 140, W - 80, H - y - 150 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, W - 80), KUI_ALIGN_CENTER, 0);
}

static void redraw_home(void) { draw_home(); }

static void home(void)
{
	for (int full = 1;;) {
		draw_home();
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, W, H - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT || quit_requested) return;
		if (e.type == KUI_EV_WAKE) {
			full = 1;
			continue;
		}
		if (e.type != KUI_EV_TAP && e.type != KUI_EV_LONGPRESS) continue;
		int top = kui_topbar_hit(W, 1, e.x, e.y);
		if (top == KUI_BTN_EXIT) return;
		if (top == KUI_BTN_MENU) {
			stats_screen();
			if (quit_requested) return;
			full = 1;
			continue;
		}
		int id = kui_buttons_hit(hb, n_hb, e.x, e.y), cf = cfg("cfocus", 40), cb = cfg("cbreak", 10);
		if (e.type == KUI_EV_LONGPRESS) {
			if (id >= H_LABEL && id < H_LABEL + n_labels && n_labels > 1) {
				const char *ch[2] = { T("Sil"), T("Vazgeç") };
				if (kui_dialog(T("Etiket silinsin mi?"), T(labels[id - H_LABEL]), ch, 2, redraw_home) == 0) {
					int li = cfg("label", 0), k = id - H_LABEL;
					n_labels = odak_labels_remove(labels, n_labels, k);
					save_labels();
					if (li >= k && li > 0) cfg_set("label", li - 1);
				}
				full = 1; /* the dialog's box leaves ghosts */
			}
			continue;
		}
		if (id >= H_PRESET && id < H_PRESET + ODAK_N_PRESETS) cfg_set("preset", id - H_PRESET);
		else if (id == H_FOCUS_DN || id == H_FOCUS_UP) {
			cfg_set("cfocus", odak_step(cf, id == H_FOCUS_UP ? 5 : -5, ODAK_CUSTOM_MIN, ODAK_CUSTOM_MAX));
			cfg_set("preset", ODAK_PRESET_CUSTOM);
		} else if (id == H_BREAK_DN || id == H_BREAK_UP) {
			cfg_set("cbreak", odak_step(cb, id == H_BREAK_UP ? 1 : -1, 0, ODAK_BREAK_MAX));
			cfg_set("preset", ODAK_PRESET_CUSTOM);
		} else if (id >= H_LABEL && id < H_LABEL + n_labels) cfg_set("label", id - H_LABEL);
		else if (id == H_ADD) {
			char buf[ODAK_LABEL_MAX] = "";
			if (kui_input_text(T("Yeni etiket"), buf, sizeof buf, 0, 1, redraw_home)) {
				int n = odak_labels_add(labels, n_labels, ODAK_MAX_LABELS, buf);
				if (n > n_labels) {
					n_labels = n;
					save_labels();
					cfg_set("label", n - 1);
				}
			}
			if (kui_quit_pending()) return;
			full = 1;
		} else if (id == H_AUTO) cfg_set("auto", !cfg("auto", 0));
		else if (id == H_LIGHT) cfg_set("light_off", !cfg("light_off", 0));
		else if (id == H_START) {
			session();
			if (quit_requested) return;
			kui_drain_input(300);
			full = 1;
		}
	}
}

int main(void)
{
	if (kui_init("odak") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes only for new screens and phase changes */
	load_labels();
	load_log();
	home();
	kui_quit();
	return 0;
}

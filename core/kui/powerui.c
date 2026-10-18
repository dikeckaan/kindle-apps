/* "Pil ve ekran": battery level and estimate, the front light, and the battery saver's intervals (stored in the
 * calling app's settings file) — shared by the hub, Masa Panosu and Masa Saati. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include "kui.h"

static const int idle_ch[] = { 1, 2, 5, 10, 30, 60 }, clock_ch[] = { 1, 5, 15, 30, 60 }, data_ch[] = { 1, 2, 3, 6, 12, 24 },
		 wx_ch[] = { 1, 3, 6, 12, 24 }, fl_ch[] = { 0, 1 };
static const struct { const char *key; const int *ch; int n, def; } saver_keys[] = {
	[KUI_SAVER_IDLE_MIN] = { "idle_min", idle_ch, 6, 2 },	   [KUI_SAVER_CLOCK_MIN] = { "clock_min", clock_ch, 5, 1 },
	[KUI_SAVER_DATA_HOURS] = { "data_hours", data_ch, 6, 1 }, [KUI_SAVER_WX_HOURS] = { "wx_hours", wx_ch, 5, 6 },
	[KUI_SAVER_LIGHT_OFF] = { "light_off", fl_ch, 2, 1 },
};

int kui_saver_get(const char *cfg, KuiSaverKey k)
{
	int v = kui_cfg_get_int(cfg, saver_keys[k].key, saver_keys[k].def);
	for (int i = 0; i < saver_keys[k].n; i++)
		if (saver_keys[k].ch[i] == v) return v;
	return saver_keys[k].def;
}

static void saver_next(const char *cfg, KuiSaverKey k)
{
	int cur = kui_saver_get(cfg, k), i = 0;
	while (i < saver_keys[k].n && saver_keys[k].ch[i] != cur) i++;
	kui_cfg_set_int(cfg, saver_keys[k].key, saver_keys[k].ch[(i + 1) % saver_keys[k].n]);
}

int kui_light_get(void) { return kui_lipc_get_int("com.lab126.powerd", "flIntensity", -1); }
int kui_light_max(void)
{
	int m = kui_lipc_get_int("com.lab126.powerd", "flMaxIntensity", 24);
	return m > 0 && m <= 100 ? m : 24;
}
void kui_light_set(int v)
{
	int m = kui_light_max();
	kui_lipc_set_int("com.lab126.powerd", "flIntensity", v < 0 ? 0 : v > m ? m : v);
}

const char *kui_batt_log_path(void)
{
	static char p[400];
	if (!p[0]) { /* own buffer: callers may hold kui_data_dir()'s static result while drawing the battery */
		const char *root = getenv("KUI_ROOT");
		snprintf(p, sizeof p, "%s/data/battery", root && *root ? root : "/mnt/us/kindle-apps");
		mkdir(p, 0755);
		size_t n = strlen(p);
		snprintf(p + n, sizeof p - n, "/log");
	}
	return p;
}

/* the estimate line under the gauge */
static void estimate_text(char *s, int cap)
{
	int lvl = kui_lipc_get_int("com.lab126.powerd", "battLevel", -1);
	if (lvl < 0) {
		s[0] = '\0';
		return;
	}
	if (kui_battery_charging()) {
		snprintf(s, (size_t) cap, "%s", T("Şarj oluyor"));
		return;
	}
	long e = kui_batt_estimate_s(kui_batt_log_path(), (long) time(NULL), lvl);
	if (e < 0) {
		snprintf(s, (size_t) cap, "%s", T("Kalan süre: biraz daha kullanım sonra hesaplanır"));
		return;
	}
	char d[48];
	kui_fmt_duration(e, d, sizeof d);
	snprintf(s, (size_t) cap, T("Tahmini kalan: %s"), d);
}

enum { B_MINUS = 1, B_PLUS, B_OFF, B_HALF, B_FULL, B_ROW0 };

void kui_power_screen(const char *cfg, int flags)
{
	int w = kui_width(), h = kui_height();
	KuiRect r_light = { 40, KUI_TOPBAR_H + 300, w - 80, 230 };
	KuiRect r_rows = { 40, KUI_TOPBAR_H + 560, w - 80, h - KUI_TOPBAR_H - 600 };
	KuiSaverKey rows[5];
	int nrows = 0;
	if (cfg && (flags & KUI_PS_SAVER)) {
		rows[nrows++] = KUI_SAVER_IDLE_MIN;
		rows[nrows++] = KUI_SAVER_CLOCK_MIN;
		if (flags & KUI_PS_DATA) rows[nrows++] = KUI_SAVER_DATA_HOURS;
		rows[nrows++] = KUI_SAVER_WX_HOURS;
		rows[nrows++] = KUI_SAVER_LIGHT_OFF;
	}
	KuiButton b[5 + 5];
	kui_battery_refresh(); /* asked for: a fresh reading */
	for (int full = 1, part = 0;;) {
		if (full || part) {
			if (full) {
				kui_fill((KuiRect){ 0, 0, w, h }, 255);
				kui_topbar(T("Pil ve ekran"), NULL);
				kui_battery((KuiRect){ 60, KUI_TOPBAR_H + 40, 420, 130 });
				char s[160];
				estimate_text(s, sizeof s);
				kui_text((KuiRect){ 60, KUI_TOPBAR_H + 180, w - 120, 70 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - 120), KUI_ALIGN_LEFT, 0);
			}
			/* front light: − level + and three presets */
			kui_fill(r_light, 255);
			int lv = kui_light_get(), mx = kui_light_max();
			kui_text((KuiRect){ r_light.x, r_light.y, 300, 90 }, T("Ön ışık"), KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
			b[0] = (KuiButton){ { r_light.x + 300, r_light.y, 150, 90 }, "−", B_MINUS };
			b[1] = (KuiButton){ { r_light.x + r_light.w - 150, r_light.y, 150, 90 }, "+", B_PLUS };
			kui_button(&b[0], 0);
			kui_button(&b[1], 0);
			char v[24];
			snprintf(v, sizeof v, lv >= 0 ? "%d / %d" : "—", lv, mx);
			kui_text((KuiRect){ r_light.x + 450, r_light.y, r_light.w - 600, 90 }, v, KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0);
			int pw = (r_light.w - 40) / 3;
			b[2] = (KuiButton){ { r_light.x, r_light.y + 120, pw, 90 }, T("Kapat"), B_OFF };
			b[3] = (KuiButton){ { r_light.x + pw + 20, r_light.y + 120, pw, 90 }, T("Yarım"), B_HALF };
			b[4] = (KuiButton){ { r_light.x + 2 * (pw + 20), r_light.y + 120, pw, 90 }, T("Tam"), B_FULL };
			for (int i = 2; i < 5; i++) kui_button(&b[i], (i == 2 && lv == 0) || (i == 3 && lv == mx / 2) || (i == 4 && lv == mx));
			kui_mark_dirty(r_light);
			/* the saver's intervals: each row cycles through its choices */
			kui_fill(r_rows, 255);
			int rh = nrows ? (r_rows.h / nrows < 120 ? r_rows.h / nrows : 120) : 0;
			for (int i = 0; i < nrows; i++) {
				char s[128];
				int val = kui_saver_get(cfg, rows[i]);
				switch (rows[i]) {
				case KUI_SAVER_IDLE_MIN: snprintf(s, sizeof s, T("Uykuya geçiş: %d dk dokunulmazsa"), val); break;
				case KUI_SAVER_CLOCK_MIN:
					if (val == 1) snprintf(s, sizeof s, "%s", T("Uykuda saat: her dakika"));
					else snprintf(s, sizeof s, T("Uykuda saat: %d dakikada bir"), val);
					break;
				case KUI_SAVER_DATA_HOURS: snprintf(s, sizeof s, T("Haber ve döviz: %d saatte bir"), val); break;
				case KUI_SAVER_WX_HOURS: snprintf(s, sizeof s, T("Hava durumu: %d saatte bir"), val); break;
				default: snprintf(s, sizeof s, "%s", val ? T("Uykuda ön ışık: kapalı") : T("Uykuda ön ışık: açık")); break;
				}
				b[5 + i] = (KuiButton){ { r_rows.x, r_rows.y + i * rh, r_rows.w, rh - 18 }, s, B_ROW0 + i };
				kui_button(&b[5 + i], 0);
			}
			if (nrows) kui_mark_dirty(r_rows);
			kui_flush(full);
			full = part = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(b, 5 + nrows, e.x, e.y), lv = kui_light_get(), mx = kui_light_max();
		if (id == B_MINUS) kui_light_set(lv - 1);
		else if (id == B_PLUS) kui_light_set(lv + 1);
		else if (id == B_OFF) kui_light_set(0);
		else if (id == B_HALF) kui_light_set(mx / 2);
		else if (id == B_FULL) kui_light_set(mx);
		else if (id >= B_ROW0) saver_next(cfg, rows[id - B_ROW0]);
		else continue;
		part = 1; /* only the light and the rows change: no flash */
	}
}


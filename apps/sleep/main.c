/* Uyku Ayarı: choose how long the Kindle waits before the screensaver (or never sleep), via powerd. */
#include <stdio.h>
#include <stdlib.h>
#include "kui.h"
#include "sleep_opts.h"

#define POWERD "com.lab126.powerd"

static const SleepOpt *opts;
static int n_opts, timeout_s, prevent;
static KuiButton buttons[8];

static int get_int(const char *prop, int def)
{
	char cmd[96], buf[32];
	snprintf(cmd, sizeof cmd, "lipc-get-prop " POWERD " %s 2>/dev/null", prop);
	FILE *p = popen(cmd, "r");
	if (!p) return def;
	int v = def;
	if (fgets(buf, sizeof buf, p)) v = atoi(buf);
	pclose(p);
	return v;
}

static void set_int(const char *prop, int v)
{
	char cmd[96];
	snprintf(cmd, sizeof cmd, "lipc-set-prop " POWERD " %s %d >/dev/null 2>&1", prop, v);
	if (system(cmd) != 0) fprintf(stderr, "failed: %s\n", cmd);
}

static void read_state(void)
{
	timeout_s = get_int("screenSaverTimeout", 600);
	prevent = get_int("preventScreenSaver", 0);
}

static void draw(int full)
{
	int w = kui_width(), h = kui_height();
	char s[128];
	if (full) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Uyku Ayarı"), NULL);
	}
	KuiRect now = { 40, KUI_TOPBAR_H + 40, w - 80, 120 };
	kui_fill(now, 255);
	sleep_describe(timeout_s, prevent, s, sizeof s);
	kui_text(now, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 60, now.w), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(now);
	int cur = sleep_current_index(timeout_s, prevent), bw = (w - 3 * 40) / 2;
	for (int i = 0; i < n_opts; i++) {
		buttons[i] = (KuiButton){ { 40 + (i % 2) * (bw + 40), KUI_TOPBAR_H + 220 + (i / 2) * 200, bw, 160 }, T(opts[i].label), i + 1 };
		kui_button(&buttons[i], i == cur);
	}
	if (full) {
		KuiRect note = { 40, KUI_TOPBAR_H + 860, w - 80, 260 };
		kui_text(note, T("Kindle bu süre dokunulmadan kalınca uyur.\n\"Hiç uyuma\" pili hızlı tüketir.\nYeniden başlatınca varsayılana dönebilir."),
			 KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
		kui_flush(1);
	} else {
		kui_flush(0);
	}
}

static void choose(int i)
{
	if (opts[i].seconds == 0) {
		set_int("preventScreenSaver", 1);
	} else {
		set_int("preventScreenSaver", 0);
		set_int("screenSaverTimeout", opts[i].seconds);
	}
	read_state();
	draw(0);
}

int main(void)
{
	if (kui_init("sleep") != 0) return 1;
	sleep_set_text_hook(kui_t);
	n_opts = sleep_opts(&opts);
	read_state();
	draw(1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { read_state(); draw(1); continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) break;
		int id = kui_buttons_hit(buttons, n_opts, e.x, e.y);
		if (id) choose(id - 1);
	}
	kui_quit();
	return 0;
}

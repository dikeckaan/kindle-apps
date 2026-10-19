/* Hub guard: which background programs count is decided by kui_proc_scan (FOREIGN with gui set: DISPLAY in the
 * environment = an X client, /dev/fb0 or /dev/input open, or a known screen program); daemons without a screen are
 * left to Görev Yöneticisi. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "guard.h"
#include "kui.h"

#define MAX_DISMISSED 32
static KuiProcId dismissed[MAX_DISMISSED];
static int n_dismissed;

static int is_dismissed(KuiProcId id)
{
	for (int i = 0; i < n_dismissed; i++)
		if (dismissed[i].pid == id.pid && dismissed[i].start == id.start) return 1;
	return 0;
}

static void notice(const char *text)
{
	int w = kui_width(), h = kui_height();
	KuiRect box = { 60, h / 2 - 120, w - 120, 240 };
	kui_fill(box, 255);
	kui_frame(box, 6, 0);
	kui_text((KuiRect){ box.x + 30, box.y, box.w - 60, box.h }, text, KUI_FONT_BOLD, kui_fit_px(text, KUI_FONT_BOLD, 48, box.w - 60),
		 KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(box);
	kui_flush(0);
}

int hub_guard(void (*redraw)(void))
{
	static KuiProc ps[KUI_PROC_MAX];
	int n = kui_proc_scan(ps, KUI_PROC_MAX, (int) getpid());
	if (n == 0) return 0;
	KuiProcGroup g[8], show[8];
	int k = kui_proc_groups(ps, n, KUI_PROC_FOREIGN, g, 8), m = 0;
	for (int i = 0; i < k; i++) {
		if (!g[i].gui) continue;
		int fresh = 0;
		for (int j = 0; j < g[i].n && j < 16; j++) fresh |= !is_dismissed(g[i].ids[j]);
		if (fresh) show[m++] = g[i];
	}
	if (!m) return 0;
	char names[160] = "";
	for (int i = 0; i < m; i++) {
		if (i) strncat(names, ", ", sizeof names - strlen(names) - 1);
		strncat(names, show[i].name, sizeof names - strlen(names) - 1);
	}
	char msg[320];
	if (m > 1) snprintf(msg, sizeof msg, T("Arkada açık programlar: %s. Ekranı ve dokunmayı kullanabilirler."), names);
	else snprintf(msg, sizeof msg, T("Arkada açık program: %s. Ekranı ve dokunmayı kullanabilir."), names);
	const char *ch[] = { T("Kapat"), T("Devam") };
	fprintf(stderr, "guard: background screen program(s): %s\n", names);
	int c = kui_dialog(T("Arka planda program var"), msg, ch, 2, redraw);
	if (c != 0) { /* Devam (or ✕): do not ask again for these processes */
		for (int i = 0; i < m; i++)
			for (int j = 0; j < show[i].n && j < 16 && n_dismissed < MAX_DISMISSED; j++)
				if (!is_dismissed(show[i].ids[j])) dismissed[n_dismissed++] = show[i].ids[j];
		return 1;
	}
	char busy[200];
	snprintf(busy, sizeof busy, T("Kapatılıyor: %s…"), names);
	notice(busy);
	KuiProcId ids[8 * 16];
	int ni = 0;
	for (int i = 0; i < m; i++)
		for (int j = 0; j < show[i].n && j < 16; j++) ids[ni++] = show[i].ids[j];
	int left = kui_proc_stop(ids, ni);
	kui_proc_after_stop();
	fprintf(stderr, "guard: stopped %s (%d left)\n", names, left);
	/* an X client that goes away makes the Kindle UI repaint where its window was: let it settle, then redraw */
	struct timespec ts = { 1, 0 };
	nanosleep(&ts, NULL);
	kui_drain_input(200);
	if (left) {
		if (redraw) redraw();
		snprintf(msg, sizeof msg, T("%s kapatılamadı."), names);
		const char *ok[] = { T("Tamam") };
		kui_dialog(T("Arka planda program var"), msg, ok, 1, redraw);
	}
	return 1;
}

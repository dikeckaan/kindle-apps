/* Görev Yöneticisi: third-party programs left running in the background (KUAL games, KOReader, kterm …) with their
 * memory and running time; close one or all of them. System services, SSH servers (and their sessions), kpm and
 * Kindle Apps are never stopped (the rules are in core/kui/procscan.c); they are listed under "Korunanlar". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kui.h"

#define MAX_GROUPS 24
#define LIST_Y (KUI_TOPBAR_H + 170)
#define ROW_H 150
#define PER_PAGE 4

static KuiProc ps[KUI_PROC_MAX];
static KuiProcGroup fg[MAX_GROUPS], pg[MAX_GROUPS];
static int n_fg, n_pg, page, show_protected;
static long mem_before = -1, mem_now = -1;
static char status_line[200];
static KuiButton row_btn[PER_PAGE], nav[2], stop_all, toggle;
static int n_row_btn;

static int by_rss(const void *a, const void *b)
{
	const KuiProcGroup *x = a, *y = b;
	return (y->rss_kb > x->rss_kb) - (y->rss_kb < x->rss_kb);
}

static void gather(void)
{
	int n = kui_proc_scan(ps, KUI_PROC_MAX, (int) getpid());
	n_fg = kui_proc_groups(ps, n, KUI_PROC_FOREIGN, fg, MAX_GROUPS);
	n_pg = kui_proc_groups(ps, n, KUI_PROC_PROTECTED, pg, MAX_GROUPS);
	qsort(fg, (size_t) n_fg, sizeof *fg, by_rss);
	mem_now = kui_mem_available_kb();
}

static int count(void) { return show_protected ? n_pg : n_fg; }
static int pages(void) { return count() ? (count() + PER_PAGE - 1) / PER_PAGE : 1; }

static void fmt_age(long s, char *out, int cap)
{
	if (s < 0) snprintf(out, (size_t) cap, "—");
	else if (s >= 86400) snprintf(out, (size_t) cap, T("%ld gün %ld sa"), s / 86400, s % 86400 / 3600);
	else if (s >= 3600) snprintf(out, (size_t) cap, T("%ld sa %ld dk"), s / 3600, s % 3600 / 60);
	else snprintf(out, (size_t) cap, T("%ld dk"), s / 60);
}

static int mb(long kb) { return kb <= 0 ? 0 : (int) ((kb + 512) / 1024); }

static void detail(const KuiProcGroup *g, char *out, int cap)
{
	char age[64];
	fmt_age(g->age_s, age, sizeof age);
	if (g->rss_kb > 0) snprintf(out, (size_t) cap, T("%d MB · %s açık"), mb(g->rss_kb) ? mb(g->rss_kb) : 1, age);
	else snprintf(out, (size_t) cap, T("%s açık"), age);
}

static const char *why_text(KuiProcWhy w)
{
	switch (w) {
	case KUI_WHY_SSH: return T("SSH sunucusu");
	case KUI_WHY_KPM: return T("Paket yöneticisi (kpm)");
	case KUI_WHY_JAILBREAK: return T("Jailbreak aracı");
	default: return T("korunuyor");
	}
}

static void draw_mem(void)
{
	int w = kui_width();
	KuiRect r = { 40, KUI_TOPBAR_H + 10, w - 80, 70 };
	kui_fill(r, 255);
	char s[200];
	if (status_line[0]) snprintf(s, sizeof s, "%s", status_line);
	else if (mem_now < 0) snprintf(s, sizeof s, "%s", T("Boş bellek: bilinmiyor"));
	else if (mem_before >= 0) snprintf(s, sizeof s, T("Boş bellek: %d MB → %d MB"), mb(mem_before), mb(mem_now));
	else snprintf(s, sizeof s, T("Boş bellek: %d MB"), mb(mem_now));
	kui_text(r, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 40, r.w), KUI_ALIGN_LEFT, 0);
	kui_mark_dirty(r);
}

static void draw_body(void)
{
	int w = kui_width(), h = kui_height();
	KuiRect body = { 0, KUI_TOPBAR_H + 80, w, h - KUI_TOPBAR_H - 80 };
	kui_fill(body, 255);
	const char *head = show_protected ? T("Korunanlar: hiçbir zaman durdurulmaz") : T("Arka planda çalışan programlar");
	kui_text((KuiRect){ 40, KUI_TOPBAR_H + 90, w - 80, 70 }, head, KUI_FONT_BOLD, kui_fit_px(head, KUI_FONT_BOLD, 42, w - 80), KUI_ALIGN_LEFT, 0);
	kui_fill((KuiRect){ 40, LIST_Y - 6, w - 80, 3 }, 0);
	n_row_btn = 0;
	if (page >= pages()) page = pages() - 1;
	const KuiProcGroup *list = show_protected ? pg : fg;
	if (!count()) {
		const char *e = show_protected ? T("Korunan üçüncü taraf program yok.") : T("Arka planda başka program yok.");
		kui_text((KuiRect){ 40, LIST_Y, w - 80, PER_PAGE * ROW_H }, e, KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
	}
	for (int i = 0; i < PER_PAGE; i++) {
		int k = page * PER_PAGE + i;
		if (k >= count()) break;
		const KuiProcGroup *g = &list[k];
		int y = LIST_Y + i * ROW_H, tw = w - 80 - 300;
		char d[160];
		detail(g, d, sizeof d);
		if (show_protected) {
			char t[400];
			snprintf(t, sizeof t, "%s · %s", why_text(g->why), d);
			snprintf(d, sizeof d, "%s", t);
		}
		kui_text((KuiRect){ 40, y + 14, tw, 66 }, g->name, KUI_FONT_BOLD, kui_fit_px(g->name, KUI_FONT_BOLD, 46, tw), KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ 40, y + 80, tw, 52 }, d, KUI_FONT_REG, kui_fit_px(d, KUI_FONT_REG, 34, tw), KUI_ALIGN_LEFT, 0);
		KuiRect br = { w - 300, y + 20, 260, ROW_H - 40 };
		if (show_protected) {
			kui_frame(br, 2, 0xAA);
			kui_text(br, T("korunuyor"), KUI_FONT_REG, kui_fit_px(T("korunuyor"), KUI_FONT_REG, 38, br.w - 20), KUI_ALIGN_CENTER, 0x55);
		} else {
			row_btn[n_row_btn] = (KuiButton){ br, T("Kapat"), 100 + k };
			kui_button(&row_btn[n_row_btn++], 0);
		}
		kui_fill((KuiRect){ 40, y + ROW_H - 2, w - 80, 1 }, 0xAA);
	}
	int y = LIST_Y + PER_PAGE * ROW_H + 10;
	if (pages() > 1) {
		nav[0] = (KuiButton){ { 40, y, 220, 100 }, "◀", 1 };
		nav[1] = (KuiButton){ { w - 260, y, 220, 100 }, "▶", 2 };
		kui_button(&nav[0], 0);
		kui_button(&nav[1], 0);
		char s[32];
		snprintf(s, sizeof s, "%d / %d", page + 1, pages());
		kui_text((KuiRect){ 260, y, w - 520, 100 }, s, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
	} else {
		nav[0].r.w = nav[1].r.w = 0;
	}
	y += 120;
	if (!show_protected && n_fg > 0) {
		stop_all = (KuiButton){ { 40, y, w - 80, 150 }, T("Hepsini durdur"), 10 };
		kui_button(&stop_all, 0);
	} else {
		stop_all.r.w = 0;
	}
	char tl[96];
	if (show_protected) snprintf(tl, sizeof tl, "%s", T("◀ Programlara dön"));
	else snprintf(tl, sizeof tl, T("Korunanları göster (%d)"), n_pg);
	toggle = (KuiButton){ { 40, y + 170, w - 80, 100 }, tl, 11 };
	kui_button(&toggle, 0);
	const char *note = T("Sistem servisleri, SSH ve Kindle Apps hiçbir zaman durdurulmaz.");
	kui_text((KuiRect){ 40, h - 80, w - 80, 70 }, note, KUI_FONT_REG, kui_fit_px(note, KUI_FONT_REG, 32, w - 80), KUI_ALIGN_CENTER, 0x55);
	kui_mark_dirty(body);
}

static void draw(int full)
{
	int w = kui_width(), h = kui_height();
	if (full) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Görev Yöneticisi"), T("Yenile"));
	}
	draw_mem();
	draw_body();
	kui_flush(full);
}

static void redraw_full(void) { draw(1); }

/* the status line alone, at once (the stop may take a few seconds) */
static void say(const char *s)
{
	snprintf(status_line, sizeof status_line, "%s", s);
	draw_mem();
	kui_flush(0);
}

static void stop_groups(const KuiProcGroup *gs, int n)
{
	if (!n) return;
	char names[200] = "", s[260];
	KuiProcId ids[MAX_GROUPS * 16];
	int ni = 0;
	for (int i = 0; i < n; i++) {
		if (i) strncat(names, ", ", sizeof names - strlen(names) - 1);
		strncat(names, gs[i].name, sizeof names - strlen(names) - 1);
		for (int j = 0; j < gs[i].n && j < 16; j++) ids[ni++] = gs[i].ids[j];
	}
	mem_before = kui_mem_available_kb();
	snprintf(s, sizeof s, T("Kapatılıyor: %s…"), names);
	say(s);
	int left = kui_proc_stop(ids, ni);
	kui_proc_after_stop();
	fprintf(stderr, "gorev: stopped %s (%d left)\n", names, left);
	/* the Kindle UI may repaint where an X window was: let it settle, then draw everything again (no flash) */
	struct timespec ts = { 1, 0 };
	nanosleep(&ts, NULL);
	kui_drain_input(200);
	status_line[0] = '\0';
	gather();
	kui_topbar(T("Görev Yöneticisi"), T("Yenile"));
	draw(0);
	if (left) {
		snprintf(s, sizeof s, T("%s kapatılamadı."), names);
		const char *ok[] = { T("Tamam") };
		kui_dialog(T("Görev Yöneticisi"), s, ok, 1, redraw_full);
		kui_topbar(T("Görev Yöneticisi"), T("Yenile"));
		draw(0);
	}
}

static void confirm_stop_all(void)
{
	char names[240] = "", msg[400];
	for (int i = 0; i < n_fg; i++) {
		if (i) strncat(names, ", ", sizeof names - strlen(names) - 1);
		strncat(names, fg[i].name, sizeof names - strlen(names) - 1);
	}
	snprintf(msg, sizeof msg, T("Durdurulacak: %s.\nSistem servisleri, SSH ve Kindle Apps açık kalır."), names);
	const char *ch[] = { T("Durdur"), T("Vazgeç") };
	int c = kui_dialog(T("Hepsini durdur"), msg, ch, 2, redraw_full);
	if (kui_quit_pending()) return;
	if (c == 0) {
		KuiProcGroup all[MAX_GROUPS];
		int n = n_fg;
		memcpy(all, fg, sizeof *fg * (size_t) n);
		stop_groups(all, n);
	} else {
		kui_topbar(T("Görev Yöneticisi"), T("Yenile"));
		draw(0); /* the dialog's area */
	}
}

int main(void)
{
	if (kui_init("gorev") != 0) return 1;
	gather();
	draw(1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) {
			gather();
			draw(1);
			continue;
		}
		if (e.type == KUI_EV_SWIPE && pages() > 1 && (e.dir == KUI_DIR_LEFT || e.dir == KUI_DIR_RIGHT)) {
			page = (page + (e.dir == KUI_DIR_LEFT ? 1 : -1) + pages()) % pages();
			draw(0);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) { /* Yenile */
			mem_before = -1;
			gather();
			draw(0);
			continue;
		}
		if (pages() > 1) {
			int id = kui_buttons_hit(nav, 2, e.x, e.y);
			if (id) {
				page = (page + (id == 1 ? -1 : 1) + pages()) % pages();
				draw(0);
				continue;
			}
		}
		if (kui_buttons_hit(&toggle, 1, e.x, e.y)) {
			show_protected = !show_protected;
			page = 0;
			gather();
			draw(0);
			continue;
		}
		if (stop_all.r.w && kui_buttons_hit(&stop_all, 1, e.x, e.y)) {
			kui_button(&stop_all, 1);
			kui_flush(0);
			confirm_stop_all();
			continue;
		}
		int id = kui_buttons_hit(row_btn, n_row_btn, e.x, e.y);
		if (id >= 100 && id - 100 < n_fg) {
			for (int i = 0; i < n_row_btn; i++)
				if (row_btn[i].id == id) kui_button(&row_btn[i], 1);
			kui_flush(0);
			KuiProcGroup g = fg[id - 100];
			stop_groups(&g, 1);
		}
	}
	kui_quit();
	return 0;
}

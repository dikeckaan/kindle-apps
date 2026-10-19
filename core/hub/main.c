/* "Uygulamalar": the single entry in the Kindle library. Shows installed apps in a grid and runs the
 * chosen one as a child process while the Kindle UI stays paused. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "appreg.h"
#include "guard.h"
#include "kui.h"
#include "upd_ui.h"

/* 3×3 tiles; Basit mod: 2×2 bigger ones */
#define COLS (kui_simple() ? 2 : 3)
#define ROWS (kui_simple() ? 2 : 3)
#define PER_PAGE (COLS * ROWS)
#define MAX_APPS 32

static HubApp apps[MAX_APPS];
static int n_apps, page;
static KuiRect grid;
static KuiButton nav[2];

static const char *root(void)
{
	const char *r = getenv("KUI_ROOT");
	return r && *r ? r : "/mnt/us/kindle-apps";
}

/* installed apps plus the built-in "Dikeç Store" tile at the end (exec empty) */
static void scan(void)
{
	n_apps = hub_scan_apps(root(), apps, MAX_APPS - 1);
	hub_apply_order(root(), apps, n_apps); /* the user's order from Dikeç Store → Sırala */
	if (kui_simple()) { /* Basit mod: the technical apps (and the store) stay out of the way */
		static const char *const hidden[] = { "ssh", "firewall", "sysinfo", "sleep", "upload", "gorev" };
		int k = 0;
		for (int i = 0; i < n_apps; i++) {
			int hide = 0;
			for (size_t j = 0; j < sizeof hidden / sizeof *hidden; j++) hide |= !strcmp(apps[i].id, hidden[j]);
			if (!hide) apps[k++] = apps[i];
		}
		n_apps = k;
	}
	if (!kui_simple()) apps[n_apps++] = (HubApp){ "store", N_("Dikeç Store"), "", 1 << 30 };
}

/* 2 connected, 1 on but not connected, 0 off */
static int wifi_state(void)
{
	char st[32];
	if (kui_wifi_enabled() == 0) return 0;
	return kui_lipc_get_str("com.lab126.wifid", "cmState", st, sizeof st) == 0 && !strcmp(st, "CONNECTED") ? 2 : 1;
}

static int shown_wifi = -1;

/* "Wi-Fi ✓ / ✗ / –" beside the exit button; redrawn (no flash) only when it changes */
static void draw_wifi(int force)
{
	int s = wifi_state();
	if (s == shown_wifi && !force) return;
	shown_wifi = s;
	KuiRect r = { 125, 20, 170, KUI_TOPBAR_H - 40 };
	kui_fill(r, 255);
	kui_text(r, s == 2 ? "Wi-Fi ✓" : s == 1 ? "Wi-Fi …" : "Wi-Fi ✗", KUI_FONT_REG, 30, KUI_ALIGN_LEFT, s == 2 ? 0 : 90);
	kui_mark_dirty(r);
}

static int pages(void) { return n_apps ? (n_apps + PER_PAGE - 1) / PER_PAGE : 1; }

static KuiRect tile(int i) { return kui_grid_cell(grid, COLS, ROWS, i % COLS, i / COLS, 30); }

/* app names wrap onto two lines inside a tile; shrink only when the longest word does not fit */
static int fit_name(const char *name, int w)
{
	int px = 52;
	char word[64];
	for (const char *p = name; *p;) {
		while (*p == ' ') p++;
		int n = (int) strcspn(p, " ");
		if (!n) break;
		snprintf(word, sizeof word, "%.*s", n, p);
		px = kui_fit_px(word, KUI_FONT_BOLD, px, w);
		p += n;
	}
	return px;
}

static int shown_batt = -2;

/* the battery level left of "Dil"; redrawn only when it changes */
static void draw_battery(int force)
{
	int v = kui_battery_level() * 2 + kui_battery_charging(); /* cached: read at most every 5 min (main) */
	if (v == shown_batt && !force) return;                  /* unchanged: no redraw, no refresh */
	shown_batt = v;
	kui_battery((KuiRect){ kui_width() - 330, 28, 180, 64 });
}

/* tiles and page controls only: turning a page redraws just this, with no full-screen flash */
static void draw_body(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H }, 255);
	grid = (KuiRect){ 20, KUI_TOPBAR_H + 20, w - 40, h - KUI_TOPBAR_H - 200 };
	for (int i = 0; i < PER_PAGE; i++) {
		int k = page * PER_PAGE + i;
		if (k >= n_apps) break;
		KuiRect r = tile(i);
		kui_frame(r, 4, 0);
		const char *name = T(apps[k].name);
		kui_text((KuiRect){ r.x + 10, r.y, r.w - 20, r.h }, name, KUI_FONT_BOLD, fit_name(name, r.w - 20), KUI_ALIGN_CENTER, 0);
	}
	if (pages() > 1) {
		int y = h - 150;
		nav[0] = (KuiButton){ { 40, y, 260, 120 }, kui_simple() ? T("Önceki") : "◀", 1 };
		nav[1] = (KuiButton){ { w - 300, y, 260, 120 }, kui_simple() ? T("Sonraki") : "▶", 2 };
		kui_button(&nav[0], 0);
		kui_button(&nav[1], 0);
		char s[32];
		snprintf(s, sizeof s, "%d / %d", page + 1, pages());
		kui_text((KuiRect){ 300, y, w - 600, 120 }, s, KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
}

static void draw(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Uygulamalar"), T("Ayarlar"));
	draw_body();
	draw_wifi(1);
	draw_battery(1);
	kui_flush(1);
}

/* pages wrap around: past the last page comes the first, and back */
static void turn_page(int d)
{
	if (!d || pages() < 2) return;
	page = (page + d + pages()) % pages();
	draw_body();
	kui_flush(0);
}

/* the 20 languages as a two-column list; the current one is shown pressed */
static void choose_language(void)
{
	int n, w = kui_width();
	const KuiLang *l = kui_languages(&n);
	KuiButton b[32];
	int bw = (w - 3 * 40) / 2, bh = 100, rows = (n + 1) / 2;
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
			kui_topbar(T("Dil"), NULL);
			for (int i = 0; i < n; i++) {
				b[i] = (KuiButton){ { 40 + (i / rows) * (bw + 40), KUI_TOPBAR_H + 30 + (i % rows) * (bh + 18), bw, bh }, l[i].name, i + 1 };
				kui_button(&b[i], !strcmp(l[i].code, kui_lang()));
			}
			kui_flush(1);
			full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(b, n, e.x, e.y);
		if (id) {
			kui_i18n_save(l[id - 1].code);
			kui_i18n_set(l[id - 1].code);
			return;
		}
	}
}

/* Ayarlar: language, Kolay okuma, Basit mod — big buttons, also reachable in Basit mod (the way back) */
static void settings(void)
{
	int w = kui_width(), h = kui_height();
	for (int full = 1;;) {
		int n;
		const KuiLang *l = kui_languages(&n);
		const char *lang = kui_lang();
		for (int i = 0; i < n; i++)
			if (!strcmp(l[i].code, lang)) lang = l[i].name;
		char s0[96], s1[96], s2[96];
		snprintf(s0, sizeof s0, T("Dil: %s"), lang);
		snprintf(s1, sizeof s1, "%s", kui_easy() ? T("Kolay okuma: Açık") : T("Kolay okuma: Kapalı"));
		snprintf(s2, sizeof s2, "%s", kui_simple() ? T("Basit mod: Açık") : T("Basit mod: Kapalı"));
		KuiButton b[3] = { { { 40, KUI_TOPBAR_H + 60, w - 80, 170 }, s0, 1 },
				   { { 40, KUI_TOPBAR_H + 290, w - 80, 170 }, s1, 2 },
				   { { 40, KUI_TOPBAR_H + 520, w - 80, 170 }, s2, 3 } };
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Ayarlar"), NULL);
		kui_button(&b[0], 0);
		kui_button(&b[1], kui_easy());
		kui_button(&b[2], kui_simple());
		kui_text((KuiRect){ 60, KUI_TOPBAR_H + 740, w - 120, 360 },
			 T("Kolay okuma: bütün yazılar büyük, kalın ve siyah olur.\nBasit mod: menüde dört büyük düğme; panoda yalnızca saat, hava ve üç "
			   "haber. Teknik uygulamalar gizlenir; bu ekrandan her zaman geri açılabilir."),
			 KUI_FONT_REG, 34, KUI_ALIGN_LEFT, 0);
		kui_flush(1); /* the whole look may change: a full refresh */
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(b, 3, e.x, e.y);
		if (id == 1) choose_language();
		else if (id == 2) kui_ui_set("easy", !kui_easy());
		else if (id == 3) kui_ui_set("simple", !kui_simple());
		(void) full;
	}
}

/* Runs an app with the screen and input handed over to it; returns its wait status. */
static int run_app(const HubApp *a)
{
	kui_quit();
	pid_t pid = fork();
	if (pid == 0) {
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		char log[320]; /* each app logs to logs/<id>.log, not into the hub's log */
		snprintf(log, sizeof log, "%s/logs/%s.log", root(), a->id);
		int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
		}
		execl(a->exec, a->exec, (char *) NULL);
		_exit(127);
	}
	int status = 0;
	if (pid > 0) {
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) kill(pid, SIGTERM);
	}
	if (pid > 0 && status != 0) /* in hub.log: which app, its exit code or signal */
		fprintf(stderr, "app %s: %s %d\n", a->id, WIFSIGNALED(status) ? "killed by signal" : "exit code",
			WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
	kui_prevent_sleep(0); /* an app that kept the Kindle awake (Masa Saati) may have crashed */
	if (kui_init("hub") != 0) exit(1);
	kui_drain_input(600); /* the end of the tap that closed the app must not hit the menu (its ✕ is in the same corner) */
	return pid > 0 ? status : -1;
}

int main(void)
{
	if (kui_init("hub") != 0) return 1;
	kui_battery_max_age(5 * 60);
	scan();
	draw();
	if (hub_guard(draw)) draw(); /* a background program (KUAL game, KOReader) would draw over us */
	for (;;) {
		/* every 30 s while on screen: Wi-Fi (and the battery, read every 5 min); nothing behind the screensaver */
		KuiEvent e = kui_next_event(kui_input_asleep() ? -1 : 30000);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_NONE) {
			if (!kui_input_asleep()) {
				draw_wifi(0);
				draw_battery(0);
				kui_flush(0);
			}
			continue;
		}
		if (e.type == KUI_EV_WAKE) { draw(); continue; }
		if (e.type == KUI_EV_SWIPE && pages() > 1) {
			turn_page(e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_rect_contains((KuiRect){ kui_width() - 340, 0, 200, KUI_TOPBAR_H }, e.x, e.y)) { /* battery: estimate, front light */
			kui_power_screen(NULL, 0);
			if (kui_quit_pending()) break;
			draw();
			continue;
		}
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			settings();
			scan(); /* Basit mod shows fewer apps */
			page = 0;
			draw();
			continue;
		}
		if (pages() > 1) {
			int id = kui_buttons_hit(nav, 2, e.x, e.y);
			if (id == 1 || id == 2) { turn_page(id == 1 ? -1 : 1); continue; }
		}
		for (int i = 0; i < PER_PAGE; i++) {
			int k = page * PER_PAGE + i;
			if (k >= n_apps || !kui_rect_contains(tile(i), e.x, e.y)) continue;
			KuiRect r = tile(i); /* tap feedback before the app takes over the screen */
			kui_fill(r, 0);
			const char *name = T(apps[k].name);
			kui_text_on((KuiRect){ r.x + 10, r.y, r.w - 20, r.h }, name, KUI_FONT_BOLD, fit_name(name, r.w - 20), KUI_ALIGN_CENTER, 255, 0);
			kui_mark_dirty(r);
			kui_flush(0);
			if (!apps[k].exec[0]) { /* Dikeç Store */
				if (hub_updates(root())) { /* the hub itself was replaced: start the new one */
					char self[320];
					snprintf(self, sizeof self, "%s/bin/hub", root());
					kui_quit();
					/* run the new hub from a RAM copy (as kui-run.sh does): /mnt/us (fuse) does not keep a
					 * replaced file alive for the process still running it */
					execl("/bin/sh", "sh", "-c",
					      "cp -f \"$1\" /tmp/kui-hub.new && chmod +x /tmp/kui-hub.new && mv -f /tmp/kui-hub.new /tmp/kui-hub && "
					      "exec /tmp/kui-hub; exec \"$1\"", "sh", self, (char *) NULL);
					return 1; /* kui-run.sh restores the Kindle UI */
				}
				scan();
				if (page >= pages()) page = pages() - 1;
				draw();
				break;
			}
			int st = run_app(&apps[k]);
			scan();
			if (page >= pages()) page = pages() - 1;
			draw();
			if (hub_guard(draw)) draw();
			if (st != 0 && !(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM)) {
				char msg[96];
				snprintf(msg, sizeof msg, T("%s beklenmedik şekilde kapandı"), T(apps[k].name));
				const char *ok[] = { T("Tamam") };
				kui_dialog(T("Hata"), msg, ok, 1, draw);
				draw();
			}
			break;
		}
	}
	kui_quit();
	return 0;
}

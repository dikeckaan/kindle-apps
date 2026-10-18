/* "Uygulamalar": the single entry in the Kindle library. Shows installed apps in a grid and runs the
 * chosen one as a child process while the Kindle UI stays paused. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "appreg.h"
#include "kui.h"

#define COLS 3
#define ROWS 3
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

static int pages(void) { return n_apps ? (n_apps + PER_PAGE - 1) / PER_PAGE : 1; }

static KuiRect tile(int i) { return kui_grid_cell(grid, COLS, ROWS, i % COLS, i / COLS, 30); }

static void draw(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar("Uygulamalar", NULL);
	grid = (KuiRect){ 20, KUI_TOPBAR_H + 20, w - 40, h - KUI_TOPBAR_H - 200 };
	if (!n_apps) {
		kui_text((KuiRect){ 40, h / 2 - 100, w - 80, 200 }, "Henüz uygulama kurulu değil", KUI_FONT_REG, 48,
			 KUI_ALIGN_CENTER, 0);
	}
	for (int i = 0; i < PER_PAGE; i++) {
		int k = page * PER_PAGE + i;
		if (k >= n_apps) break;
		KuiRect r = tile(i);
		kui_frame(r, 4, 0);
		kui_text((KuiRect){ r.x + 10, r.y, r.w - 20, r.h }, apps[k].name, KUI_FONT_BOLD, 52, KUI_ALIGN_CENTER, 0);
	}
	if (pages() > 1) {
		int y = h - 150;
		nav[0] = (KuiButton){ { 40, y, 260, 120 }, "◀", 1 };
		nav[1] = (KuiButton){ { w - 300, y, 260, 120 }, "▶", 2 };
		kui_button(&nav[0], 0);
		kui_button(&nav[1], 0);
		char s[32];
		snprintf(s, sizeof s, "%d / %d", page + 1, pages());
		kui_text((KuiRect){ 300, y, w - 600, 120 }, s, KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
	}
	kui_flush(1);
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
	if (kui_init("hub") != 0) exit(1);
	return pid > 0 ? status : -1;
}

int main(void)
{
	if (kui_init("hub") != 0) return 1;
	n_apps = hub_scan_apps(root(), apps, MAX_APPS);
	draw();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw(); continue; }
		if (e.type == KUI_EV_SWIPE && pages() > 1) {
			if (e.dir == KUI_DIR_LEFT && page + 1 < pages()) { page++; draw(); }
			if (e.dir == KUI_DIR_RIGHT && page > 0) { page--; draw(); }
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) break;
		if (pages() > 1) {
			int id = kui_buttons_hit(nav, 2, e.x, e.y);
			if (id == 1 && page > 0) { page--; draw(); continue; }
			if (id == 2 && page + 1 < pages()) { page++; draw(); continue; }
		}
		for (int i = 0; i < PER_PAGE; i++) {
			int k = page * PER_PAGE + i;
			if (k >= n_apps || !kui_rect_contains(tile(i), e.x, e.y)) continue;
			KuiRect r = tile(i); /* tap feedback before the app takes over the screen */
			kui_fill(r, 0);
			kui_text((KuiRect){ r.x + 10, r.y, r.w - 20, r.h }, apps[k].name, KUI_FONT_BOLD, 52, KUI_ALIGN_CENTER, 255);
			kui_mark_dirty(r);
			kui_flush(0);
			int st = run_app(&apps[k]);
			n_apps = hub_scan_apps(root(), apps, MAX_APPS);
			if (page >= pages()) page = pages() - 1;
			draw();
			if (st != 0 && !(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM)) {
				char msg[96];
				snprintf(msg, sizeof msg, "%s beklenmedik şekilde kapandı", apps[k].name);
				const char *ok[] = { "Tamam" };
				kui_dialog("Hata", msg, ok, 1, draw);
				draw();
			}
			break;
		}
	}
	kui_quit();
	return 0;
}

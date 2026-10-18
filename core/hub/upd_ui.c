/* "Güncellemeler": lists the repo's packages against what is installed and installs them the way kpm
 * does (download the .kpkg, unpack it in a staging dir, run its install.sh). */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "kui.h"
#include "net/net.h"
#include "updates.h"
#include "upd_ui.h"

#define REPO_BASE "https://kaandikec.com/kindle-apps/"
#define STAGING "/mnt/us/kindle-apps-staging"
#define MAX_PKG_BYTES (64L * 1024 * 1024)
#define ROW_H 96
#define PER_PAGE 11

static UpdPkg pk[UPD_MAX];
static int n_pk, page;
static char status_line[160];
static KuiButton act[PER_PAGE], bottom[3];

static int pages(void) { return n_pk ? (n_pk + PER_PAGE - 1) / PER_PAGE : 1; }

static int n_updates(void)
{
	int k = 0;
	for (int i = 0; i < n_pk; i++) k += upd_state(&pk[i]) == UPD_UPDATE;
	return k;
}

static KuiRect row(int i) { return (KuiRect){ 30, KUI_TOPBAR_H + 20 + i * ROW_H, kui_width() - 60, ROW_H }; }

static void draw_row(int i, const char *busy)
{
	int k = page * PER_PAGE + i;
	KuiRect r = row(i);
	kui_fill(r, 255);
	act[i] = (KuiButton){ { 0 }, NULL, 0 };
	if (k >= n_pk) return;
	const UpdPkg *p = &pk[k];
	int st = upd_state(p), bw = 230;
	char v[80];
	kui_text((KuiRect){ r.x, r.y + 4, r.w - bw - 20, 52 }, T(p->name), KUI_FONT_BOLD, 34, KUI_ALIGN_LEFT, 0);
	if (st == UPD_NEW) snprintf(v, sizeof v, "%s", p->ver);
	else if (st == UPD_UPDATE) snprintf(v, sizeof v, "%s → %s", p->installed, p->ver);
	else snprintf(v, sizeof v, "%s", p->installed);
	kui_text((KuiRect){ r.x, r.y + 54, r.w - bw - 20, 36 }, v, KUI_FONT_REG, 26, KUI_ALIGN_LEFT, 80);
	KuiRect b = { r.x + r.w - bw, r.y + 10, bw, ROW_H - 20 };
	if (busy) {
		kui_text(b, busy, KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 0);
	} else if (st == UPD_CURRENT) {
		kui_text(b, T("Güncel sürüm"), KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 80);
	} else {
		act[i] = (KuiButton){ b, st == UPD_NEW ? T("Kur") : T("Güncelle"), k + 1 };
		kui_button(&act[i], 0);
	}
	kui_fill((KuiRect){ r.x, r.y + r.h - 2, r.w, 2 }, 180);
	kui_mark_dirty(r);
}

static void draw_status(void)
{
	int w = kui_width(), h = kui_height();
	KuiRect s = { 30, h - 250, w - 60, 80 };
	kui_fill(s, 255);
	kui_text(s, status_line, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(s);
}

static void draw(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Güncellemeler"), NULL);
	for (int i = 0; i < PER_PAGE; i++) draw_row(i, NULL);
	int y = h - 150;
	memset(bottom, 0, sizeof bottom);
	if (pages() > 1) {
		bottom[0] = (KuiButton){ { 30, y, 160, 120 }, "◀", 1 };
		bottom[1] = (KuiButton){ { w - 190, y, 160, 120 }, "▶", 2 };
		kui_button(&bottom[0], 0);
		kui_button(&bottom[1], 0);
	}
	if (n_updates() > 0) {
		bottom[2] = (KuiButton){ { 220, y, w - 440, 120 }, T("Tümünü güncelle"), 3 };
		kui_button(&bottom[2], 0);
	}
	draw_status();
	kui_flush(1);
}

static void message(const char *msg)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Güncellemeler"), NULL);
	kui_text((KuiRect){ 40, h / 2 - 150, w - 80, 300 }, msg, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
	kui_flush(1);
}

/* unpack and install a downloaded package; 0 on success. No shell parsing of untrusted text: the paths are
 * positional parameters and the id is [a-z0-9-] (upd_parse_repo). */
static int run_install(const char *dir, const char *kpkg)
{
	static const char script[] = "rm -rf \"$1\" && mkdir -p \"$1\" && cd \"$1\" && tar xzf \"$2\" && sh ./install.sh; "
				     "r=$?; cd /; rm -rf \"$1\" \"$2\"; exit $r";
	pid_t pid = fork();
	if (pid == 0) {
		signal(SIGTERM, SIG_DFL);
		execl("/bin/sh", "sh", "-c", script, "sh", dir, kpkg, (char *) NULL);
		_exit(127);
	}
	if (pid < 0) return -1;
	int st = 0;
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR) return -1;
	return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

/* installs pk[k], showing progress in its row when it is on screen; 0 on success */
static int install_one(int k)
{
	UpdPkg *p = &pk[k];
	int i = k - page * PER_PAGE, on_screen = i >= 0 && i < PER_PAGE;
	char url[300], dir[160], kpkg[180];
	snprintf(url, sizeof url, REPO_BASE "%s", p->url);
	snprintf(dir, sizeof dir, STAGING "/%s", p->id);
	snprintf(kpkg, sizeof kpkg, STAGING "/%s.kpkg", p->id);
	if (on_screen) {
		draw_row(i, T("İndiriliyor…"));
		kui_flush(0);
	}
	int ok = system("mkdir -p " STAGING) == 0 && net_download(url, kpkg, MAX_PKG_BYTES, 300) == 0;
	if (ok && on_screen) {
		draw_row(i, T("Kuruluyor…"));
		kui_flush(0);
	}
	ok = ok && run_install(dir, kpkg) == 0;
	unlink(kpkg);
	fprintf(stderr, "update: %s %s → %s %s\n", p->id, p->installed, p->ver, ok ? "ok" : "FAILED");
	if (ok) memcpy(p->installed, p->ver, sizeof p->installed); /* same size */
	snprintf(status_line, sizeof status_line, ok ? T("%s kuruldu") : T("Kurulamadı: %s"), T(p->name));
	if (on_screen) draw_row(i, NULL);
	draw_status();
	kui_flush(0);
	return ok ? 0 : -1;
}

static void blank(void) { message(""); }

static int fetch_repo(void)
{
	char *body = NULL;
	int len = 0;
	if (net_get(REPO_BASE "manifest.json", &body, &len, 30) != 0) return -1;
	n_pk = upd_parse_repo(body, pk, UPD_MAX);
	free(body);
	if (n_pk < 0) {
		n_pk = 0;
		return -1;
	}
	return 0;
}

int hub_updates(const char *root)
{
	int core_changed = 0, wifi_was = kui_wifi_enabled();
	page = 0;
	status_line[0] = '\0';
	message(T("Denetleniyor…"));
	if (wifi_was == 0) { /* e.g. left off by the dashboard's battery saver */
		kui_wifi_enable(1);
		kui_wifi_wait(40);
	}
	if (fetch_repo() != 0) {
		const char *ok[] = { T("Tamam") };
		kui_dialog(T("Güncellemeler"), T("Depoya ulaşılamadı.\nWi-Fi bağlantısını kontrol edin."), ok, 1, blank);
		goto out;
	}
	upd_read_installed(root, pk, n_pk);
	if (!n_updates()) snprintf(status_line, sizeof status_line, "%s", T("Tüm uygulamalar güncel"));
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
		int b = kui_buttons_hit(bottom, 3, e.x, e.y);
		if (b == 1 && page > 0) { page--; draw(); continue; }
		if (b == 2 && page + 1 < pages()) { page++; draw(); continue; }
		if (b == 3) { /* every installed package with a newer version; the core first (parse order) */
			int fails = 0;
			for (int k = 0; k < n_pk; k++) {
				if (upd_state(&pk[k]) != UPD_UPDATE) continue;
				if (install_one(k) == 0) core_changed |= !strcmp(pk[k].id, "kindle-apps-core");
				else fails++;
			}
			snprintf(status_line, sizeof status_line, "%s", fails ? T("Bazı güncellemeler kurulamadı") : T("Tüm uygulamalar güncel"));
			draw();
			continue;
		}
		int k = kui_buttons_hit(act, PER_PAGE, e.x, e.y) - 1;
		if (k >= 0 && k < n_pk && install_one(k) == 0) core_changed |= !strcmp(pk[k].id, "kindle-apps-core");
		if (k >= 0) draw();
	}
out:
	if (wifi_was == 0) kui_wifi_enable(0);
	return core_changed;
}

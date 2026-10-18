/* "Dikeç Store": the repo's apps against what is installed — install, update, remove, and the order of the
 * Uygulamalar menu. Installs the way kpm does (download the .kpkg, unpack it in a staging dir, run install.sh). */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "appreg.h"
#include "kui.h"
#include "net/net.h"
#include "updates.h"
#include "upd_ui.h"

#define REPO_BASE "https://kaandikec.com/kindle-apps/"
#define STAGING "/mnt/us/kindle-apps-staging"
#define MAX_PKG_BYTES (64L * 1024 * 1024)
#define ROW_H 124
#define PER_PAGE 7
#define LIST_Y (KUI_TOPBAR_H + 125)

enum { F_ALL, F_INSTALLED, F_UPDATES, N_FILTER };
static const char *const filter_label[N_FILTER] = { N_("Tümü"), N_("Kurulu"), N_("Güncellenecek") };

static const char *root_dir;
static UpdPkg pk[UPD_MAX];
static int n_pk, page, filter, vis[UPD_MAX], n_vis, core_changed;
static char status_line[160];
static KuiButton act[PER_PAGE], tabs[N_FILTER], bottom[3];

static int pages(void) { return n_vis ? (n_vis + PER_PAGE - 1) / PER_PAGE : 1; }

static int is_core(const UpdPkg *p) { return !strcmp(p->id, "kindle-apps-core"); }

static int n_updates(void)
{
	int k = 0;
	for (int i = 0; i < n_pk; i++) k += upd_state(&pk[i]) == UPD_UPDATE;
	return k;
}

/* the packages the current filter shows; the core first, then by (translated) name */
static void build_view(void)
{
	n_vis = 0;
	for (int i = 0; i < n_pk; i++) {
		int st = upd_state(&pk[i]);
		if ((filter == F_INSTALLED && st == UPD_NEW) || (filter == F_UPDATES && st != UPD_UPDATE)) continue;
		int j = n_vis++;
		while (j > 0 && !is_core(&pk[i]) && (is_core(&pk[vis[j - 1]]) ? 0 : strcmp(T(pk[vis[j - 1]].name), T(pk[i].name)) > 0)) {
			vis[j] = vis[j - 1];
			j--;
		}
		vis[j] = i;
	}
	if (page >= pages()) page = pages() - 1;
}

static KuiRect row(int i) { return (KuiRect){ 30, LIST_Y + i * ROW_H, kui_width() - 60, ROW_H }; }

static void draw_row(int i, const char *busy)
{
	int v = page * PER_PAGE + i;
	KuiRect r = row(i);
	kui_fill(r, 255);
	act[i] = (KuiButton){ { 0 }, NULL, 0 };
	kui_mark_dirty(r);
	if (v >= n_vis) return;
	const UpdPkg *p = &pk[vis[v]];
	int st = upd_state(p), bw = 220;
	const char *name = T(p->name);
	kui_text((KuiRect){ r.x, r.y + 6, r.w - bw - 30, 50 }, name, KUI_FONT_BOLD, kui_fit_px(name, KUI_FONT_BOLD, 36, r.w - bw - 30), KUI_ALIGN_LEFT, 0);
	char line[260];
	if (st == UPD_UPDATE) snprintf(line, sizeof line, "%s → %s", p->installed, p->ver);
	else snprintf(line, sizeof line, "%s", p->desc[0] ? T(p->desc) : p->ver);
	KuiLine l[1];
	int nl = kui_wrap_text(line, r.w - bw - 60, KUI_FONT_REG, 26, l, 1); /* one line, cut with an ellipsis */
	char shown[260];
	snprintf(shown, sizeof shown, "%.*s%s", nl ? l[0].len : 0, line, nl && l[0].len < (int) strlen(line) ? "…" : "");
	kui_text((KuiRect){ r.x, r.y + 60, r.w - bw - 30, 44 }, shown, KUI_FONT_REG, 26, KUI_ALIGN_LEFT, 80);
	KuiRect b = { r.x + r.w - bw, r.y + 16, bw, ROW_H - 40 };
	if (busy) {
		kui_text(b, busy, KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 0);
	} else if (st == UPD_CURRENT) {
		kui_text(b, T("Kurulu"), KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 80);
	} else {
		act[i] = (KuiButton){ b, st == UPD_NEW ? T("Kur") : T("Güncelle"), vis[v] + 1 };
		kui_button(&act[i], 0);
	}
	kui_fill((KuiRect){ r.x, r.y + r.h - 6, r.w, 2 }, 180);
}

static void draw_tabs(void)
{
	int w = kui_width(), tw = (w - 60 - 2 * 20) / N_FILTER;
	KuiRect area = { 0, KUI_TOPBAR_H, w, LIST_Y - KUI_TOPBAR_H };
	kui_fill(area, 255);
	for (int i = 0; i < N_FILTER; i++) {
		char s[64];
		if (i == F_UPDATES && n_updates()) snprintf(s, sizeof s, "%s (%d)", T(filter_label[i]), n_updates());
		else snprintf(s, sizeof s, "%s", T(filter_label[i]));
		static char lbl[N_FILTER][64];
		memcpy(lbl[i], s, sizeof lbl[i]);
		tabs[i] = (KuiButton){ { 30 + i * (tw + 20), KUI_TOPBAR_H + 18, tw, 86 }, lbl[i], i + 1 };
		kui_button(&tabs[i], i == filter);
	}
	kui_mark_dirty(area);
}

static void draw_status(void)
{
	int w = kui_width(), h = kui_height();
	KuiRect s = { 30, h - 235, w - 60, 70 };
	kui_fill(s, 255);
	kui_text(s, status_line, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(s);
}

static void draw_bottom(void)
{
	int w = kui_width(), h = kui_height(), y = h - 150;
	KuiRect area = { 0, y, w, 150 };
	kui_fill(area, 255);
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
	kui_mark_dirty(area);
}

/* list, tabs and buttons without a flash (filters, page turns, after an install) */
static void redraw_body(void)
{
	build_view();
	draw_tabs();
	for (int i = 0; i < PER_PAGE; i++) draw_row(i, NULL);
	draw_status();
	draw_bottom();
	kui_flush(0);
}

static void draw(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Dikeç Store"), T("Sırala"));
	build_view();
	draw_tabs();
	for (int i = 0; i < PER_PAGE; i++) draw_row(i, NULL);
	draw_status();
	draw_bottom();
	kui_flush(1);
}

static void message(const char *msg)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Dikeç Store"), NULL);
	kui_text((KuiRect){ 40, h / 2 - 150, w - 80, 300 }, msg, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
	kui_flush(1);
}

/* sh -c script with positional parameters only (no untrusted text is parsed by the shell); 0 on success */
static int run_sh(const char *script, const char *a1, const char *a2)
{
	pid_t pid = fork();
	if (pid == 0) {
		signal(SIGTERM, SIG_DFL);
		execl("/bin/sh", "sh", "-c", script, "sh", a1, a2, (char *) NULL);
		_exit(127);
	}
	if (pid < 0) return -1;
	int st = 0;
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR) return -1;
	return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

/* the row of pk[k] when it is on screen, else -1 */
static int row_of(int k)
{
	for (int i = 0; i < PER_PAGE; i++)
		if (page * PER_PAGE + i < n_vis && vis[page * PER_PAGE + i] == k) return i;
	return -1;
}

/* installs pk[k], showing progress in its row when it is on screen; 0 on success */
static int install_one(int k)
{
	UpdPkg *p = &pk[k];
	int i = row_of(k);
	char url[300], dir[160], kpkg[180];
	snprintf(url, sizeof url, REPO_BASE "%s", p->url);
	snprintf(dir, sizeof dir, STAGING "/%s", p->id);
	snprintf(kpkg, sizeof kpkg, STAGING "/%s.kpkg", p->id);
	if (i >= 0) {
		draw_row(i, T("İndiriliyor…"));
		kui_flush(0);
	}
	int ok = mkdir(STAGING, 0755) == 0 || errno == EEXIST;
	ok = ok && net_download(url, kpkg, MAX_PKG_BYTES, 300) == 0;
	if (ok && i >= 0) {
		draw_row(i, T("Kuruluyor…"));
		kui_flush(0);
	}
	ok = ok && run_sh("rm -rf \"$1\" && mkdir -p \"$1\" && cd \"$1\" && tar xzf \"$2\" && sh ./install.sh; r=$?; cd /; rm -rf \"$1\" \"$2\"; exit $r",
			  dir, kpkg) == 0;
	unlink(kpkg);
	fprintf(stderr, "store: install %s %s → %s %s\n", p->id, p->installed, p->ver, ok ? "ok" : "FAILED");
	if (ok) {
		memcpy(p->installed, p->ver, sizeof p->installed); /* same size */
		core_changed |= is_core(p);
	}
	snprintf(status_line, sizeof status_line, ok ? T("%s kuruldu") : T("Kurulamadı: %s"), T(p->name));
	return ok ? 0 : -1;
}

/* removes an app (never the core): its own uninstall script when it left one, else its folder; data/ stays */
static int remove_one(int k)
{
	UpdPkg *p = &pk[k];
	if (is_core(p)) return -1;
	char dir[300], ver[300];
	upd_app_dir(root_dir, p->id, dir, sizeof dir);
	snprintf(ver, sizeof ver, "%s/versions/%s", root_dir, p->id);
	int ok = run_sh("if [ -f \"$1/.uninstall.sh\" ]; then sh \"$1/.uninstall.sh\"; fi; rm -rf \"$1\" && rm -f \"$2\"", dir, ver) == 0;
	fprintf(stderr, "store: remove %s %s\n", p->id, ok ? "ok" : "FAILED");
	if (ok) p->installed[0] = '\0';
	snprintf(status_line, sizeof status_line, ok ? T("%s kaldırıldı") : T("Kaldırılamadı: %s"), T(p->name));
	return ok ? 0 : -1;
}

static void blank(void) { message(""); }

/* one app: description, versions, install/update/reinstall and remove */
static void detail(int k)
{
	int w = kui_width(), h = kui_height();
	KuiRect msg_r = { 0 };
	for (int full = 1;;) {
		UpdPkg *p = &pk[k];
		int st = upd_state(p);
		KuiButton b[2];
		int nb = 0;
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T(p->name), NULL);
		}
		KuiRect body = { 40, KUI_TOPBAR_H + 30, w - 80, h - KUI_TOPBAR_H - 60 };
		kui_fill(body, 255);
		int y = body.y;
		if (p->desc[0]) { /* the description at the top, as tall as its lines */
			KuiLine l[8];
			int nl = kui_wrap_text(T(p->desc), body.w, KUI_FONT_SERIF, 38, l, 8), lh = kui_line_height(KUI_FONT_SERIF, 38);
			kui_text((KuiRect){ body.x, y, body.w, nl * lh }, T(p->desc), KUI_FONT_SERIF, 38, KUI_ALIGN_LEFT, 0);
			y += nl * lh + 40;
		}
		char s[160];
		snprintf(s, sizeof s, T("Depodaki sürüm: %s"), p->ver);
		kui_text((KuiRect){ body.x, y, body.w, 50 }, s, KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0);
		if (st == UPD_NEW) snprintf(s, sizeof s, "%s", T("Kurulu değil"));
		else snprintf(s, sizeof s, T("Kurulu sürüm: %s"), p->installed);
		kui_text((KuiRect){ body.x, y + 60, body.w, 50 }, s, KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0);
		y += 160;
		b[nb++] = (KuiButton){ { body.x, y, body.w, 120 }, st == UPD_NEW ? T("Kur") : st == UPD_UPDATE ? T("Güncelle") : T("Yeniden kur"), 1 };
		if (st != UPD_NEW && !is_core(p)) b[nb++] = (KuiButton){ { body.x, y + 150, body.w, 120 }, T("Kaldır"), 2 };
		for (int i = 0; i < nb; i++) kui_button(&b[i], 0);
		msg_r = (KuiRect){ body.x, y + 300, body.w, 60 };
		kui_text(msg_r, status_line, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0);
		if (!is_core(p) && st != UPD_NEW)
			kui_text((KuiRect){ body.x, y + 380, body.w, 100 }, T("Kaldırınca ayarlar ve kayıtlar (data/) korunur."), KUI_FONT_REG, 26,
				 KUI_ALIGN_CENTER, 90);
		kui_mark_dirty(body);
		kui_flush(full);
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(b, nb, e.x, e.y);
		if (id == 1) {
			kui_fill(msg_r, 255);
			kui_text(msg_r, T("Kuruluyor…"), KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0);
			kui_mark_dirty(msg_r);
			kui_flush(0);
			install_one(k);
		} else if (id == 2) {
			char q[200];
			snprintf(q, sizeof q, T("%s kaldırılsın mı?"), T(p->name));
			const char *ch[] = { T("Kaldır"), T("Vazgeç") };
			int r = kui_dialog(T("Kaldır"), q, ch, 2, blank);
			if (r == 0) remove_one(k);
			full = 1;
		}
	}
}

/* the Uygulamalar menu's order: ▲ / ▼ beside each installed app, saved at once */
static void order_screen(void)
{
	static HubApp a[40];
	int n = hub_scan_apps(root_dir, a, 40), opage = 0, per = 10, w = kui_width(), h = kui_height();
	hub_apply_order(root_dir, a, n);
	for (int full = 1;;) {
		int op = n ? (n + per - 1) / per : 1;
		KuiButton b[2 * 10 + 2];
		int nb = 0;
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T("Menü sırası"), NULL);
		}
		KuiRect body = { 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H };
		kui_fill(body, 255);
		for (int i = 0; i < per && opage * per + i < n; i++) {
			int k = opage * per + i, y = KUI_TOPBAR_H + 20 + i * 110;
			char s[64];
			snprintf(s, sizeof s, "%d. %s", k + 1, T(a[k].name));
			kui_text((KuiRect){ 40, y, w - 360, 96 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 36, w - 360), KUI_ALIGN_LEFT, 0);
			b[nb++] = (KuiButton){ { w - 300, y, 120, 96 }, "▲", 100 + k };
			b[nb++] = (KuiButton){ { w - 160, y, 120, 96 }, "▼", 200 + k };
		}
		if (op > 1) {
			b[nb++] = (KuiButton){ { 30, h - 140, 200, 110 }, "◀", 1 };
			b[nb++] = (KuiButton){ { w - 230, h - 140, 200, 110 }, "▶", 2 };
			char s[32];
			snprintf(s, sizeof s, "%d / %d", opage + 1, op);
			kui_text((KuiRect){ 230, h - 140, w - 460, 110 }, s, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
		}
		for (int i = 0; i < nb; i++) kui_button(&b[i], 0);
		kui_mark_dirty(body);
		kui_flush(full);
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type == KUI_EV_SWIPE && op > 1) {
			opage = (opage + (e.dir == KUI_DIR_LEFT ? 1 : op - 1)) % op;
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(b, nb, e.x, e.y);
		if (id == 1 || id == 2) opage = (opage + (id == 2 ? 1 : op - 1)) % op; /* wraps around */
		else if (id >= 100) {
			int k = id % 100, j = id >= 200 ? k + 1 : k - 1;
			if (j < 0 || j >= n) continue;
			HubApp t = a[k];
			a[k] = a[j];
			a[j] = t;
			hub_save_order(root_dir, a, n);
			if (j / per != opage) opage = j / per; /* follow the moved app */
		}
	}
}

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
	int wifi_was = kui_wifi_enabled();
	root_dir = root;
	core_changed = 0;
	page = 0;
	filter = F_ALL;
	status_line[0] = '\0';
	message(T("Denetleniyor…"));
	if (wifi_was == 0) { /* e.g. left off by the dashboard's battery saver */
		kui_wifi_enable(1);
		kui_wifi_wait(40);
	}
	if (fetch_repo() != 0) {
		const char *ok[] = { T("Tamam") };
		kui_dialog(T("Dikeç Store"), T("Depoya ulaşılamadı.\nWi-Fi bağlantısını kontrol edin."), ok, 1, blank);
		goto out;
	}
	upd_read_installed(root, pk, n_pk);
	if (n_updates()) filter = F_UPDATES; /* what needs doing first */
	else snprintf(status_line, sizeof status_line, "%s", T("Tüm uygulamalar güncel"));
	draw();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw(); continue; }
		if (e.type == KUI_EV_SWIPE && pages() > 1) {
			page = (page + (e.dir == KUI_DIR_LEFT ? 1 : pages() - 1)) % pages();
			redraw_body();
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			order_screen();
			draw();
			continue;
		}
		int t = kui_buttons_hit(tabs, N_FILTER, e.x, e.y);
		if (t) {
			filter = t - 1;
			page = 0;
			redraw_body();
			continue;
		}
		int b = kui_buttons_hit(bottom, 3, e.x, e.y);
		if (b == 1 || b == 2) {
			page = (page + (b == 2 ? 1 : pages() - 1)) % pages();
			redraw_body();
			continue;
		}
		if (b == 3) { /* every installed package with a newer version; the core first (parse order) */
			int fails = 0;
			for (int k = 0; k < n_pk; k++)
				if (upd_state(&pk[k]) == UPD_UPDATE && install_one(k) != 0) fails++;
			snprintf(status_line, sizeof status_line, "%s", fails ? T("Bazı güncellemeler kurulamadı") : T("Tüm uygulamalar güncel"));
			redraw_body();
			continue;
		}
		int k = kui_buttons_hit(act, PER_PAGE, e.x, e.y) - 1;
		if (k >= 0 && k < n_pk) {
			install_one(k);
			redraw_body();
			continue;
		}
		for (int i = 0; i < PER_PAGE; i++) /* the rest of a row: its details */
			if (page * PER_PAGE + i < n_vis && kui_rect_contains(row(i), e.x, e.y)) {
				detail(vis[page * PER_PAGE + i]);
				draw();
				break;
			}
	}
out:
	if (wifi_was == 0) kui_wifi_enable(0);
	return core_changed;
}

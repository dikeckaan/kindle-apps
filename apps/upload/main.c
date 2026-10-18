/* Wi-Fi ile Yükle: a small HTTP upload server that runs while the app is open, so e-books can be sent
 * from a browser on the same network into /mnt/us/documents. PIN protected; the port is opened in iptables
 * only while the app runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "httpd.h"
#include "kui.h"
#include "server.h"

#define DEFAULT_DIR "/mnt/us/documents"

static UploadServer *srv;
static char ip[64], fail_msg[200], notice[240];
static int shown_gen, shown_files, was_active, awake_on;
static long last_progress, last_ip_check;

static KuiRect r_url, r_pin, r_status, r_list;

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void run(const char *cmd)
{
	if (system(cmd) != 0) fprintf(stderr, "failed: %s\n", cmd);
}

/* Wi-Fi IPv4 address, "" when there is none (UPLOAD_IP overrides, for the simulator) */
static void wifi_ip(char *out, size_t cap)
{
	const char *e = getenv("UPLOAD_IP");
	out[0] = '\0';
	if (e) {
		snprintf(out, cap, "%s", e);
		return;
	}
	FILE *p = popen("ip -4 -o addr show wlan0 2>/dev/null", "r");
	if (!p) return;
	char line[256];
	if (fgets(line, sizeof line, p)) {
		char *inet = strstr(line, "inet ");
		if (inet) {
			inet += 5;
			size_t k = strcspn(inet, "/ \n");
			if (k < cap) {
				memcpy(out, inet, k);
				out[k] = '\0';
			}
		}
	}
	pclose(p);
}

static void layout(void)
{
	int w = kui_width(), h = kui_height();
	r_url = (KuiRect){ 30, KUI_TOPBAR_H + 20, w - 60, 200 };
	r_pin = (KuiRect){ 30, KUI_TOPBAR_H + 240, w - 60, 230 };
	r_status = (KuiRect){ 30, KUI_TOPBAR_H + 640, w - 60, 150 };
	r_list = (KuiRect){ 30, KUI_TOPBAR_H + 810, w - 60, h - (KUI_TOPBAR_H + 810) - 20 };
}

static void draw_url(void)
{
	KuiRect r = r_url;
	kui_fill(r, 255);
	if (!srv) {
		kui_text((KuiRect){ r.x, r.y + 20, r.w, 80 }, T("Sunucu başlatılamadı"), KUI_FONT_BOLD, 60, KUI_ALIGN_CENTER, 0);
		kui_text((KuiRect){ r.x, r.y + 110, r.w, 80 }, fail_msg, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
	} else if (!ip[0]) {
		kui_text((KuiRect){ r.x, r.y + 10, r.w, 100 }, T("Wi-Fi kapalı"), KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, 0);
		kui_text((KuiRect){ r.x, r.y + 120, r.w, 70 }, T("Wi-Fi'yi açın; adres burada görünecek."), KUI_FONT_REG, 36,
			 KUI_ALIGN_CENTER, 0);
	} else {
		char url[128];
		snprintf(url, sizeof url, "http://%s:%d", ip, upload_port(srv));
		kui_text((KuiRect){ r.x, r.y, r.w, 60 }, T("Tarayıcıda bu adresi açın:"), KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
		int px = kui_fit_px(url, KUI_FONT_BOLD, 84, r.w);
		kui_text((KuiRect){ r.x, r.y + 70, r.w, 120 }, url, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty(r);
}

static KuiButton unlock_btn; /* shown while locked: only the owner, at the device, can resume */

static void draw_pin(void)
{
	KuiRect r = r_pin;
	kui_fill(r, 255);
	if (srv && upload_locked(srv)) { /* somebody kept guessing: the server refuses everything now */
		kui_text((KuiRect){ r.x, r.y, r.w, 130 }, T("Çok fazla yanlış PIN denendi.\nGüvenlik için yükleme durduruldu."), KUI_FONT_BOLD, 40,
			 KUI_ALIGN_CENTER, 0);
		unlock_btn = (KuiButton){ { r.x + r.w / 2 - 220, r.y + 140, 440, 85 }, T("Kilidi aç (yeni PIN)"), 1 };
		kui_button(&unlock_btn, 0);
	} else if (srv) {
		const char *p = upload_pin(srv);
		char spaced[16];
		snprintf(spaced, sizeof spaced, "%.3s %.3s", p, p + 3);
		kui_text((KuiRect){ r.x, r.y, r.w, 50 }, "PIN", KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
		KuiRect box = { r.x + 150, r.y + 55, r.w - 300, 170 };
		kui_frame(box, 4, 0);
		kui_text(box, spaced, KUI_FONT_BOLD, 140, KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty(r);
}

static void draw_help(void)
{
	int w = kui_width();
	kui_text((KuiRect){ 50, KUI_TOPBAR_H + 485, w - 100, 140 },
		 T("Telefon ya da bilgisayar aynı Wi-Fi ağında olmalı. Sayfada PIN'i girin, kitapları seçip Gönder'e basın. "
		   "Uygulamayı kapatınca sunucu durur."),
		 KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0);
	kui_fill((KuiRect){ 30, KUI_TOPBAR_H + 630, w - 60, 3 }, 0);
}

static void draw_status(void)
{
	KuiRect r = r_status;
	kui_fill(r, 255);
	long long got = 0, total = 0;
	char name[160], a[32], b[32], line[200];
	if (srv && upload_progress(srv, &got, &total, name, sizeof name)) {
		snprintf(line, sizeof line, T("Alınıyor: %s"), name[0] ? name : "…");
		int px = kui_fit_px(line, KUI_FONT_BOLD, 44, r.w);
		kui_text((KuiRect){ r.x, r.y + 10, r.w, 60 }, line, KUI_FONT_BOLD, px, KUI_ALIGN_LEFT, 0);
		httpd_fmt_size(got, a, sizeof a);
		httpd_fmt_size(total, b, sizeof b);
		int pct = total > 0 ? (int) (got * 100 / total) : 0;
		snprintf(line, sizeof line, T("%s / %s  (%%%d)"), a, b, pct);
		kui_text((KuiRect){ r.x, r.y + 75, r.w, 60 }, line, KUI_FONT_REG, 40, KUI_ALIGN_LEFT, 0);
	} else {
		const char *t = notice[0] ? notice : srv ? T("Dosya bekleniyor…") : "";
		int px = kui_fit_px(t, KUI_FONT_REG, 40, r.w);
		kui_text((KuiRect){ r.x, r.y + 40, r.w, 70 }, t, KUI_FONT_REG, px, KUI_ALIGN_LEFT, 0);
	}
	kui_mark_dirty(r);
}

static void draw_list(void)
{
	KuiRect r = r_list;
	kui_fill(r, 255);
	const UploadFile *files = NULL;
	int n = srv ? upload_files(srv, &files) : 0;
	char head[64];
	snprintf(head, sizeof head, n ? T("Bu oturumda gelenler (%d)") : T("Bu oturumda henüz dosya gelmedi"), n);
	kui_text((KuiRect){ r.x, r.y, r.w, 56 }, head, KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
	int row = 66, rows = (r.h - 66) / row;
	int first = n > rows ? n - rows : 0; /* newest at the bottom */
	for (int i = first; i < n; i++) {
		int y = r.y + 66 + (i - first) * row;
		char sz[32];
		httpd_fmt_size(files[i].size, sz, sizeof sz);
		int szw = 230;
		int px = kui_fit_px(files[i].name, KUI_FONT_REG, 36, r.w - szw - 20);
		kui_text((KuiRect){ r.x, y, r.w - szw - 20, row - 6 }, files[i].name, KUI_FONT_REG, px, KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ r.x + r.w - szw, y, szw, row - 6 }, sz, KUI_FONT_REG, 36, KUI_ALIGN_RIGHT, 0);
		kui_fill((KuiRect){ r.x, y + row - 3, r.w, 1 }, 0xAA);
	}
	kui_mark_dirty(r);
}

static void draw_all(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar(T("Wi-Fi ile Yükle"), NULL);
	draw_url();
	draw_pin();
	draw_help();
	draw_status();
	draw_list();
	kui_flush(1);
}

static void keep_awake(int on)
{
	if (on == awake_on) return;
	awake_on = on;
	kui_prevent_sleep(on);
}

/* compares the server state with what is on screen and refreshes only what changed */
static void update(void)
{
	long t = now_ms();
	int dirty = 0;
	int active = srv && upload_progress(srv, NULL, NULL, NULL, 0);
	keep_awake(active); /* a long transfer should not be cut by the screensaver turning Wi-Fi off */
	if (kui_input_asleep()) return; /* nothing is drawn behind the screensaver; WAKE redraws everything */
	if (!ip[0] && t - last_ip_check > 5000) {
		last_ip_check = t;
		wifi_ip(ip, sizeof ip);
		if (ip[0]) {
			draw_url();
			dirty = 1;
		}
	}
	static int shown_locked;
	if (srv && upload_locked(srv) != shown_locked) { /* the temporary lock started or ended */
		shown_locked = upload_locked(srv);
		draw_pin();
		dirty = 1;
	}
	if (srv && upload_pin_generation(srv) != shown_gen) {
		shown_gen = upload_pin_generation(srv);
		snprintf(notice, sizeof notice, T("%d kez yanlış PIN girildi: yeni PIN üretildi."), UPLOAD_MAX_BAD_PINS);
		draw_pin();
		if (!active) draw_status();
		dirty = 1;
	}
	const UploadFile *files;
	int n = srv ? upload_files(srv, &files) : 0;
	if (n != shown_files) {
		if (n > shown_files) snprintf(notice, sizeof notice, T("Alındı: %s"), files[n - 1].name);
		shown_files = n;
		draw_list();
		if (!active) draw_status(); /* a small file can come and go between two polls */
		dirty = 1;
	}
	if (active && (!was_active || t - last_progress >= 1000)) { /* at most once a second */
		last_progress = t;
		draw_status();
		dirty = 1;
	} else if (!active && was_active) {
		draw_status();
		dirty = 1;
	}
	was_active = active;
	if (dirty) kui_flush(0);
}

static void firewall_cleanup(void)
{
	/* a crash may have left our ACCEPT rule behind; only this app uses 8080-8089 */
	char cmd[400];
	int n = snprintf(cmd, sizeof cmd, "for p in");
	for (int p = UPLOAD_PORT_FIRST; p <= UPLOAD_PORT_LAST; p++) n += snprintf(cmd + n, sizeof cmd - (size_t) n, " %d", p);
	snprintf(cmd + n, sizeof cmd - (size_t) n, "; do while iptables -D INPUT -p tcp --dport $p -j ACCEPT 2>/dev/null; do :; done; done");
	if (system(cmd) != 0) { /* nothing to remove, or no iptables (host) */
	}
}

int main(void)
{
	if (kui_init("upload") != 0) return 1;
	layout();
	const char *dir = getenv("UPLOAD_DIR");
	if (!dir || !*dir) dir = DEFAULT_DIR;
	const char *sim = getenv("UPLOAD_SIM_SECONDS"); /* simulator only: keep serving after the script ends */
	long sim_until = sim ? now_ms() + atol(sim) * 1000L : 0;

	firewall_cleanup();
	srv = upload_open(dir, UPLOAD_PORT_FIRST, UPLOAD_PORT_LAST);
	if (srv) {
		char cmd[128];
		snprintf(cmd, sizeof cmd, "iptables -I INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null", upload_port(srv));
		run(cmd);
		fprintf(stderr, "upload: port %d, dir %s\n", upload_port(srv), dir);
	} else {
		snprintf(fail_msg, sizeof fail_msg, "%d-%d portları dolu ya da %s yok.", UPLOAD_PORT_FIRST, UPLOAD_PORT_LAST, dir);
	}
	wifi_ip(ip, sizeof ip);
	last_ip_check = now_ms();
	draw_all();

	for (;;) {
		int busy = srv && upload_busy(srv);
		KuiEvent e = kui_next_event(busy ? 20 : 200);
		if (e.type == KUI_EV_QUIT) {
			if (now_ms() >= sim_until) break;
			e.type = KUI_EV_NONE;
		}
		if (e.type == KUI_EV_WAKE) {
			if (!ip[0]) wifi_ip(ip, sizeof ip);
			draw_all();
		} else if (e.type == KUI_EV_TAP && srv && upload_locked(srv) && kui_buttons_hit(&unlock_btn, 1, e.x, e.y)) {
			upload_unlock(srv);
		} else if (e.type == KUI_EV_TAP && kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) {
			break;
		}
		if (srv) upload_poll(srv, busy ? 150 : sim_until ? 200 : 0);
		update();
	}

	keep_awake(0);
	int received = 0;
	if (srv) {
		const UploadFile *files;
		received = upload_files(srv, &files);
		char cmd[160];
		snprintf(cmd, sizeof cmd, "while iptables -D INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null; do :; done",
			 upload_port(srv));
		upload_close(srv);
		run(cmd);
	}
	if (received > 0) run("lipc-set-prop com.lab126.scanner doFullScan 1 >/dev/null 2>&1"); /* best effort */
	kui_quit();
	return 0;
}

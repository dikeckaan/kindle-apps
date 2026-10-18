/* Sistem Bilgisi: battery, storage, memory, network, firmware, uptime and security state at a glance. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include "firewall/fw.h"
#include "kui.h"
#include "si.h"
#include "ssh/sshsrv.h"

#define POWERD "com.lab126.powerd"
#define MAX_ROWS 12

static char labels[MAX_ROWS][48], values[MAX_ROWS][128];
static int n_rows;
static KuiButton refresh_btn;

static void row(const char *label, const char *value)
{
	if (n_rows == MAX_ROWS) return;
	snprintf(labels[n_rows], sizeof labels[0], "%s", label);
	snprintf(values[n_rows], sizeof values[0], "%s", value && *value ? value : "—");
	n_rows++;
}

static int read_text(const char *path, char *buf, int cap) { return kui_read_file(path, buf, cap); }

/* first line of a command's output (no shell input from outside: fixed commands only) */
static void command_line(const char *cmd, char *out, int cap)
{
	out[0] = '\0';
	FILE *p = popen(cmd, "r");
	if (!p) return;
	if (fgets(out, cap, p)) out[strcspn(out, "\n")] = '\0';
	pclose(p);
}

static void wifi_ip(char *out, int cap)
{
	char line[256];
	const char *sim = getenv("SYSINFO_IP"); /* simulator */
	if (sim) {
		snprintf(out, (size_t) cap, "%s", sim);
		return;
	}
	out[0] = '\0';
	command_line("ip -4 -o addr show wlan0 2>/dev/null", line, sizeof line);
	char *inet = strstr(line, "inet ");
	if (!inet) return;
	inet += 5;
	int k = (int) strcspn(inet, "/ ");
	snprintf(out, (size_t) cap, "%.*s", k, inet);
}

static void gather(void)
{
	char s[160], a[48], b[48];
	static char buf[8192];
	n_rows = 0;

	int lvl = kui_lipc_get_int(POWERD, "battLevel", -1), chg = kui_lipc_get_int(POWERD, "isCharging", -1);
	if (lvl >= 0) snprintf(s, sizeof s, "%%%d%s", lvl, chg == 1 ? " · şarj oluyor" : "");
	else s[0] = '\0';
	row("Pil", s);
	int tf = kui_lipc_get_int(POWERD, "battTemperature", -1000);
	if (tf > -1000) snprintf(s, sizeof s, "%d °C", si_batt_temp_c(tf));
	else s[0] = '\0';
	row("Pil sıcaklığı", s);

	struct statvfs v;
	if (statvfs(getenv("SYSINFO_DISK") ? getenv("SYSINFO_DISK") : "/mnt/us", &v) == 0) {
		si_fmt_bytes((long long) v.f_bavail * (long long) v.f_frsize, a, sizeof a);
		si_fmt_bytes((long long) v.f_blocks * (long long) v.f_frsize, b, sizeof b);
		snprintf(s, sizeof s, "%s boş / %s", a, b);
	} else {
		s[0] = '\0';
	}
	row("Depolama", s);

	long total, avail;
	if (read_text("/proc/meminfo", buf, sizeof buf) > 0 && si_parse_meminfo(buf, &total, &avail) == 0) {
		si_fmt_bytes(avail, a, sizeof a);
		si_fmt_bytes(total, b, sizeof b);
		snprintf(s, sizeof s, "%s boş / %s", a, b);
	} else {
		s[0] = '\0';
	}
	row("Bellek", s);

	wifi_ip(s, sizeof s);
	row("Wi-Fi IP", s[0] ? s : "bağlı değil");

	if (read_text("/etc/prettyversion.txt", buf, sizeof buf) > 0) si_parse_firmware(buf, s, sizeof s);
	else s[0] = '\0';
	row("Yazılım", s);

	struct utsname u;
	if (uname(&u) == 0) snprintf(s, sizeof s, "%s %s", u.sysname, u.release);
	else s[0] = '\0';
	row("Çekirdek", s);

	long up = read_text("/proc/uptime", buf, sizeof buf) > 0 ? si_parse_uptime(buf) : -1;
	si_fmt_duration(up, s, sizeof s);
	row("Açık kalma süresi", up >= 0 ? s : "");

	static char ipt[8192];
	FILE *p = popen("iptables -S INPUT 2>/dev/null", "r");
	size_t n = p ? fread(ipt, 1, sizeof ipt - 1, p) : 0;
	int ok = p && pclose(p) == 0 && n > 0;
	ipt[n] = '\0';
	row("Güvenlik duvarı", !ok ? "" : fw_is_open_policy(ipt) ? "Kapalı (gelen bağlantılara açık)" : "Açık");

	int port = ssh_server_port();
	if (port > 0) snprintf(s, sizeof s, "Açık · port %d", port);
	else snprintf(s, sizeof s, "Kapalı");
	row("SSH sunucusu", s);
}

static void draw(int full)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar("Sistem Bilgisi", NULL);
	int y = KUI_TOPBAR_H + 20, rh = 95;
	for (int i = 0; i < n_rows; i++, y += rh) {
		kui_text((KuiRect){ 40, y, 400, rh }, labels[i], KUI_FONT_REG, 38, KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ 440, y, w - 480, rh }, values[i], KUI_FONT_BOLD, kui_fit_px(values[i], KUI_FONT_BOLD, 40, w - 480),
			 KUI_ALIGN_RIGHT, 0);
		kui_fill((KuiRect){ 40, y + rh - 2, w - 80, 1 }, 0xAA);
	}
	refresh_btn = (KuiButton){ { (w - 400) / 2, h - 140, 400, 110 }, "Yenile", 1 };
	kui_button(&refresh_btn, 0);
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

int main(void)
{
	if (kui_init("sysinfo") != 0) return 1;
	gather();
	draw(1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { /* values may have changed while asleep */
			gather();
			draw(1);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) break;
		if (kui_buttons_hit(&refresh_btn, 1, e.x, e.y)) {
			gather();
			draw(0);
		}
	}
	kui_quit();
	return 0;
}

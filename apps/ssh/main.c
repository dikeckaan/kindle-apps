/* SSH Sunucusu: password-protected dropbear that keeps running after the app closes.
 * Secrets (password record, host key, pidfile) live in SSH_DIR, which only root can read or write. */
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"
#include "sshcfg.h"
#include "sshsrv.h"

enum { BTN_TOGGLE = 1, BTN_PASSWORD };

static KuiButton buttons[2];
static char status_line[160], detail_line[160];

static int ensure_dir(void)
{
	mkdir("/var/local/kindle-apps", 0700);
	mkdir(SSH_DIR, 0700);
	chmod("/var/local/kindle-apps", 0700);
	chmod(SSH_DIR, 0700);
	struct stat st;
	return lstat(SSH_DIR, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 077) ? 0 : -1;
}

static int have_password(void)
{
	struct stat st;
	return lstat(SSH_PASSWD, &st) == 0 && S_ISREG(st.st_mode);
}

static int write_password(const char *pw)
{
	unsigned char salt[16];
	char rec[160], tmp[] = SSH_DIR "/passwd.tmp";
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, salt, sizeof salt) != (ssize_t) sizeof salt) {
		if (fd >= 0) close(fd);
		return -1;
	}
	close(fd);
	int n = ssh_make_record(pw, salt, rec, sizeof rec);
	if (n < 0) return -1;
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (fd < 0) return -1;
	int ok = write(fd, rec, (size_t) n) == n && write(fd, "\n", 1) == 1 && fsync(fd) == 0;
	close(fd);
	if (!ok || rename(tmp, SSH_PASSWD) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

static void read_proc(const char *path, char *buf, size_t cap)
{
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(buf, 1, cap - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
}

static int pick_port(void)
{
	static char tcp[16384];
	read_proc("/proc/net/tcp", tcp, sizeof tcp);
	const int candidates[] = { 22, 2222, 2200, 8022 };
	for (unsigned i = 0; i < sizeof candidates / sizeof candidates[0]; i++)
		if (!tcp_port_listening(tcp, candidates[i])) return candidates[i];
	return -1;
}

static void wifi_ip(char *out, size_t cap)
{
	snprintf(out, cap, "?");
	FILE *p = popen("ip -4 -o addr show wlan0 2>/dev/null", "r");
	if (!p) return;
	char line[256];
	if (fgets(line, sizeof line, p)) {
		char *inet = strstr(line, "inet ");
		if (inet) {
			inet += 5;
			size_t k = strcspn(inet, "/ ");
			if (k < cap) {
				memcpy(out, inet, k);
				out[k] = '\0';
			}
		}
	}
	pclose(p);
}

static int run(const char *cmd)
{
	int rc = system(cmd);
	if (rc != 0) fprintf(stderr, "failed (%d): %s\n", rc, cmd);
	return rc;
}

static int start_server(void)
{
	char cmd[512];
	struct stat st;
	if (ensure_dir() != 0) return -1;
	const char *root = getenv("KUI_ROOT");
	char keygen[300];
	snprintf(keygen, sizeof keygen, "%s/apps/ssh/dropbearkey", root && *root ? root : "/mnt/us/kindle-apps");
	if (lstat(SSH_HOSTKEY, &st) != 0) {
		snprintf(cmd, sizeof cmd, "'%s' -t ed25519 -f " SSH_HOSTKEY " >/dev/null 2>&1", keygen);
		if (run(cmd) != 0) return -1;
	}
	int port = pick_port();
	if (port < 0) return -1;
	unlink(SSH_PIDFILE);
	/* dropbear daemonises itself; -R off, -s off: password auth against our record only */
	snprintf(cmd, sizeof cmd, SSH_BIN " -p %d -P " SSH_PIDFILE " -r " SSH_HOSTKEY, port);
	if (run(cmd) != 0) return -1;
	for (int i = 0; i < 20 && ssh_server_pid() < 0; i++) usleep(100000);
	if (ssh_server_pid() < 0) return -1;
	snprintf(cmd, sizeof cmd, "iptables -I INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null", port);
	run(cmd);
	return 0;
}

static void stop_server(void)
{
	int port = ssh_server_port();
	if (ssh_stop_all() != 0) fprintf(stderr, "dropbear did not stop\n");
	if (port > 0) {
		char cmd[128];
		snprintf(cmd, sizeof cmd, "while iptables -D INPUT -p tcp --dport %d -j ACCEPT 2>/dev/null; do :; done", port);
		run(cmd);
	}
}

static void describe(void)
{
	int port = ssh_server_port();
	if (port > 0) {
		char ip[64];
		wifi_ip(ip, sizeof ip);
		snprintf(status_line, sizeof status_line, "AÇIK");
		if (port == 22) snprintf(detail_line, sizeof detail_line, "ssh root@%s", ip);
		else snprintf(detail_line, sizeof detail_line, "ssh root@%s -p %d", ip, port);
	} else {
		snprintf(status_line, sizeof status_line, "KAPALI");
		snprintf(detail_line, sizeof detail_line, have_password() ? "Parola belirlenmiş" : "Henüz parola yok");
	}
}

static void draw(void)
{
	int w = kui_width(), h = kui_height();
	describe();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar("SSH Sunucusu", NULL);
	kui_text((KuiRect){ 40, KUI_TOPBAR_H + 60, w - 80, 120 }, status_line, KUI_FONT_BOLD, 90, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 40, KUI_TOPBAR_H + 200, w - 80, 100 }, detail_line, KUI_FONT_REG, 50, KUI_ALIGN_CENTER, 0);
	int on = ssh_server_pid() > 0;
	buttons[0] = (KuiButton){ { 120, KUI_TOPBAR_H + 380, w - 240, 150 }, on ? "Kapat" : "Aç", BTN_TOGGLE };
	buttons[1] = (KuiButton){ { 120, KUI_TOPBAR_H + 570, w - 240, 150 }, "Parolayı değiştir", BTN_PASSWORD };
	for (int i = 0; i < 2; i++) kui_button(&buttons[i], 0);
	kui_text((KuiRect){ 40, KUI_TOPBAR_H + 800, w - 80, 300 },
		 "Kullanıcı adı: root\nSunucu bu uygulama kapanınca da çalışır.\nKindle uyuyunca Wi-Fi ile birlikte bağlantı kopar.\nYeniden başlatınca kapalı açılır.",
		 KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 0);
	kui_flush(1);
}

static void message(const char *title, const char *msg)
{
	const char *ok[] = { "Tamam" };
	kui_dialog(title, msg, ok, 1, draw);
	draw();
}

/* asks for a new password twice; 1 when saved */
static int set_password(void)
{
	char a[128] = "", b[128] = "";
	if (!kui_input_text("Yeni SSH parolası (en az 6)", a, sizeof a, 1, 6, NULL)) {
		draw();
		return 0;
	}
	if (!kui_input_text("Parolayı tekrar girin", b, sizeof b, 1, 6, NULL)) {
		draw();
		return 0;
	}
	draw();
	if (strcmp(a, b) != 0) {
		message("Parola", "Parolalar aynı değil, tekrar deneyin.");
		return 0;
	}
	int ok = ensure_dir() == 0 && write_password(a) == 0;
	memset(a, 0, sizeof a);
	memset(b, 0, sizeof b);
	message("Parola", ok ? "Parola kaydedildi." : "Parola kaydedilemedi.");
	return ok;
}

int main(void)
{
	if (kui_init("ssh") != 0) return 1;
	draw();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw(); continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) break;
		int id = kui_buttons_hit(buttons, 2, e.x, e.y);
		if (id == BTN_TOGGLE) {
			if (ssh_server_pid() > 0) {
				stop_server();
				draw();
			} else {
				if (!have_password() && !set_password()) continue;
				if (start_server() != 0) message("SSH", "Sunucu başlatılamadı (günlüğe bakın).");
				else draw();
			}
		} else if (id == BTN_PASSWORD) {
			set_password();
		}
	}
	kui_quit();
	return 0;
}

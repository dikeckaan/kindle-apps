/* Güvenlik Duvarı: tells whether incoming connections are blocked and offers to toggle it (runtime only). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fw.h"
#include "ssh/sshsrv.h"
#include "kui.h"

/* Kindle's own default rules: root-owned on the read-only rootfs, so nothing writable is ever restored. */
#define DEFAULT_RULES "/etc/sysconfig/iptables"

static int wall_down(void)
{
	static char out[8192];
	FILE *p = popen("iptables -S INPUT 2>/dev/null", "r");
	if (!p) return 0;
	size_t n = fread(out, 1, sizeof out - 1, p);
	pclose(p);
	out[n] = '\0';
	return fw_is_open_policy(out);
}

/* Re-opens the port of our SSH server (if running) after the default rules were restored. The port comes
 * from ssh_server_port(), which only trusts a root-owned pidfile and a root process running our binary. */
static void keep_ssh_reachable(void)
{
	char cmd[96];
	int port = ssh_server_port();
	if (port <= 0) return;
	snprintf(cmd, sizeof cmd, "iptables -I INPUT -p tcp --dport %d -j ACCEPT", port);
	if (system(cmd) != 0) fprintf(stderr, "could not re-open ssh port %d\n", port);
}

static void blank(void)
{
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar(T("Güvenlik Duvarı"), NULL);
}

static void redraw(void)
{
	blank();
	kui_flush(1);
}

int main(void)
{
	if (kui_init("firewall") != 0) return 1;
	redraw();
	int down = wall_down();
	const char *ch_down[] = { T("Aç"), T("Vazgeç") }, *ch_up[] = { T("Kapat"), T("Vazgeç") };
	int k = down ? kui_dialog(T("Güvenlik Duvarı"), T("Şu an KAPALI: gelen bağlantılara izin veriliyor.\nAçılsın mı?"), ch_down, 2, redraw)
		     : kui_dialog(T("Güvenlik Duvarı"), T("Şu an AÇIK: gelen bağlantılar engelleniyor.\nKapatılsın mı?"), ch_up, 2, redraw);
	if (k == 0) {
		int rc;
		if (down) {
			rc = system("iptables-restore < " DEFAULT_RULES);
			if (rc == 0) keep_ssh_reachable();
		} else {
			rc = system("iptables -P INPUT ACCEPT && iptables -F INPUT");
		}
		int now_down = wall_down();
		const char *msg = rc != 0 || now_down == down ? T("İşlem başarısız oldu.")
				  : now_down ? T("Kapatıldı. Kindle yeniden başlayınca tekrar açılır.")
					     : T("Açıldı. Gelen bağlantılar engelleniyor.");
		const char *ok[] = { T("Tamam") };
		redraw();
		kui_dialog(T("Güvenlik Duvarı"), msg, ok, 1, redraw);
	}
	kui_quit();
	return 0;
}

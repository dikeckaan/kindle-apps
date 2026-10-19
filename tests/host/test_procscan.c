/* procscan: classification of a fake /proc tree (the incident: gambatte-k2 left running behind Uygulamalar) and the
 * staged stop (signals are only logged and simulated with a fake root). */
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"
#include "t.h"

static char root[256];
static long hz;

static void put(const char *rel, const char *data, int len)
{
	char p[512];
	snprintf(p, sizeof p, "%s/%s", root, rel);
	FILE *f = fopen(p, "wb");
	if (!f) { perror(p); exit(2); }
	fwrite(data, 1, (size_t) len, f);
	fclose(f);
}

/* argv: NULL-terminated; exe NULL: none (kernel thread); age in seconds before uptime 200000 */
static void mkproc(int pid, int ppid, char state, long age, const char *exe, long rss_kb, const char *env, const char *fd0, ...)
{
	char rel[512], buf[1024];
	snprintf(rel, sizeof rel, "%s/%d", root, pid);
	mkdir(rel, 0755);
	va_list ap;
	va_start(ap, fd0);
	int n = 0;
	const char *comm = "x";
	for (const char *a; (a = va_arg(ap, const char *));) {
		if (!n) comm = strrchr(a, '/') ? strrchr(a, '/') + 1 : a;
		n += snprintf(buf + n, sizeof buf - (size_t) n, "%s", a) + 1;
	}
	va_end(ap);
	snprintf(rel, sizeof rel, "%d/cmdline", pid);
	put(rel, buf, n);
	unsigned long long start = (unsigned long long) ((200000 - age) * hz);
	char st[512];
	int l = snprintf(st, sizeof st, "%d (%.15s) %c %d 0 0 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 %llu 1000 100\n", pid, n ? comm : "kworker", state, ppid, start);
	snprintf(rel, sizeof rel, "%d/stat", pid);
	put(rel, st, l);
	l = snprintf(st, sizeof st, "Name:\tx\nVmRSS:\t   %ld kB\n", rss_kb);
	snprintf(rel, sizeof rel, "%d/status", pid);
	put(rel, st, l);
	if (exe) {
		char p[512];
		snprintf(p, sizeof p, "%s/%d/exe", root, pid);
		if (symlink(exe, p) != 0) { perror(p); exit(2); }
	}
	if (env) {
		char e[256];
		int k = snprintf(e, sizeof e, "%s", env);
		snprintf(rel, sizeof rel, "%d/environ", pid);
		put(rel, e, k + 1);
	}
	if (fd0) {
		char p[512];
		snprintf(p, sizeof p, "%s/%d/fd", root, pid);
		mkdir(p, 0755);
		snprintf(p, sizeof p, "%s/%d/fd/3", root, pid);
		if (symlink(fd0, p) != 0) { perror(p); exit(2); }
	}
}

static void build_tree(void)
{
	char cmd[400];
	snprintf(cmd, sizeof cmd, "rm -rf %s", root);
	if (root[0] && system(cmd) != 0) exit(2);
	mkdir(root, 0755);
	put("uptime", "200000.00 90000.00\n", 19);
	put("meminfo", "MemTotal:  500000 kB\nMemFree:  10000 kB\nMemAvailable:  123456 kB\n", 65);
	mkproc(1, 0, 'S', 100000, "/sbin/init", 900, NULL, NULL, "/sbin/init", NULL);
	mkproc(2, 0, 'S', 100000, NULL, 0, NULL, NULL, NULL);
	mkproc(100, 1, 'S', 99000, "/bin/busybox", 300, NULL, NULL, "/bin/sh", "-e", "/proc/self/fd/9", NULL);
	mkproc(101, 1, 'S', 99000, "/usr/bin/winmgr", 2000, NULL, NULL, "/usr/bin/winmgr", NULL);
	mkproc(102, 1, 'S', 99000, "/usr/sbin/wpa_supplicant", 2000, NULL, NULL, "wpa_supplicant", "-B", NULL);
	/* KOReader's SSH server, a logged-in session and what runs in it */
	mkproc(200, 1, 'S', 90000, "/mnt/us/koreader/dropbear", 800, NULL, NULL, "./dropbear", "-E", "-R", "-p22", "-P", "/tmp/dropbear_koreader.pid", "-n", NULL);
	mkproc(201, 200, 'S', 60, "/mnt/us/koreader/dropbear", 900, NULL, NULL, "./dropbear", "-E", "-R", "-p22", NULL);
	mkproc(202, 201, 'S', 60, "/bin/busybox", 300, NULL, NULL, "-sh", NULL);
	mkproc(203, 202, 'S', 30, "/mnt/us/extensions/foo/bar", 300, "DISPLAY=:0.0", NULL, "/mnt/us/extensions/foo/bar", NULL);
	mkproc(210, 1, 'S', 5000, "/mnt/us/dbg/dropbear", 700, NULL, NULL, "./dropbear", "-E", "-p2222", "-P", "/tmp/dropbear_dbg.pid", NULL);
	/* us: kui-run.sh, the hub (self), its lipc-wait-event; our SSH server */
	mkproc(300, 1, 'S', 120, "/bin/busybox", 300, NULL, NULL, "/bin/sh", "/tmp/kui-run.4242.sh", "hub", "/mnt/us/kindle-apps/bin/hub", NULL);
	mkproc(301, 300, 'S', 119, "/tmp/kui-hub", 3000, NULL, "/dev/fb0", "/tmp/kui-hub", NULL);
	mkproc(302, 300, 'S', 119, "/usr/bin/lipc-wait-event", 400, NULL, NULL, "lipc-wait-event", "-m", "com.lab126.powerd", "goingToScreenSaver", NULL);
	mkproc(310, 1, 'S', 3000, "/mnt/us/kindle-apps/apps/ssh/dropbear (deleted)", 600, NULL, NULL, "/mnt/us/kindle-apps/apps/ssh/dropbear", "-p", "2022", NULL);
	/* the incident: gambatte-k2 started from KUAL 34 h ago */
	mkproc(400, 1, 'S', 34 * 3600, "/mnt/us/extensions/gambatte-k2/gambatte-k2-armhf", 23456, "HOME=/tmp\0DISPLAY=:0", NULL,
	       "/mnt/us/extensions/gambatte-k2/gambatte-k2-armhf", "rom.gb", NULL);
	/* KOReader: its launcher runs from /var/tmp, reader.lua (luajit) holds the touchscreen */
	mkproc(500, 1, 'S', 7200, "/bin/busybox", 400, NULL, NULL, "/bin/sh", "/var/tmp/koreader.sh", NULL);
	mkproc(501, 500, 'S', 7199, "/mnt/us/koreader/luajit", 41000, NULL, "/dev/input/event1", "./reader.lua", "/mnt/us/documents", NULL);
	put("500/exits_after", "501\n", 4);
	mkproc(510, 1, 'S', 7300, "/bin/busybox", 300, NULL, NULL, "/bin/sh", "/mnt/us/koreader/koreader.sh", NULL);
	/* kterm and its shell */
	mkproc(600, 1, 'S', 600, "/mnt/us/extensions/kterm/bin/kterm", 5000, "DISPLAY=:0", NULL, "/mnt/us/extensions/kterm/bin/kterm", NULL);
	mkproc(601, 600, 'S', 600, "/bin/busybox", 200, NULL, NULL, "sh", NULL);
	/* kpm installing a package */
	mkproc(700, 1, 'S', 10, "/var/local/kmc/bin/kpm", 900, NULL, NULL, "kpm", "-i", "x", NULL);
	mkproc(701, 700, 'S', 9, "/bin/busybox", 200, NULL, NULL, "sh", "/mnt/us/kindle-apps-staging/x/install.sh", NULL);
	mkproc(702, 700, 'S', 9, "/bin/busybox", 200, NULL, NULL, "sh", "/mnt/us/other-staging/install.sh", NULL);
	/* system binaries that open files on /mnt/us stay SYSTEM; jailbreak tools are protected */
	mkproc(800, 1, 'S', 9000, "/usr/bin/indexer", 3000, NULL, NULL, "/usr/bin/indexer", "/mnt/us/documents/a.pdf", NULL);
	mkproc(801, 1, 'S', 9000, "/var/local/mkk/hotfix", 300, NULL, NULL, "/var/local/mkk/hotfix", NULL);
	mkproc(802, 100, 'S', 9000, "/bin/busybox", 300, NULL, NULL, "sleep", "/mnt/us/x", NULL);
	mkproc(950, 400, 'Z', 5, "/mnt/us/extensions/gambatte-k2/helper", 0, NULL, NULL, NULL);
}

static KuiProc ps[KUI_PROC_MAX];
static int n;

static const KuiProc *P(int pid)
{
	for (int i = 0; i < n; i++)
		if (ps[i].pid == pid) return &ps[i];
	return NULL;
}

#define CLS(pid, c)                                                                              \
	do {                                                                                     \
		const KuiProc *q_ = P(pid);                                                      \
		if (!q_) T_FAIL_("pid %d missing", pid);                                         \
		else if (q_->cls != (c)) T_FAIL_("pid %d (%s): class %d, expected %d", pid, q_->name, q_->cls, c); \
	} while (0)

static void test_classes(void)
{
	build_tree();
	n = kui_proc_scan(ps, KUI_PROC_MAX, 301);
	T_EQ_INT(n, 26); /* the zombie is left out */
	T_ASSERT(P(950) == NULL);
	CLS(1, KUI_PROC_SYSTEM);
	CLS(2, KUI_PROC_SYSTEM);
	T_ASSERT(P(2)->kthread);
	CLS(100, KUI_PROC_SYSTEM);
	CLS(101, KUI_PROC_SYSTEM);
	CLS(102, KUI_PROC_SYSTEM);
	CLS(200, KUI_PROC_PROTECTED);
	T_EQ_INT(P(200)->why, KUI_WHY_SSH);
	CLS(201, KUI_PROC_PROTECTED);
	CLS(202, KUI_PROC_PROTECTED);
	CLS(203, KUI_PROC_PROTECTED); /* run from an SSH session */
	T_EQ_INT(P(203)->why, KUI_WHY_SSH);
	T_EQ_STR(P(202)->name, "KOReader");
	CLS(210, KUI_PROC_PROTECTED);
	CLS(300, KUI_PROC_OURS);
	CLS(301, KUI_PROC_OURS);
	CLS(302, KUI_PROC_OURS);
	CLS(310, KUI_PROC_PROTECTED);
	T_EQ_STR(P(310)->exe, "/mnt/us/kindle-apps/apps/ssh/dropbear");
	CLS(400, KUI_PROC_FOREIGN);
	T_EQ_STR(P(400)->name, "gambatte-k2");
	T_ASSERT(P(400)->gui);
	T_EQ_INT(P(400)->rss_kb, 23456);
	T_EQ_INT(P(400)->age_s, 34 * 3600);
	CLS(500, KUI_PROC_FOREIGN); /* launcher shell from /var/tmp */
	T_ASSERT(P(500)->shell);
	T_EQ_STR(P(500)->name, "KOReader");
	CLS(501, KUI_PROC_FOREIGN);
	T_ASSERT(P(501)->gui && !P(501)->shell);
	CLS(510, KUI_PROC_FOREIGN);
	T_EQ_STR(P(510)->name, "KOReader");
	T_ASSERT(!P(510)->gui);
	CLS(600, KUI_PROC_FOREIGN);
	CLS(601, KUI_PROC_FOREIGN);
	T_EQ_STR(P(601)->name, "kterm");
	CLS(700, KUI_PROC_PROTECTED);
	T_EQ_INT(P(700)->why, KUI_WHY_KPM);
	CLS(701, KUI_PROC_PROTECTED);
	CLS(702, KUI_PROC_PROTECTED);
	CLS(800, KUI_PROC_SYSTEM);
	CLS(801, KUI_PROC_PROTECTED);
	T_EQ_INT(P(801)->why, KUI_WHY_JAILBREAK);
	CLS(802, KUI_PROC_SYSTEM); /* a busybox applet is not an interpreter */
	T_EQ_INT(kui_mem_available_kb(), 123456);
}

static void test_groups(void)
{
	build_tree();
	n = kui_proc_scan(ps, KUI_PROC_MAX, 301);
	KuiProcGroup g[16];
	int k = kui_proc_groups(ps, n, KUI_PROC_FOREIGN, g, 16);
	T_EQ_INT(k, 3);
	int seen = 0;
	for (int i = 0; i < k; i++) {
		if (!strcmp(g[i].name, "gambatte-k2")) {
			seen |= 1;
			T_EQ_INT(g[i].n, 1);
			T_ASSERT(g[i].gui);
		}
		if (!strcmp(g[i].name, "KOReader")) {
			seen |= 2;
			T_EQ_INT(g[i].n, 3);
			T_EQ_INT(g[i].rss_kb, 41700);
			T_EQ_INT(g[i].age_s, 7300);
		}
		if (!strcmp(g[i].name, "kterm")) {
			seen |= 4;
			T_EQ_INT(g[i].n, 2);
			T_ASSERT(g[i].gui);
		}
	}
	T_EQ_INT(seen, 7);
	k = kui_proc_groups(ps, n, KUI_PROC_PROTECTED, g, 16);
	/* KOReader's SSH (with its session), dbg's SSH, our SSH server, kpm, the hotfix */
	T_EQ_INT(k, 5);
	int ko = 0;
	while (ko < k && strcmp(g[ko].name, "KOReader")) ko++;
	T_ASSERT(ko < k);
	if (ko < k) {
		T_EQ_INT(g[ko].why, KUI_WHY_SSH);
		T_EQ_INT(g[ko].n, 4);
	}
}

static void test_ancestors_never_foreign(void)
{
	/* our hub started from a kterm shell: kterm and its shell must survive "stop everything" */
	KuiProc a[4];
	memset(a, 0, sizeof a);
	a[0] = (KuiProc){ .pid = 600, .ppid = 1, .exe = "/mnt/us/extensions/kterm/bin/kterm" };
	a[1] = (KuiProc){ .pid = 601, .ppid = 600, .exe = "/bin/busybox" };
	a[2] = (KuiProc){ .pid = 602, .ppid = 601, .exe = "/tmp/kui-hub" };
	a[3] = (KuiProc){ .pid = 603, .ppid = 600, .exe = "/mnt/us/extensions/kterm/bin/helper" };
	memcpy(a[1].args, "sh", 3), a[1].args_len = 3;
	kui_proc_classify(a, 4, 602);
	T_EQ_INT(a[0].cls, KUI_PROC_PROTECTED);
	T_EQ_INT(a[0].why, KUI_WHY_SELF);
	T_EQ_INT(a[1].cls, KUI_PROC_PROTECTED);
	T_EQ_INT(a[2].cls, KUI_PROC_OURS);
	T_EQ_INT(a[3].cls, KUI_PROC_FOREIGN); /* a sibling is not an ancestor */
}

static int log_pos(const char *log, const char *line)
{
	const char *p = strstr(log, line);
	return p ? (int) (p - log) : -1;
}

static void test_stop_order(void)
{
	build_tree();
	n = kui_proc_scan(ps, KUI_PROC_MAX, 301);
	KuiProcGroup g[16];
	int k = kui_proc_groups(ps, n, KUI_PROC_FOREIGN, g, 16);
	KuiProcId ids[64];
	int m = 0;
	for (int i = 0; i < k; i++)
		for (int j = 0; j < g[i].n; j++) ids[m++] = g[i].ids[j];
	/* protected and our own pids slipped into the list are ignored */
	ids[m++] = (KuiProcId){ 200, P(200)->start };
	ids[m++] = (KuiProcId){ 301, P(301)->start };
	ids[m++] = (KuiProcId){ 1, P(1)->start };
	ids[m++] = (KuiProcId){ 400, P(400)->start + 1 }; /* stale identity: a reused pid */
	T_EQ_INT(kui_proc_stop(ids, m), 0);
	if (t_failures) { /* show what was sent */
		char c[600];
		snprintf(c, sizeof c, "cat %s/signals.log >&2", root);
		if (system(c) != 0) T_FAIL_("%s", "no signals.log");
	}
	char log[1024], path[300];
	snprintf(path, sizeof path, "%s/signals.log", root);
	T_ASSERT(kui_read_file(path, log, sizeof log) > 0);
	/* programs first; KOReader's launcher ends by itself; a shell without a program gets SIGTERM afterwards */
	T_ASSERT(log_pos(log, "400 TERM") >= 0);
	T_ASSERT(log_pos(log, "501 TERM") >= 0);
	T_ASSERT(log_pos(log, "600 TERM") >= 0);
	T_ASSERT(log_pos(log, "500 ") < 0);
	T_ASSERT(log_pos(log, "510 TERM") > log_pos(log, "501 TERM"));
	T_ASSERT(log_pos(log, "601 TERM") > log_pos(log, "600 TERM"));
	T_ASSERT(log_pos(log, "200 ") < 0 && log_pos(log, "301 ") < 0 && log_pos(log, "KILL") < 0);
	n = kui_proc_scan(ps, KUI_PROC_MAX, 301);
	T_EQ_INT(kui_proc_groups(ps, n, KUI_PROC_FOREIGN, g, 16), 0);
	T_ASSERT(P(200) && P(301) && P(101));
}

static void test_stop_kills_survivor(void)
{
	build_tree();
	put("400/ignore_term", "", 0);
	n = kui_proc_scan(ps, KUI_PROC_MAX, 301);
	KuiProcId id = { 400, P(400)->start };
	T_EQ_INT(kui_proc_stop(&id, 1), 0);
	char log[256], path[300];
	snprintf(path, sizeof path, "%s/signals.log", root);
	T_ASSERT(kui_read_file(path, log, sizeof log) > 0);
	T_EQ_STR(log, "400 TERM\n400 KILL\n");
}

static void test_no_proc(void)
{
	setenv("KUI_PROC_ROOT", "/nonexistent-proc-root", 1);
	T_EQ_INT(kui_proc_scan(ps, KUI_PROC_MAX, 1), 0);
	T_EQ_INT(kui_mem_available_kb(), -1);
	kui_proc_after_stop(); /* nothing off the device */
	setenv("KUI_PROC_ROOT", root, 1);
}

int main(void)
{
	hz = sysconf(_SC_CLK_TCK);
	snprintf(root, sizeof root, "%s/kui-procscan-test.%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int) getpid());
	setenv("KUI_PROC_ROOT", root, 1);
	unsetenv("KUI_ROOT");
	T_RUN(test_classes);
	T_RUN(test_groups);
	T_RUN(test_ancestors_never_foreign);
	T_RUN(test_stop_order);
	T_RUN(test_stop_kills_survivor);
	T_RUN(test_no_proc);
	char cmd[400];
	snprintf(cmd, sizeof cmd, "rm -rf %s", root);
	if (system(cmd) != 0) return 1;
	return T_DONE();
}

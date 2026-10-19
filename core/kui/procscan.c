/* Background programs: /proc scan, classification (SYSTEM / OURS / PROTECTED / FOREIGN) and a safe, staged stop.
 * Rules (first match wins; see kui.h):
 *   1. pid 1 and kernel threads (no exe, empty cmdline): SYSTEM
 *   2. the scanning process and its ancestors: never FOREIGN (a FOREIGN one becomes PROTECTED/SELF)
 *   3. SSH servers (dropbear, sshd, sftp-server — any path, KOReader's and ours included): PROTECTED/SSH,
 *      and so is every descendant of one (logged-in sessions and what they run)
 *   4. kpm: PROTECTED/KPM, with its descendants (a package install in progress)
 *   5. by path — the exe, or for a shell/interpreter (sh, bash, busybox sh, lua, python …) a path in its arguments:
 *      /mnt/us/kindle-apps/, /mnt/us/kindle-apps-staging/, $KUI_ROOT/, /tmp/kui-*, /var/local/kindle-apps/ → OURS;
 *      /var/local/ (kmc, mkk: jailbreak tools) → PROTECTED/JAILBREAK; /mnt/us/ → FOREIGN.
 *      Only interpreters are judged by their arguments: a system binary that merely opens a file on /mnt/us stays SYSTEM.
 *   6. a SYSTEM-path process below a FOREIGN or OURS one takes its class and name (kterm's shell, a game's helper)
 *   7. a shell running a script from /tmp or /var/tmp (not ours) that has a FOREIGN descendant is its launcher: FOREIGN
 *      (KOReader runs from /var/tmp/koreader.sh)
 *   everything else: SYSTEM. Nothing from /mnt/us is needed by the Kindle itself (it is unmounted for USB storage). */
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "kui.h"

const char *kui_proc_root(void)
{
	const char *r = getenv("KUI_PROC_ROOT");
	return r && *r ? r : "/proc";
}

/* signals go to real processes only with the real /proc on Linux */
static int real_procs(void)
{
#ifdef __linux__
	return strcmp(kui_proc_root(), "/proc") == 0;
#else
	return 0;
#endif
}

static int read_small(const char *path, char *buf, int cap)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	int n = 0;
	for (;;) {
		ssize_t k = read(fd, buf + n, (size_t) (cap - 1 - n));
		if (k <= 0) break;
		n += (int) k;
		if (n >= cap - 1) break;
	}
	close(fd);
	buf[n] = '\0';
	return n;
}

static int pexists(const char *fmt, int pid)
{
	char p[300];
	snprintf(p, sizeof p, fmt, kui_proc_root(), pid);
	struct stat st;
	return lstat(p, &st) == 0;
}

/* ---- path rules ---- */

static const char *base(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

/* bounded copy (the source may be another entry of the same array) */
static void cpy(char *d, size_t cap, const char *s)
{
	size_t l = strnlen(s, cap - 1);
	memmove(d, s, l);
	d[l] = '\0';
}

static int starts(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

static int is_shell_name(const char *b)
{
	static const char *const sh[] = { "sh", "ash", "bash", "dash", "hush", "ksh" };
	for (size_t i = 0; i < sizeof sh / sizeof *sh; i++)
		if (!strcmp(b, sh[i])) return 1;
	return 0;
}

static int is_interp_name(const char *b)
{
	return is_shell_name(b) || !strcmp(b, "lua") || !strcmp(b, "luajit") || starts(b, "python") || !strcmp(b, "perl");
}

static int is_ssh_name(const char *b) { return !strcmp(b, "dropbear") || !strcmp(b, "sshd") || !strcmp(b, "sftp-server"); }

static const char *argv_n(const KuiProc *p, int k)
{
	int off = 0;
	for (int i = 0; i < k; i++) {
		int l = (int) strnlen(p->args + off, (size_t) (p->args_len - off));
		off += l + 1;
		if (off >= p->args_len) return NULL;
	}
	return off < p->args_len ? p->args + off : NULL;
}

/* OURS, PROTECTED (jailbreak), FOREIGN or SYSTEM for one path */
static KuiProcClass path_class(const char *path)
{
	const char *root = getenv("KUI_ROOT");
	size_t rl = root ? strlen(root) : 0;
	if (starts(path, "/mnt/us/kindle-apps/") || starts(path, "/mnt/us/kindle-apps-staging/") || starts(path, "/tmp/kui-") || starts(path, "/var/local/kindle-apps/") ||
	    (rl > 1 && strncmp(path, root, rl) == 0 && path[rl] == '/'))
		return KUI_PROC_OURS;
	if (starts(path, "/var/local/")) return KUI_PROC_PROTECTED;
	if (starts(path, "/mnt/us/")) return KUI_PROC_FOREIGN;
	return KUI_PROC_SYSTEM;
}

/* "/mnt/us/extensions/gambatte-k2/x" → "gambatte-k2", "/mnt/us/koreader/luajit" → "KOReader", "/mnt/us/usbnet/bin/sshd"
 * → "usbnet", other paths → the file name */
static void name_from_path(const char *path, char *out, int cap)
{
	char comp[64];
	if (starts(path, "/mnt/us/")) {
		const char *s = path + 8;
		if (starts(s, "extensions/")) s += 11;
		int n = (int) strcspn(s, "/");
		if (s[n] == '/' && n > 0) {
			snprintf(comp, sizeof comp, "%.*s", n, s);
			if (!strcmp(comp, "koreader")) snprintf(out, (size_t) cap, "KOReader");
			else cpy(out, (size_t) cap, comp);
			return;
		}
	}
	snprintf(out, (size_t) cap, "%.47s", base(path));
	out[strcspn(out, " ")] = '\0';
}

/* for an interpreter: the most specific class of the absolute paths in its arguments (also inside "sh -c '…'") */
static KuiProcClass args_class(const KuiProc *p, char *path_out, int cap)
{
	KuiProcClass best = KUI_PROC_SYSTEM;
	static const int rank[] = { [KUI_PROC_SYSTEM] = 0, [KUI_PROC_FOREIGN] = 1, [KUI_PROC_PROTECTED] = 2, [KUI_PROC_OURS] = 3 };
	for (int k = 1;; k++) {
		const char *a = argv_n(p, k);
		if (!a) break;
		for (const char *t = a; *t;) {
			if (*t == '/' && (t == a || strchr(" \t;&|'\"=(", t[-1]))) {
				char tok[160];
				int n = (int) strcspn(t, " \t;&|'\")");
				snprintf(tok, sizeof tok, "%.*s", n, t);
				KuiProcClass c = path_class(tok);
				if (rank[c] > rank[best]) {
					best = c;
					if (path_out) snprintf(path_out, (size_t) cap, "%s", tok);
				}
				t += n ? n : 1;
			} else {
				t++;
			}
		}
	}
	return best;
}

/* shell running a script from /tmp or /var/tmp that is not ours */
static int tmp_script_shell(const KuiProc *p)
{
	if (!p->shell) return 0;
	for (int k = 1;; k++) {
		const char *a = argv_n(p, k);
		if (!a) return 0;
		if (*a == '-') continue;
		return (starts(a, "/tmp/") || starts(a, "/var/tmp/")) && !starts(a, "/tmp/kui-");
	}
}

/* ---- classification ---- */

static int find(const KuiProc *ps, int n, int pid)
{
	for (int i = 0; i < n; i++)
		if (ps[i].pid == pid) return i;
	return -1;
}

enum { B_SYSTEM, B_OURS, B_FOREIGN, B_JAIL, B_SSH, B_KPM, B_KTHREAD };

void kui_proc_classify(KuiProc *ps, int n, int self_pid)
{
	unsigned char *b = calloc((size_t) (n ? n : 1), 1);
	if (!b) return;
	for (int i = 0; i < n; i++) {
		KuiProc *p = &ps[i];
		const char *a0 = argv_n(p, 0);
		const char *eb = base(p->exe), *ab = a0 ? base(a0) : "";
		p->kthread = !p->exe[0] && p->args_len == 0;
		p->shell = is_shell_name(eb) || (!strcmp(eb, "busybox") && is_shell_name(ab));
		int interp = is_interp_name(eb) || p->shell;
		p->third = 0;
		p->cls = KUI_PROC_SYSTEM;
		p->why = KUI_WHY_NONE;
		p->gui = 0;
		char path[160] = "";
		KuiProcClass pc = path_class(p->exe);
		snprintf(path, sizeof path, "%s", p->exe);
		if (pc == KUI_PROC_SYSTEM && interp) pc = args_class(p, path, sizeof path);
		p->third = pc == KUI_PROC_FOREIGN || pc == KUI_PROC_PROTECTED;
		if (pc == KUI_PROC_OURS) snprintf(p->name, sizeof p->name, "Kindle Apps");
		else if (pc == KUI_PROC_SYSTEM) cpy(p->name, sizeof p->name, p->exe[0] ? base(p->exe) : p->comm);
		else name_from_path(path, p->name, sizeof p->name);
		if (p->pid == 1 || p->kthread) b[i] = B_KTHREAD;
		else if (is_ssh_name(eb) || is_ssh_name(ab)) b[i] = B_SSH;
		else if (!strcmp(eb, "kpm") || !strcmp(ab, "kpm")) b[i] = B_KPM;
		else if (pc == KUI_PROC_OURS) b[i] = B_OURS;
		else if (pc == KUI_PROC_PROTECTED) b[i] = B_JAIL;
		else if (pc == KUI_PROC_FOREIGN) b[i] = B_FOREIGN;
		else b[i] = B_SYSTEM;
	}
	for (int i = 0; i < n; i++) {
		KuiProc *p = &ps[i];
		switch (b[i]) {
		case B_KTHREAD: p->cls = KUI_PROC_SYSTEM; continue;
		case B_SSH: p->cls = KUI_PROC_PROTECTED, p->why = KUI_WHY_SSH; continue;
		case B_KPM: p->cls = KUI_PROC_PROTECTED, p->why = KUI_WHY_KPM; continue;
		default: break;
		}
		/* ancestors: an SSH server above protects; the nearest FOREIGN/OURS one is inherited by a SYSTEM-path process */
		int guard = 0, inherit = -1;
		for (int j = find(ps, n, p->ppid), depth = 0; j >= 0 && depth < 64; j = find(ps, n, ps[j].ppid), depth++) {
			if (b[j] == B_KTHREAD) break;
			if (b[j] == B_SSH || b[j] == B_KPM) {
				guard = b[j];
				cpy(p->name, sizeof p->name, ps[j].name); /* listed with its server */
				break;
			}
			if (inherit < 0 && (b[j] == B_FOREIGN || b[j] == B_OURS)) inherit = j;
		}
		if (guard) { /* an SSH session, or a package being installed */
			p->cls = KUI_PROC_PROTECTED, p->why = guard == B_SSH ? KUI_WHY_SSH : KUI_WHY_KPM;
			continue;
		}
		if (b[i] == B_OURS) p->cls = KUI_PROC_OURS;
		else if (b[i] == B_JAIL) p->cls = KUI_PROC_PROTECTED, p->why = KUI_WHY_JAILBREAK;
		else if (b[i] == B_FOREIGN) p->cls = KUI_PROC_FOREIGN;
		else if (inherit >= 0) {
			p->cls = b[inherit] == B_FOREIGN ? KUI_PROC_FOREIGN : KUI_PROC_OURS;
			if (p->cls == KUI_PROC_FOREIGN) {
				cpy(p->name, sizeof p->name, ps[inherit].name);
				p->third = 1;
			}
		}
	}
	/* launcher shells in /tmp or /var/tmp above a FOREIGN program */
	for (int i = 0; i < n; i++) {
		if (ps[i].cls != KUI_PROC_FOREIGN || b[i] != B_FOREIGN) continue;
		for (int j = find(ps, n, ps[i].ppid), depth = 0; j >= 0 && depth < 8; j = find(ps, n, ps[j].ppid), depth++) {
			if (ps[j].cls != KUI_PROC_SYSTEM || !tmp_script_shell(&ps[j])) break;
			ps[j].cls = KUI_PROC_FOREIGN;
			ps[j].third = 1;
			cpy(ps[j].name, sizeof ps[j].name, ps[i].name);
		}
	}
	/* never stop ourselves or what started us */
	for (int j = find(ps, n, self_pid), depth = 0; j >= 0 && depth < 64; j = find(ps, n, ps[j].ppid), depth++) {
		if (ps[j].cls == KUI_PROC_FOREIGN) ps[j].cls = KUI_PROC_PROTECTED, ps[j].why = KUI_WHY_SELF;
		if (ps[j].pid <= 1) break;
	}
	free(b);
}

/* ---- reading /proc ---- */

/* fake roots: <pid>/dead, or <pid>/exits_after naming a gone process */
static int fake_gone(int pid, int depth)
{
	if (real_procs()) return 0;
	if (pexists("%s/%d/dead", pid)) return 1;
	char p[300], buf[32];
	snprintf(p, sizeof p, "%s/%d/exits_after", kui_proc_root(), pid);
	if (depth < 8 && read_small(p, buf, sizeof buf) > 0) {
		int q = atoi(buf);
		if (q > 0 && (!pexists("%s/%d/stat", q) || fake_gone(q, depth + 1))) return 1;
	}
	return 0;
}

static int read_stat(int pid, KuiProc *p)
{
	char path[300], buf[1024];
	snprintf(path, sizeof path, "%s/%d/stat", kui_proc_root(), pid);
	if (read_small(path, buf, sizeof buf) <= 0) return -1;
	char *l = strchr(buf, '('), *r = strrchr(buf, ')');
	if (!l || !r || r < l) return -1;
	snprintf(p->comm, sizeof p->comm, "%.*s", (int) (r - l - 1), l + 1);
	/* after ')': state(3) ppid(4) … starttime(22) */
	char *s = r + 1;
	int field = 2;
	unsigned long long start = 0;
	char state = '?';
	int ppid = 0;
	for (char *t = strtok(s, " "); t; t = strtok(NULL, " ")) {
		field++;
		if (field == 3) state = t[0];
		else if (field == 4) ppid = atoi(t);
		else if (field == 22) {
			start = strtoull(t, NULL, 10);
			break;
		}
	}
	p->pid = pid;
	p->state = state;
	p->ppid = ppid;
	p->start = start;
	return 0;
}

static double uptime_s(void)
{
	char path[300], buf[64];
	snprintf(path, sizeof path, "%s/uptime", kui_proc_root());
	return read_small(path, buf, sizeof buf) > 0 ? atof(buf) : -1;
}

static int read_proc(int pid, KuiProc *p, double up, long hz)
{
	memset(p, 0, sizeof *p);
	if (read_stat(pid, p) != 0 || fake_gone(pid, 0)) return -1;
	char path[300];
	snprintf(path, sizeof path, "%s/%d/cmdline", kui_proc_root(), pid);
	int n = read_small(path, p->args, sizeof p->args);
	if (n < 0) n = 0;
	while (n > 0 && p->args[n - 1] == '\0') n--; /* trailing NULs */
	p->args_len = n ? n + 1 : 0;
	snprintf(path, sizeof path, "%s/%d/exe", kui_proc_root(), pid);
	ssize_t l = readlink(path, p->exe, sizeof p->exe - 1);
	p->exe[l > 0 ? l : 0] = '\0';
	char *del = strstr(p->exe, " (deleted)");
	if (del && del[10] == '\0') *del = '\0';
	if (!p->exe[0] && p->args_len && p->args[0] == '/') cpy(p->exe, sizeof p->exe, p->args);
	p->rss_kb = -1;
	char st[2048];
	snprintf(path, sizeof path, "%s/%d/status", kui_proc_root(), pid);
	if (read_small(path, st, sizeof st) > 0) {
		char *v = strstr(st, "VmRSS:");
		if (v) p->rss_kb = atol(v + 6);
	}
	p->age_s = up >= 0 && hz > 0 ? (long) (up - (double) p->start / (double) hz) : -1;
	if (p->age_s < -1) p->age_s = 0;
	return 0;
}

/* does a FOREIGN program use the screen or the touchscreen? */
static int uses_screen(const KuiProc *p)
{
	if (!strcmp(p->name, "KOReader") || strstr(p->exe, "gambatte") || !strcmp(base(p->exe), "kterm")) return 1;
	char path[300], env[8192];
	snprintf(path, sizeof path, "%s/%d/environ", kui_proc_root(), p->pid);
	int n = read_small(path, env, sizeof env);
	for (int off = 0; off < n; off += (int) strlen(env + off) + 1)
		if (starts(env + off, "DISPLAY=")) return 1;
	snprintf(path, sizeof path, "%s/%d/fd", kui_proc_root(), p->pid);
	DIR *d = opendir(path);
	if (!d) return 0;
	int hit = 0;
	struct dirent *de;
	for (int k = 0; !hit && k < 256 && (de = readdir(d)); k++) {
		if (de->d_name[0] == '.') continue;
		char fp[600], tgt[128];
		snprintf(fp, sizeof fp, "%s/%s", path, de->d_name);
		ssize_t l = readlink(fp, tgt, sizeof tgt - 1);
		if (l <= 0) continue;
		tgt[l] = '\0';
		hit = starts(tgt, "/dev/fb") || starts(tgt, "/dev/input/");
	}
	closedir(d);
	return hit;
}

int kui_proc_scan(KuiProc *out, int max, int self_pid)
{
	DIR *d = opendir(kui_proc_root());
	if (!d) return 0;
	double up = uptime_s();
	long hz = sysconf(_SC_CLK_TCK);
	int n = 0;
	struct dirent *de;
	while (n < max && (de = readdir(d))) {
		if (!isdigit((unsigned char) de->d_name[0])) continue;
		int pid = atoi(de->d_name);
		if (pid <= 0 || read_proc(pid, &out[n], up, hz) != 0) continue;
		if (out[n].state == 'Z' || out[n].state == 'X') continue; /* already exited */
		n++;
	}
	closedir(d);
	kui_proc_classify(out, n, self_pid);
	for (int i = 0; i < n; i++)
		if (out[i].cls == KUI_PROC_FOREIGN && !out[i].shell) out[i].gui = (unsigned char) uses_screen(&out[i]);
	return n;
}

int kui_proc_groups(const KuiProc *ps, int n, KuiProcClass cls, KuiProcGroup *out, int max)
{
	int g = 0;
	for (int i = 0; i < n; i++) {
		const KuiProc *p = &ps[i];
		if (p->cls != cls || (cls == KUI_PROC_PROTECTED && !p->third && p->why != KUI_WHY_SSH)) continue;
		if (cls == KUI_PROC_PROTECTED && p->why == KUI_WHY_SELF) continue;
		int k = 0;
		while (k < g && (strcmp(out[k].name, p->name) || out[k].why != p->why)) k++;
		if (k == g) {
			if (g == max) continue;
			memset(&out[g], 0, sizeof out[g]);
			snprintf(out[g].name, sizeof out[g].name, "%s", p->name);
			out[g].cls = cls;
			out[g].why = p->why;
			out[g].age_s = -1;
			g++;
		}
		KuiProcGroup *gr = &out[k];
		if (gr->n < 16) gr->ids[gr->n] = (KuiProcId){ p->pid, p->start };
		gr->n++;
		gr->gui |= p->gui;
		if (p->rss_kb > 0) gr->rss_kb += p->rss_kb;
		if (p->age_s > gr->age_s) gr->age_s = p->age_s;
	}
	return g;
}

/* ---- stopping ---- */

static void send_sig(int pid, int sig)
{
	if (pid <= 1 || pid == (int) getpid()) return;
	if (real_procs()) {
		kill(pid, sig);
		return;
	}
	/* fake root: log, and let the "process" end */
	char path[300];
	snprintf(path, sizeof path, "%s/signals.log", kui_proc_root());
	FILE *f = fopen(path, "a");
	if (f) {
		fprintf(f, "%d %s\n", pid, sig == SIGKILL ? "KILL" : sig == SIGTERM ? "TERM" : "OTHER");
		fclose(f);
	}
	if (sig == SIGTERM && pexists("%s/%d/ignore_term", pid)) return;
	snprintf(path, sizeof path, "%s/%d/dead", kui_proc_root(), pid);
	f = fopen(path, "w");
	if (f) fclose(f);
}

static void nap_ms(int ms)
{
	struct timespec ts = { ms / 1000, (long) (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

#define MAX_TARGETS 64
typedef struct { KuiProcId id; int shell, alive, termed, seen; } Target; /* seen: was FOREIGN in a scan */

/* rescans; marks which targets live (same pid and start time, still FOREIGN) and adds new FOREIGN descendants */
static void refresh(KuiProc *ps, Target *t, int *nt)
{
	int n = kui_proc_scan(ps, KUI_PROC_MAX, (int) getpid());
	for (int k = 0; k < *nt; k++) t[k].alive = 0;
	for (int i = 0; i < n; i++) {
		/* a child whose FOREIGN parent already ended is SYSTEM by now (it moved to init), but it is the same process */
		if (ps[i].cls != KUI_PROC_FOREIGN && ps[i].cls != KUI_PROC_SYSTEM) continue;
		int k = 0;
		while (k < *nt && !(t[k].id.pid == ps[i].pid && t[k].id.start == ps[i].start)) k++;
		if (k < *nt && (ps[i].cls == KUI_PROC_FOREIGN || t[k].seen)) {
			t[k].alive = t[k].seen = 1;
			t[k].shell = ps[i].shell;
		}
	}
	/* descendants (also new ones a launcher just started) */
	for (int added = 1; added;) {
		added = 0;
		for (int i = 0; i < n && *nt < MAX_TARGETS; i++) {
			if (ps[i].cls != KUI_PROC_FOREIGN) continue;
			int known = 0, parent = 0;
			for (int k = 0; k < *nt; k++) {
				if (t[k].id.pid == ps[i].pid && t[k].id.start == ps[i].start) known = 1;
				if (t[k].alive && t[k].id.pid == ps[i].ppid) parent = 1;
			}
			if (known || !parent) continue;
			t[*nt] = (Target){ { ps[i].pid, ps[i].start }, ps[i].shell, 1, 0, 1 };
			(*nt)++;
			added = 1;
		}
	}
}

static int count(const Target *t, int nt, int shells_too)
{
	int c = 0;
	for (int k = 0; k < nt; k++) c += t[k].alive && (shells_too || !t[k].shell);
	return c;
}

static void term_programs(Target *t, int nt, int shells_too)
{
	for (int k = 0; k < nt; k++)
		if (t[k].alive && !t[k].termed && (shells_too || !t[k].shell)) {
			send_sig(t[k].id.pid, SIGTERM);
			t[k].termed = 1;
		}
}

/* polls every 100 ms (5 ms off the device) until fewer remain or the time is up */
static void wait_for(KuiProc *ps, Target *t, int *nt, int ms, int shells_too, int term_new)
{
	int step = real_procs() ? 100 : 5, steps = real_procs() ? ms / 100 : ms / 100 + 1;
	for (int i = 0; i < steps && count(t, *nt, shells_too) > 0; i++) {
		nap_ms(step);
		refresh(ps, t, nt);
		if (term_new) term_programs(t, *nt, 0);
	}
}

int kui_proc_stop(const KuiProcId *ids, int n)
{
	KuiProc *ps = malloc(sizeof *ps * KUI_PROC_MAX);
	if (!ps) return n;
	Target t[MAX_TARGETS];
	int nt = 0;
	for (int i = 0; i < n && nt < MAX_TARGETS; i++) t[nt++] = (Target){ ids[i], 0, 0, 0, 0 };
	refresh(ps, t, &nt);
	/* 1. the programs themselves; their launchers notice and clean up */
	int programs = count(t, nt, 0);
	term_programs(t, nt, 0);
	if (programs) wait_for(ps, t, &nt, 3000, 0, 1);
	/* 2. launcher shells get time to finish (KOReader's restores the Kindle UI) */
	if (programs && count(t, nt, 1)) wait_for(ps, t, &nt, 5000, 1, 1);
	/* 3. whatever is left (shells, programs that ignored the first SIGTERM get no second one) */
	if (count(t, nt, 1)) {
		term_programs(t, nt, 1);
		wait_for(ps, t, &nt, 2000, 1, 0);
	}
	/* 4. SIGKILL survivors */
	if (count(t, nt, 1)) {
		for (int k = 0; k < nt; k++)
			if (t[k].alive) send_sig(t[k].id.pid, SIGKILL);
		wait_for(ps, t, &nt, 1000, 1, 0);
	}
	int left = count(t, nt, 1);
	free(ps);
	return left;
}

void kui_proc_after_stop(void)
{
	if (!real_procs()) return;
	KuiProc *ps = malloc(sizeof *ps * KUI_PROC_MAX);
	if (!ps) return;
	int n = kui_proc_scan(ps, KUI_PROC_MAX, (int) getpid());
	for (int i = 0; i < n; i++) /* a launcher that was killed may have left the framework frozen */
		if (ps[i].state == 'T' && ps[i].cls == KUI_PROC_SYSTEM && (!strcmp(ps[i].comm, "cvm") || !strcmp(ps[i].comm, "KPPMainApp")))
			kill(ps[i].pid, SIGCONT);
	free(ps);
	struct stat st;
	if (stat("/tmp/kui-run.lock", &st) != 0 || !S_ISDIR(st.st_mode)) return; /* kui-run.sh is not in front */
	/* the same pause as kui-run.sh: a launcher's clean-up may have brought the Kindle UI back; kui-run.sh restores it
	 * all when our app exits */
	if (system("lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>/dev/null; killall -STOP awesome 2>/dev/null; "
		   "if [ -f /etc/upstart/statusbar.conf ] && status statusbar 2>/dev/null | grep -q running; then "
		   "stop statusbar >/dev/null 2>&1; fi; killall -STOP volumd 2>/dev/null; "
		   "if [ -f /etc/upstart/lab126_gui.conf ] && status lab126_gui 2>/dev/null | grep -q 'stop/'; then "
		   "touch /tmp/kui-run.lock/start_gui; fi") != 0) {
		/* best effort */
	}
}

long kui_mem_available_kb(void)
{
	char path[300], buf[4096];
	snprintf(path, sizeof path, "%s/meminfo", kui_proc_root());
	if (read_small(path, buf, sizeof buf) <= 0) return -1;
	char *v = strstr(buf, "MemAvailable:");
	if (v) return atol(v + 13);
	long f = -1, b = 0, c = 0; /* old kernels: free + buffers + cached */
	if ((v = strstr(buf, "MemFree:"))) f = atol(v + 8);
	if ((v = strstr(buf, "Buffers:"))) b = atol(v + 8);
	if ((v = strstr(buf, "\nCached:"))) c = atol(v + 8);
	return f < 0 ? -1 : f + b + c;
}

/* lipc property helpers (lipc-get-prop / lipc-set-prop) and a crash-safe "keep awake" switch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"

static void tool(char *out, int cap, const char *name)
{
	const char *dir = getenv("KUI_LIPC_DIR");
	if (dir && *dir) snprintf(out, (size_t) cap, "%s/%s", dir, name);
	else snprintf(out, (size_t) cap, "%s", name);
}

/* publisher/prop come from our own code: only [A-Za-z0-9._] are accepted, so they are safe in a command line */
static int safe(const char *s)
{
	for (; *s; s++)
		if (!(*s == '.' || *s == '_' || (*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return 0;
	return 1;
}

int kui_lipc_get_int(const char *pub, const char *prop, int def)
{
	char t[300], cmd[512], buf[32];
	if (!safe(pub) || !safe(prop)) return def;
	tool(t, sizeof t, "lipc-get-prop");
	snprintf(cmd, sizeof cmd, "'%s' %s %s 2>/dev/null", t, pub, prop);
	FILE *p = popen(cmd, "r");
	if (!p) return def;
	int v = def;
	char *end;
	if (fgets(buf, sizeof buf, p)) {
		long l = strtol(buf, &end, 10);
		if (end != buf) v = (int) l;
	}
	if (pclose(p) != 0) v = def;
	return v;
}

void kui_lipc_set_int(const char *pub, const char *prop, int v)
{
	char t[300], cmd[512];
	if (!safe(pub) || !safe(prop)) return;
	tool(t, sizeof t, "lipc-set-prop");
	snprintf(cmd, sizeof cmd, "'%s' %s %s %d >/dev/null 2>&1", t, pub, prop, v);
	if (system(cmd) != 0) fprintf(stderr, "failed: %s\n", cmd);
}

static void restore_path(char *out, int cap)
{
	const char *d = getenv("KUI_STATE_DIR");
	if (!d || !*d) d = "/var/local/kindle-apps";
	mkdir(d, 0700);
	snprintf(out, (size_t) cap, "%s/prevent_ss.restore", d);
}

/* id of the current boot: a restore file from an earlier boot (battery died, hard reset) is stale */
static void boot_id(char *out, int cap)
{
	const char *e = getenv("KUI_BOOT_ID");
	out[0] = '\0';
	if (e) {
		snprintf(out, (size_t) cap, "%s", e);
		return;
	}
	if (kui_read_file("/proc/sys/kernel/random/boot_id", out, cap) > 0) out[strcspn(out, "\n")] = '\0';
}

/* saved value (0/1) from this boot's restore file, or -1; a stale or malformed file is removed */
static int saved_value(const char *path)
{
	char buf[96], id[64];
	if (kui_read_file(path, buf, sizeof buf) <= 0) return -1;
	boot_id(id, sizeof id);
	buf[strcspn(buf, "\n")] = '\0';
	if ((buf[0] != '0' && buf[0] != '1') || buf[1] != ' ' || strcmp(buf + 2, id) != 0) {
		unlink(path);
		return -1;
	}
	return buf[0] - '0';
}

void kui_prevent_sleep(int on)
{
	char path[300], buf[96], id[64];
	restore_path(path, sizeof path);
	int saved = saved_value(path);
	if (on) {
		if (saved < 0) { /* keep the oldest value of this boot */
			int cur = kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1);
			if (cur < 0) return; /* cannot read the user's setting: leave it alone */
			boot_id(id, sizeof id);
			snprintf(buf, sizeof buf, "%d %s\n", cur ? 1 : 0, id);
			if (kui_write_atomic(path, buf, (int) strlen(buf)) != 0) return; /* no way back: do not keep awake */
		}
		kui_lipc_set_int("com.lab126.powerd", "preventScreenSaver", 1);
	} else {
		if (saved < 0) return; /* nothing of ours to undo */
		kui_lipc_set_int("com.lab126.powerd", "preventScreenSaver", saved);
		unlink(path);
	}
}

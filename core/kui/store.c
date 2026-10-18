#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"

static void mkdir_p(const char *path)
{
	char tmp[256];
	snprintf(tmp, sizeof tmp, "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

const char *kui_data_dir(const char *app)
{
	static char dir[256];
	const char *root = getenv("KUI_ROOT");
	snprintf(dir, sizeof dir, "%s/data/%s", root && *root ? root : "/mnt/us/kindle-apps", app);
	mkdir_p(dir);
	return dir;
}

int kui_write_atomic(const char *path, const void *buf, int len)
{
	char tmp[300];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -1;
	const char *p = buf;
	int left = len;
	while (left > 0) {
		ssize_t w = write(fd, p, (size_t) left);
		if (w < 0) {
			if (errno == EINTR) continue;
			close(fd);
			unlink(tmp);
			return -1;
		}
		p += w;
		left -= (int) w;
	}
	fsync(fd);
	close(fd);
	return rename(tmp, path) == 0 ? 0 : -1;
}

int kui_read_file(const char *path, char *buf, int cap)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	int n = (int) fread(buf, 1, (size_t) (cap - 1), f);
	fclose(f);
	buf[n] = '\0';
	return n;
}

/* finds "key=" at a line start; returns pointer to value or NULL */
static const char *cfg_find(const char *text, const char *key)
{
	size_t kl = strlen(key);
	for (const char *line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
		if (strncmp(line, key, kl) == 0 && line[kl] == '=') return line + kl + 1;
	return NULL;
}

int kui_cfg_get_int(const char *path, const char *key, int def)
{
	char buf[4096];
	if (kui_read_file(path, buf, sizeof buf) < 0) return def;
	const char *v = cfg_find(buf, key);
	if (!v) return def;
	char *end;
	long n = strtol(v, &end, 10);
	return end == v ? def : (int) n;
}

int kui_cfg_set_int(const char *path, const char *key, int val)
{
	char in[4096], out[4200];
	int o = 0, found = 0;
	size_t kl = strlen(key);
	if (kui_read_file(path, in, sizeof in) < 0) in[0] = '\0';
	for (char *line = in; *line;) {
		char *nl = strchr(line, '\n');
		int len = nl ? (int) (nl - line) : (int) strlen(line);
		if (strncmp(line, key, kl) == 0 && line[kl] == '=') {
			o += snprintf(out + o, sizeof out - (size_t) o, "%s=%d\n", key, val);
			found = 1;
		} else if (len > 0) {
			o += snprintf(out + o, sizeof out - (size_t) o, "%.*s\n", len, line);
		}
		if (!nl) break;
		line = nl + 1;
	}
	if (!found) o += snprintf(out + o, sizeof out - (size_t) o, "%s=%d\n", key, val);
	return kui_write_atomic(path, out, o);
}

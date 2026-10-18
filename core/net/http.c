/* net_get: runs curl as a child process and reads its stdout with a hard deadline. */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "net.h"

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static const char *curl_path(void)
{
	const char *curl = getenv("NET_CURL");
	return curl && *curl ? curl : "/usr/bin/curl";
}

int net_get(const char *url, char **body, int *len, int timeout_s)
{
	*body = NULL;
	*len = 0;
	const char *curl = curl_path();
	char tmax[16];
	snprintf(tmax, sizeof tmax, "%d", timeout_s);
	int fds[2];
	if (pipe(fds) != 0) return -1;
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (pid == 0) {
		dup2(fds[1], 1);
		int dn = open("/dev/null", O_RDWR);
		if (dn >= 0) {
			dup2(dn, 0);
			dup2(dn, 2);
		}
		close(fds[0]);
		close(fds[1]);
		execl(curl, curl, "-fsSL", "--max-time", tmax, "--connect-timeout", "10", "--proto", "=http,https",
		      "--proto-redir", "=http,https", "-A", "Mozilla/5.0 (Kindle; kindle-apps)", url, (char *) NULL);
		_exit(127);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	long deadline = now_ms() + (timeout_s + 2) * 1000L;
	int cap = 65536, n = 0, ok = 1;
	char *buf = malloc((size_t) cap + 1);
	if (!buf) ok = 0;
	while (ok) {
		long left = deadline - now_ms();
		if (left <= 0) { ok = 0; break; }
		struct pollfd pf = { fds[0], POLLIN, 0 };
		int r = poll(&pf, 1, (int) left);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) { ok = 0; break; }
		if (n == cap) {
			if (cap >= NET_MAX_BODY) { ok = 0; break; }
			cap *= 2;
			char *nb = realloc(buf, (size_t) cap + 1);
			if (!nb) { ok = 0; break; }
			buf = nb;
		}
		ssize_t k = read(fds[0], buf + n, (size_t) (cap - n));
		if (k < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (k < 0) { ok = 0; break; }
		if (k == 0) break; /* EOF */
		n += (int) k;
		if (n > NET_MAX_BODY) { ok = 0; break; }
	}
	close(fds[0]);
	if (!ok) kill(pid, SIGKILL);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	if (!ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		free(buf);
		return -1;
	}
	buf[n] = '\0';
	*body = buf;
	*len = n;
	return 0;
}

int net_download(const char *url, const char *path, long max_bytes, int timeout_s)
{
	const char *curl = curl_path();
	char tmax[16], fmax[24], part[1024];
	snprintf(tmax, sizeof tmax, "%d", timeout_s);
	snprintf(fmax, sizeof fmax, "%ld", max_bytes);
	if (snprintf(part, sizeof part, "%s.part", path) >= (int) sizeof part) return -1;
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		int dn = open("/dev/null", O_RDWR);
		if (dn >= 0) {
			dup2(dn, 0);
			dup2(dn, 1);
			dup2(dn, 2);
		}
		execl(curl, curl, "-fsSL", "--max-time", tmax, "--max-filesize", fmax, "--connect-timeout", "10", "--proto",
		      "=http,https", "--proto-redir", "=http,https", "-A", "Mozilla/5.0 (Kindle; kindle-apps)", "-o", part, url,
		      (char *) NULL);
		_exit(127);
	}
	/* curl's own --max-time normally ends it; the deadline is the backstop for a stuck child */
	long deadline = now_ms() + (timeout_s + 2) * 1000L;
	int status = 0, done = 0;
	for (;;) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) { done = 1; break; }
		if (r < 0 && errno != EINTR) break;
		if (now_ms() >= deadline) break;
		struct timespec ts = { 0, 50 * 1000000L };
		nanosleep(&ts, NULL);
	}
	if (!done) {
		kill(pid, SIGKILL);
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	}
	struct stat st;
	int ok = done && WIFEXITED(status) && WEXITSTATUS(status) == 0 && stat(part, &st) == 0 && S_ISREG(st.st_mode) &&
		 st.st_size > 0 && (long long) st.st_size <= (long long) max_bytes;
	if (ok) {
		int fd = open(part, O_RDONLY);
		ok = fd >= 0 && fsync(fd) == 0;
		if (fd >= 0) close(fd);
	}
	if (ok && rename(part, path) != 0) ok = 0;
	if (!ok) {
		unlink(part);
		return -1;
	}
	/* make the rename durable too (best effort) */
	char dir[1024];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		slash[slash == dir ? 1 : 0] = '\0';
		int dfd = open(dir, O_RDONLY);
		if (dfd >= 0) {
			fsync(dfd);
			close(dfd);
		}
	}
	return 0;
}

void net_urlencode(const char *in, char *out, int cap)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *p = (const unsigned char *) in;
	int o = 0;
	while (*p) {
		/* one whole UTF-8 sequence at a time, so truncation never splits a character */
		int seq = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1, need = 0;
		for (int k = 0; k < seq; k++) {
			if (!p[k]) { seq = k; break; }
			int keep = p[k] < 0x80 && (isalnum(p[k]) || strchr("-_.~", p[k]));
			need += keep ? 1 : 3;
		}
		if (seq == 0 || o + need >= cap) break;
		for (int k = 0; k < seq; k++, p++) {
			if (*p < 0x80 && (isalnum(*p) || strchr("-_.~", *p))) {
				out[o++] = (char) *p;
			} else {
				out[o++] = '%';
				out[o++] = hex[*p >> 4];
				out[o++] = hex[*p & 15];
			}
		}
	}
	if (cap > 0) out[o] = '\0';
}

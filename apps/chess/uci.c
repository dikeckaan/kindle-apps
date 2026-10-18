#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "uci.h"

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int send_line(Uci *u, const char *s)
{
	size_t n = strlen(s);
	if (write(u->to_fd, s, n) != (ssize_t) n || write(u->to_fd, "\n", 1) != 1) return -1;
	return 0;
}

/* Reads available output; returns 1 when a complete line starting with `prefix` was found (copied into line),
 * 0 when nothing yet, -1 on EOF/error. */
static int read_line_with(Uci *u, const char *prefix, char *line, int cap)
{
	for (;;) {
		char *nl;
		while ((nl = memchr(u->buf, '\n', (size_t) u->len))) {
			int ll = (int) (nl - u->buf);
			int match = strncmp(u->buf, prefix, strlen(prefix)) == 0;
			if (match && line) {
				int k = ll < cap - 1 ? ll : cap - 1;
				memcpy(line, u->buf, (size_t) k);
				line[k] = '\0';
			}
			memmove(u->buf, nl + 1, (size_t) (u->len - ll - 1));
			u->len -= ll + 1;
			if (match) return 1;
		}
		if (u->len == (int) sizeof u->buf) u->len = 0; /* overlong line: drop it */
		ssize_t r = read(u->from_fd, u->buf + u->len, sizeof u->buf - (size_t) u->len);
		if (r > 0) {
			u->len += (int) r;
			continue;
		}
		if (r == 0) return -1;
		return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
	}
}

static int wait_for(Uci *u, const char *prefix, char *line, int cap, int timeout_ms)
{
	long end = now_ms() + timeout_ms;
	for (;;) {
		int r = read_line_with(u, prefix, line, cap);
		if (r != 0) return r;
		long left = end - now_ms();
		if (left <= 0) return 0;
		struct pollfd pf = { u->from_fd, POLLIN, 0 };
		poll(&pf, 1, (int) left);
	}
}

int uci_start(Uci *u, const char *path)
{
	int in[2], out[2];
	memset(u, 0, sizeof *u);
	u->pid = -1;
	signal(SIGPIPE, SIG_IGN); /* writing to a dead engine must fail, not kill us */
	/* no access(X_OK) pre-check: Kindle's /mnt/us (fuse) reports EACCES although exec works;
	 * a missing/unrunnable engine is detected when the child exits before "uciok". */
	if (pipe(in) < 0) return -1;
	if (pipe(out) < 0) {
		close(in[0]);
		close(in[1]);
		return -1;
	}
	u->pid = fork();
	if (u->pid == 0) {
		dup2(in[0], 0);
		dup2(out[1], 1);
		close(in[0]);
		close(in[1]);
		close(out[0]);
		close(out[1]);
		execl(path, path, (char *) NULL);
		_exit(127);
	}
	close(in[0]);
	close(out[1]);
	u->to_fd = in[1];
	u->from_fd = out[0];
	if (u->pid < 0) {
		close(u->to_fd);
		close(u->from_fd);
		return -1;
	}
	fcntl(u->from_fd, F_SETFL, O_NONBLOCK);
	if (send_line(u, "uci") != 0 || wait_for(u, "uciok", NULL, 0, 5000) != 1) {
		uci_close(u);
		return -1;
	}
	return 0;
}

int uci_configure(Uci *u, const Level *lv)
{
	char cmd[96];
	send_line(u, "setoption name Hash value 16");
	snprintf(cmd, sizeof cmd, "setoption name UCI_LimitStrength value %s", lv->limit_strength ? "true" : "false");
	send_line(u, cmd);
	if (lv->limit_strength) {
		snprintf(cmd, sizeof cmd, "setoption name UCI_Elo value %d", lv->elo);
		send_line(u, cmd);
	}
	snprintf(cmd, sizeof cmd, "setoption name Skill Level value %d", lv->skill);
	send_line(u, cmd);
	if (send_line(u, "isready") != 0) return -1;
	return wait_for(u, "readyok", NULL, 0, 5000) == 1 ? 0 : -1;
}

int uci_go(Uci *u, const char *position_cmd, const Level *lv)
{
	char cmd[48];
	if (lv->depth > 0) snprintf(cmd, sizeof cmd, "go depth %d", lv->depth);
	else snprintf(cmd, sizeof cmd, "go movetime %d", lv->movetime_ms);
	if (send_line(u, position_cmd) != 0) return -1;
	return send_line(u, cmd);
}

static int take_bestmove(int r, const char *line, char move[6])
{
	if (r != 1) return r;
	const char *m = line + strlen("bestmove ");
	int k = 0;
	while (m[k] && m[k] != ' ' && k < 5) {
		move[k] = m[k];
		k++;
	}
	move[k] = '\0';
	return k >= 4 ? 1 : -1; /* "bestmove (none)" means no legal move */
}

int uci_poll_bestmove(Uci *u, char move[6])
{
	char line[128];
	return take_bestmove(read_line_with(u, "bestmove ", line, sizeof line), line, move);
}

int uci_wait_bestmove(Uci *u, char move[6], int timeout_ms)
{
	char line[128];
	return take_bestmove(wait_for(u, "bestmove ", line, sizeof line, timeout_ms), line, move);
}

void uci_stop(Uci *u) { send_line(u, "stop"); }

int uci_read_fd(const Uci *u) { return u->from_fd; }

void uci_close(Uci *u)
{
	if (u->pid <= 0) return;
	send_line(u, "quit");
	close(u->to_fd);
	long end = now_ms() + 500;
	int done = 0;
	while (!done && now_ms() < end) {
		if (waitpid(u->pid, NULL, WNOHANG) == u->pid) done = 1;
		else usleep(20000);
	}
	if (!done) {
		kill(u->pid, SIGKILL);
		waitpid(u->pid, NULL, 0);
	}
	close(u->from_fd);
	u->pid = -1;
}

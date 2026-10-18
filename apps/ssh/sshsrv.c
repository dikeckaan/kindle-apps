#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sshsrv.h"

int tcp_port_listening(const char *text, int port)
{
	char want[8];
	snprintf(want, sizeof want, ":%04X ", port);
	for (const char *line = text; line && *line;) {
		/* fields: sl local_address rem_address st … */
		char local[64] = "", st[8] = "";
		if (sscanf(line, " %*s %63s %*s %7s", local, st) == 2) {
			char *colon = strchr(local, ':');
			if (colon && strncmp(colon, want, 5) == 0 && strcmp(st, "0A") == 0) return 1;
		}
		line = strchr(line, '\n');
		if (line) line++;
	}
	return 0;
}

/* 0 if path is a regular, root-owned file not writable by group/others (lstat: symlinks rejected) */
static int root_only_file(const char *path)
{
	struct stat st;
	if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & 022)) return -1;
	if (lstat(SSH_DIR, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0 || (st.st_mode & 077)) return -1;
	return 0;
}

int ssh_server_pid(void)
{
	if (root_only_file(SSH_PIDFILE) != 0) return -1;
	char buf[32], path[64], exe[256];
	int fd = open(SSH_PIDFILE, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) return -1;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n <= 0) return -1;
	buf[n] = '\0';
	int pid = atoi(buf);
	if (pid <= 1) return -1;
	struct stat st;
	snprintf(path, sizeof path, "/proc/%d", pid);
	if (stat(path, &st) != 0 || st.st_uid != 0) return -1;
	snprintf(path, sizeof path, "/proc/%d/exe", pid);
	ssize_t l = readlink(path, exe, sizeof exe - 1);
	if (l <= 0) return -1;
	exe[l] = '\0';
	return strcmp(exe, SSH_BIN) == 0 ? pid : -1;
}

int ssh_server_port(void)
{
	int pid = ssh_server_pid();
	if (pid < 0) return -1;
	char path[64], cl[1024];
	snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	ssize_t n = read(fd, cl, sizeof cl - 1);
	close(fd);
	if (n <= 0) return -1;
	cl[n] = '\0';
	for (int i = 0; i < n; i += (int) strlen(cl + i) + 1)
		if (strcmp(cl + i, "-p") == 0 && i + 3 < n) return atoi(cl + i + 3);
	return -1;
}

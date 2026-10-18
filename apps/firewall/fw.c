#include <string.h>
#include "fw.h"

int fw_is_open_policy(const char *out)
{
	if (!strstr(out, "-P INPUT ACCEPT")) return 0;
	return !strstr(out, "-j DROP") && !strstr(out, "-j REJECT");
}

static int parse_port(const char *s)
{
	if (!*s) return -1;
	long v = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9') return -1;
		v = v * 10 + (*s - '0');
		if (v > 65535) return -1;
	}
	return v >= 1 ? (int) v : -1;
}

int fw_dropbear_port(const char *cmdline, int len)
{
	const char *args[32];
	int n = 0;
	for (int i = 0; i < len && n < 32; i += (int) strlen(cmdline + i) + 1) args[n++] = cmdline + i;
	if (n == 0) return -1;
	const char *base = strrchr(args[0], '/');
	if (strcmp(base ? base + 1 : args[0], "dropbear") != 0) return -1;
	for (int i = 1; i < n; i++) {
		if (strcmp(args[i], "-p") == 0) return i + 1 < n ? parse_port(args[i + 1]) : -1;
		if (strncmp(args[i], "-p", 2) == 0) return parse_port(args[i] + 2);
	}
	return -1;
}

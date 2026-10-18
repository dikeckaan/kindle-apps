#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "kui.h"
#include "sshcfg.h"

static void derive(const char *password, const unsigned char salt[16], unsigned char out[32])
{
	unsigned char buf[32 + 16 + 256];
	size_t pl = strlen(password);
	if (pl > 256) pl = 256;
	memcpy(buf, salt, 16);
	memcpy(buf + 16, password, pl);
	kui_sha256(buf, (int) (16 + pl), out);
	for (int i = 0; i < SSH_ITERATIONS; i++) {
		memcpy(buf, out, 32);
		memcpy(buf + 32, salt, 16);
		kui_sha256(buf, 48, out);
	}
	memset(buf, 0, sizeof buf);
}

static int unhex(const char *s, unsigned char *out, int n)
{
	for (int i = 0; i < n; i++) {
		unsigned v = 0;
		for (int k = 0; k < 2; k++) {
			char c = s[2 * i + k];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= (unsigned) (c - '0');
			else if (c >= 'a' && c <= 'f') v |= (unsigned) (c - 'a' + 10);
			else return -1;
		}
		out[i] = (unsigned char) v;
	}
	return 0;
}

int ssh_make_record(const char *password, const unsigned char salt[16], char *out, int cap)
{
	unsigned char h[32];
	derive(password, salt, h);
	int o = snprintf(out, (size_t) cap, "kav1$");
	for (int i = 0; i < 16 && o < cap; i++) o += snprintf(out + o, (size_t) (cap - o), "%02x", salt[i]);
	if (o < cap) o += snprintf(out + o, (size_t) (cap - o), "$");
	for (int i = 0; i < 32 && o < cap; i++) o += snprintf(out + o, (size_t) (cap - o), "%02x", h[i]);
	return o < cap ? o : -1;
}

int ssh_check_record(const char *record, const char *password)
{
	unsigned char salt[16], want[32], got[32];
	if (strncmp(record, "kav1$", 5) != 0 || strlen(record) < 5 + 32 + 1 + 64) return -1;
	if (record[5 + 32] != '$' || unhex(record + 5, salt, 16) || unhex(record + 5 + 33, want, 32)) return -1;
	derive(password, salt, got);
	unsigned char diff = 0; /* constant time */
	for (int i = 0; i < 32; i++) diff |= (unsigned char) (got[i] ^ want[i]);
	return diff == 0;
}

int ssh_check_file(const char *path, const char *password)
{
	char rec[160];
	int fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) return -1;
	ssize_t n = read(fd, rec, sizeof rec - 1);
	close(fd);
	if (n <= 0) return -1;
	rec[n] = '\0';
	rec[strcspn(rec, "\r\n")] = '\0';
	return ssh_check_record(rec, password);
}

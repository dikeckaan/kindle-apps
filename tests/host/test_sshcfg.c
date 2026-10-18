#include <stdlib.h>
#include <unistd.h>
#include "ssh/sshcfg.h"
#include "t.h"

/* computed with Python hashlib from the documented algorithm */
#define KNOWN_VECTOR "kav1$000102030405060708090a0b0c0d0e0f$c585ae81765f1222eeaa0fd0a4ca5e5250d176e8279518ad351d9c63f2c4253b"

static const unsigned char salt[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static void test_record_roundtrip(void)
{
	char rec[160];
	T_ASSERT(ssh_make_record("kindle123", salt, rec, sizeof rec) > 0);
	T_EQ_INT(ssh_check_record(rec, "kindle123"), 1);
}

static void test_record_wrong_password(void)
{
	char rec[160];
	ssh_make_record("kindle123", salt, rec, sizeof rec);
	T_EQ_INT(ssh_check_record(rec, "kindle124"), 0);
	T_EQ_INT(ssh_check_record(rec, ""), 0);
}

static void test_record_rejects_malformed(void)
{
	T_EQ_INT(ssh_check_record("", "x"), -1);
	T_EQ_INT(ssh_check_record("kav2$00$00", "x"), -1);
	T_EQ_INT(ssh_check_record("kav1$zz$00", "x"), -1);
	T_EQ_INT(ssh_check_record("kav1$000102030405060708090a0b0c0d0e0f$abc", "x"), -1);
}

static void test_record_known_vector(void)
{
	char rec[160];
	ssh_make_record("kindle123", salt, rec, sizeof rec);
	T_EQ_STR(rec, KNOWN_VECTOR);
}

static void test_check_file(void)
{
	char path[] = "/tmp/kav1-XXXXXX", rec[160];
	int fd = mkstemp(path);
	ssh_make_record("parola!", salt, rec, sizeof rec);
	T_ASSERT(write(fd, rec, strlen(rec)) > 0);
	T_ASSERT(write(fd, "\n", 1) == 1);
	close(fd);
	T_EQ_INT(ssh_check_file(path, "parola!"), 1);
	T_EQ_INT(ssh_check_file(path, "yanlis"), 0);
	char link[64];
	snprintf(link, sizeof link, "%s.lnk", path);
	T_ASSERT(symlink(path, link) == 0);
	T_EQ_INT(ssh_check_file(link, "parola!"), -1); /* never follow a planted symlink */
	unlink(link);
	unlink(path);
	T_EQ_INT(ssh_check_file("/tmp/kav1-missing-file", "x"), -1);
}

int main(void)
{
	T_RUN(test_record_roundtrip);
	T_RUN(test_record_wrong_password);
	T_RUN(test_record_rejects_malformed);
	T_RUN(test_record_known_vector);
	T_RUN(test_check_file);
	return T_DONE();
}

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"
#include "t.h"

static char root[] = "/tmp/kui-test-XXXXXX";

static void test_data_dir_created(void)
{
	const char *d = kui_data_dir("demo");
	char want[128];
	snprintf(want, sizeof want, "%s/data/demo", root);
	T_EQ_STR(d, want);
	struct stat st;
	T_ASSERT(stat(d, &st) == 0 && S_ISDIR(st.st_mode));
}

static void test_store_atomic_roundtrip(void)
{
	char path[160], buf[64];
	snprintf(path, sizeof path, "%s/save.txt", kui_data_dir("demo"));
	T_EQ_INT(kui_write_atomic(path, "merhaba", 7), 0);
	T_EQ_INT(kui_read_file(path, buf, sizeof buf), 7);
	T_EQ_STR(buf, "merhaba");
	T_EQ_INT(kui_write_atomic(path, "x", 1), 0);
	T_EQ_INT(kui_read_file(path, buf, sizeof buf), 1);
	T_EQ_STR(buf, "x");
}

static void test_read_missing_file_returns_minus1(void)
{
	char buf[8];
	T_EQ_INT(kui_read_file("/tmp/kui-definitely-missing-file", buf, sizeof buf), -1);
}

static void test_cfg_missing_key_returns_default(void)
{
	char path[160];
	snprintf(path, sizeof path, "%s/settings", kui_data_dir("demo"));
	T_EQ_INT(kui_cfg_get_int(path, "full_refresh_every", 25), 25);
}

static void test_cfg_set_then_get(void)
{
	char path[160];
	snprintf(path, sizeof path, "%s/settings2", kui_data_dir("demo"));
	T_EQ_INT(kui_cfg_set_int(path, "a", 1), 0);
	T_EQ_INT(kui_cfg_set_int(path, "full_refresh_every", 0), 0);
	T_EQ_INT(kui_cfg_set_int(path, "a", 7), 0);
	T_EQ_INT(kui_cfg_get_int(path, "a", -1), 7);
	T_EQ_INT(kui_cfg_get_int(path, "full_refresh_every", 25), 0);
	T_EQ_INT(kui_cfg_get_int(path, "full_refresh", 25), 25); /* prefix must not match */
}

static void test_cfg_set_stays_bounded_near_buffer_limit(void)
{
	char path[160], big[4100];
	snprintf(path, sizeof path, "%s/settings_big", kui_data_dir("demo"));
	int n = 0;
	for (int i = 0; n < 4088; i++) n += snprintf(big + n, sizeof big - (size_t) n, "k%04d=1\n", i);
	T_EQ_INT(kui_write_atomic(path, big, n), 0);
	/* adding a key to an almost-full file must not overflow; it fails cleanly instead */
	char key[200];
	memset(key, 'x', 180);
	key[180] = '\0';
	int rc = kui_cfg_set_int(path, key, 123456789);
	T_ASSERT(rc == 0 || rc == -1);
	T_EQ_INT(kui_cfg_get_int(path, "k0000", -1), 1);
}

int main(void)
{
	if (!mkdtemp(root)) return 2;
	setenv("KUI_ROOT", root, 1);
	T_RUN(test_data_dir_created);
	T_RUN(test_store_atomic_roundtrip);
	T_RUN(test_read_missing_file_returns_minus1);
	T_RUN(test_cfg_missing_key_returns_default);
	T_RUN(test_cfg_set_then_get);
	T_RUN(test_cfg_set_stays_bounded_near_buffer_limit);
	return T_DONE();
}

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kui.h"
#include "t.h"

#define DB "build/host/lipcdb"
#define STATE "build/host/lipcstate"

static void setup(int prevent)
{
	system("rm -rf " DB " " STATE " build/host/lipcbin && mkdir -p " DB " " STATE " build/host/lipcbin && "
	       "cp tests/host/fake_lipc.sh build/host/lipcbin/lipc-get-prop && cp tests/host/fake_lipc.sh build/host/lipcbin/lipc-set-prop");
	setenv("KUI_LIPC_DIR", "build/host/lipcbin", 1);
	setenv("FAKE_LIPC_DB", DB, 1);
	setenv("KUI_STATE_DIR", STATE, 1);
	setenv("KUI_BOOT_ID", "boot-a", 1);
	FILE *f = fopen(DB "/com.lab126.powerd.preventScreenSaver", "w");
	fprintf(f, "%d\n", prevent);
	fclose(f);
}

static void test_get_set(void)
{
	setup(0);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 0);
	kui_lipc_set_int("com.lab126.powerd", "preventScreenSaver", 1);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 1);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "missing", 42), 42);
}

static void test_prevent_sleep_restores_previous_value(void)
{
	setup(0);
	kui_prevent_sleep(1);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 1);
	T_ASSERT(access(STATE "/prevent_ss.restore", F_OK) == 0); /* the launcher can undo it after a crash */
	kui_prevent_sleep(0);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 0);
	T_ASSERT(access(STATE "/prevent_ss.restore", F_OK) != 0);
}

static void test_prevent_sleep_keeps_never_sleep_setting(void)
{
	setup(1); /* user chose "Hiç uyuma" in Uyku Ayarı */
	kui_prevent_sleep(1);
	kui_prevent_sleep(0);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 1);
}

static void test_second_prevent_does_not_overwrite_saved_value(void)
{
	setup(0);
	kui_prevent_sleep(1);
	kui_prevent_sleep(1); /* restore file still says 0 */
	kui_prevent_sleep(0);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 0);
}

static void test_release_without_prevent_is_a_no_op(void)
{
	setup(1); /* the hub cleans up after every app; it must not undo "Hiç uyuma" */
	kui_prevent_sleep(0);
	T_EQ_INT(kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1), 1);
}

static int prevent(void) { return kui_lipc_get_int("com.lab126.powerd", "preventScreenSaver", -1); }

static void test_file_from_an_earlier_boot_is_ignored(void)
{
	setup(0);
	kui_prevent_sleep(1);               /* clock running when the battery died: file says 0 */
	setenv("KUI_BOOT_ID", "boot-b", 1); /* next boot */
	kui_lipc_set_int("com.lab126.powerd", "preventScreenSaver", 1); /* user picks "Hiç uyuma" */
	kui_prevent_sleep(0);               /* hub cleanup after the sleep app */
	T_EQ_INT(prevent(), 1);
	T_ASSERT(access(STATE "/prevent_ss.restore", F_OK) != 0); /* stale file removed */
	kui_prevent_sleep(1);               /* a new clock session saves the current value, not the stale one */
	kui_prevent_sleep(0);
	T_EQ_INT(prevent(), 1);
}

static void test_unwritable_state_dir_does_not_keep_awake(void)
{
	setup(0);
	setenv("KUI_STATE_DIR", "/nonexistent/dir/state", 1);
	kui_prevent_sleep(1);
	T_EQ_INT(prevent(), 0); /* could not save a way back: do not change anything */
	kui_prevent_sleep(0);
	T_EQ_INT(prevent(), 0);
}

static void test_failed_read_changes_nothing(void)
{
	setup(1);
	unlink(DB "/com.lab126.powerd.preventScreenSaver"); /* lipc-get-prop fails */
	kui_prevent_sleep(1);
	T_ASSERT(access(STATE "/prevent_ss.restore", F_OK) != 0);
	kui_prevent_sleep(0);
	T_EQ_INT(prevent(), -1); /* never written */
}

int main(void)
{
	T_RUN(test_file_from_an_earlier_boot_is_ignored);
	T_RUN(test_unwritable_state_dir_does_not_keep_awake);
	T_RUN(test_failed_read_changes_nothing);
	T_RUN(test_release_without_prevent_is_a_no_op);
	T_RUN(test_get_set);
	T_RUN(test_prevent_sleep_restores_previous_value);
	T_RUN(test_prevent_sleep_keeps_never_sleep_setting);
	T_RUN(test_second_prevent_does_not_overwrite_saved_value);
	return T_DONE();
}

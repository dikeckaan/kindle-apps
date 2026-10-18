#include <stdlib.h>
#include <unistd.h>
#include "kui.h"
#include "t.h"

#define D "build/host/powertest"

static void setup(void)
{
	system("rm -rf " D " && mkdir -p " D "/db " D "/bin && cp tests/host/fake_lipc.sh " D "/bin/lipc-get-prop && cp tests/host/fake_lipc.sh " D
	       "/bin/lipc-set-prop && : > " D "/wakealarm && : > " D "/state");
	setenv("KUI_LIPC_DIR", D "/bin", 1);
	setenv("FAKE_LIPC_DB", D "/db", 1);
	setenv("KUI_RTC_ALARM", D "/wakealarm", 1);
	setenv("KUI_POWER_STATE", D "/state", 1);
}

static char *slurp(const char *p)
{
	static char buf[256];
	FILE *f = fopen(p, "r");
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	return buf;
}

static void test_wifi(void)
{
	setup();
	system("echo 1 > " D "/db/com.lab126.cmd.wirelessEnable");
	T_EQ_INT(kui_wifi_enabled(), 1);
	kui_wifi_enable(0);
	T_EQ_INT(kui_wifi_enabled(), 0);
	system("echo CONNECTED > " D "/db/com.lab126.wifid.cmState");
	T_EQ_INT(kui_wifi_wait(2), 1);
	system("echo NA > " D "/db/com.lab126.wifid.cmState");
	T_EQ_INT(kui_wifi_wait(1), 0);
	system("rm " D "/db/com.lab126.cmd.wirelessEnable");
	T_EQ_INT(kui_wifi_enabled(), -1);
}

static void test_suspend_writes_alarm_and_state(void)
{
	setup();
	int slept = kui_suspend(30);
	T_ASSERT(slept >= 0 && slept <= 1); /* a plain file returns at once */
	T_EQ_STR(slurp(D "/state"), "mem");
	T_EQ_STR(slurp(D "/wakealarm"), "0"); /* the alarm is cleared after waking */
	T_EQ_INT(kui_woke_early(30, slept), 1);
	setenv("KUI_POWER_STATE", "/nonexistent/state", 1);
	T_EQ_INT(kui_suspend(30), -1);
	T_EQ_INT(kui_suspend(0), -1); /* nothing to sleep */
}

static void test_woke_early(void)
{
	T_EQ_INT(kui_woke_early(60, 60), 0);
	T_EQ_INT(kui_woke_early(60, 58), 0); /* resume overhead */
	T_EQ_INT(kui_woke_early(60, 20), 1); /* power button or touch */
	T_EQ_INT(kui_woke_early(60, 75), 0);
}

int main(void)
{
	T_RUN(test_wifi);
	T_RUN(test_suspend_writes_alarm_and_state);
	T_RUN(test_woke_early);
	return T_DONE();
}

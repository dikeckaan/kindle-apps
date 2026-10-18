/* Power helpers: Wi-Fi on/off through lipc and a timed suspend-to-RAM through the RTC alarm. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kui.h"

int kui_wifi_enabled(void) { return kui_lipc_get_int("com.lab126.cmd", "wirelessEnable", -1); }

void kui_wifi_enable(int on) { kui_lipc_set_int("com.lab126.cmd", "wirelessEnable", on ? 1 : 0); }

int kui_wifi_wait(int timeout_s)
{
	time_t end = time(NULL) + timeout_s;
	do {
		char st[32];
		if (kui_lipc_get_str("com.lab126.wifid", "cmState", st, sizeof st) == 0 && !strcmp(st, "CONNECTED")) return 1;
		sleep(1);
	} while (time(NULL) < end);
	return 0;
}

static const char *path_env(const char *env, const char *def)
{
	const char *p = getenv(env);
	return p && *p ? p : def;
}

/* writes s to path (truncating), 0 on success */
static int write_str(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd < 0) return -1;
	ssize_t n = (ssize_t) strlen(s), w = write(fd, s, (size_t) n);
	int e = errno;
	close(fd);
	errno = e;
	return w == n ? 0 : -1;
}

int kui_suspend(int seconds)
{
	if (seconds <= 0) return -1;
	const char *alarm = path_env("KUI_RTC_ALARM", "/sys/class/rtc/rtc0/wakealarm");
	const char *state = path_env("KUI_POWER_STATE", "/sys/power/state");
	struct timespec t0, t1;
	clock_gettime(CLOCK_REALTIME, &t0);
	sync(); /* nothing half-written on flash while asleep */
	/* the e-ink controller holds a wakelock for a few seconds after a refresh (hwtcon_wakelock): the kernel
	 * refuses with EBUSY meanwhile, so try again each second while there is time left */
	for (;;) {
		clock_gettime(CLOCK_REALTIME, &t1);
		int left = seconds - (int) (t1.tv_sec - t0.tv_sec);
		if (left <= 2) {
			fprintf(stderr, "suspend failed: still busy\n");
			return -1;
		}
		char rel[24];
		snprintf(rel, sizeof rel, "+%d", left);
		if (write_str(alarm, "0") != 0 || write_str(alarm, rel) != 0) return -1;
		/* blocks until the device wakes */
		int rc = write_str(state, "mem"), e = errno;
		write_str(alarm, "0");
		if (rc == 0) break;
		if (e != EBUSY || seconds - left >= 12) {
			fprintf(stderr, "suspend failed: %s\n", strerror(e));
			return -1;
		}
		sleep(1);
	}
	clock_gettime(CLOCK_REALTIME, &t1);
	return (int) (t1.tv_sec - t0.tv_sec);
}

int kui_woke_early(int requested, int slept) { return slept < requested - 3; }

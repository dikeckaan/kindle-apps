/* Battery saver helper shared by the desk apps: suspend-to-RAM until the next minute begins. */
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "kui.h"

int kui_ms_to_next_minute(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int) (60000 - (tv.tv_sec % 60) * 1000 - tv.tv_usec / 1000) + 200;
}

static int user_event(KuiEvType t) { return t == KUI_EV_TAP || t == KUI_EV_POWER || t == KUI_EV_SWIPE || t == KUI_EV_LONGPRESS; }

int kui_sleep_to_next_minute(void)
{
	long minute = (long) (time(NULL) / 60);
	int secs = (kui_ms_to_next_minute() + 999) / 1000;
	if (secs > 4) { /* a few seconds are not worth a suspend: wait awake instead */
		int slept = kui_suspend(secs), early = slept >= 0 && kui_woke_early(secs, slept);
		fprintf(stderr, "deep: asked %d s, slept %d s%s\n", secs, slept, early ? " (woken)" : "");
		if (early) return KUI_SLEEP_WOKEN; /* the power button or a touch */
	}
	/* the suspend failed, or the RTC (whole seconds) woke us a moment before the minute: wait awake for it */
	while ((long) (time(NULL) / 60) == minute) {
		KuiEvent e = kui_next_event(kui_ms_to_next_minute());
		if (e.type == KUI_EV_QUIT) return KUI_SLEEP_QUIT;
		if (user_event(e.type)) return KUI_SLEEP_WOKEN;
	}
	return KUI_SLEEP_MINUTE;
}

void kui_drain_input(int ms)
{
	struct timespec t0, t;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &t);
		int left = ms - (int) ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
		if (left <= 0) break;
		KuiEvent e = kui_next_event(left);
		if (e.type == KUI_EV_QUIT) break; /* kui_quit_pending() still reports it */
	}
	kui_drain_power_key();
}

#include <stdio.h>
#include <stdlib.h>
#include "kui.h"
#include "t.h"

#define LOG "build/host/batt.log"

static void test_estimate_from_the_discharge_since_unplugging(void)
{
	remove(LOG);
	long t = 1800000000L;
	T_EQ_INT(kui_batt_estimate_s(LOG, t, 80), -1); /* no data yet */
	kui_batt_log(LOG, t - 7200, 70, 1);            /* charging … */
	kui_batt_log(LOG, t - 3600, 100, 0);           /* … unplugged at 100 % */
	kui_batt_log(LOG, t - 3000, 100, 0);           /* unchanged: not written again */
	kui_batt_log(LOG, t - 1800, 99, 0);
	T_EQ_INT(kui_batt_estimate_s(LOG, t - 1800, 99), -1); /* 1 % in half an hour: too little to tell */
	kui_batt_log(LOG, t, 98, 0);
	/* 2 % in 3600 s → 98 % lasts 98 × 1800 s */
	T_EQ_INT(kui_batt_estimate_s(LOG, t, 98), 98L * 1800);
	kui_batt_log(LOG, t + 60, 98, 1); /* plugged in again: no estimate while charging */
	T_EQ_INT(kui_batt_estimate_s(LOG, t + 60, 98), -1);
}

static void test_log_stays_small(void)
{
	remove(LOG);
	for (int i = 0; i < 3000; i++) kui_batt_log(LOG, 1800000000L + i * 60, 100 - i % 100, i % 2);
	FILE *f = fopen(LOG, "r");
	T_ASSERT(f != NULL);
	if (!f) return;
	fseek(f, 0, SEEK_END);
	T_ASSERT(ftell(f) < 16 * 1024);
	fclose(f);
}

static void test_duration_text(void)
{
	char s[64];
	kui_fmt_duration(3 * 86400 + 4 * 3600 + 100, s, sizeof s);
	T_EQ_STR(s, "≈ 3 gün 4 sa");
	kui_fmt_duration(5 * 3600 + 20 * 60, s, sizeof s);
	T_EQ_STR(s, "≈ 5 sa 20 dk");
	kui_fmt_duration(42 * 60, s, sizeof s);
	T_EQ_STR(s, "≈ 42 dk");
}

int main(void)
{
	T_RUN(test_estimate_from_the_discharge_since_unplugging);
	T_RUN(test_log_stays_small);
	T_RUN(test_duration_text);
	return T_DONE();
}

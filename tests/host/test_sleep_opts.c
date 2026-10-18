#include "sleep/sleep_opts.h"
#include "t.h"

static void test_opts_table(void)
{
	const SleepOpt *o;
	int n = sleep_opts(&o);
	T_EQ_INT(n, 6);
	T_EQ_STR(o[0].label, "5 dakika");
	T_EQ_INT(o[0].seconds, 300);
	T_EQ_INT(o[1].seconds, 600);
	T_EQ_INT(o[2].seconds, 1800);
	T_EQ_INT(o[3].seconds, 3600);
	T_EQ_INT(o[4].seconds, 7200);
	T_EQ_STR(o[5].label, "Hiç uyuma");
	T_EQ_INT(o[5].seconds, 0);
}

static void test_current_index_prevent_wins(void)
{
	T_EQ_INT(sleep_current_index(600, 1), 5);
	T_EQ_INT(sleep_current_index(600, 0), 1);
	T_EQ_INT(sleep_current_index(7200, 0), 4);
}

static void test_current_index_unknown_is_minus1(void) { T_EQ_INT(sleep_current_index(2700, 0), -1); }

static void test_describe_minutes_hours_never(void)
{
	char s[64];
	sleep_describe(600, 0, s, sizeof s);
	T_EQ_STR(s, "Şu an: 10 dakika");
	sleep_describe(3600, 0, s, sizeof s);
	T_EQ_STR(s, "Şu an: 1 saat");
	sleep_describe(7200, 0, s, sizeof s);
	T_EQ_STR(s, "Şu an: 2 saat");
	sleep_describe(2700, 0, s, sizeof s);
	T_EQ_STR(s, "Şu an: 45 dakika");
	sleep_describe(5400, 0, s, sizeof s);
	T_EQ_STR(s, "Şu an: 90 dakika");
	sleep_describe(600, 1, s, sizeof s);
	T_EQ_STR(s, "Şu an: hiç uyumuyor");
}

int main(void)
{
	T_RUN(test_opts_table);
	T_RUN(test_current_index_prevent_wins);
	T_RUN(test_current_index_unknown_is_minus1);
	T_RUN(test_describe_minutes_hours_never);
	return T_DONE();
}

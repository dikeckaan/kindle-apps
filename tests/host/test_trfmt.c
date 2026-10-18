#include <time.h>
#include "kui.h"
#include "t.h"

static void test_names(void)
{
	T_EQ_STR(kui_tr_weekday(0), "Pazar");
	T_EQ_STR(kui_tr_weekday(6), "Cumartesi");
	T_EQ_STR(kui_tr_weekday_short(3), "Çar");
	T_EQ_STR(kui_tr_month(0), "Ocak");
	T_EQ_STR(kui_tr_month(9), "Ekim");
	T_EQ_STR(kui_tr_month(12), "");
	T_EQ_STR(kui_tr_weekday(-1), "");
}

static void test_date(void)
{
	struct tm tm = { 0 };
	tm.tm_mday = 18;
	tm.tm_mon = 9;
	tm.tm_year = 126;
	tm.tm_wday = 0;
	char s[64];
	kui_tr_date(&tm, s, sizeof s);
	T_EQ_STR(s, "18 Ekim 2026, Pazar");
}

static void test_fmt_num(void)
{
	char s[32];
	kui_fmt_num(6632.39, 2, s, sizeof s);
	T_EQ_STR(s, "6.632,39");
	kui_fmt_num(49.3573, 4, s, sizeof s);
	T_EQ_STR(s, "49,3573");
	kui_fmt_num(1234567.0, 0, s, sizeof s);
	T_EQ_STR(s, "1.234.567");
	kui_fmt_num(-0.5, 1, s, sizeof s);
	T_EQ_STR(s, "-0,5");
	kui_fmt_num(999.999, 2, s, sizeof s);
	T_EQ_STR(s, "1.000,00");
	kui_fmt_num(0, 2, s, sizeof s);
	T_EQ_STR(s, "0,00");
}

int main(void)
{
	T_RUN(test_names);
	T_RUN(test_date);
	T_RUN(test_fmt_num);
	return T_DONE();
}

#include <math.h>
#include <time.h>
#include "pano/pano.h"
#include "t.h"

static char *slurp(const char *path)
{
	static char buf[65536];
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	return buf;
}

static void test_prayer_times(void)
{
	PanoPrayer p;
	T_EQ_INT(pano_parse_prayer(slurp("tests/host/fixtures/aladhan.json"), &p), 0);
	T_EQ_STR(p.t[PANO_IMSAK], "05:46"); /* Diyanet's imsak is Aladhan's Fajr */
	T_EQ_STR(p.t[PANO_GUNES], "07:11");
	T_EQ_STR(p.t[PANO_OGLE], "12:54");
	T_EQ_STR(p.t[PANO_IKINDI], "15:56");
	T_EQ_STR(p.t[PANO_AKSAM], "18:26");
	T_EQ_STR(p.t[PANO_YATSI], "19:45");
	T_EQ_STR(p.date, "18-10-2026");
	T_EQ_INT(pano_parse_prayer("{\"code\":400}", &p), -1);
	T_EQ_INT(pano_parse_prayer("junk", &p), -1);
	char url[256];
	pano_prayer_url(40.935, 29.155, 18, 10, 2026, url, sizeof url);
	T_EQ_STR(url, "https://api.aladhan.com/v1/timings/18-10-2026?latitude=40.9350&longitude=29.1550&method=13");
}

static void test_next_prayer(void)
{
	PanoPrayer p;
	pano_parse_prayer(slurp("tests/host/fixtures/aladhan.json"), &p);
	int mins;
	T_EQ_INT(pano_next_prayer(&p, 17 * 60 + 14, &mins), PANO_AKSAM);
	T_EQ_INT(mins, 72); /* 18:26 − 17:14 */
	T_EQ_INT(pano_next_prayer(&p, 5 * 60, &mins), PANO_IMSAK);
	T_EQ_INT(mins, 46);
	T_EQ_INT(pano_next_prayer(&p, 21 * 60, &mins), PANO_IMSAK); /* after yatsı: tomorrow's imsak (today's times as a guess) */
	T_EQ_INT(mins, 3 * 60 + 5 * 60 + 46);
	T_EQ_STR(pano_prayer_name(PANO_IKINDI), "İkindi");
}

static void test_onthisday(void)
{
	PanoEvent e[8];
	int n = pano_parse_onthisday(slurp("tests/host/fixtures/onthisday.json"), e, 8);
	T_EQ_INT(n, 3);
	T_EQ_INT(e[0].year, 1989);
	T_EQ_STR(e[0].text, "Doğu Almanya lideri Erich Honecker istifa etti.");
	T_EQ_INT(pano_parse_onthisday("{}", e, 8), 0);
	char url[256];
	pano_onthisday_url("tr", 10, 18, url, sizeof url);
	T_EQ_STR(url, "https://api.wikimedia.org/feed/v1/wikipedia/tr/onthisday/selected/10/18");
	pano_onthisday_url("nb", 1, 2, url, sizeof url); /* Wikipedia's code for Bokmål */
	T_EQ_STR(url, "https://api.wikimedia.org/feed/v1/wikipedia/no/onthisday/selected/01/02");
}

static void test_moon(void)
{
	/* new moon on 2026-10-10, full moon on 2026-10-26: the mean-lunation estimate is within about a day */
	double a = pano_moon_age(1791607800L), b = pano_moon_age(1792987920L);
	T_ASSERT(a < 1.5 || a > 28.0);
	T_ASSERT(fabs(b - 14.8) < 1.5);
	T_EQ_STR(pano_moon_phase_name(0.3), "Yeni ay");
	T_EQ_STR(pano_moon_phase_name(7.4), "İlk dördün");
	T_EQ_STR(pano_moon_phase_name(14.8), "Dolunay");
	T_EQ_STR(pano_moon_phase_name(22.1), "Son dördün");
	T_EQ_STR(pano_moon_phase_name(4.0), "Hilal (büyüyen)");
	T_EQ_INT(pano_moon_lit_percent(14.77), 100);
	T_EQ_INT(pano_moon_lit_percent(0.0), 0);
}

static void test_week_and_day(void)
{
	T_EQ_INT(pano_iso_week(2026, 10, 18), 42);
	T_EQ_INT(pano_iso_week(2026, 1, 1), 1);    /* Thursday */
	T_EQ_INT(pano_iso_week(2027, 1, 1), 53);   /* Friday: still week 53 of 2026 */
	T_EQ_INT(pano_iso_week(2024, 12, 30), 1);  /* Monday of week 1 of 2025 */
	T_EQ_INT(pano_day_of_year(2026, 10, 18), 291);
	T_EQ_INT(pano_day_of_year(2024, 12, 31), 366);
}

int main(void)
{
	T_RUN(test_prayer_times);
	T_RUN(test_next_prayer);
	T_RUN(test_onthisday);
	T_RUN(test_moon);
	T_RUN(test_week_and_day);
	return T_DONE();
}

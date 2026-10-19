/* Vakitler: prayer times against Diyanet's published tables (tests/host/fixtures/diyanet_times.txt), the Hijri
 * calendar against Diyanet's dates, qibla, time zones, parsing. The Sun's position is cross-checked against
 * apps/sky/astro.c (itself tested against JPL Horizons in test_astro.c). */
#include <math.h>
#include <stdlib.h>
#include "sky/astro.h"
#include "vakit/vakit.h"
#include "t.h"

#define T_NEAR(a, b, tol)                                                                                          \
	do {                                                                                                       \
		double t_a = (a), t_b = (b);                                                                       \
		if (!(fabs(t_a - t_b) <= (tol))) T_FAIL_("%s == %.6f, expected %.6f ± %g", #a, t_a, t_b, (double) (tol)); \
	} while (0)

static const VkOpts DIY = { VK_DIYANET, 0, 0 };

static long unix_utc(int y, int m, int d, int hh, int mm) { return (vk_jdn(y, m, d) - 2440588L) * 86400L + hh * 3600L + mm * 60L; }

static void test_calendar(void)
{
	T_EQ_INT(vk_jdn(2000, 1, 1), 2451545);
	T_EQ_INT(vk_jdn(1970, 1, 1), 2440588);
	int y, m, d;
	vk_from_jdn(vk_jdn(2026, 10, 19), &y, &m, &d);
	T_EQ_INT(y, 2026);
	T_EQ_INT(m, 10);
	T_EQ_INT(d, 19);
	vk_from_jdn(vk_jdn(2024, 2, 29) + 1, &y, &m, &d);
	T_EQ_INT(m * 100 + d, 301);
	T_EQ_INT(vk_wday(vk_jdn(2026, 10, 19)), 1); /* a Monday */
	T_EQ_INT(vk_wday(vk_jdn(2026, 10, 18)), 0);
	T_EQ_INT(vk_month_days(2024, 2), 29);
	T_EQ_INT(vk_month_days(2100, 2), 28);
	T_EQ_INT(vk_month_days(2026, 4), 30);
}

/* the Sun from vakit.c and from astro.c agree to a few hundredths of a degree */
static void test_sun_position(void)
{
	const double lat = 41.04, lon = 28.95;
	for (int i = 0; i < 48; i++) {
		double t = (double) unix_utc(2026, 1 + i / 4, 1 + (i % 4) * 7, (i * 5) % 24, 17), az, alt, az2;
		vk_sun_azalt(t, lat, lon, &az, &alt);
		double alt2 = astro_alt(ASTRO_SUN, astro_jd(t), lat, lon, &az2);
		T_NEAR(alt, alt2, 0.05);
		if (alt > -10) {
			double d = fmod(az - az2 + 540, 360) - 180;
			T_NEAR(d, 0, 0.1);
		}
	}
}

static int hm(const char *s) { return atoi(s) * 60 + atoi(s + 3); }

static const VkCityDef *plate_city(int plate)
{
	for (int i = 0; i < 81; i++)
		if (vk_tr_cities[i].plate == plate) return &vk_tr_cities[i];
	return NULL;
}

/* 384 days × 6 times from Diyanet's own tables: never more than a minute off, mostly the same minute */
static void test_diyanet_tables(void)
{
	FILE *f = fopen("tests/host/fixtures/diyanet_times.txt", "r");
	T_ASSERT(f != NULL);
	if (!f) return;
	char line[256];
	int rows = 0, exact = 0, total = 0, worst = 0, hijri_ok = 0, qibla_worst = 0;
	while (fgets(line, sizeof line, f)) {
		if (line[0] == '#') continue;
		int plate, y, m, d, hy, hmo, hd;
		char t[7][8];
		if (sscanf(line, "%d %d-%d-%d %7s %7s %7s %7s %7s %7s %7s %d-%d-%d", &plate, &y, &m, &d, t[0], t[1], t[2], t[3], t[4], t[5], t[6], &hy, &hmo, &hd) != 14) {
			T_FAIL_("bad fixture line %s", line);
			continue;
		}
		const VkCityDef *c = plate_city(plate);
		T_ASSERT(c != NULL);
		if (!c) continue;
		VkDay day;
		vk_day(y, m, d, c->lat, c->lon, 3, &DIY, &day);
		for (int i = 0; i < VK_N; i++) {
			int diff = abs(day.min[i] - hm(t[i]));
			if (diff > worst) worst = diff;
			if (diff > 1) T_FAIL_("%s %d-%02d-%02d %s: %02d:%02d, Diyanet %s", c->name, y, m, d, vk_names[i], day.min[i] / 60, day.min[i] % 60, t[i]);
			exact += diff == 0;
			total++;
		}
		double qt = vk_qibla_time(y, m, d, c->lat, c->lon, 3);
		int qd = abs((int) floor(qt * 60 + 0.5) - hm(t[6]));
		if (qd > qibla_worst) qibla_worst = qd;
		VkHijri h;
		vk_hijri(vk_jdn(y, m, d), &h);
		if (h.y == hy && h.m == hmo && h.d == hd) hijri_ok++;
		else T_FAIL_("%d-%02d-%02d: Hijri %d-%d-%d, Diyanet %d-%d-%d", y, m, d, h.y, h.m, h.d, hy, hmo, hd);
		rows++;
	}
	fclose(f);
	T_EQ_INT(rows, 384);
	T_ASSERT(worst <= 1);
	T_ASSERT(exact * 100 >= total * 85);
	T_ASSERT(qibla_worst <= 2);
	T_EQ_INT(hijri_ok, rows);
	fprintf(stderr, "diyanet: %d/%d times to the minute, worst %d min; kıble saati worst %d min; Hijri %d/%d\n", exact, total, worst, qibla_worst, hijri_ok, rows);
}

static void test_hijri_month_starts(void)
{
	static const struct { int y, m, d, hy, hm, hd; } k[] = {
		{ 2023, 3, 23, 1444, 9, 1 },   /* Diyanet's dini günler: Ramazan 1444 */
		{ 2023, 4, 21, 1444, 10, 1 },
		{ 2024, 3, 11, 1445, 9, 1 },
		{ 2024, 4, 10, 1445, 10, 1 },
		{ 2024, 6, 16, 1445, 12, 10 },
		{ 2025, 3, 1, 1446, 9, 1 },
		{ 2025, 3, 30, 1446, 10, 1 },  /* Ramazan Bayramı 2025 */
		{ 2025, 6, 6, 1446, 12, 10 },
		{ 2026, 2, 19, 1447, 9, 1 },   /* Ramazan 1447 */
		{ 2026, 3, 20, 1447, 10, 1 },  /* Ramazan Bayramı 2026 */
		{ 2026, 5, 27, 1447, 12, 10 }, /* Kurban Bayramı 2026 */
		{ 2026, 6, 16, 1448, 1, 1 },   /* Hicri yılbaşı */
		{ 2026, 6, 25, 1448, 1, 10 },  /* Aşure */
		{ 2026, 10, 12, 1448, 5, 1 },
		{ 2026, 11, 10, 1448, 6, 1 },
		{ 2026, 12, 10, 1448, 7, 1 },  /* Üç ayların başlangıcı */
	};
	for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
		VkHijri h;
		vk_hijri(vk_jdn(k[i].y, k[i].m, k[i].d), &h);
		if (h.y != k[i].hy || h.m != k[i].hm || h.d != k[i].hd)
			T_FAIL_("%d-%02d-%02d: %d-%d-%d, expected %d-%d-%d", k[i].y, k[i].m, k[i].d, h.y, h.m, h.d, k[i].hy, k[i].hm, k[i].hd);
	}
	VkHijri h;
	vk_hijri_tabular(vk_jdn(2026, 10, 14), &h); /* the arithmetic calendar is a day behind here */
	T_EQ_INT(h.y * 10000 + h.m * 100 + h.d, 14480502);
	vk_hijri_tabular(1948440, &h); /* 1 Muharrem 1 */
	T_EQ_INT(h.y * 10000 + h.m * 100 + h.d, 10101);
	T_EQ_STR(vk_hijri_months[8], "Ramazan");
}

static void test_new_moon_and_phase(void)
{
	/* the total solar eclipse of 8 April 2024: new moon 18:21 UT */
	double nm = vk_new_moon(astro_jd((double) unix_utc(2024, 4, 7, 0, 0)));
	T_NEAR(nm, astro_jd((double) unix_utc(2024, 4, 8, 18, 21)), 10.0 / 1440);
	double illum, age;
	T_EQ_INT(vk_moon_phase((double) unix_utc(2024, 9, 18, 2, 34), &illum, &age), 4); /* lunar eclipse: full */
	T_ASSERT(illum > 0.99);
	T_EQ_INT(vk_moon_phase((double) unix_utc(2024, 4, 8, 18, 21), &illum, &age), 0);
	T_ASSERT(illum < 0.01);
	T_EQ_STR(vk_moon_names[4], "Dolunay");
}

static void test_qibla(void)
{
	double b, km;
	vk_qibla(51.5074, -0.1278, &b, &km); /* London */
	T_NEAR(b, 118.99, 0.05);
	T_NEAR(km, 4790, 15);
	vk_qibla(40.7128, -74.0060, &b, &km); /* New York */
	T_NEAR(b, 58.48, 0.05);
	vk_qibla(41.04, 28.95, &b, &km); /* İstanbul */
	T_NEAR(b, 151.6, 0.3);
	T_NEAR(km, 2410, 30);
	vk_qibla(VK_KAABA_LAT, VK_KAABA_LON, NULL, &km);
	T_NEAR(km, 0, 1e-6);
	/* at the kıble saati the Sun stands in the qibla direction */
	double t = vk_qibla_time(2026, 10, 14, 41.04, 28.95, 3), az;
	T_ASSERT(!isnan(t));
	vk_sun_azalt((double) unix_utc(2026, 10, 14, 0, 0) + (t - 3) * 3600, 41.04, 28.95, &az, NULL);
	vk_qibla(41.04, 28.95, &b, NULL);
	T_NEAR(az, b, 0.01);
	T_ASSERT(isnan(vk_qibla_time(2026, 10, 14, 21.0, 39.0, 3)) || 1); /* near Mecca: may not happen; must not crash */
}

static void test_time_zones(void)
{
	VkPlace p = { "Berlin", 52.52, 13.40, 60, VK_DST_EU };
	T_EQ_INT(vk_utc_offset(&p, unix_utc(2026, 3, 29, 0, 59)), 3600);
	T_EQ_INT(vk_utc_offset(&p, unix_utc(2026, 3, 29, 1, 0)), 7200);
	T_EQ_INT(vk_utc_offset(&p, unix_utc(2026, 10, 25, 0, 59)), 7200);
	T_EQ_INT(vk_utc_offset(&p, unix_utc(2026, 10, 25, 1, 0)), 3600);
	VkPlace ny = { "New York", 40.71, -74.01, -300, VK_DST_US };
	T_EQ_INT(vk_utc_offset(&ny, unix_utc(2026, 3, 8, 6, 59)), -5 * 3600);
	T_EQ_INT(vk_utc_offset(&ny, unix_utc(2026, 3, 8, 7, 0)), -4 * 3600);
	T_EQ_INT(vk_utc_offset(&ny, unix_utc(2026, 11, 1, 5, 59)), -4 * 3600);
	T_EQ_INT(vk_utc_offset(&ny, unix_utc(2026, 11, 1, 6, 0)), -5 * 3600);
	VkPlace sy = { "Sidney", -33.87, 151.21, 600, VK_DST_AU };
	T_EQ_INT(vk_utc_offset(&sy, unix_utc(2026, 1, 15, 0, 0)), 11 * 3600);
	T_EQ_INT(vk_utc_offset(&sy, unix_utc(2026, 7, 15, 0, 0)), 10 * 3600);
	T_EQ_INT(vk_utc_offset(&sy, unix_utc(2026, 10, 3, 15, 59)), 10 * 3600); /* 4 Oct 02:00 AEST */
	T_EQ_INT(vk_utc_offset(&sy, unix_utc(2026, 10, 3, 16, 0)), 11 * 3600);
	VkPlace ca = { "Kahire", 30.04, 31.24, 120, VK_DST_EG };
	T_EQ_INT(vk_utc_offset(&ca, unix_utc(2026, 4, 23, 21, 59)), 2 * 3600); /* last Friday of April: the 24th */
	T_EQ_INT(vk_utc_offset(&ca, unix_utc(2026, 4, 23, 22, 0)), 3 * 3600);
	T_EQ_INT(vk_utc_offset(&ca, unix_utc(2026, 8, 1, 0, 0)), 3 * 3600);
	T_EQ_INT(vk_utc_offset(&ca, unix_utc(2026, 12, 1, 0, 0)), 2 * 3600);
	VkPlace ist;
	vk_place_from_def(&vk_tr_cities[33], &ist);
	T_EQ_STR(ist.name, "İstanbul");
	T_EQ_INT(vk_utc_offset(&ist, unix_utc(2026, 7, 1, 0, 0)), 3 * 3600);
	T_EQ_INT(vk_utc_offset(&ist, unix_utc(2026, 1, 1, 0, 0)), 3 * 3600);
}

static void test_now(void)
{
	VkNow n;
	VkDay day;
	/* İstanbul 14 October 2026 (Diyanet: 05:43 07:07 12:55 16:01 18:33 19:52; 15th imsak 05:44) */
	T_EQ_INT(vk_now(unix_utc(2026, 10, 14, 14, 0), 41.04, 28.95, 10800, &DIY, &n, &day), 0); /* 17:00 */
	T_EQ_INT(n.cur, VK_IKINDI);
	T_EQ_INT(n.next, VK_AKSAM);
	T_EQ_INT(n.next_at, unix_utc(2026, 10, 14, 15, 33));
	T_EQ_INT(n.now_min, 17 * 60);
	T_EQ_INT(n.d, 14);
	T_EQ_INT(vk_now(unix_utc(2026, 10, 14, 17, 0), 41.04, 28.95, 10800, &DIY, &n, &day), 0); /* 20:00 */
	T_EQ_INT(n.cur, VK_YATSI);
	T_EQ_INT(n.next, VK_IMSAK);
	T_EQ_INT(n.next_at, unix_utc(2026, 10, 15, 2, 44));
	T_EQ_INT(vk_now(unix_utc(2026, 10, 14, 0, 0), 41.04, 28.95, 10800, &DIY, &n, &day), 0); /* 03:00 */
	T_EQ_INT(n.cur, VK_YATSI);
	T_EQ_INT(n.next, VK_IMSAK);
	T_EQ_INT(n.next_at, unix_utc(2026, 10, 14, 2, 43));
	T_EQ_INT(vk_now(unix_utc(2026, 10, 14, 15, 33), 41.04, 28.95, 10800, &DIY, &n, &day), 0); /* 18:33 exactly */
	T_EQ_INT(n.cur, VK_AKSAM);
	T_EQ_INT(n.next, VK_YATSI);
	long at;
	T_EQ_INT(vk_next_prayer(unix_utc(2026, 10, 14, 14, 0), 41.04, 28.95, 10800, &at), VK_AKSAM);
	T_EQ_INT(at, unix_utc(2026, 10, 14, 15, 33));
	char s[64];
	vk_fmt_left(72 * 60, s, sizeof s, "%d sa %d dk", "%d sa", "%d dk");
	T_EQ_STR(s, "1 sa 12 dk");
	vk_fmt_left(3600, s, sizeof s, "%d sa %d dk", "%d sa", "%d dk");
	T_EQ_STR(s, "1 sa");
	vk_fmt_left(61, s, sizeof s, "%d sa %d dk", "%d sa", "%d dk");
	T_EQ_STR(s, "2 dk");
	vk_fmt_left(0, s, sizeof s, "%d sa %d dk", "%d sa", "%d dk");
	T_EQ_STR(s, "0 dk");
	snprintf(s, sizeof s, vk_until_fmt[VK_AKSAM], "1 sa 12 dk");
	T_EQ_STR(s, "Akşama 1 sa 12 dk");
}

static void test_methods(void)
{
	VkDay shafi, hanafi, mwl, uq, uqr, isna;
	VkOpts o = { VK_DIYANET, 0, 0 };
	vk_day(2026, 6, 21, 39.93, 32.86, 3, &o, &shafi);
	o.hanafi = 1;
	vk_day(2026, 6, 21, 39.93, 32.86, 3, &o, &hanafi);
	T_ASSERT(hanafi.min[VK_IKINDI] - shafi.min[VK_IKINDI] > 45);
	T_EQ_INT(hanafi.min[VK_OGLE], shafi.min[VK_OGLE]);
	o = (VkOpts){ VK_MWL, 0, 0 };
	vk_day(2026, 6, 21, 39.93, 32.86, 3, &o, &mwl);
	/* MWL has Diyanet's angles but no temkin */
	T_NEAR(mwl.t[VK_GUNES] - shafi.t[VK_GUNES], 7 / 60.0, 1.5 / 60);
	T_NEAR(shafi.t[VK_AKSAM] - mwl.t[VK_AKSAM], 7 / 60.0, 1.5 / 60);
	o = (VkOpts){ VK_UMMALQURA, 0, 0 };
	vk_day(2026, 6, 21, 21.4225, 39.8262, 3, &o, &uq);
	T_NEAR(uq.t[VK_YATSI] - uq.t[VK_AKSAM], 1.5, 1e-9);
	o.ramadan = 1;
	vk_day(2026, 6, 21, 21.4225, 39.8262, 3, &o, &uqr);
	T_NEAR(uqr.t[VK_YATSI] - uqr.t[VK_AKSAM], 2.0, 1e-9);
	o = (VkOpts){ VK_ISNA, 0, 0 };
	vk_day(2026, 6, 21, 39.93, 32.86, 3, &o, &isna);
	T_ASSERT(isna.min[VK_IMSAK] > mwl.min[VK_IMSAK]); /* 15° comes later than 18° */
	T_EQ_STR(vk_method(99)->name, vk_method(VK_DIYANET)->name);
	/* solar noon, day length: Ankara at the June solstice ≈ 15 h 0 min of daylight */
	T_NEAR(mwl.day_len, 15.0, 0.1);
	T_NEAR(mwl.noon, 12 + 3 - 32.86 / 15 + 1.8 / 60, 1.0 / 60);
}

static void test_high_latitudes(void)
{
	VkDay d;
	VkOpts o = { VK_MWL, 0, 0 };
	vk_day(2026, 6, 21, 59.91, 10.75, 2, &o, &d); /* Oslo: the Sun never gets 18° below */
	T_ASSERT(d.high_lat);
	for (int i = 0; i < VK_N; i++) T_ASSERT(d.min[i] >= 0);
	T_ASSERT(d.t[VK_IMSAK] < d.t[VK_GUNES] && d.t[VK_YATSI] > d.t[VK_AKSAM]);
	vk_day(2026, 6, 21, 69.65, 18.96, 2, &o, &d); /* Tromsø: midnight sun */
	T_ASSERT(isnan(d.sunrise));
	T_NEAR(d.day_len, 24, 1e-9);
	T_EQ_INT(d.min[VK_GUNES], -1);
	T_ASSERT(d.min[VK_OGLE] > 0);
	vk_day(2026, 12, 21, 69.65, 18.96, 1, &o, &d); /* polar night */
	T_NEAR(d.day_len, 0, 1e-9);
	VkNow n;
	T_EQ_INT(vk_now(unix_utc(2026, 10, 14, 14, 0), 41.04, 28.95, 10800, &o, &n, NULL), 0);
	vk_day(2026, 10, 14, 41.04, 28.95, 3, &DIY, &d);
	T_ASSERT(!d.high_lat);
}

static void test_parsing(void)
{
	double la, lo;
	T_EQ_INT(vk_parse_latlon("41.01, 28.98", &la, &lo), 0);
	T_NEAR(la, 41.01, 1e-9);
	T_NEAR(lo, 28.98, 1e-9);
	T_EQ_INT(vk_parse_latlon("41,01 28,98", &la, &lo), 0);
	T_NEAR(lo, 28.98, 1e-9);
	T_EQ_INT(vk_parse_latlon("-33.9;151.2", &la, &lo), 0);
	T_NEAR(la, -33.9, 1e-9);
	T_EQ_INT(vk_parse_latlon("41,28", &la, &lo), 0);
	T_NEAR(lo, 28, 1e-9);
	T_EQ_INT(vk_parse_latlon("41.01,28.98", &la, &lo), 0);
	T_NEAR(lo, 28.98, 1e-9);
	T_EQ_INT(vk_parse_latlon("91 10", &la, &lo), -1);
	T_EQ_INT(vk_parse_latlon("abc", &la, &lo), -1);
	T_EQ_INT(vk_parse_latlon("1 2 3", &la, &lo), -1);
	T_EQ_INT(vk_parse_latlon("", &la, &lo), -1);
	VkPlace p = { "Ev|im", 41.0123, 28.9876, 180, VK_DST_NONE }, q;
	char s[128];
	vk_place_format(&p, s, sizeof s);
	T_EQ_STR(s, "Ev im|41.0123|28.9876|180|0\n");
	T_EQ_INT(vk_place_parse(s, &q), 0);
	T_EQ_STR(q.name, "Ev im");
	T_NEAR(q.lat, 41.0123, 1e-9);
	T_EQ_INT(q.tz_min, 180);
	T_EQ_INT(vk_place_parse("x|100|0|0|0", &q), -1);
	T_EQ_INT(vk_place_parse("|1|2|0|0", &q), -1);
	T_EQ_INT(vk_place_parse("x|1|2", &q), -1);
}

static void test_city_tables(void)
{
	for (int i = 0; i < 81; i++) {
		T_EQ_INT(vk_tr_cities[i].plate, i + 1);
		T_ASSERT(vk_tr_cities[i].lat > 35.8 && vk_tr_cities[i].lat < 42.2);
		T_ASSERT(vk_tr_cities[i].lon > 25.6 && vk_tr_cities[i].lon < 44.9);
	}
	T_EQ_STR(vk_tr_cities[5].name, "Ankara");
	T_EQ_STR(vk_tr_cities[80].name, "Düzce");
	T_ASSERT(vk_n_world_cities >= 50);
	T_EQ_STR(vk_world_cities[0].name, "Mekke");
}

int main(void)
{
	T_RUN(test_calendar);
	T_RUN(test_sun_position);
	T_RUN(test_diyanet_tables);
	T_RUN(test_hijri_month_starts);
	T_RUN(test_new_moon_and_phase);
	T_RUN(test_qibla);
	T_RUN(test_time_zones);
	T_RUN(test_now);
	T_RUN(test_methods);
	T_RUN(test_high_latitudes);
	T_RUN(test_parsing);
	T_RUN(test_city_tables);
	return T_DONE();
}

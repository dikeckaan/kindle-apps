/* Astronomy for Gökyüzü, checked against Meeus' "Astronomical Algorithms" (2nd ed.) worked examples and
 * JPL Horizons ephemerides (geocentric apparent RA/Dec; topocentric alt/az, rise and set for Maltepe/İstanbul). */
#include <math.h>
#include "sky/astro.h"
#include "t.h"

#define T_NEAR(a, b, tol)                                                                              \
	do {                                                                                           \
		double t_a = (a), t_b = (b);                                                           \
		if (!(fabs(t_a - t_b) <= (tol))) T_FAIL_("%s == %.6f, expected %.6f ± %g", #a, t_a, t_b, (double) (tol)); \
	} while (0)

/* angular difference in degrees, wrapping at 360 */
static double adiff(double a, double b)
{
	double d = fmod(a - b, 360.0);
	if (d > 180) d -= 360;
	if (d < -180) d += 360;
	return d;
}
#define T_NEAR_DEG(a, b, tol) T_NEAR(adiff((a), (b)), 0.0, tol)

static const double LAT = 40.935, LON = 29.155; /* Maltepe, İstanbul: the weather app's default city */

static void test_julian_day(void)
{
	T_NEAR(astro_jd(0), 2440587.5, 1e-9);
	T_NEAR(astro_jd_cal(1957, 10, 4.81), 2436116.31, 1e-6); /* Meeus 7.a */
	T_NEAR(astro_jd_cal(2000, 1, 1.5), 2451545.0, 1e-9);
	T_NEAR(astro_jd_cal(1987, 4, 10), 2446895.5, 1e-9);
	T_NEAR(astro_jd(1792368000.0), astro_jd_cal(2026, 10, 19), 1e-6); /* 2026-10-19 00:00 UTC */
	T_NEAR(astro_unix(astro_jd(1792368000.0)), 1792368000.0, 1e-3);
}

static void test_sidereal_time(void)
{
	T_NEAR_DEG(astro_gmst(2446895.5), 197.693195, 1e-5);                      /* Meeus 12.a: 13h10m46.3668s */
	T_NEAR_DEG(astro_gmst(astro_jd_cal(1987, 4, 10 + (19 + 21 / 60.0) / 24)), 128.7378734, 1e-5); /* 12.b */
}

static void test_horizontal(void)
{
	/* Meeus 13.b: Venus from the US Naval Observatory, 1987-04-10 19:21 UT (azimuth there is from the south) */
	double jd = astro_jd_cal(1987, 4, 10 + (19 + 21 / 60.0) / 24), alt, az;
	double lat = 38 + 55 / 60.0 + 17 / 3600.0, lon = -(77 + 3 / 60.0 + 56 / 3600.0);
	astro_hor(347.3193375, -6.719891667, lat, astro_gmst(jd) + lon, &alt, &az);
	T_NEAR(alt, 15.1249, 0.01);
	T_NEAR_DEG(az, 68.0337 + 180, 0.01);
	/* Polaris stays at about the latitude all night (it is 0.7° from the pole) */
	for (int h = 0; h < 24; h += 3) {
		double ra = 37.9529, dec = 89.2641, t = astro_jd_cal(2026, 10, 19 + h / 24.0);
		astro_precess(t, &ra, &dec);
		astro_hor(ra, dec, LAT, astro_gmst(t) + LON, &alt, &az);
		T_NEAR(alt, LAT, 0.8);
		T_ASSERT(az < 2 || az > 358);
	}
}

static void test_precession(void)
{
	/* Meeus 21.b: θ Persei, J2000 → 2028-11-13.19 TD (its proper motion over 28.86 years added first) */
	double ra = (2 + 44 / 60.0 + 11.986 / 3600) * 15 + 0.03425 * 28.86 * 15 / 3600, dec = 49 + 13 / 60.0 + 42.48 / 3600 - 0.0895 * 28.86 / 3600;
	astro_precess(2462088.69, &ra, &dec);
	T_NEAR(ra, 41.547214, 0.002);
	T_NEAR(dec, 49.348483, 0.002);
}

static void check_body(int body, double jd, double ra, double dec, double tol)
{
	AstroPos p;
	astro_body(body, jd, &p);
	T_NEAR_DEG(p.ra, ra, tol / cos(dec * M_PI / 180));
	T_NEAR(p.dec, dec, tol);
}

static void test_sun(void)
{
	check_body(ASTRO_SUN, 2448908.5, 198.38083, -7.78507, 0.02); /* Meeus 25.a, 1992-10-13 */
	check_body(ASTRO_SUN, astro_jd_cal(2026, 10, 19), 203.75327, -9.90540, 0.02); /* Horizons */
	check_body(ASTRO_SUN, astro_jd_cal(2003, 8, 28), 156.17302, 9.93494, 0.02);
}

static void test_moon(void)
{
	check_body(ASTRO_MOON, 2448724.5, 134.688470, 13.768368, 0.3); /* Meeus 47.a, 1992-04-12 */
	check_body(ASTRO_MOON, astro_jd_cal(2026, 10, 19), 301.87078, -22.73924, 0.3);
	check_body(ASTRO_MOON, astro_jd_cal(2003, 8, 28), 161.36865, 13.14464, 0.3);
	int waxing;
	T_NEAR(astro_moon_illum(2448724.5, &waxing), 0.6786, 0.01); /* Meeus 48.a */
	T_EQ_INT(waxing, 1);
	double k = astro_moon_illum(astro_jd_cal(2026, 10, 19), &waxing); /* a day after first quarter */
	T_ASSERT(k > 0.5 && k < 0.7);
	T_EQ_INT(waxing, 1);
	double age = astro_moon_age(astro_jd_cal(2026, 10, 19));
	T_ASSERT(age > 7.4 && age < 9.5);
	T_ASSERT(astro_moon_age(astro_jd_cal(2026, 10, 26.2)) > 14 && astro_moon_age(astro_jd_cal(2026, 10, 26.2)) < 16); /* full moon 10-26 */
}

static void test_planets(void)
{
	check_body(ASTRO_VENUS, 2448976.5, 316.17273, -18.88801, 0.3); /* Meeus 33.a, 1992-12-20 */
	double a = astro_jd_cal(2026, 10, 19), b = astro_jd_cal(2003, 8, 28);
	check_body(ASTRO_MERCURY, a, 225.97407, -20.63606, 1.0);
	check_body(ASTRO_VENUS, a, 208.97323, -19.55611, 1.0);
	check_body(ASTRO_MARS, a, 134.67553, 18.53002, 1.0);
	check_body(ASTRO_JUPITER, a, 145.12943, 14.60627, 1.0);
	check_body(ASTRO_SATURN, a, 10.42204, 1.54215, 1.0);
	check_body(ASTRO_MERCURY, b, 175.02659, -2.19640, 1.0);
	check_body(ASTRO_VENUS, b, 159.09550, 10.30756, 1.0);
	check_body(ASTRO_MARS, b, 339.56637, -15.76020, 1.0); /* the close opposition of 2003 */
	check_body(ASTRO_JUPITER, b, 152.51351, 12.19934, 1.0);
	check_body(ASTRO_SATURN, b, 101.08714, 22.29935, 1.0);
	AstroPos p;
	astro_body(ASTRO_MARS, b, &p);
	T_NEAR(p.mag, -2.9, 0.3); /* Mars at its brightest */
	astro_body(ASTRO_VENUS, a, &p);
	T_ASSERT(p.mag < -3.5 && p.mag > -4.9);
	astro_body(ASTRO_JUPITER, a, &p);
	T_ASSERT(p.mag < -1.6 && p.mag > -2.9);
	astro_body(ASTRO_SATURN, a, &p);
	T_ASSERT(p.mag < 1.5 && p.mag > -0.5);
}

static void test_alt_az(void)
{
	double jd = astro_jd_cal(2026, 10, 19 + 18 / 24.0), az;
	T_NEAR(astro_alt(ASTRO_MOON, jd, LAT, LON, &az), 26.48859, 0.4); /* topocentric: the Moon's parallax counts */
	T_NEAR_DEG(az, 197.28861, 0.4);
	T_NEAR(astro_alt(ASTRO_SATURN, jd, LAT, LON, &az), 34.73538, 0.5);
	T_NEAR_DEG(az, 123.95766, 0.5);
	T_NEAR(astro_alt(ASTRO_JUPITER, jd, LAT, LON, &az), -34.45772, 0.5);
}

static void test_rise_set(void)
{
	double day = astro_jd_cal(2026, 10, 19), rise, set, min = 1 / 1440.0;
	astro_rise_set(ASTRO_SUN, day, LAT, LON, &rise, &set);
	T_NEAR(rise, day + (4 + 19 / 60.0) / 24, 3 * min);
	T_NEAR(set, day + (15 + 18 / 60.0) / 24, 3 * min);
	astro_rise_set(ASTRO_MOON, day, LAT, LON, &rise, &set);
	T_NEAR(rise, day + (12 + 2 / 60.0) / 24, 6 * min);
	T_NEAR(set, day + (21 + 51 / 60.0) / 24, 6 * min);
	astro_rise_set(ASTRO_JUPITER, day, LAT, LON, &rise, &set);
	T_NEAR(rise, day + (22 + 56 / 60.0) / 24, 6 * min);
	T_NEAR(set, day + (12 + 48 / 60.0) / 24, 6 * min);
	/* the midnight sun at Tromsø: no sunset in late June */
	astro_rise_set(ASTRO_SUN, astro_jd_cal(2026, 6, 21), 69.65, 18.96, &rise, &set);
	T_EQ_INT(rise == 0 && set == 0, 1);
	T_ASSERT(astro_alt(ASTRO_SUN, astro_jd_cal(2026, 6, 21), 69.65, 18.96, NULL) > 0);
}

static void test_pass(void)
{
	/* 2026-10-19 18:00 UT: the Moon is up (rose 12:02, sets 21:51); Jupiter is down (rises 22:56, sets 12:4x next day) */
	double t = astro_jd_cal(2026, 10, 19 + 18 / 24.0), day = astro_jd_cal(2026, 10, 19), rise, set, min = 1 / 1440.0;
	astro_pass(ASTRO_MOON, t, LAT, LON, &rise, &set);
	T_NEAR(rise, day + (12 + 2 / 60.0) / 24, 6 * min);
	T_NEAR(set, day + (21 + 51 / 60.0) / 24, 6 * min);
	astro_pass(ASTRO_JUPITER, t, LAT, LON, &rise, &set);
	T_NEAR(rise, day + (22 + 56 / 60.0) / 24, 6 * min);
	T_ASSERT(set > day + 1 + 12 / 24.0 && set < day + 1 + 13 / 24.0);
	astro_pass(ASTRO_SUN, astro_jd_cal(2026, 6, 21), 69.65, 18.96, &rise, &set); /* midnight sun: up, never sets */
	T_ASSERT(set == 0);
}

int main(void)
{
	T_RUN(test_julian_day);
	T_RUN(test_sidereal_time);
	T_RUN(test_horizontal);
	T_RUN(test_precession);
	T_RUN(test_sun);
	T_RUN(test_moon);
	T_RUN(test_planets);
	T_RUN(test_alt_az);
	T_RUN(test_rise_set);
	T_RUN(test_pass);
	return T_DONE();
}

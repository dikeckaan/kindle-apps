/* Gökyüzü astronomy, offline and low precision (about 1 arc minute for the Sun, a few for the Moon, well under a degree
 * for the planets): Julian day, sidereal time, precession, equatorial → horizontal, Sun/Moon/planet positions from
 * Paul Schlyter's "How to compute planetary positions" elements, Moon phase, rise and set times.
 * Angles are in degrees, times are Julian days (UT; the difference to TT is ignored). */
#ifndef ASTRO_H
#define ASTRO_H

double astro_jd(double unix_t);                 /* Unix time → Julian day */
double astro_unix(double jd);
double astro_jd_cal(int year, int month, double day); /* Gregorian date, day with fraction (UT) */
double astro_gmst(double jd);                   /* Greenwich mean sidereal time, 0 … 360 */
/* altitude and azimuth (from north through east) for a local sidereal time lst = gmst + east longitude */
void astro_hor(double ra, double dec, double lat, double lst, double *alt, double *az);
void astro_precess(double jd, double *ra, double *dec); /* J2000 mean place → mean place of the date, in place */

enum { ASTRO_SUN, ASTRO_MOON, ASTRO_MERCURY, ASTRO_VENUS, ASTRO_MARS, ASTRO_JUPITER, ASTRO_SATURN, ASTRO_N_BODY };
/* geocentric, equator and ecliptic of date; dist in AU (the Moon: Earth radii); mag the visual magnitude */
typedef struct { double ra, dec, lon, lat, dist, mag; } AstroPos;
void astro_body(int body, double jd, AstroPos *p);

double astro_moon_illum(double jd, int *waxing); /* illuminated fraction 0 … 1; waxing may be NULL */
double astro_moon_age(double jd);                /* days since new moon (0 … 29.53), from the Moon's elongation */

/* altitude seen from lat/lon (the Moon topocentric, i.e. with its parallax); az may be NULL */
double astro_alt(int body, double jd, double lat, double lon, double *az);
/* the rising and setting in [jd0, jd0 + 1) (upper limb with refraction for the Sun and Moon), 0 when there is none */
void astro_rise_set(int body, double jd0, double lat, double lon, double *rise, double *set);
/* the current or next time above the horizon: when it rose (or will rise) and when it sets; 0 when not found within
 * a day (rise: up all of the past day; set: up all of the next) */
void astro_pass(int body, double jd, double lat, double lon, double *rise, double *set);
/* the first time after jd within `days` when the body's altitude crosses h going up (up = 1) or down; 0 if none */
double astro_next_cross(int body, double jd, double lat, double lon, double h, int up, double days);
#endif

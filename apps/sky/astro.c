/* Low-precision astronomy: Meeus' "Astronomical Algorithms" for time, sidereal time and precession; orbital elements
 * and perturbations from Paul Schlyter, "How to compute planetary positions" (stjarnhimlen.se/comp/ppcomp.html). */
#include <math.h>
#include <stddef.h>
#include "astro.h"

#define RAD (M_PI / 180.0)
static double sind(double x) { return sin(x * RAD); }
static double cosd(double x) { return cos(x * RAD); }
static double atan2d(double y, double x) { return atan2(y, x) / RAD; }
static double rev(double x) { return x - 360.0 * floor(x / 360.0); } /* 0 … 360 */

double astro_jd(double unix_t) { return unix_t / 86400.0 + 2440587.5; }
double astro_unix(double jd) { return (jd - 2440587.5) * 86400.0; }

double astro_jd_cal(int y, int m, double d)
{
	if (m <= 2) {
		y--;
		m += 12;
	}
	int a = y / 100, b = 2 - a + a / 4;
	return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + b - 1524.5;
}

double astro_gmst(double jd)
{
	double t = (jd - 2451545.0) / 36525.0;
	return rev(280.46061837 + 360.98564736629 * (jd - 2451545.0) + t * t * (0.000387933 - t / 38710000.0));
}

void astro_hor(double ra, double dec, double lat, double lst, double *alt, double *az)
{
	double h = lst - ra;
	*alt = asin(sind(lat) * sind(dec) + cosd(lat) * cosd(dec) * cosd(h)) / RAD;
	*az = rev(atan2d(-cosd(dec) * sind(h), sind(dec) * cosd(lat) - cosd(dec) * sind(lat) * cosd(h)));
}

void astro_precess(double jd, double *ra, double *dec)
{
	double t = (jd - 2451545.0) / 36525.0;
	double zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t * t * t) / 3600.0;
	double z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t * t * t) / 3600.0;
	double th = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t * t * t) / 3600.0;
	double a = cosd(*dec) * sind(*ra + zeta);
	double b = cosd(th) * cosd(*dec) * cosd(*ra + zeta) - sind(th) * sind(*dec);
	double c = sind(th) * cosd(*dec) * cosd(*ra + zeta) + cosd(th) * sind(*dec);
	*ra = rev(atan2d(a, b) + z);
	*dec = asin(c) / RAD;
}

/* ---- Schlyter's method: d = days since 1999-12-31 0h, elements referred to the ecliptic and equinox of the date ---- */

typedef struct { double N, i, w, a, e, M; } Orbit;

static void orbit(int body, double d, Orbit *o)
{
	switch (body) {
	case ASTRO_SUN:
		*o = (Orbit){ 0, 0, 282.9404 + 4.70935E-5 * d, 1.0, 0.016709 - 1.151E-9 * d, 356.0470 + 0.9856002585 * d };
		break;
	case ASTRO_MOON:
		*o = (Orbit){ 125.1228 - 0.0529538083 * d, 5.1454, 318.0634 + 0.1643573223 * d, 60.2666, 0.054900, 115.3654 + 13.0649929509 * d };
		break;
	case ASTRO_MERCURY:
		*o = (Orbit){ 48.3313 + 3.24587E-5 * d, 7.0047 + 5.00E-8 * d, 29.1241 + 1.01444E-5 * d, 0.387098, 0.205635 + 5.59E-10 * d, 168.6562 + 4.0923344368 * d };
		break;
	case ASTRO_VENUS:
		*o = (Orbit){ 76.6799 + 2.46590E-5 * d, 3.3946 + 2.75E-8 * d, 54.8910 + 1.38374E-5 * d, 0.723330, 0.006773 - 1.302E-9 * d, 48.0052 + 1.6021302244 * d };
		break;
	case ASTRO_MARS:
		*o = (Orbit){ 49.5574 + 2.11081E-5 * d, 1.8497 - 1.78E-8 * d, 286.5016 + 2.92961E-5 * d, 1.523688, 0.093405 + 2.516E-9 * d, 18.6021 + 0.5240207766 * d };
		break;
	case ASTRO_JUPITER:
		*o = (Orbit){ 100.4542 + 2.76854E-5 * d, 1.3030 - 1.557E-7 * d, 273.8777 + 1.64505E-5 * d, 5.20256, 0.048498 + 4.469E-9 * d, 19.8950 + 0.0830853001 * d };
		break;
	default: /* Saturn */
		*o = (Orbit){ 113.6634 + 2.38980E-5 * d, 2.4886 - 1.081E-7 * d, 339.3939 + 2.97661E-5 * d, 9.55475, 0.055546 - 9.499E-9 * d, 316.9670 + 0.0334442282 * d };
		break;
	}
	o->M = rev(o->M);
}

/* position in the orbit's own plane: true anomaly v and radius r */
static void kepler(const Orbit *o, double *v, double *r)
{
	double E = o->M + o->e / RAD * sind(o->M) * (1.0 + o->e * cosd(o->M));
	for (int k = 0; k < 20; k++) {
		double dE = (E - o->e / RAD * sind(E) - o->M) / (1.0 - o->e * cosd(E));
		E -= dE;
		if (fabs(dE) < 1e-7) break;
	}
	double xv = o->a * (cosd(E) - o->e), yv = o->a * sqrt(1.0 - o->e * o->e) * sind(E);
	*v = atan2d(yv, xv);
	*r = sqrt(xv * xv + yv * yv);
}

/* ecliptic rectangular coordinates of the orbit */
static void ecl_xyz(const Orbit *o, double v, double r, double *x, double *y, double *z)
{
	double vw = v + o->w;
	*x = r * (cosd(o->N) * cosd(vw) - sind(o->N) * sind(vw) * cosd(o->i));
	*y = r * (sind(o->N) * cosd(vw) + cosd(o->N) * sind(vw) * cosd(o->i));
	*z = r * sind(vw) * sind(o->i);
}

static double obliquity(double d) { return 23.4393 - 3.563E-7 * d; }

static void ecl_to_eq(double lon, double lat, double d, double *ra, double *dec)
{
	double x = cosd(lon) * cosd(lat), y = sind(lon) * cosd(lat), z = sind(lat), e = obliquity(d);
	double ye = y * cosd(e) - z * sind(e), ze = y * sind(e) + z * cosd(e);
	*ra = rev(atan2d(ye, x));
	*dec = atan2d(ze, sqrt(x * x + ye * ye));
}

static void sun_lon(double d, double *lon, double *r)
{
	Orbit o;
	double v;
	orbit(ASTRO_SUN, d, &o);
	kepler(&o, &v, r);
	*lon = rev(v + o.w);
}

static void moon_pos(double d, AstroPos *p)
{
	Orbit o, s;
	double v, r, x, y, z;
	orbit(ASTRO_MOON, d, &o);
	orbit(ASTRO_SUN, d, &s);
	kepler(&o, &v, &r);
	ecl_xyz(&o, v, r, &x, &y, &z);
	double lon = atan2d(y, x), lat = atan2d(z, sqrt(x * x + y * y));
	double Ms = s.M, Mm = o.M, Ls = Ms + s.w, Lm = Mm + o.w + o.N, D = Lm - Ls, F = Lm - o.N;
	lon += -1.274 * sind(Mm - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms) - 0.059 * sind(2 * Mm - 2 * D) - 0.057 * sind(Mm - 2 * D + Ms) +
	       0.053 * sind(Mm + 2 * D) + 0.046 * sind(2 * D - Ms) + 0.041 * sind(Mm - Ms) - 0.035 * sind(D) - 0.031 * sind(Mm + Ms) -
	       0.015 * sind(2 * F - 2 * D) + 0.011 * sind(Mm - 4 * D);
	lat += -0.173 * sind(F - 2 * D) - 0.055 * sind(Mm - F - 2 * D) - 0.046 * sind(Mm + F - 2 * D) + 0.033 * sind(F + 2 * D) + 0.017 * sind(2 * Mm + F);
	r += -0.58 * cosd(Mm - 2 * D) - 0.46 * cosd(2 * D);
	p->lon = rev(lon);
	p->lat = lat;
	p->dist = r;
	p->mag = -12.7;
}

static void planet_pos(int body, double d, AstroPos *p)
{
	Orbit o;
	double v, r, x, y, z;
	orbit(body, d, &o);
	kepler(&o, &v, &r);
	ecl_xyz(&o, v, r, &x, &y, &z);
	if (body == ASTRO_JUPITER || body == ASTRO_SATURN) { /* the great inequality and friends */
		Orbit j, s;
		orbit(ASTRO_JUPITER, d, &j);
		orbit(ASTRO_SATURN, d, &s);
		double Mj = j.M, Ms = s.M, lon = atan2d(y, x), lat = atan2d(z, sqrt(x * x + y * y));
		if (body == ASTRO_JUPITER) {
			lon += -0.332 * sind(2 * Mj - 5 * Ms - 67.6) - 0.056 * sind(2 * Mj - 2 * Ms + 21) + 0.042 * sind(3 * Mj - 5 * Ms + 21) -
			       0.036 * sind(Mj - 2 * Ms) + 0.022 * cosd(Mj - Ms) + 0.023 * sind(2 * Mj - 3 * Ms + 52) - 0.016 * sind(Mj - 5 * Ms - 69);
		} else {
			lon += 0.812 * sind(2 * Mj - 5 * Ms - 67.6) - 0.229 * cosd(2 * Mj - 4 * Ms - 2) + 0.119 * sind(Mj - 2 * Ms - 3) +
			       0.046 * sind(2 * Mj - 6 * Ms - 69) + 0.014 * sind(Mj - 3 * Ms + 32);
			lat += -0.020 * cosd(2 * Mj - 4 * Ms - 2) + 0.018 * sind(2 * Mj - 6 * Ms - 49);
		}
		x = r * cosd(lon) * cosd(lat);
		y = r * sind(lon) * cosd(lat);
		z = r * sind(lat);
	}
	double ls, rs;
	sun_lon(d, &ls, &rs);
	double xg = x + rs * cosd(ls), yg = y + rs * sind(ls), zg = z;
	double R = sqrt(xg * xg + yg * yg + zg * zg);
	p->lon = rev(atan2d(yg, xg));
	p->lat = atan2d(zg, sqrt(xg * xg + yg * yg));
	p->dist = R;
	/* magnitude from the phase angle FV (Sun–planet–Earth) */
	double cfv = (r * r + R * R - rs * rs) / (2 * r * R);
	double fv = acos(cfv > 1 ? 1 : cfv < -1 ? -1 : cfv) / RAD, l5 = 5 * log10(r * R);
	switch (body) {
	case ASTRO_MERCURY: p->mag = -0.36 + l5 + 0.027 * fv + 2.2E-13 * pow(fv, 6); break;
	case ASTRO_VENUS: p->mag = -4.34 + l5 + 0.013 * fv + 4.2E-7 * fv * fv * fv; break;
	case ASTRO_MARS: p->mag = -1.51 + l5 + 0.016 * fv; break;
	case ASTRO_JUPITER: p->mag = -9.25 + l5 + 0.014 * fv; break;
	default: { /* Saturn: the rings' tilt B towards us brightens it */
		double ir = 28.06, Nr = 169.51 + 3.82E-5 * d;
		double B = asin(sind(p->lat) * cosd(ir) - cosd(p->lat) * sind(ir) * sind(p->lon - Nr));
		p->mag = -9.0 + l5 + 0.044 * fv - 2.6 * sin(fabs(B)) + 1.2 * sin(B) * sin(B);
	}
	}
}

void astro_body(int body, double jd, AstroPos *p)
{
	double d = jd - 2451543.5;
	if (body == ASTRO_SUN) {
		double r;
		sun_lon(d, &p->lon, &r);
		p->lat = 0;
		p->dist = r;
		p->mag = -26.7;
	} else if (body == ASTRO_MOON) {
		moon_pos(d, p);
	} else {
		planet_pos(body, d, p);
	}
	ecl_to_eq(p->lon, p->lat, d, &p->ra, &p->dec);
}

/* elongation of the Moon from the Sun (0 … 180) and whether it is east of the Sun (waxing) */
static double moon_elong(double jd, int *waxing)
{
	AstroPos s, m;
	astro_body(ASTRO_SUN, jd, &s);
	astro_body(ASTRO_MOON, jd, &m);
	if (waxing) *waxing = rev(m.lon - s.lon) < 180;
	double c = cosd(m.lat) * cosd(m.lon - s.lon);
	return acos(c > 1 ? 1 : c < -1 ? -1 : c) / RAD;
}

double astro_moon_illum(double jd, int *waxing)
{
	double i = 180 - moon_elong(jd, waxing); /* phase angle; the Moon's distance is small next to the Sun's */
	return (1 + cosd(i)) / 2;
}

double astro_moon_age(double jd)
{
	AstroPos s, m;
	astro_body(ASTRO_SUN, jd, &s);
	astro_body(ASTRO_MOON, jd, &m);
	return rev(m.lon - s.lon) / 360.0 * 29.530588853;
}

double astro_alt(int body, double jd, double lat, double lon, double *az)
{
	AstroPos p;
	double alt, a;
	astro_body(body, jd, &p);
	astro_hor(p.ra, p.dec, lat, astro_gmst(jd) + lon, &alt, &a);
	if (body == ASTRO_MOON) alt -= asin(1.0 / p.dist) / RAD * cosd(alt); /* parallax: about a degree lower */
	if (az) *az = a;
	return alt;
}

static double h0_of(int body)
{
	return body == ASTRO_SUN || body == ASTRO_MOON ? -0.833 : -0.567; /* refraction (+ semidiameter) */
}

/* bisect a crossing of h between t0 (alt - h has sign s0) and t1 */
static double refine(int body, double t0, double t1, double lat, double lon, double h)
{
	double f0 = astro_alt(body, t0, lat, lon, NULL) - h;
	for (int k = 0; k < 24; k++) {
		double tm = (t0 + t1) / 2, fm = astro_alt(body, tm, lat, lon, NULL) - h;
		if ((fm < 0) == (f0 < 0)) {
			t0 = tm;
			f0 = fm;
		} else {
			t1 = tm;
		}
	}
	return (t0 + t1) / 2;
}

double astro_next_cross(int body, double jd, double lat, double lon, double h, int up, double days)
{
	const double step = 10.0 / 1440;
	double prev = astro_alt(body, jd, lat, lon, NULL) - h;
	for (double t = jd + step; t <= jd + days + 1e-9; t += step) {
		double f = astro_alt(body, t, lat, lon, NULL) - h;
		if (up ? (prev < 0 && f >= 0) : (prev >= 0 && f < 0)) return refine(body, t - step, t, lat, lon, h);
		prev = f;
	}
	return 0;
}

void astro_rise_set(int body, double jd0, double lat, double lon, double *rise, double *set)
{
	double h = h0_of(body), r, s;
	/* start a moment before jd0 so that a crossing exactly at jd0 is found */
	r = astro_next_cross(body, jd0 - 1e-6, lat, lon, h, 1, 1.0);
	s = astro_next_cross(body, jd0 - 1e-6, lat, lon, h, 0, 1.0);
	*rise = r >= jd0 && r < jd0 + 1 ? r : 0;
	*set = s >= jd0 && s < jd0 + 1 ? s : 0;
}

void astro_pass(int body, double jd, double lat, double lon, double *rise, double *set)
{
	double h = h0_of(body);
	*rise = *set = 0;
	if (astro_alt(body, jd, lat, lon, NULL) >= h) { /* up now: the last rising within the past day */
		for (double t = jd - 1;;) {
			double c = astro_next_cross(body, t, lat, lon, h, 1, jd - t);
			if (!c || c > jd) break;
			*rise = c;
			t = c + 1e-4;
		}
		*set = astro_next_cross(body, jd, lat, lon, h, 0, 1.0);
	} else {
		*rise = astro_next_cross(body, jd, lat, lon, h, 1, 1.0);
		if (*rise) *set = astro_next_cross(body, *rise, lat, lon, h, 0, 1.0);
	}
}

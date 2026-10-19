/* Vakitler pure logic: see vakit.h.
 *
 * Accuracy against Diyanet (checked 2026-10-19 with tests/host/test_vakit.c and a wider offline comparison):
 * Diyanet's own tables (namazvakitleri.diyanet.gov.tr data, as served by its mirror ezanvakti.emushaf.net) for
 * Ankara, Antalya, Edirne, Erzurum, Hakkari, İstanbul, İzmir, Sinop, Trabzon and Van, 14.10–15.11.2026, and for
 * İstanbul 12.04–13.05.2025 and 20.02–23.03.2026 (Ramazan 1447) — 384 days × 6 times: before rounding every
 * computed time is within 0.7 min of the published minute, about 90 % round to the same minute and none is off by
 * more than 1 min. The remaining difference is mostly the coordinates: Diyanet's exact reference point for a
 * province is not published, these are the province centres used by apps/deprem (two decimals ≈ ±4 s).
 * The temkin values (güneş −7, öğle +5, ikindi +4, akşam +7, none for imsak and yatsı) are the ones Diyanet states
 * for its calendars (e.g. quoted in Yeni Şafak's "il il iftar saatleri"); the 18°/17° angles are Diyanet's since 1983.
 * Kıble saati (the Sun's azimuth = qibla bearing) is within 1 min of Diyanet's KibleSaati on all 384 days.
 * Hijri dates: all 384 days of the same tables and Diyanet's 2023–2026 religious days (Ramazan, bayrams, Hicri
 * yılbaşı, Aşure, üç aylar) match; the plain arithmetic calendar would be a day behind on many of them.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vakit.h"
#include "sky/astro.h"

#ifndef N_
#define N_(s) (s) /* marks text for lang/template.txt (tools/i18n/extract.py); shown with T() */
#endif

#define RAD (M_PI / 180.0)
static double sind(double x) { return sin(x * RAD); }
static double cosd(double x) { return cos(x * RAD); }
static double rev(double x) { return x - 360.0 * floor(x / 360.0); }

const char *const vk_names[VK_N] = { N_("İmsak"), N_("Güneş"), N_("Öğle"), N_("İkindi"), N_("Akşam"), N_("Yatsı") };
const char *const vk_until_fmt[VK_N] = { N_("İmsaka %s"), N_("Güneşe %s"), N_("Öğleye %s"), N_("İkindiye %s"), N_("Akşama %s"), N_("Yatsıya %s") };

static const VkMethod methods[VK_N_METHODS] = {
	{ N_("Diyanet (Türkiye)"), 18, 17, 0, { 0, -7, 5, 4, 7, 0 }, 1 },
	{ N_("Müslüman Dünya Birliği (MWL)"), 18, 17, 0, { 0 }, 0 },
	{ N_("Kuzey Amerika (ISNA)"), 15, 15, 0, { 0 }, 0 },
	{ N_("Mısır (Genel Etüt Kurumu)"), 19.5, 17.5, 0, { 0 }, 0 },
	{ N_("Ümmü'l-Kurâ (Mekke)"), 18.5, 0, 90, { 0 }, 0 },
	{ N_("Karaçi (İslami İlimler Üniv.)"), 18, 18, 0, { 0 }, 0 },
	{ N_("Fransa (UOIF)"), 12, 12, 0, { 0 }, 0 },
};

const VkMethod *vk_method(int id) { return &methods[id >= 0 && id < VK_N_METHODS ? id : 0]; }

/* ---- calendar ---- */

long vk_jdn(int y, int m, int d)
{
	long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
	return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

void vk_from_jdn(long j, int *y, int *m, int *d)
{
	long a = j + 32044, b = (4 * a + 3) / 146097, c = a - 146097 * b / 4;
	long dd = (4 * c + 3) / 1461, e = c - 1461 * dd / 4, mm = (5 * e + 2) / 153;
	*d = (int) (e - (153 * mm + 2) / 5 + 1);
	*m = (int) (mm + 3 - 12 * (mm / 10));
	*y = (int) (100 * b + dd - 4800 + mm / 10);
}

int vk_wday(long jdn) { return (int) ((jdn + 1) % 7); }

int vk_month_days(int y, int m)
{
	static const int n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m == 2) return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 ? 29 : 28;
	return m >= 1 && m <= 12 ? n[m - 1] : 30;
}

/* ---- the Sun ---- */

/* Meeus ch. 25 (low accuracy): apparent declination (degrees) and the equation of time (minutes) */
static void sun_ephem(double jd, double *dec, double *eot)
{
	double T = (jd - 2451545.0) / 36525;
	double L0 = rev(280.46646 + T * (36000.76983 + 0.0003032 * T));
	double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
	double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
	double C = (1.914602 - T * (0.004817 + 0.000014 * T)) * sind(M) + (0.019993 - 0.000101 * T) * sind(2 * M) + 0.000289 * sind(3 * M);
	double om = 125.04 - 1934.136 * T, lam = L0 + C - 0.00569 - 0.00478 * sind(om);
	double eps = 23 + (26 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60) / 60 + 0.00256 * cosd(om);
	*dec = asin(sind(eps) * sind(lam)) / RAD;
	double y = tan(eps * RAD / 2);
	y *= y;
	*eot = 4 / RAD * (y * sind(2 * L0) - 2 * e * sind(M) + 4 * e * y * sind(M) * cosd(2 * L0) - 0.5 * y * y * sind(4 * L0) - 1.25 * e * e * sind(2 * M));
}

/* hour angle (degrees) at which the Sun has altitude h; NAN if it never does */
static double hour_angle(double lat, double dec, double h)
{
	double c = (sind(h) - sind(lat) * sind(dec)) / (cosd(lat) * cosd(dec));
	if (c < -1 || c > 1) return NAN;
	return acos(c) / RAD;
}

enum { EV_NOON, EV_ALT, EV_ASR };
typedef struct { double j0, lat, lon, tz; int ut0; } DayCtx;

/* local hour of an event: kind EV_ALT with altitude h on side (-1 morning, +1 evening), EV_ASR with shadow factor h */
static double event(const DayCtx *c, int kind, double h, int side, double guess)
{
	double t = guess;
	for (int i = 0; i < (c->ut0 ? 1 : 4); i++) {
		double dec, eot;
		sun_ephem(c->ut0 ? c->j0 : c->j0 + (t - c->tz) / 24, &dec, &eot);
		double noon = 12 + c->tz - c->lon / 15 - eot / 60, H = 0;
		if (kind == EV_ALT) H = hour_angle(c->lat, dec, h);
		else if (kind == EV_ASR) H = hour_angle(c->lat, dec, atan(1 / (h + tan(fabs(c->lat - dec) * RAD))) / RAD);
		if (isnan(H)) return NAN;
		t = noon + side * H / 15;
	}
	return t;
}

void vk_day(int y, int m, int d, double lat, double lon, double tz, const VkOpts *o, VkDay *out)
{
	const VkMethod *me = vk_method(o ? o->method : 0);
	DayCtx c = { (double) vk_jdn(y, m, d) - 0.5, lat, lon, tz, me->ut0 };
	memset(out, 0, sizeof *out);
	out->noon = event(&c, EV_NOON, 0, 0, 12);
	out->sunrise = event(&c, EV_ALT, -0.833, -1, 6);
	out->sunset = event(&c, EV_ALT, -0.833, 1, 18);
	if (isnan(out->sunrise)) { /* polar: always up or always down */
		double dec, eot;
		sun_ephem(c.j0 + 0.5, &dec, &eot);
		out->day_len = (sind(lat) * sind(dec) > 0 && fabs(lat) + fabs(dec) > 89.17) ? 24 : 0;
		out->sunset = NAN;
	} else {
		out->day_len = out->sunset - out->sunrise;
	}
	double fajr = event(&c, EV_ALT, -me->fajr, -1, 5);
	double asr = event(&c, EV_ASR, o && o->hanafi ? 2 : 1, 1, 15);
	double isha;
	if (me->isha_min) isha = isnan(out->sunset) ? NAN : out->sunset + (me->isha_min + (o && o->ramadan ? 30 : 0)) / 60.0;
	else isha = event(&c, EV_ALT, -me->isha, 1, 20);
	if (!isnan(out->sunrise)) { /* high latitudes: at most angle/60 of the night */
		double night = 24 - out->day_len;
		double pf = me->fajr / 60 * night;
		if (isnan(fajr) || out->sunrise - fajr > pf) {
			fajr = out->sunrise - pf;
			out->high_lat = 1;
		}
		if (!me->isha_min) {
			double pi = me->isha / 60 * night;
			if (isnan(isha) || isha - out->sunset > pi) {
				isha = out->sunset + pi;
				out->high_lat = 1;
			}
		}
	}
	double raw[VK_N] = { fajr, out->sunrise, out->noon, asr, out->sunset, isha };
	for (int i = 0; i < VK_N; i++) {
		out->t[i] = isnan(raw[i]) ? NAN : raw[i] + me->temkin[i] / 60.0;
		out->min[i] = isnan(out->t[i]) ? -1 : (int) floor(out->t[i] * 60 + 0.5);
	}
}

void vk_sun_azalt(double unix_t, double lat, double lon, double *az, double *alt)
{
	double dec, eot;
	sun_ephem(unix_t / 86400.0 + 2440587.5, &dec, &eot);
	double ut_min = fmod(unix_t, 86400.0) / 60;
	if (ut_min < 0) ut_min += 1440;
	double H = (ut_min + eot + 4 * lon) / 4 - 180; /* true solar time → hour angle */
	double a = asin(sind(lat) * sind(dec) + cosd(lat) * cosd(dec) * cosd(H)) / RAD;
	double z = atan2(sind(H), cosd(H) * sind(lat) - tan(dec * RAD) * cosd(lat)) / RAD + 180;
	if (alt) *alt = a;
	if (az) *az = rev(z);
}

/* ---- qibla ---- */

void vk_qibla(double lat, double lon, double *bearing, double *km)
{
	double dl = VK_KAABA_LON - lon;
	double b = atan2(sind(dl), cosd(lat) * tan(VK_KAABA_LAT * RAD) - sind(lat) * cosd(dl)) / RAD;
	if (bearing) *bearing = rev(b);
	if (km) {
		double p1 = lat * RAD, p2 = VK_KAABA_LAT * RAD, dp = p2 - p1, dlr = dl * RAD;
		double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dlr / 2) * sin(dlr / 2);
		*km = 2 * 6371.0 * atan2(sqrt(a), sqrt(1 - a));
	}
}

static double wrap180(double x) { return rev(x + 180) - 180; }

double vk_qibla_time(int y, int m, int d, double lat, double lon, double tz)
{
	double q;
	vk_qibla(lat, lon, &q, NULL);
	double u0 = ((double) vk_jdn(y, m, d) - 0.5 - 2440587.5) * 86400.0 - tz * 3600.0; /* local midnight */
	double prev = NAN;
	for (int k = 0; k <= 1440; k += 2) {
		double az, alt, t = u0 + k * 60.0;
		vk_sun_azalt(t, lat, lon, &az, &alt);
		double diff = wrap180(az - q);
		if (!isnan(prev) && prev < 0 && diff >= 0 && diff - prev < 90 && alt > 0) {
			double lo = t - 120, hi = t;
			for (int i = 0; i < 30; i++) {
				double mid = (lo + hi) / 2;
				vk_sun_azalt(mid, lat, lon, &az, NULL);
				if (wrap180(az - q) < 0) lo = mid;
				else hi = mid;
			}
			return (lo - u0) / 3600.0;
		}
		prev = alt > 0 ? diff : NAN;
	}
	return NAN;
}

/* ---- places and time zones ---- */

/* unix time of y-m-d at hour h UTC */
static long unix_at(int y, int m, int d, double h) { return (vk_jdn(y, m, d) - 2440588L) * 86400L + (long) (h * 3600); }

/* the day of month of the last (n = -1) or n-th weekday wd in y-m */
static int nth_wday(int y, int m, int wd, int n)
{
	if (n < 0) {
		int last = vk_month_days(y, m), w = vk_wday(vk_jdn(y, m, last));
		return last - (w - wd + 7) % 7;
	}
	int w = vk_wday(vk_jdn(y, m, 1));
	return 1 + (wd - w + 7) % 7 + 7 * (n - 1);
}

int vk_utc_offset(const VkPlace *p, long t)
{
	int std = p->tz_min * 60, y, mo, d;
	long ls = t + std, days = ls / 86400 - (ls < 0 && ls % 86400 ? 1 : 0);
	vk_from_jdn(days + 2440588L, &y, &mo, &d);
	double sh = std / 3600.0; /* standard offset in hours */
	int on = 0;
	switch (p->dst) {
	case VK_DST_EU:
		on = t >= unix_at(y, 3, nth_wday(y, 3, 0, -1), 1) && t < unix_at(y, 10, nth_wday(y, 10, 0, -1), 1);
		break;
	case VK_DST_US:
		on = t >= unix_at(y, 3, nth_wday(y, 3, 0, 2), 2 - sh) && t < unix_at(y, 11, nth_wday(y, 11, 0, 1), 1 - sh);
		break;
	case VK_DST_AU: /* southern summer: from October to April */
		on = t >= unix_at(y, 10, nth_wday(y, 10, 0, 1), 2 - sh) || t < unix_at(y, 4, nth_wday(y, 4, 0, 1), 2 - sh);
		break;
	case VK_DST_EG: /* last Friday of April 00:00 to the end of the last Thursday of October */
		on = t >= unix_at(y, 4, nth_wday(y, 4, 5, -1), -sh) && t < unix_at(y, 10, nth_wday(y, 10, 4, -1), 24 - 1 - sh);
		break;
	default: break;
	}
	return std + (on ? 3600 : 0);
}

void vk_place_from_def(const VkCityDef *c, VkPlace *p)
{
	memset(p, 0, sizeof *p);
	snprintf(p->name, sizeof p->name, "%s", c->name);
	p->lat = c->lat;
	p->lon = c->lon;
	p->tz_min = c->tz_min;
	p->dst = c->dst;
}

int vk_place_parse(const char *s, VkPlace *p)
{
	VkPlace q;
	memset(&q, 0, sizeof q);
	const char *bar = strchr(s, '|');
	if (!bar || bar == s || bar - s >= (long) sizeof q.name) return -1;
	memcpy(q.name, s, (size_t) (bar - s));
	if (sscanf(bar + 1, "%lf|%lf|%d|%d", &q.lat, &q.lon, &q.tz_min, &q.dst) != 4) return -1;
	if (fabs(q.lat) > 90 || fabs(q.lon) > 180 || q.tz_min < -14 * 60 || q.tz_min > 14 * 60 || q.dst < 0 || q.dst > VK_DST_DEVICE) return -1;
	*p = q;
	return 0;
}

void vk_place_format(const VkPlace *p, char *out, int cap)
{
	char name[64];
	snprintf(name, sizeof name, "%s", p->name);
	for (char *c = name; *c; c++)
		if (*c == '|' || *c == '\n') *c = ' ';
	snprintf(out, (size_t) cap, "%s|%.4f|%.4f|%d|%d\n", name, p->lat, p->lon, p->tz_min, p->dst);
}

int vk_parse_latlon(const char *s, double *lat, double *lon)
{
	char b[80], t[2][40];
	snprintf(b, sizeof b, "%s", s);
	for (char *c = b; *c; c++)
		if (*c == ';' || *c == '\t') *c = ' ';
	int has_dot = strchr(b, '.') != NULL, n = 0;
	if (has_dot) {
		for (char *c = b; *c; c++)
			if (*c == ',') *c = ' ';
	}
	for (char *c = strtok(b, " "); c && n < 3; c = strtok(NULL, " ")) {
		if (n < 2) snprintf(t[n], sizeof t[n], "%s", c);
		n++;
	}
	if (n == 1) { /* "41,28": the comma separates */
		char *k = strchr(t[0], ',');
		if (!k || strchr(k + 1, ',')) return -1;
		*k = '\0';
		snprintf(t[1], sizeof t[1], "%s", k + 1);
		n = 2;
	}
	if (n != 2) return -1;
	double v[2];
	for (int i = 0; i < 2; i++) {
		size_t l = strlen(t[i]);
		if (l && t[i][l - 1] == ',') t[i][l - 1] = '\0';
		for (char *c = t[i]; *c; c++)
			if (*c == ',') *c = '.';
		char *end;
		v[i] = strtod(t[i], &end);
		if (end == t[i] || *end) return -1;
	}
	if (fabs(v[0]) > 90 || fabs(v[1]) > 180) return -1;
	*lat = v[0];
	*lon = v[1];
	return 0;
}

/* ---- now ---- */

int vk_now(long now, double lat, double lon, int utc_off, const VkOpts *o, VkNow *out, VkDay *today)
{
	long local = now + utc_off, days = local / 86400 - (local < 0 && local % 86400 ? 1 : 0);
	long sec = local - days * 86400;
	double tz = utc_off / 3600.0;
	VkDay day;
	memset(out, 0, sizeof *out);
	vk_from_jdn(days + 2440588L, &out->y, &out->m, &out->d);
	out->now_min = (int) (sec / 60);
	vk_day(out->y, out->m, out->d, lat, lon, tz, o, &day);
	if (today) *today = day;
	out->cur = VK_YATSI;
	for (int i = 0; i < VK_N; i++)
		if (day.min[i] >= 0 && day.min[i] * 60L <= sec) out->cur = i;
	for (int k = 0; k < 3; k++) {
		if (k) {
			int y, m, d;
			vk_from_jdn(days + k + 2440588L, &y, &m, &d);
			vk_day(y, m, d, lat, lon, tz, o, &day);
		}
		for (int i = 0; i < VK_N; i++) {
			long at = (days + k) * 86400L + day.min[i] * 60L;
			if (day.min[i] >= 0 && at > local) {
				out->next = i;
				out->next_at = at - utc_off;
				return 0;
			}
		}
	}
	out->next = -1;
	return -1;
}

void vk_fmt_left(long secs, char *out, int cap, const char *hm_fmt, const char *h_fmt, const char *m_fmt)
{
	long mins = secs <= 0 ? 0 : (secs + 59) / 60;
	if (mins < 60) snprintf(out, (size_t) cap, m_fmt, (int) mins);
	else if (mins % 60 == 0) snprintf(out, (size_t) cap, h_fmt, (int) (mins / 60));
	else snprintf(out, (size_t) cap, hm_fmt, (int) (mins / 60), (int) (mins % 60));
}

int vk_next_prayer(long now, double lat, double lon, int utc_off, long *at)
{
	VkOpts o = { VK_DIYANET, 0, 0 };
	VkNow n;
	if (vk_now(now, lat, lon, utc_off, &o, &n, NULL) != 0) return -1;
	if (at) *at = n.next_at;
	return n.next;
}

/* ---- Hijri ---- */

const char *const vk_hijri_months[12] = { N_("Muharrem"), N_("Safer"), N_("Rebiülevvel"), N_("Rebiülahir"), N_("Cemaziyelevvel"), N_("Cemaziyelahir"),
					  N_("Recep"), N_("Şaban"), N_("Ramazan"), N_("Şevval"), N_("Zilkade"), N_("Zilhicce") };

void vk_hijri_tabular(long jdn, VkHijri *h)
{
	long l = jdn - 1948440 + 10632, n = (l - 1) / 10631;
	l = l - 10631 * n + 354;
	long j = ((10985 - l) / 5316) * ((50 * l) / 17719) + (l / 5670) * ((43 * l) / 15238);
	l = l - ((30 - j) / 15) * ((17719 * j) / 50) - (j / 16) * ((15238 * j) / 43) + 29;
	long m = (24 * l) / 709;
	h->d = (int) (l - (709 * m) / 24);
	h->m = (int) m;
	h->y = (int) (30 * n + j - 30);
}

double vk_new_moon(double jd)
{
	for (int i = 0; i < 8; i++) {
		AstroPos s, m;
		astro_body(ASTRO_SUN, jd, &s);
		astro_body(ASTRO_MOON, jd, &m);
		double diff = wrap180(m.lon - s.lon);
		jd -= diff / 12.190749; /* the Moon gains on the Sun about 12.19° a day */
		if (fabs(diff) < 1e-5) break;
	}
	return jd;
}

/* sunset at a site on the UT date jdn (tz 0), as a Julian day; NAN if none */
static double sunset_jd(long jdn, double lat, double lon)
{
	DayCtx c = { (double) jdn - 0.5, lat, lon, 0, 0 };
	double t = event(&c, EV_ALT, -0.833, 1, 18 - lon / 15);
	return isnan(t) ? NAN : c.j0 + t / 24;
}

/* The 2016 criterion on the evening of date jdn for a conjunction at jd conj: somewhere with sunset before 24:00 UT
 * the Moon is ≥ 5° high and ≥ 8° from the Sun at sunset — or, the exception, it is so at sunset on land in the
 * Americas (later than 24:00 UT) while the conjunction came before dawn in New Zealand (≈ 17:00 UT). */
static int crescent_seen(long jdn, double conj)
{
	double day_end = (double) jdn + 0.5; /* 24:00 UT */
	int nz_ok = conj < (double) jdn - 0.5 + 17.0 / 24;
	for (int lat = -40; lat <= 60; lat += 10)
		for (int lon = -120; lon <= 180; lon += 15) {
			int america = lon <= -35 && (lon >= -80 || lat >= 10); /* roughly the Americas' land at these latitudes */
			if (lon < -30 && !america) continue;
			double s = sunset_jd(jdn, lat, lon);
			if (isnan(s) || s < conj) continue;
			if (s >= day_end && !(america && nz_ok)) continue;
			AstroPos su, mo;
			astro_body(ASTRO_SUN, s, &su);
			astro_body(ASTRO_MOON, s, &mo);
			double elong = acos(cosd(mo.lat) * cosd(mo.lon - su.lon)) / RAD;
			if (elong >= 8 && astro_alt(ASTRO_MOON, s, lat, lon, NULL) >= 5) return 1;
		}
	return 0;
}

#define SYNODIC 29.530588861
#define NM0 2451550.09766 /* a new moon (6 January 2000) */

static long month_start(long k)
{
	static struct { long k, start; } cache[8];
	static int used, next;
	for (int i = 0; i < used; i++)
		if (cache[i].k == k) return cache[i].start;
	double conj = vk_new_moon(NM0 + SYNODIC * (double) k);
	long d0 = (long) floor(conj + 0.5), s = d0 + 3;
	for (long d = d0; d < d0 + 3; d++)
		if (crescent_seen(d, conj)) {
			s = d + 1;
			break;
		}
	cache[next].k = k;
	cache[next].start = s;
	next = (next + 1) % 8;
	if (used < 8) used++;
	return s;
}

void vk_hijri(long jdn, VkHijri *h)
{
	long k = (long) floor(((double) jdn - NM0) / SYNODIC) + 1, s = 0;
	for (int i = 0; i < 4; i++, k--) {
		s = month_start(k);
		if (s <= jdn) break;
	}
	VkHijri mid;
	vk_hijri_tabular(s + 14, &mid); /* the arithmetic calendar is never two weeks off: it names the month */
	h->y = mid.y;
	h->m = mid.m;
	h->d = (int) (jdn - s + 1);
}

/* ---- the Moon ---- */

const char *const vk_moon_names[8] = { N_("Yeni Ay"), N_("Büyüyen hilal"), N_("İlk dördün"), N_("Büyüyen ay"),
				       N_("Dolunay"), N_("Küçülen ay"), N_("Son dördün"), N_("Küçülen hilal") };

int vk_moon_phase(double unix_t, double *illum, double *age_days)
{
	double jd = astro_jd(unix_t), age = astro_moon_age(jd);
	if (illum) *illum = astro_moon_illum(jd, NULL);
	if (age_days) *age_days = age;
	return (int) floor(age / SYNODIC * 8 + 0.5) % 8;
}

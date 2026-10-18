#include <math.h>
#include <stdio.h>
#include <string.h>
#include "kui.h"
#include "pano.h"
#include "vendor/cjson/cJSON.h"

void pano_prayer_url(double lat, double lon, int day, int month, int year, char *out, int cap)
{
	/* method 13: Diyanet İşleri Başkanlığı, Türkiye */
	snprintf(out, (size_t) cap, "https://api.aladhan.com/v1/timings/%02d-%02d-%04d?latitude=%.4f&longitude=%.4f&method=13", day, month, year, lat, lon);
}

int pano_parse_prayer(const char *json, PanoPrayer *out)
{
	static const char *const keys[PANO_N_PRAYER] = { "Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha" };
	memset(out, 0, sizeof *out);
	cJSON *root = cJSON_Parse(json);
	const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data"), *t = cJSON_GetObjectItemCaseSensitive(data, "timings");
	int ok = cJSON_IsObject(t);
	for (int i = 0; ok && i < PANO_N_PRAYER; i++) {
		const cJSON *v = cJSON_GetObjectItemCaseSensitive(t, keys[i]);
		int hh, mm;
		if (!cJSON_IsString(v) || sscanf(v->valuestring, "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) ok = 0;
		else snprintf(out->t[i], sizeof out->t[i], "%02d:%02d", hh, mm);
	}
	const cJSON *g = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(data, "date"), "gregorian"), "date");
	if (ok && cJSON_IsString(g)) snprintf(out->date, sizeof out->date, "%.10s", g->valuestring);
	cJSON_Delete(root);
	return ok ? 0 : -1;
}

const char *pano_prayer_name(int i)
{
	static const char *const n[PANO_N_PRAYER] = { /* N_ */ "İmsak", "Güneş", "Öğle", "İkindi", "Akşam", "Yatsı" };
	return i >= 0 && i < PANO_N_PRAYER ? n[i] : "";
}

static int minute_of(const char *hhmm)
{
	int h, m;
	return sscanf(hhmm, "%d:%d", &h, &m) == 2 ? h * 60 + m : -1;
}

int pano_next_prayer(const PanoPrayer *p, int now_min, int *mins_left)
{
	for (int i = 0; i < PANO_N_PRAYER; i++) {
		int m = minute_of(p->t[i]);
		if (m > now_min) {
			*mins_left = m - now_min;
			return i;
		}
	}
	*mins_left = 24 * 60 - now_min + minute_of(p->t[PANO_IMSAK]);
	return PANO_IMSAK;
}

void pano_onthisday_url(const char *lang, int month, int day, char *out, int cap)
{
	const char *wiki = !strcmp(lang, "nb") ? "no" : lang; /* Wikipedia uses "no" for Bokmål */
	snprintf(out, (size_t) cap, "https://api.wikimedia.org/feed/v1/wikipedia/%s/onthisday/selected/%02d/%02d", wiki, month, day);
}

int pano_parse_onthisday(const char *json, PanoEvent *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *sel = cJSON_GetObjectItemCaseSensitive(root, "selected"), *e;
	int n = 0;
	cJSON_ArrayForEach(e, sel)
	{
		if (n == max) break;
		const cJSON *y = cJSON_GetObjectItemCaseSensitive(e, "year"), *t = cJSON_GetObjectItemCaseSensitive(e, "text");
		if (!cJSON_IsNumber(y) || !cJSON_IsString(t) || !t->valuestring[0]) continue;
		out[n].year = y->valueint;
		snprintf(out[n].text, sizeof out[n].text, "%s", t->valuestring);
		n++;
	}
	cJSON_Delete(root);
	return n;
}

#define SYNODIC 29.530588853
double pano_moon_age(long t)
{
	const long ref = 947182440L; /* new moon 2000-01-06 18:14 UTC */
	double a = fmod((double) (t - ref) / 86400.0, SYNODIC);
	return a < 0 ? a + SYNODIC : a;
}

const char *pano_moon_phase_name(double age)
{
	static const char *const n[8] = { /* N_ */ "Yeni ay", "Hilal (büyüyen)", "İlk dördün", "Şişkin ay (büyüyen)",
					   "Dolunay", "Şişkin ay (küçülen)", "Son dördün", "Hilal (küçülen)" };
	int i = (int) floor(age / SYNODIC * 8 + 0.5) % 8;
	return n[i];
}

int pano_moon_lit_percent(double age)
{
	return (int) lround((1 - cos(2 * M_PI * age / SYNODIC)) / 2 * 100);
}

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int pano_day_of_year(int y, int m, int d)
{
	static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	return cum[m - 1] + d + (m > 2 && leap(y));
}

/* 0 = Monday … 6 = Sunday */
static int weekday_mon0(int y, int m, int d)
{
	static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if (m < 3) y--;
	int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7; /* 0 = Sunday */
	return (w + 6) % 7;
}

int pano_iso_week(int y, int m, int d)
{
	int doy = pano_day_of_year(y, m, d), wd = weekday_mon0(y, m, d);
	int w = (doy - wd + 9) / 7; /* week of the Thursday of this week */
	if (w < 1) {
		int py = y - 1, pdoy = 365 + leap(py) + doy, pw = (pdoy - wd + 9) / 7;
		return pw;
	}
	if (w == 53) {
		int days = 365 + leap(y);
		if (doy - wd + 3 > days) return 1; /* that Thursday is already next year */
	}
	return w;
}

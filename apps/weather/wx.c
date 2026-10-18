#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vendor/cjson/cJSON.h"
#include "kui.h"
#include "net/net.h"
#include "wx.h"

static double num(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

static int inum(const cJSON *o, const char *k)
{
	double d = num(o, k);
	return isnan(d) ? -1 : (int) lround(d);
}

static double arr_num(const cJSON *o, const char *k, int i)
{
	const cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(o, k), i);
	return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

static int arr_int(const cJSON *o, const char *k, int i)
{
	double d = arr_num(o, k, i);
	return isnan(d) ? -1 : (int) lround(d);
}

static const char *arr_str(const cJSON *o, const char *k, int i)
{
	const cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(o, k), i);
	return cJSON_IsString(v) ? v->valuestring : "";
}

/* "2026-10-18T16:30" → "16:30" */
static void hhmm(const char *iso, char out[6])
{
	const char *t = strchr(iso, 'T');
	snprintf(out, 6, "%.5s", t ? t + 1 : "");
}

/* day of week (0 = Sunday) of a "YYYY-MM-DD" date */
static int weekday(const char *date)
{
	int y, m, d;
	if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) return -1;
	static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if (m < 1 || m > 12) return -1;
	if (m < 3) y--;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int wx_parse(const char *json, Wx *w)
{
	memset(w, 0, sizeof *w);
	cJSON *root = cJSON_Parse(json);
	const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
	if (!cJSON_IsObject(cur)) {
		cJSON_Delete(root);
		return -1;
	}
	w->utc_offset = inum(root, "utc_offset_seconds");
	if (w->utc_offset == -1) w->utc_offset = 0;
	const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "time");
	hhmm(cJSON_IsString(t) ? t->valuestring : "", w->now);
	w->temp = num(cur, "temperature_2m");
	w->feels = num(cur, "apparent_temperature");
	w->wind = num(cur, "wind_speed_10m");
	w->hum = inum(cur, "relative_humidity_2m");
	w->wind_dir = inum(cur, "wind_direction_10m");
	w->code = inum(cur, "weather_code");
	w->is_day = inum(cur, "is_day");
	const cJSON *h = cJSON_GetObjectItemCaseSensitive(root, "hourly");
	int nh = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(h, "time"));
	for (int i = 0; i < nh && w->n_hours < 12; i++, w->n_hours++) {
		hhmm(arr_str(h, "time", i), w->hour[i].hh);
		w->hour[i].t = arr_num(h, "temperature_2m", i);
		w->hour[i].code = arr_int(h, "weather_code", i);
		w->hour[i].pop = arr_int(h, "precipitation_probability", i);
	}
	const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "daily");
	int nd = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(d, "time"));
	for (int i = 0; i < nd && w->n_days < 7; i++, w->n_days++) {
		snprintf(w->day[i].date, sizeof w->day[i].date, "%s", arr_str(d, "time", i));
		w->day[i].wday = weekday(w->day[i].date);
		w->day[i].code = arr_int(d, "weather_code", i);
		w->day[i].tmax = arr_num(d, "temperature_2m_max", i);
		w->day[i].tmin = arr_num(d, "temperature_2m_min", i);
		w->day[i].pop = arr_int(d, "precipitation_probability_max", i);
		hhmm(arr_str(d, "sunrise", i), w->day[i].sunrise);
		hhmm(arr_str(d, "sunset", i), w->day[i].sunset);
	}
	cJSON_Delete(root);
	return 0;
}

void wx_forecast_url(const WxCity *c, char *out, int cap)
{
	snprintf(out, (size_t) cap,
		 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
		 "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,is_day"
		 "&hourly=temperature_2m,weather_code,precipitation_probability"
		 "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
		 "&timezone=auto&forecast_days=7&forecast_hours=12",
		 c->lat, c->lon);
}

void wx_geocode_url(const char *query, const char *lang, char *out, int cap)
{
	char q[256];
	net_urlencode(query, q, sizeof q);
	snprintf(out, (size_t) cap, "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=50&language=%s&format=json", q, lang);
}

static void copy_str(char *dst, int cap, const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	snprintf(dst, (size_t) cap, "%s", cJSON_IsString(v) ? v->valuestring : "");
	for (char *p = dst; *p; p++) /* our file format separators */
		if (*p == '|' || *p == '\n' || *p == '\r') *p = ' ';
}

int wx_geocode_parse(const char *json, const char *query, WxCity *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "results"), *it;
	static WxCity all[64];
	double score[64];
	char q[96], nm[96];
	kui_tr_upper_str(query, q, sizeof q);
	int n = 0;
	cJSON_ArrayForEach(it, res)
	{
		if (n == 64) break;
		double lat = num(it, "latitude"), lon = num(it, "longitude");
		if (isnan(lat) || isnan(lon)) continue;
		WxCity *c = &all[n];
		char a1[64], a2[64], fc[16];
		copy_str(c->name, sizeof c->name, it, "name");
		copy_str(a1, sizeof a1, it, "admin1");
		copy_str(a2, sizeof a2, it, "admin2");
		copy_str(fc, sizeof fc, it, "feature_code");
		if (!a1[0]) copy_str(a1, sizeof a1, it, "country");
		if (!c->name[0]) continue;
		/* the district tells same-named places apart, unless it just repeats the name */
		int centre = a2[0] && strcmp(a2, c->name) == 0;
		if (a2[0] && !centre && strcmp(a2, a1) != 0)
			snprintf(c->admin, sizeof c->admin, "%.30s, %.30s", a2, a1);
		else
			snprintf(c->admin, sizeof c->admin, "%s", a1);
		c->lat = lat;
		c->lon = lon;
		/* rank: exact name, then capitals / district centres, then population */
		double pop = num(it, "population");
		kui_tr_upper_str(c->name, nm, sizeof nm);
		score[n] = (isnan(pop) ? 0 : pop) + (strcmp(nm, q) == 0 ? 1e9 : 0) +
			   (!strcmp(fc, "PPLC") || !strcmp(fc, "PPLA") ? 1e8 : (!strcmp(fc, "PPLA2") || centre) ? 1e7 : 0);
		n++;
	}
	cJSON_Delete(root);
	/* stable insertion sort by score, best first */
	for (int i = 1; i < n; i++) {
		WxCity c = all[i];
		double sc = score[i];
		int j = i;
		for (; j > 0 && score[j - 1] < sc; j--) {
			all[j] = all[j - 1];
			score[j] = score[j - 1];
		}
		all[j] = c;
		score[j] = sc;
	}
	if (n > max) n = max;
	memcpy(out, all, sizeof all[0] * (size_t) n);
	return n;
}

const char *wx_code_text(int c)
{
	switch (c) {
	case 0: return N_("Açık");
	case 1: return N_("Az bulutlu");
	case 2: return N_("Parçalı bulutlu");
	case 3: return N_("Kapalı");
	case 45: case 48: return N_("Sisli");
	case 51: case 53: case 55: return N_("Çisenti");
	case 56: case 57: return N_("Donan çisenti");
	case 61: case 63: return N_("Yağmurlu");
	case 65: return N_("Şiddetli yağmur");
	case 66: case 67: return N_("Donan yağmur");
	case 71: case 73: return N_("Karlı");
	case 75: return N_("Yoğun kar");
	case 77: return N_("Kar taneleri");
	case 80: case 81: return N_("Sağanak");
	case 82: return N_("Şiddetli sağanak");
	case 85: case 86: return N_("Kar sağanağı");
	case 95: return N_("Gök gürültülü");
	case 96: case 99: return N_("Dolu, fırtına");
	default: return "—";
	}
}

WxIcon wx_code_icon(int c, int is_day)
{
	if (c <= 1) return is_day == 0 ? WX_ICON_MOON : WX_ICON_SUN;
	if (c == 2) return is_day == 0 ? WX_ICON_PARTLY_NIGHT : WX_ICON_PARTLY;
	if (c == 3) return WX_ICON_CLOUD;
	if (c == 45 || c == 48) return WX_ICON_FOG;
	if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_ICON_SNOW;
	if (c >= 95) return WX_ICON_STORM;
	if (c >= 51) return WX_ICON_RAIN;
	return WX_ICON_CLOUD;
}

const char *wx_wind_dir(int deg)
{
	static const char *const d[] = { /* N_ */ "K", "KD", "D", "GD", "G", "GB", "B", "KB" };
	if (deg < 0) return "";
	return d[((deg % 360) * 2 + 45) / 90 % 8];
}

int wx_cities_load(const char *path, WxCity *out, int max)
{
	static char buf[8192];
	int n = 0;
	if (kui_read_file(path, buf, sizeof buf) > 0) {
		for (char *line = strtok(buf, "\n"); line && n < max; line = strtok(NULL, "\n")) {
			char *f[4], *p = line;
			int k = 0;
			for (; k < 4 && p; k++) {
				f[k] = p;
				p = strchr(p, '|');
				if (p) *p++ = '\0';
			}
			if (k < 4 || !f[0][0]) continue;
			char *e1, *e2;
			double lat = strtod(f[2], &e1), lon = strtod(f[3], &e2);
			if (e1 == f[2] || e2 == f[3]) continue;
			snprintf(out[n].name, sizeof out[n].name, "%s", f[0]);
			snprintf(out[n].admin, sizeof out[n].admin, "%s", f[1]);
			out[n].lat = lat;
			out[n].lon = lon;
			n++;
		}
	}
	if (n == 0 && max > 0) {
		out[0] = (WxCity){ "Maltepe", "İstanbul", 40.935, 29.155 };
		n = 1;
	}
	return n;
}

int wx_cities_save(const char *path, const WxCity *c, int n)
{
	char buf[8192];
	int o = 0;
	for (int i = 0; i < n; i++) {
		if (strpbrk(c[i].name, "|\n\r") || strpbrk(c[i].admin, "|\n\r")) return -1;
		int k = snprintf(buf + o, sizeof buf - (size_t) o, "%s|%s|%.5f|%.5f\n", c[i].name, c[i].admin, c[i].lat, c[i].lon);
		if (k < 0 || o + k >= (int) sizeof buf) return -1;
		o += k;
	}
	return kui_write_atomic(path, buf, o);
}

void wx_cache_name(const WxCity *c, char *out, int cap)
{
	snprintf(out, (size_t) cap, "c_%.4f_%.4f.json", c->lat, c->lon);
}

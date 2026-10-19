/* Son Depremler: parsing (Kandilli text table, EMSC JSON), place names, filters, distances, map projection. */
#include "deprem.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "vendor/cjson/cJSON.h"

/* ---- text ---- */

/* windows-1254 0x80–0x9F (as cp1252; 0 = undefined) */
static const unsigned short cp1254_hi[32] = {
	0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0, 0,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0, 0x0178,
};

int dq_cp1254_to_utf8(const char *in, int len, char *out, int cap)
{
	int o = 0;
	if (cap <= 0) return 0;
	for (int i = 0; i < len; i++) {
		int c = (unsigned char) in[i], cp;
		if (c < 0x80) cp = c;
		else if (c < 0xA0) cp = cp1254_hi[c - 0x80] ? cp1254_hi[c - 0x80] : '?';
		else if (c == 0xD0) cp = 0x011E; /* Ğ */
		else if (c == 0xDD) cp = 0x0130; /* İ */
		else if (c == 0xDE) cp = 0x015E; /* Ş */
		else if (c == 0xF0) cp = 0x011F; /* ğ */
		else if (c == 0xFD) cp = 0x0131; /* ı */
		else if (c == 0xFE) cp = 0x015F; /* ş */
		else cp = c;                     /* the rest is Latin-1 */
		char b[4];
		int n = kui_utf8_encode(cp, b);
		if (o + n >= cap) break;
		memcpy(out + o, b, (size_t) n);
		o += n;
	}
	out[o] = '\0';
	return o;
}

/* appends n bytes of UTF-8 s, whole characters only */
static void put(char *out, int cap, int *o, const char *s, int n)
{
	int i = 0;
	while (i < n) {
		int k = 1;
		while (i + k < n && ((unsigned char) s[i + k] & 0xC0) == 0x80) k++;
		if (*o + k >= cap) break;
		memcpy(out + *o, s + i, (size_t) k);
		*o += k;
		i += k;
	}
	out[*o] = '\0';
}

static int cmp_place(const void *k, const void *e) { return strcmp(k, ((const DqPlaceName *) e)->key); }

static int is_word(int c) { return isalpha(c) || c >= 0x80; }

/* words: Turkish spelling from the table when known, else Title case (ASCII rules: Kandilli has no diacritics) */
static void pretty(const char *s, char *out, int cap, int use_table)
{
	int o = 0;
	if (cap <= 0) return;
	out[0] = '\0';
	while (*s) {
		if (!is_word((unsigned char) *s)) {
			put(out, cap, &o, s, 1);
			s++;
			continue;
		}
		int n = 0;
		while (is_word((unsigned char) s[n])) n++;
		char w[64];
		int wl = n < (int) sizeof w - 1 ? n : (int) sizeof w - 1, ascii = 1;
		for (int i = 0; i < wl; i++) {
			w[i] = (char) toupper((unsigned char) s[i]);
			if ((unsigned char) s[i] >= 0x80) ascii = 0;
		}
		w[wl] = '\0';
		const DqPlaceName *hit = use_table && ascii ? bsearch(w, dq_place_names, (size_t) dq_n_place_names, sizeof *dq_place_names, cmp_place) : NULL;
		if (hit) {
			put(out, cap, &o, hit->name, (int) strlen(hit->name));
		} else if (ascii) {
			for (int i = 1; i < wl; i++) w[i] = (char) tolower((unsigned char) w[i]);
			put(out, cap, &o, w, wl);
		} else {
			put(out, cap, &o, s, n);
		}
		s += n;
	}
}

void dq_place_pretty(const char *caps, char *out, int cap) { pretty(caps, out, cap, 1); }

static const char *const regions[][2] = {
	{ "WESTERN TURKEY", "Batı Türkiye" },
	{ "CENTRAL TURKEY", "Orta Türkiye" },
	{ "EASTERN TURKEY", "Doğu Türkiye" },
	{ "TURKEY", "Türkiye" },
	{ "NEAR THE COAST OF WESTERN TURKEY", "Batı Türkiye kıyısı" },
	{ "NEAR THE SOUTH COAST OF TURKEY", "Türkiye güney kıyısı" },
	{ "NEAR THE NORTH COAST OF TURKEY", "Türkiye kuzey kıyısı" },
	{ "BLACK SEA", "Karadeniz" },
	{ "AEGEAN SEA", "Ege Denizi" },
	{ "EASTERN MEDITERRANEAN SEA", "Doğu Akdeniz" },
	{ "CENTRAL MEDITERRANEAN SEA", "Orta Akdeniz" },
	{ "CYPRUS REGION", "Kıbrıs bölgesi" },
	{ "DODECANESE ISLANDS, GREECE", "Oniki Adalar, Yunanistan" },
	{ "DODECANESE IS.-TURKEY BORDER REG", "Oniki Adalar – Türkiye sınırı" },
	{ "CRETE, GREECE", "Girit, Yunanistan" },
	{ "GREECE", "Yunanistan" },
	{ "NORTHERN GREECE", "Kuzey Yunanistan" },
	{ "SOUTHERN GREECE", "Güney Yunanistan" },
	{ "GREECE-BULGARIA BORDER REGION", "Yunanistan – Bulgaristan sınırı" },
	{ "BULGARIA", "Bulgaristan" },
	{ "ROMANIA", "Romanya" },
	{ "TURKEY-SYRIA BORDER REGION", "Türkiye – Suriye sınırı" },
	{ "TURKEY-SYRIA-IRAQ BORDER REGION", "Türkiye – Suriye – Irak sınırı" },
	{ "TURKEY-IRAN-IRAQ BORDER REGION", "Türkiye – İran – Irak sınırı" },
	{ "TURKEY-IRAN BORDER REGION", "Türkiye – İran sınırı" },
	{ "TURKEY-IRAQ BORDER REGION", "Türkiye – Irak sınırı" },
	{ "SYRIA", "Suriye" },
	{ "LEBANON", "Lübnan" },
	{ "LEBANON-SYRIA BORDER REGION", "Lübnan – Suriye sınırı" },
	{ "IRAQ", "Irak" },
	{ "IRAN", "İran" },
	{ "NORTHWESTERN IRAN", "Kuzeybatı İran" },
	{ "WESTERN IRAN", "Batı İran" },
	{ "IRAN-IRAQ BORDER REGION", "İran – Irak sınırı" },
	{ "GEORGIA (SAK'ART'VELO)", "Gürcistan" },
	{ "ARMENIA", "Ermenistan" },
	{ "AZERBAIJAN", "Azerbaycan" },
	{ "CAUCASUS REGION, RUSSIA", "Kafkasya, Rusya" },
	{ "ISRAEL", "İsrail" },
	{ "JORDAN", "Ürdün" },
};

void dq_region_pretty(const char *region, char *out, int cap)
{
	for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++)
		if (!strcmp(region, regions[i][0])) {
			int o = 0;
			if (cap > 0) put(out, cap, &o, regions[i][1], (int) strlen(regions[i][1]));
			return;
		}
	pretty(region, out, cap, 0);
}

/* ---- time ---- */

static long days_from_civil(int y, int m, int d) /* days since 1970-01-01 (proleptic Gregorian) */
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static long utc_time(int y, int mo, int d, int h, int mi, int s) { return days_from_civil(y, mo, d) * 86400 + h * 3600L + mi * 60L + s; }

/* ---- Kandilli ---- */

static int mag_tok(const char *s, double *v) /* "-.-" → 0 */
{
	char *e;
	*v = strtod(s, &e);
	return e != s && *v > 0;
}

static int cmp_newest(const void *a, const void *b)
{
	long ta = ((const Quake *) a)->t, tb = ((const Quake *) b)->t;
	return ta < tb ? 1 : ta > tb ? -1 : 0;
}

/* both feeds come over plain HTTP (Kandilli has no HTTPS): accept only physically possible values */
static int sane(double lat, double lon, double depth, double mag)
{
	return isfinite(lat) && isfinite(lon) && isfinite(depth) && isfinite(mag) && lat >= -90 && lat <= 90 && lon >= -180 &&
	       lon <= 180 && depth >= -10 && depth <= 800 && mag >= -2 && mag <= 10;
}

/* one complete line (without \r\n) of the table; 1 if it is an earthquake */
static int kandilli_line(const char *line, Quake *q)
{
	int y, mo, d, h, mi, s, used = 0;
	double lat, lon, dep, v[3];
	char m[3][8];
	if (sscanf(line, "%4d.%2d.%2d %2d:%2d:%2d %lf %lf %lf %7s %7s %7s %n", &y, &mo, &d, &h, &mi, &s, &lat, &lon, &dep, m[0], m[1], m[2], &used) < 12 ||
	    !used)
		return 0;
	if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return 0;
	int ok[3];
	for (int i = 0; i < 3; i++) ok[i] = mag_tok(m[i], &v[i]); /* MD, ML, Mw */
	int pick = ok[2] && v[2] >= 5 ? 2 : ok[1] ? 1 : ok[2] ? 2 : ok[0] ? 0 : -1;
	if (pick < 0 || !sane(lat, lon, dep, v[pick])) return 0;
	static const char *const names[3] = { "MD", "ML", "Mw" };
	q->t = utc_time(y, mo, d, h, mi, s) - DQ_TR_OFFSET;
	q->lat = lat;
	q->lon = lon;
	q->depth = dep;
	q->mag = v[pick];
	snprintf(q->mtype, sizeof q->mtype, "%s", names[pick]);
	/* the place runs up to two spaces (then the solution quality) or the end of the line */
	const char *p = line + used, *e = p;
	while (*e && !(e[0] == ' ' && (e[1] == ' ' || e[1] == '\0'))) e++;
	char raw[96], utf[200];
	int n = (int) (e - p) < (int) sizeof raw - 1 ? (int) (e - p) : (int) sizeof raw - 1;
	memcpy(raw, p, (size_t) n);
	raw[n] = '\0';
	dq_cp1254_to_utf8(raw, n, utf, sizeof utf);
	dq_place_pretty(utf, q->place, sizeof q->place);
	return 1;
}

int dq_parse_kandilli(const char *body, int len, Quake *out, int max)
{
	int n = 0;
	const char *p = body, *end = body + len;
	while (p < end && n < max) {
		const char *nl = memchr(p, '\n', (size_t) (end - p));
		if (!nl) break; /* a truncated last line is not trusted */
		int l = (int) (nl - p);
		if (l > 0 && p[l - 1] == '\r') l--;
		if (l >= 40 && l < 400 && isdigit((unsigned char) p[0]) && p[4] == '.') {
			char line[400];
			memcpy(line, p, (size_t) l);
			line[l] = '\0';
			if (kandilli_line(line, &out[n])) n++;
		}
		p = nl + 1;
	}
	qsort(out, (size_t) n, sizeof *out, cmp_newest);
	return n;
}

/* ---- EMSC ---- */

static double num(const cJSON *o, const char *k, double def)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static const char *str(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

int dq_parse_emsc(const char *body, int len, Quake *out, int max)
{
	cJSON *root = cJSON_ParseWithLength(body, (size_t) len);
	const cJSON *fs = cJSON_GetObjectItemCaseSensitive(root, "features"), *f;
	int n = 0;
	cJSON_ArrayForEach(f, fs)
	{
		if (n >= max) break;
		const cJSON *p = cJSON_GetObjectItemCaseSensitive(f, "properties");
		int y, mo, d, h, mi, s;
		double lat = num(p, "lat", NAN), lon = num(p, "lon", NAN), mag = num(p, "mag", NAN);
		if (sscanf(str(p, "time"), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6 || !sane(lat, lon, fabs(num(p, "depth", 0)), mag)) continue;
		Quake *q = &out[n++];
		q->t = utc_time(y, mo, d, h, mi, s);
		q->lat = lat;
		q->lon = lon;
		q->depth = fabs(num(p, "depth", 0));
		q->mag = mag;
		const char *mt = str(p, "magtype");
		if (!strcmp(mt, "ml")) snprintf(q->mtype, sizeof q->mtype, "ML");
		else if (!strcmp(mt, "mw")) snprintf(q->mtype, sizeof q->mtype, "Mw");
		else if (!strcmp(mt, "md")) snprintf(q->mtype, sizeof q->mtype, "MD");
		else if (!strcmp(mt, "m") || !*mt) snprintf(q->mtype, sizeof q->mtype, "M");
		else snprintf(q->mtype, sizeof q->mtype, "%.3s", mt);
		dq_region_pretty(str(p, "flynn_region"), q->place, sizeof q->place);
	}
	cJSON_Delete(root);
	qsort(out, (size_t) n, sizeof *out, cmp_newest);
	return n;
}

int dq_parse(const char *body, int len, Quake *out, int max, DqSource *src)
{
	int i = 0;
	while (i < len && isspace((unsigned char) body[i])) i++;
	int emsc = i < len && body[i] == '{';
	int n = emsc ? dq_parse_emsc(body, len, out, max) : dq_parse_kandilli(body, len, out, max);
	*src = n == 0 ? DQ_SRC_NONE : emsc ? DQ_SRC_EMSC : DQ_SRC_KANDILLI;
	return n;
}

/* ---- lists ---- */

int dq_filter(const Quake *q, int n, double min_mag, long since, int *idx, int max)
{
	int k = 0;
	for (int i = 0; i < n && k < max; i++)
		if (q[i].mag >= min_mag - 1e-9 && q[i].t >= since) idx[k++] = i;
	return k;
}

void dq_sort_by_mag(const Quake *q, int *idx, int n)
{
	for (int i = 1; i < n; i++) { /* insertion sort: stable, lists are short */
		int v = idx[i], j = i;
		while (j > 0 && (q[idx[j - 1]].mag < q[v].mag || (q[idx[j - 1]].mag == q[v].mag && q[idx[j - 1]].t < q[v].t))) {
			idx[j] = idx[j - 1];
			j--;
		}
		idx[j] = v;
	}
}

double dq_distance_km(double lat1, double lon1, double lat2, double lon2)
{
	const double r = M_PI / 180;
	double dlat = (lat2 - lat1) * r, dlon = (lon2 - lon1) * r;
	double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1 * r) * cos(lat2 * r) * sin(dlon / 2) * sin(dlon / 2);
	return 2 * 6371.0 * asin(sqrt(a < 1 ? a : 1));
}

int dq_radius(double mag, int scale_px)
{
	double r = scale_px * pow(1.6, mag - 3); /* area grows ~2.5× per magnitude */
	if (r < 4) r = 4;
	if (r > 80) r = 80;
	return (int) lround(r);
}

void dq_ago(long now, long t, char *out, int cap)
{
	long d = now - t;
	if (d < 60) snprintf(out, (size_t) cap, "%s", T("az önce"));
	else if (d < 3600) snprintf(out, (size_t) cap, T("%d dk önce"), (int) (d / 60));
	else if (d < 2 * 86400) snprintf(out, (size_t) cap, T("%d sa önce"), (int) (d / 3600));
	else snprintf(out, (size_t) cap, T("%d gün önce"), (int) (d / 86400));
}

/* ---- map ---- */

void dq_proj_fit(DqProj *p, int x, int y, int w, int h, double lon0, double lon1, double lat0, double lat1)
{
	p->kx = cos((lat0 + lat1) / 2 * M_PI / 180);
	double wu = (lon1 - lon0) * p->kx, hu = lat1 - lat0;
	p->scale = w / wu < h / hu ? w / wu : h / hu;
	p->w = (int) lround(wu * p->scale);
	p->h = (int) lround(hu * p->scale);
	p->x = x + (w - p->w) / 2;
	p->y = y + (h - p->h) / 2;
	p->lon0 = lon0;
	p->lat1 = lat1;
}

static void project_d(const DqProj *p, double lat, double lon, double *x, double *y)
{
	*x = p->x + (lon - p->lon0) * p->kx * p->scale;
	*y = p->y + (p->lat1 - lat) * p->scale;
}

void dq_project(const DqProj *p, double lat, double lon, int *px, int *py)
{
	double x, y;
	project_d(p, lat, lon, &x, &y);
	*px = (int) lround(x);
	*py = (int) lround(y);
}

void dq_unproject(const DqProj *p, int px, int py, double *lat, double *lon)
{
	*lon = p->lon0 + (px - p->x) / (p->kx * p->scale);
	*lat = p->lat1 - (py - p->y) / p->scale;
}

/* ---- map view (zoom and pan) ---- */

void dq_view_reset(DqView *v)
{
	v->zoom = 1;
	v->lat = (DQ_MAP_LAT0 + DQ_MAP_LAT1) / 2;
	v->lon = (DQ_MAP_LON0 + DQ_MAP_LON1) / 2;
}

static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

void dq_view_clamp(DqView *v)
{
	int z = 1;
	while (z < DQ_ZOOM_MAX && z * 2 <= v->zoom) z *= 2;
	v->zoom = z;
	double hw = (DQ_MAP_LON1 - DQ_MAP_LON0) / (2.0 * z), hh = (DQ_MAP_LAT1 - DQ_MAP_LAT0) / (2.0 * z);
	v->lon = clampd(v->lon, DQ_MAP_LON0 + hw, DQ_MAP_LON1 - hw);
	v->lat = clampd(v->lat, DQ_MAP_LAT0 + hh, DQ_MAP_LAT1 - hh);
}

int dq_view_zoom(DqView *v, int dir)
{
	DqView old = *v;
	v->zoom = dir > 0 ? v->zoom * 2 : v->zoom / 2;
	dq_view_clamp(v);
	return v->zoom != old.zoom || v->lat != old.lat || v->lon != old.lon;
}

int dq_view_pan(DqView *v, double fx, double fy)
{
	DqView old = *v;
	v->lon += fx * (DQ_MAP_LON1 - DQ_MAP_LON0) / v->zoom;
	v->lat -= fy * (DQ_MAP_LAT1 - DQ_MAP_LAT0) / v->zoom;
	dq_view_clamp(v);
	return fabs(v->lat - old.lat) > 1e-9 || fabs(v->lon - old.lon) > 1e-9;
}

void dq_proj_view(DqProj *p, const DqProj *full, const DqView *v)
{
	*p = *full;
	if (v->zoom <= 1) return; /* exactly the fitted map */
	p->scale = full->scale * v->zoom;
	p->lon0 = v->lon - full->w / 2.0 / (p->kx * p->scale);
	p->lat1 = v->lat + full->h / 2.0 / p->scale;
}

int dq_proj_height(int w, double lon0, double lon1, double lat0, double lat1)
{
	double kx = cos((lat0 + lat1) / 2 * M_PI / 180);
	return (int) ceil(w * (lat1 - lat0) / ((lon1 - lon0) * kx));
}

int dq_ring_contains(const DqRing *r, double lat, double lon)
{
	int in = 0;
	for (int i = 0, j = r->n - 1; i < r->n; j = i++) {
		double xi = dq_map_pts[r->start + i][0] / 100.0, yi = dq_map_pts[r->start + i][1] / 100.0;
		double xj = dq_map_pts[r->start + j][0] / 100.0, yj = dq_map_pts[r->start + j][1] / 100.0;
		if ((yi > lat) != (yj > lat) && lon < (xj - xi) * (lat - yi) / (yj - yi) + xi) in = !in;
	}
	return in;
}

int dq_ring_scan(const DqProj *p, const DqRing *r, int y, int *xs, int max)
{
	int n = 0;
	double yc = y + 0.5;
	for (int i = 0, j = r->n - 1; i < r->n; j = i++) {
		double xa, ya, xb, yb;
		project_d(p, dq_map_pts[r->start + j][1] / 100.0, dq_map_pts[r->start + j][0] / 100.0, &xa, &ya);
		project_d(p, dq_map_pts[r->start + i][1] / 100.0, dq_map_pts[r->start + i][0] / 100.0, &xb, &yb);
		if ((ya <= yc) == (yb <= yc)) continue;
		if (n >= max) break;
		int x = (int) lround(xa + (yc - ya) * (xb - xa) / (yb - ya));
		int k = n++;
		while (k > 0 && xs[k - 1] > x) {
			xs[k] = xs[k - 1];
			k--;
		}
		xs[k] = x;
	}
	return n & ~1;
}

/* province centres (approximate); listed by importance: when labels collide the earlier one wins */
const DqCity dq_cities[] = {
	{ "İstanbul", 41.04, 28.95, 1 }, { "Ankara", 39.93, 32.86, 1 }, { "İzmir", 38.42, 27.14, 1 },
	{ "Antalya", 36.89, 30.71, 1 }, { "Adana", 37.00, 35.32, 1 }, { "Samsun", 41.29, 36.33, 1 },
	{ "Erzurum", 39.90, 41.27, 1 }, { "Diyarbakır", 37.91, 40.24, 1 },
	{ "Bursa", 40.19, 29.06, 2 }, { "Konya", 37.87, 32.48, 2 }, { "Gaziantep", 37.07, 37.38, 2 },
	{ "Kayseri", 38.73, 35.48, 2 }, { "Trabzon", 41.00, 39.72, 2 }, { "Van", 38.49, 43.38, 2 },
	{ "Malatya", 38.35, 38.31, 2 }, { "Eskişehir", 39.78, 30.52, 2 }, { "Denizli", 37.78, 29.09, 2 },
	{ "Sivas", 39.75, 37.02, 2 }, { "Kahramanmaraş", 37.58, 36.94, 2 }, { "Elazığ", 38.67, 39.22, 2 },
	{ "Muğla", 37.21, 28.36, 2 }, { "Balıkesir", 39.65, 27.88, 2 }, { "Çanakkale", 40.13, 26.46, 2 },
	{ "Edirne", 41.68, 26.56, 2 }, { "Hatay", 36.20, 36.16, 2 }, { "Mersin", 36.81, 34.64, 2 },
	{ "Şanlıurfa", 37.16, 38.79, 2 }, { "Kocaeli", 40.77, 29.92, 2 }, { "Manisa", 38.61, 27.43, 2 },
	{ "Erzincan", 39.75, 39.49, 2 }, { "Kars", 40.60, 43.10, 2 }, { "Zonguldak", 41.45, 31.79, 2 },
	{ "Sinop", 42.00, 35.10, 2 }, { "Kastamonu", 41.38, 33.78, 2 }, { "Bingöl", 38.88, 40.50, 2 },
	{ "Aydın", 37.85, 27.84, 4 }, { "Sakarya", 40.78, 30.40, 4 }, { "Afyonkarahisar", 38.76, 30.54, 4 },
	{ "Kütahya", 39.42, 29.98, 4 }, { "Uşak", 38.68, 29.41, 4 }, { "Isparta", 37.76, 30.55, 4 },
	{ "Burdur", 37.72, 30.29, 4 }, { "Aksaray", 38.37, 34.03, 4 }, { "Niğde", 37.97, 34.68, 4 },
	{ "Nevşehir", 38.62, 34.71, 4 }, { "Kırşehir", 39.15, 34.16, 4 }, { "Yozgat", 39.82, 34.81, 4 },
	{ "Çorum", 40.55, 34.95, 4 }, { "Tokat", 40.31, 36.55, 4 }, { "Amasya", 40.65, 35.83, 4 },
	{ "Ordu", 40.98, 37.88, 4 }, { "Giresun", 40.91, 38.39, 4 }, { "Rize", 41.02, 40.52, 4 },
	{ "Artvin", 41.18, 41.82, 4 }, { "Gümüşhane", 40.46, 39.48, 4 }, { "Bayburt", 40.26, 40.23, 4 },
	{ "Tunceli", 39.11, 39.55, 4 }, { "Muş", 38.74, 41.49, 4 }, { "Bitlis", 38.40, 42.11, 4 },
	{ "Siirt", 37.93, 41.94, 4 }, { "Batman", 37.89, 41.13, 4 }, { "Mardin", 37.31, 40.74, 4 },
	{ "Şırnak", 37.52, 42.46, 4 }, { "Hakkari", 37.58, 43.74, 4 }, { "Ağrı", 39.72, 43.05, 4 },
	{ "Iğdır", 39.92, 44.05, 4 }, { "Ardahan", 41.11, 42.70, 4 }, { "Adıyaman", 37.76, 38.28, 4 },
	{ "Osmaniye", 37.07, 36.25, 4 }, { "Kilis", 36.72, 37.12, 4 }, { "Karaman", 37.18, 33.22, 4 },
	{ "Bolu", 40.73, 31.61, 4 }, { "Düzce", 40.84, 31.16, 4 }, { "Bartın", 41.63, 32.34, 4 },
	{ "Karabük", 41.20, 32.62, 4 }, { "Çankırı", 40.60, 33.61, 4 }, { "Kırıkkale", 39.85, 33.51, 4 },
	{ "Bilecik", 40.14, 29.98, 4 }, { "Yalova", 40.65, 29.27, 4 }, { "Tekirdağ", 40.98, 27.51, 4 },
	{ "Kırklareli", 41.73, 27.22, 4 },
};
const int dq_n_cities = (int) (sizeof dq_cities / sizeof dq_cities[0]);

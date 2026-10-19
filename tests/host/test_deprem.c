#include <math.h>
#include <stdlib.h>
#include "deprem/deprem.h"
#include "kui.h"
#include "t.h"

static char *slurp(const char *path, int *len)
{
	static char buf[262144];
	FILE *f = fopen(path, "rb");
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	*len = (int) n;
	return buf;
}

static Quake q[DQ_MAX];

static void test_cp1254(void)
{
	char out[64];
	/* "İğdır Şile ı" in windows-1254 */
	const char in[] = "\xDD\xF0" "d\xFDr \xDE" "ile \xFD \xE7\xF6\xFC";
	int n = dq_cp1254_to_utf8(in, (int) strlen(in), out, sizeof out);
	T_EQ_STR(out, "İğdır Şile ı çöü");
	T_EQ_INT(n, (int) strlen("İğdır Şile ı çöü"));
	/* never splits a two-byte character at the end of the buffer */
	char small[4];
	dq_cp1254_to_utf8("a\xDD\xDD", 3, small, sizeof small);
	T_EQ_STR(small, "aİ");
}

static void test_kandilli(void)
{
	int len;
	const char *s = slurp("tests/host/fixtures/kandilli.html", &len);
	int n = dq_parse_kandilli(s, len, q, DQ_MAX);
	T_EQ_INT(n, 500);
	T_EQ_INT(q[0].t, 1792360180); /* 2026.10.19 00:49:40 TSİ = 18 Oct 21:49:40 UTC */
	T_ASSERT(fabs(q[0].lat - 39.2538) < 1e-6);
	T_ASSERT(fabs(q[0].lon - 29.0098) < 1e-6);
	T_ASSERT(fabs(q[0].depth - 15.2) < 1e-6);
	T_ASSERT(fabs(q[0].mag - 1.1) < 1e-6);
	T_EQ_STR(q[0].mtype, "ML");
	T_EQ_STR(q[0].place, "Yesildere-Simav (Kütahya)");
	for (int i = 1; i < n; i++) T_ASSERT(q[i].t <= q[i - 1].t);
	/* ML preferred: 3.5 (Mw 3.3) */
	int found = 0;
	for (int i = 0; i < n; i++)
		if (q[i].t == 1792217571) {
			found = 1;
			T_ASSERT(fabs(q[i].mag - 3.5) < 1e-6);
			T_EQ_STR(q[i].place, "Yemisli-Simav (Kütahya)");
		}
	T_ASSERT(found);
	/* the revised entry ("REVIZE01 (…)") is parsed like the others */
	found = 0;
	for (int i = 0; i < n; i++)
		if (!strcmp(q[i].place, "Akdeniz") && fabs(q[i].lat - 36.0933) < 1e-6) found = 1;
	T_ASSERT(found);
	/* DqSource detection */
	DqSource src = DQ_SRC_NONE;
	T_EQ_INT(dq_parse(s, len, q, DQ_MAX, &src), 500);
	T_EQ_INT(src, DQ_SRC_KANDILLI);
}

static void test_kandilli_garbage(void)
{
	T_EQ_INT(dq_parse_kandilli("<html>503 Service Unavailable</html>", 36, q, DQ_MAX), 0);
	T_EQ_INT(dq_parse_kandilli("", 0, q, DQ_MAX), 0);
	const char *one = "<pre>\r\n2026.10.19 00:49:40  39.2538   29.0098       15.2      -.-  -.-  -.-   X\r\n"
			  "2026.10.19 00:48:45  39.2102   29.0945       10.4      2.0  -.-  -.-   TOKAT-HISARCIK (KUTAHYA)     \xDDlksel\r\n</pre>";
	int n = dq_parse_kandilli(one, (int) strlen(one), q, DQ_MAX);
	T_EQ_INT(n, 1); /* no magnitude at all: skipped */
	T_ASSERT(fabs(q[0].mag - 2.0) < 1e-6); /* MD when there is nothing else … */
	T_EQ_STR(q[0].mtype, "MD");
	T_EQ_STR(q[0].place, "Tokat-Hisarcık (Kütahya)");
	const char *mw = "2026.10.19 00:48:45  39.2102   29.0945       10.4      2.0  -.-  4.1   X   Y\n";
	T_EQ_INT(dq_parse_kandilli(mw, (int) strlen(mw), q, DQ_MAX), 1);
	T_EQ_STR(q[0].mtype, "Mw"); /* Mw before MD */
	T_EQ_STR(q[0].place, "X");
	const char *big = "<pre>\n2023.02.06 04:17:32  37.1757   37.0560        8.6      -.-  6.7  7.7   SOFULAR-SEHITKAMIL (GAZIANTEP)   \xDDlksel\n</pre>";
	n = dq_parse_kandilli(big, (int) strlen(big), q, DQ_MAX);
	T_EQ_INT(n, 1);
	T_ASSERT(fabs(q[0].mag - 7.7) < 1e-6); /* … and Mw for large quakes (ML saturates) */
	T_EQ_STR(q[0].mtype, "Mw");
	T_EQ_STR(q[0].place, "Sofular-Şehitkamil (Gaziantep)");
	n = dq_parse_kandilli(big, 60, q, DQ_MAX); /* truncated mid-line */
	T_EQ_INT(n, 0);
	/* the feed is plain HTTP: impossible values (a tampered or broken page) are dropped */
	const char *bad = "2026.10.19 00:48:45  99.2102   29.0945       10.4      2.0  -.-  -.-   X\n"
			  "2026.10.19 00:48:45  39.2102  529.0945       10.4      2.0  -.-  -.-   X\n"
			  "2026.10.19 00:48:45  39.2102   29.0945     9999.0      2.0  -.-  -.-   X\n"
			  "2026.10.19 00:48:45  39.2102   29.0945       10.4     99.0  -.-  -.-   X\n";
	T_EQ_INT(dq_parse_kandilli(bad, (int) strlen(bad), q, DQ_MAX), 0);
	const char *ej = "{\"features\":[{\"properties\":{\"time\":\"2026-10-19T00:00:00\",\"lat\":38,\"lon\":27,\"mag\":42,\"depth\":5}}]}";
	T_EQ_INT(dq_parse_emsc(ej, (int) strlen(ej), q, DQ_MAX), 0);
}

static void test_emsc(void)
{
	int len;
	const char *s = slurp("tests/host/fixtures/emsc.json", &len);
	DqSource src = DQ_SRC_NONE;
	int n = dq_parse(s, len, q, DQ_MAX, &src);
	T_EQ_INT(n, 200);
	T_EQ_INT(src, DQ_SRC_EMSC);
	T_EQ_INT(q[0].t, 1792352219);
	T_ASSERT(fabs(q[0].lat - 40.5125) < 1e-6);
	T_ASSERT(fabs(q[0].lon - 33.3925) < 1e-6);
	T_ASSERT(fabs(q[0].depth - 1.7) < 1e-6);
	T_ASSERT(fabs(q[0].mag - 2.2) < 1e-6);
	T_EQ_STR(q[0].mtype, "ML");
	T_EQ_STR(q[0].place, "Orta Türkiye");
	for (int i = 1; i < n; i++) T_ASSERT(q[i].t <= q[i - 1].t);
	T_EQ_INT(dq_parse_emsc("{\"type\":\"FeatureCollection\"}", 28, q, DQ_MAX), 0);
	T_EQ_INT(dq_parse_emsc("not json", 8, q, DQ_MAX), 0);
}

static void test_places(void)
{
	char s[128];
	dq_place_pretty("ORLEMIS-BERGAMA (IZMIR)", s, sizeof s);
	T_EQ_STR(s, "Orlemis-Bergama (İzmir)");
	dq_place_pretty("GUZELKOY ACIKLARI-TEKIRDAG (MARMARA DENIZI)", s, sizeof s);
	T_EQ_STR(s, "Guzelkoy Açıkları-Tekirdağ (Marmara Denizi)");
	dq_place_pretty("EGE DENIZI", s, sizeof s);
	T_EQ_STR(s, "Ege Denizi");
	dq_place_pretty("CAPAR-SABANOZU (CANKIRI)", s, sizeof s);
	T_EQ_STR(s, "Capar-Şabanözü (Çankırı)");
	dq_place_pretty("SURIYE", s, sizeof s);
	T_EQ_STR(s, "Suriye");
	dq_place_pretty("", s, sizeof s);
	T_EQ_STR(s, "");
	char tiny[8];
	dq_place_pretty("CANKIRI", tiny, sizeof tiny); /* "Çankırı" is 10 bytes: cut between characters */
	T_ASSERT(strlen(tiny) < sizeof tiny);
	T_EQ_STR(tiny, "Çankı");
	dq_region_pretty("WESTERN TURKEY", s, sizeof s);
	T_EQ_STR(s, "Batı Türkiye");
	dq_region_pretty("DODECANESE ISLANDS, GREECE", s, sizeof s);
	T_EQ_STR(s, "Oniki Adalar, Yunanistan");
	dq_region_pretty("GEORGIA (SAK'ART'VELO)", s, sizeof s);
	T_EQ_STR(s, "Gürcistan");
	dq_region_pretty("NORTHERN IRAQ", s, sizeof s);
	T_EQ_STR(s, "Northern Iraq");
}

static void test_filter_sort(void)
{
	Quake a[4] = { { 1000, 0, 0, 5, 2.0, "ML", "a" }, { 900, 0, 0, 5, 4.5, "ML", "b" }, { 500, 0, 0, 5, 3.0, "ML", "c" }, { 100, 0, 0, 5, 4.5, "ML", "d" } };
	int idx[4];
	T_EQ_INT(dq_filter(a, 4, 0, 0, idx, 4), 4);
	T_EQ_INT(dq_filter(a, 4, 3.0, 0, idx, 4), 3);
	T_EQ_INT(idx[0], 1);
	T_EQ_INT(idx[1], 2);
	T_EQ_INT(dq_filter(a, 4, 3.0, 600, idx, 4), 1);
	T_EQ_INT(idx[0], 1);
	T_EQ_INT(dq_filter(a, 4, 0, 0, idx, 2), 2); /* bounded by max */
	int n = dq_filter(a, 4, 0, 0, idx, 4);
	dq_sort_by_mag(a, idx, n);
	T_EQ_INT(idx[0], 1); /* 4.5, newer */
	T_EQ_INT(idx[1], 3); /* 4.5, older */
	T_EQ_INT(idx[2], 2);
	T_EQ_INT(idx[3], 0);
}

static void test_distance_radius(void)
{
	double d = dq_distance_km(41.0082, 28.9784, 39.9334, 32.8597); /* İstanbul – Ankara */
	T_ASSERT(fabs(d - 349.36) < 0.5);
	T_ASSERT(dq_distance_km(39, 29, 39, 29) < 1e-9);
	T_EQ_INT(dq_radius(3.0, 16), 16);
	T_ASSERT(dq_radius(1.0, 16) >= 4);
	T_ASSERT(dq_radius(2.0, 16) < dq_radius(3.0, 16));
	T_ASSERT(dq_radius(5.0, 16) > dq_radius(4.0, 16));
	T_ASSERT(dq_radius(9.0, 16) <= 80);
	T_ASSERT(dq_radius(0.0, 16) >= 4);
}

static void test_ago(void)
{
	char s[64];
	dq_ago(10000, 10000 - 20, s, sizeof s);
	T_EQ_STR(s, "az önce");
	dq_ago(10000, 10000 - 12 * 60 - 5, s, sizeof s);
	T_EQ_STR(s, "12 dk önce");
	dq_ago(100000, 100000 - 3 * 3600 - 100, s, sizeof s);
	T_EQ_STR(s, "3 sa önce");
	dq_ago(1000000, 1000000 - 30 * 3600, s, sizeof s); /* hours up to two days: never "1 days ago" */
	T_EQ_STR(s, "30 sa önce");
	dq_ago(1000000, 1000000 - 2 * 86400 - 5, s, sizeof s);
	T_EQ_STR(s, "2 gün önce");
	dq_ago(10000, 10050, s, sizeof s); /* clock skew: never "in the future" */
	T_EQ_STR(s, "az önce");
}

static void test_projection(void)
{
	DqProj p;
	dq_proj_fit(&p, 20, 300, 1032, 600, DQ_MAP_LON0, DQ_MAP_LON1, DQ_MAP_LAT0, DQ_MAP_LAT1);
	int x0, y0, x1, y1;
	dq_project(&p, DQ_MAP_LAT1, DQ_MAP_LON0, &x0, &y0);
	dq_project(&p, DQ_MAP_LAT0, DQ_MAP_LON1, &x1, &y1);
	T_ASSERT(x0 >= 20 && y0 >= 300 && x1 <= 20 + 1032 && y1 <= 300 + 600);
	T_ASSERT((x0 == 20 && x1 >= 1050) || (y0 == 300 && y1 >= 898)); /* fills one dimension */
	int h = dq_proj_height(1032, DQ_MAP_LON0, DQ_MAP_LON1, DQ_MAP_LAT0, DQ_MAP_LAT1);
	T_ASSERT(h > 300 && h < 700);
	int ax, ay, ix, iy;
	dq_project(&p, 39.93, 32.86, &ax, &ay); /* Ankara east of and south of İstanbul */
	dq_project(&p, 41.01, 28.98, &ix, &iy);
	T_ASSERT(ax > ix && ay > iy);
}

static void test_view(void)
{
	DqProj full, p;
	DqView v;
	dq_proj_fit(&full, 20, 300, 1032, 520, DQ_MAP_LON0, DQ_MAP_LON1, DQ_MAP_LAT0, DQ_MAP_LAT1);
	dq_view_reset(&v);
	T_EQ_INT(v.zoom, 1);
	T_EQ_INT(dq_view_pan(&v, 0.5, 0), 0); /* nothing to pan at 1× */
	dq_proj_view(&p, &full, &v);
	T_ASSERT(p.scale == full.scale && p.lon0 == full.lon0 && p.lat1 == full.lat1);
	/* 2× around Ankara: Ankara in the middle, same pixel rectangle, distances doubled */
	v.lat = 39.93, v.lon = 32.86;
	T_EQ_INT(dq_view_zoom(&v, 1), 1);
	T_EQ_INT(v.zoom, 2);
	dq_proj_view(&p, &full, &v);
	T_ASSERT(p.x == full.x && p.y == full.y && p.w == full.w && p.h == full.h);
	int ax, ay, ix, iy, fax, fay, fix, fiy;
	dq_project(&p, 39.93, 32.86, &ax, &ay);
	T_ASSERT(abs(ax - (p.x + p.w / 2)) <= 1 && abs(ay - (p.y + p.h / 2)) <= 1);
	dq_project(&p, 41.01, 28.98, &ix, &iy);
	dq_project(&full, 39.93, 32.86, &fax, &fay);
	dq_project(&full, 41.01, 28.98, &fix, &fiy);
	T_ASSERT(abs((ax - ix) - 2 * (fax - fix)) <= 2 && abs((ay - iy) - 2 * (fay - fiy)) <= 2);
	/* unproject is the inverse */
	double lat, lon;
	dq_unproject(&p, ix, iy, &lat, &lon);
	T_ASSERT(fabs(lat - 41.01) < 0.02 && fabs(lon - 28.98) < 0.02);
	/* up to 4×, then no further; the visible area never leaves the map */
	T_EQ_INT(dq_view_zoom(&v, 1), 1);
	T_EQ_INT(v.zoom, 4);
	T_EQ_INT(dq_view_zoom(&v, 1), 0);
	T_EQ_INT(v.zoom, DQ_ZOOM_MAX);
	v.lat = 50, v.lon = 10; /* far off the map: clamped to the north-west corner */
	dq_view_clamp(&v);
	dq_proj_view(&p, &full, &v);
	int x0, y0;
	dq_project(&p, DQ_MAP_LAT1, DQ_MAP_LON0, &x0, &y0);
	T_ASSERT(abs(x0 - p.x) <= 1 && abs(y0 - p.y) <= 1);
	T_EQ_INT(dq_view_pan(&v, -0.5, -0.5), 0); /* already in the corner */
	T_EQ_INT(dq_view_pan(&v, 0.5, 0.5), 1);   /* half a view east and south */
	T_ASSERT(fabs(v.lon - (DQ_MAP_LON0 + (DQ_MAP_LON1 - DQ_MAP_LON0) / 8 + (DQ_MAP_LON1 - DQ_MAP_LON0) / 8)) < 1e-9);
	T_ASSERT(fabs(v.lat - (DQ_MAP_LAT1 - (DQ_MAP_LAT1 - DQ_MAP_LAT0) / 4)) < 1e-9);
	for (int i = 0; i < 20; i++) dq_view_pan(&v, 0.5, 0.5);
	dq_proj_view(&p, &full, &v);
	dq_project(&p, DQ_MAP_LAT0, DQ_MAP_LON1, &x0, &y0);
	T_ASSERT(abs(x0 - (p.x + p.w)) <= 1 && abs(y0 - (p.y + p.h)) <= 1); /* south-east corner, not beyond */
	/* zooming out keeps the centre where possible; at 1× it is the whole map again */
	T_EQ_INT(dq_view_zoom(&v, -1), 1);
	T_EQ_INT(v.zoom, 2);
	T_EQ_INT(dq_view_zoom(&v, -1), 1);
	T_EQ_INT(v.zoom, 1);
	T_ASSERT(fabs(v.lat - (DQ_MAP_LAT0 + DQ_MAP_LAT1) / 2) < 1e-9 && fabs(v.lon - (DQ_MAP_LON0 + DQ_MAP_LON1) / 2) < 1e-9);
	T_EQ_INT(dq_view_zoom(&v, -1), 0);
	v.zoom = 3; /* not a level: rounded down */
	dq_view_clamp(&v);
	T_EQ_INT(v.zoom, 2);
}

static void test_cities(void)
{
	T_ASSERT(dq_n_cities >= 60);
	for (int i = 0; i < dq_n_cities; i++) {
		const DqCity *c = &dq_cities[i];
		T_ASSERT(c->zoom == 1 || c->zoom == 2 || c->zoom == 4);
		if (i) T_ASSERT(c->zoom >= dq_cities[i - 1].zoom); /* by importance: the 1× ones first */
		for (int k = 0; k < i; k++) T_ASSERT(strcmp(c->name, dq_cities[k].name) != 0);
		int in = 0; /* every centre lies inside the Türkiye outline */
		for (int r = 0; r < dq_map_n_rings; r++)
			if (dq_map_rings[r].kind == 1 && dq_ring_contains(&dq_map_rings[r], c->lat, c->lon)) in = 1;
		if (!in) printf("  outside Türkiye: %s\n", c->name);
		T_ASSERT(in);
	}
}

static void test_map_data(void)
{
	int tr = 0, in_tr = 0, athens = 0;
	for (int i = 0; i < dq_map_n_rings; i++) {
		const DqRing *r = &dq_map_rings[i];
		T_ASSERT(r->n >= 3);
		if (r->kind == 1) {
			tr++;
			if (dq_ring_contains(r, 39.93, 32.86)) in_tr = 1; /* Ankara */
			T_ASSERT(!dq_ring_contains(r, 37.98, 23.73)); /* Athens */
		}
		if (r->kind == 0 && dq_ring_contains(r, 38.0, 23.9)) athens = 1; /* Attica, Greece */
	}
	T_ASSERT(tr >= 1);
	T_ASSERT(in_tr);
	T_ASSERT(athens);
	/* a scan line through Ankara crosses the Turkey outline an even number of times, Ankara between two of them */
	DqProj p;
	dq_proj_fit(&p, 0, 0, 1072, 600, DQ_MAP_LON0, DQ_MAP_LON1, DQ_MAP_LAT0, DQ_MAP_LAT1);
	int ax, ay, xs[64], ok = 0;
	dq_project(&p, 39.93, 32.86, &ax, &ay);
	for (int i = 0; i < dq_map_n_rings; i++) {
		if (dq_map_rings[i].kind != 1) continue;
		int n = dq_ring_scan(&p, &dq_map_rings[i], ay, xs, 64);
		if (n == 0) continue;
		T_EQ_INT(n % 2, 0);
		for (int k = 1; k < n; k++) T_ASSERT(xs[k] >= xs[k - 1]);
		for (int k = 0; k + 1 < n; k += 2)
			if (xs[k] <= ax && ax <= xs[k + 1]) ok = 1;
	}
	T_ASSERT(ok);
	/* place table is sorted for bsearch */
	for (int i = 1; i < dq_n_place_names; i++) T_ASSERT(strcmp(dq_place_names[i - 1].key, dq_place_names[i].key) < 0);
}

int main(void)
{
	T_RUN(test_cp1254);
	T_RUN(test_kandilli);
	T_RUN(test_kandilli_garbage);
	T_RUN(test_emsc);
	T_RUN(test_places);
	T_RUN(test_filter_sort);
	T_RUN(test_distance_radius);
	T_RUN(test_ago);
	T_RUN(test_projection);
	T_RUN(test_view);
	T_RUN(test_cities);
	T_RUN(test_map_data);
	return T_DONE();
}

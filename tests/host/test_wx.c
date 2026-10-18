#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include "kui.h"
#include "weather/wx.h"
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

static void test_parse_forecast(void)
{
	static Wx w;
	T_EQ_INT(wx_parse(slurp("tests/host/fixtures/om.json"), &w), 0);
	T_ASSERT(fabs(w.temp - 13.2) < 1e-9);
	T_ASSERT(fabs(w.feels - 10.0) < 1e-9);
	T_ASSERT(fabs(w.wind - 18.7) < 1e-9);
	T_EQ_INT(w.hum, 73);
	T_EQ_INT(w.wind_dir, 240);
	T_EQ_INT(w.code, 61);
	T_EQ_INT(w.is_day, 1);
	T_EQ_INT(w.utc_offset, 10800);
	T_EQ_STR(w.now, "16:30");
	T_EQ_INT(w.n_hours, 12);
	T_EQ_INT((int) strlen(w.hour[0].iso), 16); /* "YYYY-MM-DDTHH:MM": the date is kept for wx_advance */
	T_EQ_STR(w.hour[0].hh, "16:00");
	T_EQ_INT(w.hour[0].pop, 33);
	T_EQ_INT(w.hour[2].code, 3);
	T_EQ_INT(w.n_days, 7);
	T_EQ_STR(w.day[0].date, "2026-10-18");
	T_EQ_INT(w.day[0].wday, 0); /* Sunday */
	T_EQ_INT(w.day[5].code, 95);
	T_ASSERT(fabs(w.day[1].tmax - 16.0) < 1e-9);
	T_ASSERT(fabs(w.day[1].tmin - 12.3) < 1e-9);
	T_EQ_INT(w.day[0].pop, 100);
	T_EQ_STR(w.day[0].sunrise, "07:17");
	T_EQ_STR(w.day[0].sunset, "18:18");
	T_EQ_STR(wx_code_text(w.code), "Yağmurlu");
}

static void test_parse_partial_and_broken(void)
{
	static Wx w;
	T_EQ_INT(wx_parse("{\"utc_offset_seconds\":0,\"current\":{\"temperature_2m\":5,\"weather_code\":0}}", &w), 0);
	T_EQ_INT(w.n_days, 0);
	T_EQ_INT(w.n_hours, 0);
	T_ASSERT(isnan(w.feels)); /* missing value: shown as "—" */
	T_EQ_INT(w.hum, -1);
	T_EQ_INT(wx_parse("{\"current\":{\"temperature_2m\":5", &w), -1);
	T_EQ_INT(wx_parse("{\"error\":true,\"reason\":\"x\"}", &w), -1);
	T_EQ_INT(wx_parse("", &w), -1);
}

static void test_codes_icons_wind(void)
{
	T_EQ_STR(wx_code_text(0), "Açık");
	T_EQ_STR(wx_code_text(2), "Parçalı bulutlu");
	T_EQ_STR(wx_code_text(45), "Sisli");
	T_EQ_STR(wx_code_text(73), "Karlı");
	T_EQ_STR(wx_code_text(95), "Gök gürültülü");
	T_EQ_STR(wx_code_text(1234), "—");
	T_EQ_INT(wx_code_icon(0, 1), WX_ICON_SUN);
	T_EQ_INT(wx_code_icon(0, 0), WX_ICON_MOON);
	T_EQ_INT(wx_code_icon(2, 1), WX_ICON_PARTLY);
	T_EQ_INT(wx_code_icon(2, 0), WX_ICON_PARTLY_NIGHT);
	T_EQ_INT(wx_code_icon(3, 1), WX_ICON_CLOUD);
	T_EQ_INT(wx_code_icon(48, 1), WX_ICON_FOG);
	T_EQ_INT(wx_code_icon(81, 1), WX_ICON_RAIN);
	T_EQ_INT(wx_code_icon(86, 1), WX_ICON_SNOW);
	T_EQ_INT(wx_code_icon(99, 1), WX_ICON_STORM);
	T_EQ_STR(wx_wind_dir(0), "K");
	T_EQ_STR(wx_wind_dir(225), "GB");
	T_EQ_STR(wx_wind_dir(359), "K");
	T_EQ_STR(wx_wind_dir(90), "D");
	T_EQ_STR(wx_wind_dir(-1), "");
}

static void test_urls(void)
{
	WxCity c = { "Maltepe", "İstanbul", 40.935, 29.155 };
	char u[512];
	wx_forecast_url(&c, u, sizeof u);
	const char *want = "https://api.open-meteo.com/v1/forecast?latitude=40.9350&longitude=29.1550&";
	T_ASSERT(strncmp(u, want, strlen(want)) == 0);
	T_ASSERT(strstr(u, "timezone=auto") != NULL);
	wx_geocode_url("Kadıköy", "tr", u, sizeof u);
	T_EQ_STR(u, "https://geocoding-api.open-meteo.com/v1/search?name=Kad%C4%B1k%C3%B6y&count=50&language=tr&format=json");
}

static void test_geocode(void)
{
	WxCity c[10];
	int n = wx_geocode_parse(slurp("tests/host/fixtures/geo.json"), "Maltepe", c, 10);
	T_ASSERT(n >= 2);
	T_EQ_STR(c[0].name, "Maltepe");
	T_EQ_STR(c[0].admin, "İstanbul");
	T_ASSERT(fabs(c[0].lat - 40.93567) < 1e-6);
	T_EQ_INT(wx_geocode_parse("{\"generationtime_ms\":0.1}", "x", c, 10), 0);
	T_EQ_INT(wx_geocode_parse("garbage", "x", c, 10), 0);
}

static void test_geocode_sorted_by_population_with_district(void)
{
	const char *j = "{\"results\":["
			"{\"name\":\"Babadağ\",\"latitude\":37.8,\"longitude\":28.8,\"admin1\":\"Denizli\",\"admin2\":\"Babadağ\",\"population\":5000},"
			"{\"name\":\"Kadıköy\",\"latitude\":39.9,\"longitude\":32.8,\"admin1\":\"Ankara\",\"admin2\":\"Polatlı\"},"
			"{\"name\":\"Kadıköy\",\"latitude\":40.99,\"longitude\":29.03,\"admin1\":\"İstanbul\",\"admin2\":\"Kadıköy\",\"population\":482000}]}";
	WxCity c[10];
	T_EQ_INT(wx_geocode_parse(j, "kadıköy", c, 10), 3);
	T_EQ_STR(c[0].name, "Kadıköy");
	T_EQ_STR(c[0].admin, "İstanbul");       /* district centre; district equal to the name is not repeated */
	T_EQ_STR(c[1].admin, "Polatlı, Ankara"); /* exact name before the more populous Babadağ */
	T_EQ_STR(c[2].name, "Babadağ");
}

static void test_cities_file(void)
{
	const char *path = "build/host/cities_test.txt";
	unlink(path);
	WxCity c[WX_MAX_CITIES];
	T_EQ_INT(wx_cities_load(path, c, WX_MAX_CITIES), 1); /* default */
	T_EQ_STR(c[0].name, "Maltepe");
	T_EQ_STR(c[0].admin, "İstanbul");
	c[1] = (WxCity){ "Kadıköy", "İstanbul", 40.99, 29.03 };
	T_EQ_INT(wx_cities_save(path, c, 2), 0);
	WxCity d[WX_MAX_CITIES];
	T_EQ_INT(wx_cities_load(path, d, WX_MAX_CITIES), 2);
	T_EQ_STR(d[1].name, "Kadıköy");
	T_ASSERT(fabs(d[1].lon - 29.03) < 1e-4);
	WxCity bad = { "A|B", "x", 1, 2 };
	T_EQ_INT(wx_cities_save(path, &bad, 1), -1);
	T_EQ_INT(wx_cities_load(path, d, WX_MAX_CITIES), 2); /* unchanged */
	FILE *f = fopen(path, "w");
	fputs("junk line\nAnkara|Ankara|39.92|32.85\n|x|1|2\n", f);
	fclose(f);
	T_EQ_INT(wx_cities_load(path, d, WX_MAX_CITIES), 1);
	T_EQ_STR(d[0].name, "Ankara");
}

static void test_cache_name_follows_coordinates(void)
{
	WxCity a = { "Maltepe", "İstanbul", 40.935, 29.155 }, b = { "Ankara", "Ankara", 39.92, -32.85 };
	char s[64];
	wx_cache_name(&a, s, sizeof s);
	T_EQ_STR(s, "c_40.9350_29.1550.json");
	wx_cache_name(&b, s, sizeof s);
	T_EQ_STR(s, "c_39.9200_-32.8500.json");
}

/* a forecast up to 6 h old: past hours and days are dropped and "now" comes from the current hour */
static void test_advance_drops_the_past(void)
{
	Wx w;
	memset(&w, 0, sizeof w);
	w.temp = 20;
	w.code = 0;
	w.n_hours = 4;
	const char *iso[] = { "2026-10-18T23:00", "2026-10-19T00:00", "2026-10-19T01:00", "2026-10-19T02:00" };
	for (int i = 0; i < 4; i++) {
		snprintf(w.hour[i].iso, sizeof w.hour[i].iso, "%s", iso[i]);
		snprintf(w.hour[i].hh, sizeof w.hour[i].hh, "%s", iso[i] + 11);
		w.hour[i].t = 10 + i;
		w.hour[i].code = 3;
	}
	w.n_days = 2;
	snprintf(w.day[0].date, sizeof w.day[0].date, "2026-10-18");
	snprintf(w.day[1].date, sizeof w.day[1].date, "2026-10-19");
	w.day[1].tmax = 15;
	wx_advance(&w, "2026-10-19T01:05");
	T_EQ_INT(w.n_hours, 2); /* 01:00 (the current hour) and 02:00 */
	T_EQ_STR(w.hour[0].hh, "01:00");
	T_ASSERT(w.temp == 12); /* the current hour's forecast, not the 23:00 reading */
	T_EQ_INT(w.code, 3);
	T_EQ_INT(w.n_days, 1);
	T_EQ_STR(w.day[0].date, "2026-10-19");
	T_ASSERT(w.day[0].tmax == 15);
	wx_advance(&w, "2026-10-19T01:30"); /* nothing more to drop: unchanged */
	T_EQ_INT(w.n_hours, 2);
}

int main(void)
{
	T_RUN(test_advance_drops_the_past);
	T_RUN(test_cache_name_follows_coordinates);
	T_RUN(test_parse_forecast);
	T_RUN(test_parse_partial_and_broken);
	T_RUN(test_codes_icons_wind);
	T_RUN(test_urls);
	T_RUN(test_geocode);
	T_RUN(test_geocode_sorted_by_population_with_district);
	T_RUN(test_cities_file);
	return T_DONE();
}

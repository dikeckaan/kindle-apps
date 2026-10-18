/* Weather data: Open-Meteo forecast/geocoding parsing, WMO codes in Turkish, saved cities. */
#ifndef WX_H
#define WX_H

#define WX_MAX_CITIES 10
typedef struct { char name[64], admin[64]; double lat, lon; } WxCity;

typedef enum { WX_ICON_SUN, WX_ICON_MOON, WX_ICON_PARTLY, WX_ICON_CLOUD, WX_ICON_FOG, WX_ICON_RAIN, WX_ICON_SNOW, WX_ICON_STORM, WX_ICON_PARTLY_NIGHT } WxIcon;

typedef struct {
	int utc_offset;            /* seconds east of UTC for the city */
	char now[6];               /* "16:30" local time of the current values */
	double temp, feels, wind;  /* NAN when missing */
	int hum, wind_dir, code, is_day; /* -1 when missing */
	int n_hours;
	struct { char hh[6]; double t; int code, pop; } hour[12];
	int n_days;
	struct { char date[11]; int wday, code; double tmax, tmin; int pop; char sunrise[6], sunset[6]; } day[7];
} Wx;

int  wx_parse(const char *json, Wx *out); /* 0 = ok (current block present), -1 = unusable */
void wx_forecast_url(const WxCity *c, char *out, int cap);
void wx_geocode_url(const char *query, const char *lang, char *out, int cap); /* lang: "tr", "de", … */
/* up to max results ranked for `query`: exact name, then capitals/district centres, then population */
int  wx_geocode_parse(const char *json, const char *query, WxCity *out, int max);
const char *wx_code_text(int code); /* WMO weather code → Turkish source text (show with T()), "—" when unknown */
WxIcon      wx_code_icon(int code, int is_day);
const char *wx_wind_dir(int deg);   /* "K", "KD", … (Turkish source, show with T()); "" when unknown */

/* cache file name for a city's last forecast (keyed by coordinates, so deleting/reordering cities is safe) */
void wx_cache_name(const WxCity *c, char *out, int cap);

/* saved cities, one "name|admin|lat|lon" per line; a missing/empty file gives İstanbul / Maltepe */
int wx_cities_load(const char *path, WxCity *out, int max);
int wx_cities_save(const char *path, const WxCity *c, int n); /* -1 if a name contains '|' or a newline */
#endif

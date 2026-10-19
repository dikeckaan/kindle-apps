/* Vakitler: prayer times, sunrise/sunset, qibla and the Hijri date, computed offline (pure logic, no kui).
 *
 * Sun: Jean Meeus, "Astronomical Algorithms" ch. 25 low-precision solar coordinates (about 0.01°) and the equation
 * of time (Smart's series, as in the NOAA solar calculator). A time with the Sun at altitude h is solar noon ± the
 * hour angle of h. Times are local hours (tz = hours east of UTC) for the civil date y-m-d.
 *
 * Diyanet İşleri Başkanlığı (Türkiye) method, as compared against its published tables (accuracy note in vakit.c):
 *   İmsak  = Sun 18° below the horizon (no temkin since 1983)
 *   Güneş  = sunrise (upper limb, refraction: −0.833°) − 7 min temkin
 *   Öğle   = solar noon + 5 min
 *   İkindi = shadow = 1 × length + noon shadow (asr-ı evvel) + 4 min
 *   Akşam  = sunset + 7 min
 *   Yatsı  = Sun 17° below the horizon
 *   the Sun's declination and the equation of time are taken once per day, at 0h UT (this is what makes the
 *   published tables match to the minute; evaluating them at each event instead is off by up to 1.5 min in spring
 *   and autumn), and each time is rounded to the nearest minute.
 * Other methods use the angles of the PrayTimes.org table, no temkin, the Sun evaluated at each event time.
 * Above about 48° in summer, when the Sun does not reach the fajr/isha angle (or would take longer than the
 * "angle-based" share of the night, angle/60 of it), the angle-based high-latitude rule is used.
 */
#ifndef VAKIT_H
#define VAKIT_H

enum { VK_IMSAK, VK_GUNES, VK_OGLE, VK_IKINDI, VK_AKSAM, VK_YATSI, VK_N };
extern const char *const vk_names[VK_N];     /* "İmsak", … (Turkish source, show with T()) */
extern const char *const vk_until_fmt[VK_N]; /* "İmsaka %s", "Akşama %s", … (%s = vk_fmt_left) */

enum { VK_DIYANET, VK_MWL, VK_ISNA, VK_EGYPT, VK_UMMALQURA, VK_KARACHI, VK_UOIF, VK_N_METHODS };
typedef struct {
	const char *name;          /* Turkish source text */
	double fajr, isha;         /* depression angles in degrees */
	int isha_min;              /* > 0: isha this many minutes after maghrib instead of an angle */
	int temkin[VK_N];          /* minutes added to each time */
	int ut0;                   /* the Sun is evaluated once at 0h UT (Diyanet) instead of at each event */
} VkMethod;
const VkMethod *vk_method(int id); /* out of range → Diyanet */

typedef struct { int method, hanafi, ramadan; } VkOpts; /* ramadan: Umm al-Qura's isha is 120 min instead of 90 */

typedef struct {
	double t[VK_N];   /* local hours with temkin, NAN when there is none (polar day or night) */
	int min[VK_N];    /* the same rounded to minutes after local midnight, -1 when none */
	double sunrise, sunset, noon; /* astronomical, local hours (no temkin); sunrise/sunset NAN in polar day/night */
	double day_len;   /* hours (24 polar day, 0 polar night) */
	int high_lat;     /* fajr or isha came from the high-latitude rule */
} VkDay;
void vk_day(int y, int m, int d, double lat, double lon, double tz, const VkOpts *o, VkDay *out);

/* the Sun now: azimuth from true north through east, altitude (no refraction), degrees */
void vk_sun_azalt(double unix_t, double lat, double lon, double *az, double *alt);

/* qibla: initial great-circle bearing to the Kaaba (21.4225 N, 39.8262 E), degrees from true north, and the
 * distance in km (sphere, R = 6371.0 km) */
#define VK_KAABA_LAT 21.4225
#define VK_KAABA_LON 39.8262
void vk_qibla(double lat, double lon, double *bearing, double *km);
/* "kıble saati": the local hour when the Sun's azimuth equals the qibla bearing (facing the Sun then faces the
 * qibla), NAN when it does not happen while the Sun is up */
double vk_qibla_time(int y, int m, int d, double lat, double lon, double tz);

/* ---- calendar ---- */
long vk_jdn(int y, int m, int d);                 /* Gregorian date → Julian day number */
void vk_from_jdn(long jdn, int *y, int *m, int *d);
int  vk_wday(long jdn);                           /* 0 = Sunday */
int  vk_month_days(int y, int m);

/* places: a time zone as minutes east of UTC plus a daylight-saving rule */
enum { VK_DST_NONE, VK_DST_EU, VK_DST_US, VK_DST_AU, VK_DST_EG, VK_DST_DEVICE };
typedef struct { char name[64]; double lat, lon; int tz_min, dst; } VkPlace;
typedef struct { const char *name; double lat, lon; int tz_min, dst, plate; } VkCityDef;
extern const VkCityDef vk_tr_cities[81];          /* the 81 provinces in plate order */
extern const VkCityDef vk_world_cities[];
extern const int vk_n_world_cities;
/* seconds east of UTC at unix time t (VK_DST_DEVICE is resolved by the caller: here it is tz_min) */
int  vk_utc_offset(const VkPlace *p, long t);
void vk_place_from_def(const VkCityDef *c, VkPlace *p);
int  vk_place_parse(const char *s, VkPlace *p);   /* "name|lat|lon|tz_min|dst" → 0 = ok */
void vk_place_format(const VkPlace *p, char *out, int cap);
/* "41.01, 28.98" / "41,01 28,98" / "-33.9 151.2" → 0 = ok (|lat| ≤ 90, |lon| ≤ 180) */
int  vk_parse_latlon(const char *s, double *lat, double *lon);

/* ---- now: the current and next prayer ---- */
typedef struct {
	int cur;        /* the time whose period we are in (VK_YATSI before today's imsak) */
	int next;       /* the next time */
	long next_at;   /* unix seconds of the next time */
	int y, m, d;    /* the local date of the times shown (today) */
	int now_min;    /* local minutes after midnight */
} VkNow;
/* times for place p at unix time `now`; utc_off: seconds east of UTC; 0 = ok, -1 when there is no next time
 * within two days (polar regions) */
int  vk_now(long now, double lat, double lon, int utc_off, const VkOpts *o, VkNow *out, VkDay *today);
/* minutes rounded up: hm_fmt "%d sa %d dk", h_fmt "%d sa" (whole hours), m_fmt "%d dk" */
void vk_fmt_left(long secs, char *out, int cap, const char *hm_fmt, const char *h_fmt, const char *m_fmt);
/* The hook for Pano and others: the next prayer with the Diyanet method; returns its index (VK_IMSAK …), -1 if none.
 * Pure (link vakit.c, cities.c not needed, sky/astro.c). The place Vakitler uses is $KUI_ROOT/data/vakit/place:
 *   VkPlace p; char b[256];
 *   if (kui_read_file(path, b, sizeof b) > 0 && vk_place_parse(b, &p) == 0) {
 *       long at; int i = vk_next_prayer(now, p.lat, p.lon, vk_utc_offset(&p, now), &at);  // T(vk_names[i]) at `at`
 *   }
 * (p.dst == VK_DST_DEVICE: use the device's own UTC offset instead of vk_utc_offset) */
int  vk_next_prayer(long now, double lat, double lon, int utc_off, long *at);

/* ---- Hijri calendar ---- */
typedef struct { int y, m, d; } VkHijri;
extern const char *const vk_hijri_months[12];     /* "Muharrem" … "Zilhicce" */
void vk_hijri_tabular(long jdn, VkHijri *h);      /* arithmetic (civil, epoch 16 July 622 Julian), ±1–2 days */
/* the month begins on the day after the first evening on which, somewhere on Earth before 24:00 UT, the Moon is at
 * least 5° high at sunset and 8° from the Sun (the 2016 İstanbul Hicri Takvim Birliği criterion Diyanet uses;
 * evaluated on a coarse grid of sites, so it can rarely be a day off) */
void vk_hijri(long jdn, VkHijri *h);
double vk_new_moon(double jd);                    /* the conjunction (geocentric, ecliptic) nearest jd */

/* ---- the Moon ---- */
extern const char *const vk_moon_names[8];        /* "Yeni Ay", "Hilal", "İlk Dördün", … */
int vk_moon_phase(double unix_t, double *illum, double *age_days); /* index into vk_moon_names */
#endif

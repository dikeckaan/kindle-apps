/* Gökyüzü: a live star chart for the first saved weather city, computed offline. A stereographic all-sky map
 * (zenith in the middle, north up, east left — as when holding a planisphere overhead) with the stars to magnitude
 * 4.5, constellation figures, the planets, the Moon in its phase and the Sun. Tapping an object opens a small info
 * box (partial refresh); a new time redraws the chart with one full refresh. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "canvas.h"
#include "kui.h"
#include "pano/pano.h"
#include "sky/astro.h"
#include "skydata.h"
#include "weather/wx.h"

#define RAD (M_PI / 180.0)
#define MAX_STARS 2048
#define MAX_LABELS 160

/* geometry (1072 × 1448) */
static int CX = 536, CY = 632, R = 460;
static KuiRect r_chart, r_panel;
enum { BTN_BACK_DAY = 1, BTN_BACK_HOUR, BTN_NOW, BTN_FWD_HOUR, BTN_FWD_DAY, N_BTN = 5 };
static KuiButton btns[N_BTN];

static WxCity city;
static double t_view; /* unix time shown */
static int live = 1;  /* following the clock ("Şimdi") */
static SkyCanvas cv;

/* the sky at t_view */
typedef struct { double e, n, u; } Vec; /* horizon frame: east, north, up */
static double jd, lst;
static Vec sv[MAX_STARS];
static struct { AstroPos p; double alt, az, x, y, r; int vis; } bod[ASTRO_N_BODY];
static int is_day;

static const char *const body_name[ASTRO_N_BODY] = { /* N_ */ "☉ Güneş", "☾ Ay", "☿ Merkür", "♀ Venüs", "♂ Mars", "♃ Jüpiter", "♄ Satürn" };

/* what is selected (info box) */
enum { SEL_NONE, SEL_BODY, SEL_STAR, SEL_CON };
static int sel_kind, sel_idx;
static KuiRect r_box, r_ring, r_fig; /* on screen now (w == 0: none): the box, the ring, the selected figure */

typedef struct { KuiRect r; char text[96]; KuiFont f; int px; unsigned char g; int con; } Label;
static Label labels[MAX_LABELS];
static int n_labels, labels_ok; /* labels_ok: placed for the sky shown */

/* ---- time ---- */

static int local_off(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_gmtoff ? (int) tm.tm_gmtoff : 3 * 3600; /* no zone on the device: Türkiye (UTC+3) */
}

static void local_tm(double unix_t, struct tm *tm)
{
	time_t t = (time_t) floor(unix_t + local_off());
	gmtime_r(&t, tm);
}

static void fmt_hhmm(double jd_t, char *out, int cap)
{
	if (jd_t <= 0) {
		snprintf(out, (size_t) cap, "—");
		return;
	}
	struct tm tm;
	local_tm(astro_unix(jd_t) + 30, &tm); /* rounded to the minute */
	snprintf(out, (size_t) cap, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* ---- projection ---- */

static Vec hvec(double alt, double az)
{
	return (Vec){ cos(alt * RAD) * sin(az * RAD), cos(alt * RAD) * cos(az * RAD), sin(alt * RAD) };
}

static Vec radec_vec(double ra, double dec)
{
	double alt, az;
	astro_hor(ra, dec, city.lat, lst, &alt, &az);
	return hvec(alt, az);
}

/* stereographic from the nadir: the horizon is the circle of radius R, east to the left */
static void proj(Vec v, double *x, double *y)
{
	double k = R / (1 + v.u);
	*x = CX - k * v.e;
	*y = CY - k * v.n;
}

static Vec vnorm(Vec v)
{
	double l = sqrt(v.e * v.e + v.n * v.n + v.u * v.u);
	return (Vec){ v.e / l, v.n / l, v.u / l };
}

static Vec vmix(Vec a, Vec b, double t) { return vnorm((Vec){ a.e + (b.e - a.e) * t, a.n + (b.n - a.n) * t, a.u + (b.u - a.u) * t }); }

static double vang(Vec a, Vec b)
{
	double d = a.e * b.e + a.n * b.n + a.u * b.u;
	return acos(d > 1 ? 1 : d < -1 ? -1 : d) / RAD;
}

/* where the arc from below (lo) to above the horizon (hi) crosses it */
static Vec horizon_point(Vec lo, Vec hi)
{
	for (int k = 0; k < 20; k++) {
		Vec m = vmix(lo, hi, 0.5);
		if (m.u < 0) lo = m;
		else hi = m;
	}
	return hi;
}

/* a great-circle arc above the horizon, as short straight pieces; dash: on/off pattern length in pieces (0 = solid) */
static void arc(Vec a, Vec b, double width, unsigned char g, int dash)
{
	int n = (int) ceil(vang(a, b) / 2.0);
	if (n < 1) n = 1;
	for (int i = 0; i < n; i++) {
		if (dash && (i / dash) % 2) continue;
		Vec p = vmix(a, b, (double) i / n), q = vmix(a, b, (double) (i + 1) / n);
		if (p.u < 0 && q.u < 0) continue;
		if (p.u < 0) p = horizon_point(p, q); /* clip at the horizon */
		else if (q.u < 0) q = horizon_point(q, p);
		double x0, y0, x1, y1;
		proj(p, &x0, &y0);
		proj(q, &x1, &y1);
		cv_line(&cv, x0, y0, x1, y1, width, g);
	}
}

static double star_r(double mag)
{
	double r = 1.0 + (4.8 - mag) * 1.05;
	return r < 1.0 ? 1.0 : r > 8.5 ? 8.5 : r;
}

/* ---- the sky at t_view ---- */

static void compute(void)
{
	jd = astro_jd(t_view);
	lst = astro_gmst(jd) + city.lon;
	for (int i = 0; i < sky_n_stars && i < MAX_STARS; i++) {
		double ra = sky_stars[i].ra, dec = sky_stars[i].dec;
		astro_precess(jd, &ra, &dec);
		sv[i] = radec_vec(ra, dec);
	}
	for (int b = 0; b < ASTRO_N_BODY; b++) {
		astro_body(b, jd, &bod[b].p);
		bod[b].alt = astro_alt(b, jd, city.lat, city.lon, &bod[b].az);
		bod[b].vis = bod[b].alt > 0;
		proj(hvec(bod[b].alt, bod[b].az), &bod[b].x, &bod[b].y);
		bod[b].r = b == ASTRO_SUN ? 14 : b == ASTRO_MOON ? 17 : star_r(bod[b].p.mag) + 0.5;
	}
	is_day = bod[ASTRO_SUN].alt > -0.833;
	labels_ok = 0;
}

static const char *con_name(int c)
{
	if (c < 0 || c >= sky_n_cons) return "";
	return sky_cons[c].tr ? T(sky_cons[c].tr) : sky_cons[c].latin;
}

/* capitals for the constellation labels in every language (kui_tr_upper_str knows only Turkish: i → İ) */
static int upper_cp(int c, int turkic)
{
	static const int greek_acc[][2] = { { 0x3AC, 0x391 }, { 0x3AD, 0x395 }, { 0x3AE, 0x397 }, { 0x3AF, 0x399 }, { 0x3CC, 0x39F }, { 0x3CD, 0x3A5 }, { 0x3CE, 0x3A9 },
					    { 0x386, 0x391 }, { 0x388, 0x395 }, { 0x389, 0x397 }, { 0x38A, 0x399 }, { 0x38C, 0x39F }, { 0x38E, 0x3A5 }, { 0x38F, 0x3A9 } };
	if (turkic) {
		int u = kui_tr_upper(c);
		if (u != c) return u;
		if (c == 0x259) return 0x18F; /* ə → Ə */
	}
	if (c >= 'a' && c <= 'z') return c - 32;
	if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
	if (c == 0x131) return 'I';
	if ((c >= 0x100 && c <= 0x137 && (c & 1)) || (c >= 0x14A && c <= 0x177 && (c & 1))) return c - 1;
	if ((c >= 0x139 && c <= 0x148 && !(c & 1)) || (c >= 0x179 && c <= 0x17E && !(c & 1))) return c - 1;
	if (c == 0x219 || c == 0x21B) return c - 1; /* ș ț */
	for (size_t i = 0; i < sizeof greek_acc / sizeof greek_acc[0]; i++) /* Greek capitals drop the accents */
		if (greek_acc[i][0] == c) return greek_acc[i][1];
	if (c == 0x3C2) return 0x3A3;
	if (c >= 0x3B1 && c <= 0x3C9) return c - 32;
	if (c >= 0x430 && c <= 0x44F) return c - 32;
	if (c >= 0x450 && c <= 0x45F) return c - 80;
	if (c == 0x491) return 0x490;
	return c;
}

static void upper_label(const char *in, char *out, int cap)
{
	int turkic = !strcmp(kui_lang(), "tr") || !strcmp(kui_lang(), "az"), n = 0;
	while (*in && n < cap - 5) {
		int c;
		in += kui_utf8_decode(in, &c);
		if (c == 0xDF) { /* ß */
			out[n++] = 'S';
			out[n++] = 'S';
			continue;
		}
		n += kui_utf8_encode(upper_cp(c, turkic), out + n);
	}
	out[n] = '\0';
}

static const char *star_name(int i) { return sky_stars[i].name ? T(sky_stars[i].name) : sky_stars[i].desig; }

/* ---- labels: placed greedily without overlapping each other or bright objects ---- */

typedef struct { KuiRect r; double mag; int body; } Obstacle;
static Obstacle obst[MAX_STARS / 4];
static int n_obst;

static int overlaps(KuiRect a, KuiRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static int inside_circle(KuiRect r)
{
	double lim = (double) (R - 4) * (R - 4);
	int xs[2] = { r.x, r.x + r.w }, ys[2] = { r.y, r.y + r.h };
	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 2; j++)
			if ((xs[i] - CX) * (double) (xs[i] - CX) + (ys[j] - CY) * (double) (ys[j] - CY) > lim) return 0;
	return 1;
}

/* how bad a spot is: 0 = free; over another label 100, over a body or a bright star 10, over a fainter star 1 */
static int spot_cost(KuiRect r, int self)
{
	if (!inside_circle(r)) return 1000;
	int cost = 0;
	for (int i = 0; i < n_labels; i++)
		if (overlaps(r, labels[i].r)) cost += 100;
	for (int i = 0; i < n_obst; i++)
		if (obst[i].body != self && overlaps(r, obst[i].r)) cost += obst[i].body >= 0 || obst[i].mag < 2.0 ? 10 : 1;
	return cost;
}

static void obstacle(double x, double y, int r, double mag, int body)
{
	if (n_obst < (int) (sizeof obst / sizeof obst[0])) obst[n_obst++] = (Obstacle){ { (int) x - r, (int) y - r, 2 * r, 2 * r }, mag, body };
}

/* a label beside an object of radius orad at (x, y): right, left, above, below or a diagonal, or near the middle of
 * a figure (centered); con: constellation index or -1; body: the labelled body (its own obstacle is ignored) or -1.
 * Bodies are always labelled (at the least crowded spot), the rest only where they fit. */
static void add_label(double x, double y, double orad, const char *text, KuiFont f, int px, unsigned char g, int centered, int con, int body)
{
	if (n_labels == MAX_LABELS) return;
	int w = kui_text_width(text, f, px) + 6, h = px + 8, gap = (int) orad + 5;
	KuiRect c[16];
	int n = 0;
	if (centered) { /* near the middle of the figure */
		static const int off[][2] = { { 0, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 }, { 0, 2 }, { 0, -2 } };
		for (int i = 0; i < 11; i++) c[n++] = (KuiRect){ (int) x - w / 2 + off[i][0] * w / 2, (int) y - h / 2 + off[i][1] * h, w, h };
	} else { /* beside the object: right, left, above, below, then the diagonals */
		int d = (int) (gap * 0.75);
		c[n++] = (KuiRect){ (int) x + gap, (int) y - h / 2, w, h };
		c[n++] = (KuiRect){ (int) x - gap - w, (int) y - h / 2, w, h };
		c[n++] = (KuiRect){ (int) x - w / 2, (int) y - gap - h + 4, w, h };
		c[n++] = (KuiRect){ (int) x - w / 2, (int) y + gap - 4, w, h };
		c[n++] = (KuiRect){ (int) x + d, (int) y - d - h + 6, w, h };
		c[n++] = (KuiRect){ (int) x + d, (int) y + d - 6, w, h };
		c[n++] = (KuiRect){ (int) x - d - w, (int) y - d - h + 6, w, h };
		c[n++] = (KuiRect){ (int) x - d - w, (int) y + d - 6, w, h };
		if (body >= 0) { /* a crowded spot: a bit farther away */
			int f = gap + h;
			c[n++] = (KuiRect){ (int) x + f, (int) y - h / 2, w, h };
			c[n++] = (KuiRect){ (int) x - f - w, (int) y - h / 2, w, h };
			c[n++] = (KuiRect){ (int) x - w / 2, (int) y - f - h, w, h };
			c[n++] = (KuiRect){ (int) x - w / 2, (int) y + f, w, h };
		}
	}
	int best = -1, best_cost = 1 << 30;
	for (int i = 0; i < n; i++) {
		int cost = spot_cost(c[i], body);
		if (cost < best_cost) {
			best_cost = cost;
			best = i;
		}
		if (!cost) break;
	}
	if (best < 0 || (best_cost > 0 && (body < 0 || best_cost >= 1000))) return;
	Label *l = &labels[n_labels++];
	l->r = c[best];
	snprintf(l->text, sizeof l->text, "%s", text);
	l->f = f;
	l->px = px;
	l->g = g;
	l->con = con;
}

static void place_labels(void)
{
	n_labels = n_obst = 0;
	/* obstacles: the bodies and the bright stars */
	for (int b = 0; b < ASTRO_N_BODY; b++)
		if (bod[b].vis) obstacle(bod[b].x, bod[b].y, (int) bod[b].r + (b == ASTRO_SUN ? 12 : 6), -30, b);
	for (int i = 0; i < sky_n_stars && i < MAX_STARS && sky_stars[i].mag < 3.6; i++) {
		if (sv[i].u <= 0) continue;
		double x, y;
		proj(sv[i], &x, &y);
		obstacle(x, y, (int) star_r(sky_stars[i].mag) + 1, sky_stars[i].mag, -1);
	}
	/* the bodies first, then the named stars, then the constellations */
	for (int b = 0; b < ASTRO_N_BODY; b++)
		if (bod[b].vis) add_label(bod[b].x, bod[b].y, bod[b].r + (b == ASTRO_SUN ? 12 : 4), T(body_name[b]), KUI_FONT_BOLD, 27, 0, 0, -1, b);
	for (int i = 0; i < sky_n_stars && i < MAX_STARS; i++) {
		if (!sky_stars[i].name || sv[i].u <= sin(3 * RAD)) continue;
		double x, y;
		proj(sv[i], &x, &y);
		add_label(x, y, star_r(sky_stars[i].mag), T(sky_stars[i].name), KUI_FONT_REG, 23, 0, 0, -1, -1);
	}
	for (int rank = 1; rank <= 3; rank++)
		for (int c = 0; c < sky_n_cons; c++) {
			if (sky_cons[c].rank != rank || !sky_cons[c].tr) continue;
			Vec m = { 0, 0, 0 };
			int n = 0;
			for (int s = 0; s < sky_n_segs; s++)
				if (sky_segs[s].con == c) {
					Vec a = sv[sky_segs[s].a], b = sv[sky_segs[s].b];
					m = (Vec){ m.e + a.e + b.e, m.n + a.n + b.n, m.u + a.u + b.u };
					n += 2;
				}
			if (!n) continue;
			m = vnorm(m);
			if (m.u < sin(6 * RAD)) continue;
			double x, y;
			char up[96];
			proj(m, &x, &y);
			upper_label(T(sky_cons[c].tr), up, sizeof up);
			add_label(x, y, 0, up, KUI_FONT_SERIF, 21, 60, 1, c, -1);
		}
}

/* ---- drawing the chart ---- */

static void draw_moon(double x, double y, double r)
{
	/* the lit side faces the Sun: its direction on the chart from a small step along the great circle */
	Vec m = hvec(bod[ASTRO_MOON].alt, bod[ASTRO_MOON].az), s = hvec(bod[ASTRO_SUN].alt, bod[ASTRO_SUN].az);
	double d = m.e * s.e + m.n * s.n + m.u * s.u;
	Vec t = vnorm((Vec){ s.e - d * m.e, s.n - d * m.n, s.u - d * m.u });
	double x1, y1;
	proj(vnorm((Vec){ m.e + 0.01 * t.e, m.n + 0.01 * t.n, m.u + 0.01 * t.u }), &x1, &y1);
	double dx = x1 - x, dy = y1 - y, l = hypot(dx, dy);
	dx /= l;
	dy /= l;
	double k = astro_moon_illum(jd, NULL), cos_i = 2 * k - 1, sin_i = sqrt(1 - cos_i * cos_i);
	cv_disc(&cv, x, y, r + 3, 255);
	for (int py = (int) floor(y - r - 1); py <= (int) ceil(y + r + 1); py++)
		for (int px = (int) floor(x - r - 1); px <= (int) ceil(x + r + 1); px++) {
			double u = (px + 0.5 - x) / r, v = (py + 0.5 - y) / r, q = u * u + v * v;
			if (q > 1) continue;
			double along = u * dx + v * dy, z = sqrt(1 - q);
			int lit = along * sin_i + z * cos_i > 0;
			KuiRect pr = { px, py, 1, 1 };
			cv_rect(&cv, pr, lit ? 255 : 85);
		}
	cv_ring(&cv, x, y, r, 2.2, 0);
}

static void draw_sun(double x, double y)
{
	cv_disc(&cv, x, y, 26, 255);
	cv_ring(&cv, x, y, 12, 2.6, 0);
	cv_disc(&cv, x, y, 3.2, 0);
	for (int k = 0; k < 8; k++) {
		double a = k * 45 * RAD;
		cv_line(&cv, x + 17 * cos(a), y + 17 * sin(a), x + 24 * cos(a), y + 24 * sin(a), 2.2, 0);
	}
}

static void draw_planet(double x, double y, double r)
{
	cv_disc(&cv, x, y, r + 7, 255);
	cv_disc(&cv, x, y, r, 0);
	cv_ring(&cv, x, y, r + 4, 1.6, 0);
}

static void sel_pos(double *x, double *y, double *r);

/* the chart into the off-screen canvas */
static void render_chart(void)
{
	cv_clear(&cv, 255);
	unsigned char g_line = is_day ? 205 : 120, g_star = is_day ? 130 : 0, g_faint = is_day ? 190 : 110;
	/* altitude circles at 30° and 60° (dotted) and the zenith */
	for (int alt = 30; alt <= 60; alt += 30)
		for (int az = 0; az < 360; az += 3) {
			double x, y;
			proj(hvec(alt, az), &x, &y);
			cv_disc(&cv, x, y, 0.9, 150);
		}
	cv_line(&cv, CX - 8, CY, CX + 8, CY, 1.4, 120);
	cv_line(&cv, CX, CY - 8, CX, CY + 8, 1.4, 120);
	/* the ecliptic (dashed): the path of the Sun, the Moon and the planets */
	{
		double eps = 23.4393 * RAD;
		for (int l = 0; l < 360; l += 4) {
			double l0 = l * RAD, l1 = (l + 4) * RAD;
			double ra0 = atan2(sin(l0) * cos(eps), cos(l0)) / RAD, de0 = asin(sin(eps) * sin(l0)) / RAD;
			double ra1 = atan2(sin(l1) * cos(eps), cos(l1)) / RAD, de1 = asin(sin(eps) * sin(l1)) / RAD;
			if ((l / 4) % 2 == 0) arc(radec_vec(ra0, de0), radec_vec(ra1, de1), 1.3, 150, 0);
		}
	}
	/* constellation figures; the selected one in black */
	r_fig = (KuiRect){ 0, 0, 0, 0 };
	for (int s = 0; s < sky_n_segs; s++) {
		int on = sel_kind == SEL_CON && sky_segs[s].con == sel_idx;
		arc(sv[sky_segs[s].a], sv[sky_segs[s].b], on ? 3.0 : 1.7, on ? 0 : g_line, 0);
		Vec a = sv[sky_segs[s].a], b = sv[sky_segs[s].b];
		if (on && (a.u > 0 || b.u > 0)) { /* its extent (an end below the horizon: the horizon point is inside R) */
			for (int k = 0; k < 2; k++) {
				Vec v = k ? b : a, o = k ? a : b;
				double x, y;
				if (v.u < 0) v = horizon_point(v, o);
				proj(v, &x, &y);
				KuiRect p = { (int) x - 4, (int) y - 4, 9, 9 };
				r_fig = kui_rect_union(r_fig, p);
			}
		}
	}
	/* stars, faintest first, each with a thin white halo so that lines stop short of it */
	for (int i = sky_n_stars - 1; i >= 0; i--) {
		if (i >= MAX_STARS || sv[i].u <= 0) continue;
		double x, y, m = sky_stars[i].mag;
		proj(sv[i], &x, &y);
		if (m > 4.5) {
			cv_disc(&cv, x, y, 2.6, 255);
			cv_disc(&cv, x, y, 1.0, g_faint);
			continue;
		}
		double r = star_r(m);
		cv_disc(&cv, x, y, r + 2.2, 255);
		cv_disc(&cv, x, y, r, g_star);
	}
	/* the horizon: a double ring with azimuth ticks every 5° (long every 45°) */
	cv_ring(&cv, CX, CY, R, 3.2, 0);
	cv_ring(&cv, CX, CY, R + 14, 1.4, 0);
	for (int az = 0; az < 360; az += 5) {
		double a = az * RAD, l = az % 45 == 0 ? 14 : az % 15 == 0 ? 9 : 5;
		cv_line(&cv, CX - (R + 1) * sin(a), CY - (R + 1) * cos(a), CX - (R + 1 + l) * sin(a), CY - (R + 1 + l) * cos(a), 1.4, 0);
	}
	/* the bodies */
	for (int b = ASTRO_SATURN; b >= ASTRO_SUN; b--) {
		if (!bod[b].vis) continue;
		if (b == ASTRO_SUN) draw_sun(bod[b].x, bod[b].y);
		else if (b == ASTRO_MOON) draw_moon(bod[b].x, bod[b].y, bod[b].r);
		else draw_planet(bod[b].x, bod[b].y, bod[b].r);
	}
	/* the selection */
	r_ring = (KuiRect){ 0, 0, 0, 0 };
	if (sel_kind == SEL_BODY || sel_kind == SEL_STAR) {
		double x, y, r;
		sel_pos(&x, &y, &r);
		cv_ring(&cv, x, y, r + 8, 2.6, 0); /* the labels knock it out where they are */
		int rr = (int) r + 13;
		r_ring = (KuiRect){ (int) x - rr, (int) y - rr, 2 * rr, 2 * rr };
	}
	/* labels: knocked out of the lines */
	if (!labels_ok) place_labels();
	labels_ok = 1;
	for (int i = 0; i < n_labels; i++) {
		KuiRect r = labels[i].r;
		cv_rect(&cv, (KuiRect){ r.x + 1, r.y + 3, r.w - 2, r.h - 6 }, 255);
	}
	/* the magnitude scale's dots (bottom left corner) */
	for (int m = 0; m <= 4; m++) cv_disc(&cv, 34 + m * 44, r_chart.y + r_chart.h - 42, star_r(m), 0);
}

/* the chart's texts, collected so that a partial repaint can draw each one exactly once over a clean background */
typedef struct { KuiRect r; char s[160]; KuiFont f; int px; KuiAlign a; unsigned char g; } Txt;
static Txt txts[MAX_LABELS + 32];
static int n_txts;

static void ctext(KuiRect box, const char *text, KuiFont f, int px, KuiAlign a, unsigned char g)
{
	if (n_txts == (int) (sizeof txts / sizeof txts[0])) return;
	Txt *t = &txts[n_txts++];
	*t = (Txt){ box, "", f, px, a, g };
	snprintf(t->s, sizeof t->s, "%s", text);
}

static void chart_texts(void);

/* the canvas and the chart's texts onto the screen buffer (no refresh). n == 0: all of the chart; otherwise only the
 * rects rs[], each grown to whole texts (so no glyph is blended twice); rs[] then holds the areas to refresh. */
static void paint_chart(KuiRect *rs, int n)
{
	n_txts = 0;
	chart_texts();
	if (!n) {
		cv_blit(&cv, r_chart);
		for (int i = 0; i < n_txts; i++) kui_text(txts[i].r, txts[i].s, txts[i].f, txts[i].px, txts[i].a, txts[i].g);
		return;
	}
	for (int i = 0; i < n; i++)
		for (int grown = rs[i].w > 0; grown;) {
			grown = 0;
			for (int k = 0; k < n_txts; k++)
				if (overlaps(rs[i], txts[k].r) && (kui_rect_union(rs[i], txts[k].r).w != rs[i].w || kui_rect_union(rs[i], txts[k].r).h != rs[i].h)) {
					rs[i] = kui_rect_union(rs[i], txts[k].r);
					grown = 1;
				}
		}
	for (int i = 0; i < n; i++)
		if (rs[i].w > 0) cv_blit(&cv, rs[i]);
	for (int k = 0; k < n_txts; k++) {
		int on = 0;
		for (int i = 0; i < n && !on; i++) on = rs[i].w > 0 && overlaps(rs[i], txts[k].r);
		if (on) kui_text(txts[k].r, txts[k].s, txts[k].f, txts[k].px, txts[k].a, txts[k].g);
	}
}

static void chart_texts(void)
{
	for (int i = 0; i < n_labels; i++)
		ctext(labels[i].r, labels[i].text, labels[i].f, labels[i].px, KUI_ALIGN_CENTER, labels[i].g);
	/* the cardinal points */
	static const char *const card[4] = { "K", "D", "G", "B" }; /* wx_wind_dir's texts, already translated */
	for (int k = 0; k < 4; k++) {
		double a = k * 90 * RAD;
		int x = (int) (CX - (R + 36) * sin(a)), y = (int) (CY - (R + 36) * cos(a));
		ctext((KuiRect){ x - 30, y - 22, 60, 44 }, T(card[k]), KUI_FONT_BOLD, 34, KUI_ALIGN_CENTER, 0);
	}
	/* the corners: time and place (top), magnitude scale and a hint (bottom) */
	char s[128];
	struct tm tm;
	local_tm(t_view, &tm);
	snprintf(s, sizeof s, "%02d:%02d", tm.tm_hour, tm.tm_min);
	ctext((KuiRect){ 20, r_chart.y + 6, 220, 56 }, s, KUI_FONT_BOLD, 50, KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, "%d %s %d", tm.tm_mday, kui_tr_month(tm.tm_mon), tm.tm_year + 1900);
	ctext((KuiRect){ 20, r_chart.y + 62, 230, 32 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 25, 220), KUI_ALIGN_LEFT, 0);
	const char *wd = kui_tr_weekday(tm.tm_wday);
	ctext((KuiRect){ 20, r_chart.y + 92, 230, 32 }, wd, KUI_FONT_REG, kui_fit_px(wd, KUI_FONT_REG, 25, 220), KUI_ALIGN_LEFT, 0);
	int rw = 250, rx = r_chart.x + r_chart.w - rw - 20;
	ctext((KuiRect){ rx, r_chart.y + 10, rw, 40 }, city.name, KUI_FONT_BOLD, kui_fit_px(city.name, KUI_FONT_BOLD, 32, rw), KUI_ALIGN_RIGHT, 0);
	snprintf(s, sizeof s, "%.1f° %s  %.1f° %s", fabs(city.lat), T(city.lat >= 0 ? "K" : "G"), fabs(city.lon), T(city.lon >= 0 ? "D" : "B"));
	for (char *p = s; *p; p++) /* decimal mark of the language */
		if (*p == '.') {
			char dm[8];
			kui_fmt_num(0.5, 1, dm, sizeof dm); /* "0,5" or "0.5" */
			*p = dm[1];
		}
	ctext((KuiRect){ rx, r_chart.y + 52, rw, 32 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 24, rw), KUI_ALIGN_RIGHT, 0);
	/* magnitude scale */
	int ly = r_chart.y + r_chart.h - 46;
	const char *ml = T("Parlaklık (kadir)");
	ctext((KuiRect){ 20, ly - 44, 320, 34 }, ml, KUI_FONT_REG, kui_fit_px(ml, KUI_FONT_REG, 22, 310), KUI_ALIGN_LEFT, 0);
	for (int m = 0; m <= 4; m++) {
		double x = 34 + m * 44;
		snprintf(s, sizeof s, "%d", m);
		ctext((KuiRect){ (int) x - 20, ly + 14, 40, 28 }, s, KUI_FONT_REG, 20, KUI_ALIGN_CENTER, 0);
	}
	const char *hint = T("Ayrıntı için bir yıldıza ya da gezegene dokunun");
	KuiRect hr = { rx - 20, ly - 44, rw + 20, 86 };
	static KuiLine hl[4];
	int nl = kui_wrap_text(hint, hr.w, KUI_FONT_REG, 22, hl, 4), lh = kui_line_height(KUI_FONT_REG, 22);
	for (int i = 0; i < nl && i < 3; i++) {
		char line[160];
		snprintf(line, sizeof line, "%.*s", hl[i].len, hint + hl[i].off);
		ctext((KuiRect){ hr.x, hr.y + hr.h - (nl - i) * lh, hr.w, lh }, line, KUI_FONT_REG, 22, KUI_ALIGN_RIGHT, 90);
	}
}

/* ---- the panel and the buttons ---- */

static void planet_list(char *out, int cap)
{
	/* tonight: from dusk (Sun 6° below the horizon) to dawn, a planet at least 5° up at some time */
	double start = bod[ASTRO_SUN].alt < -6 ? jd : astro_next_cross(ASTRO_SUN, jd, city.lat, city.lon, -6, 0, 1.0);
	out[0] = '\0';
	if (!start) return;
	double end = astro_next_cross(ASTRO_SUN, start, city.lat, city.lon, -6, 1, 1.0);
	if (!end) end = start + 1;
	for (int b = ASTRO_MERCURY; b <= ASTRO_SATURN; b++)
		for (double t = start; t <= end; t += 20.0 / 1440)
			if (astro_alt(b, t, city.lat, city.lon, NULL) > 5) {
				snprintf(out + strlen(out), (size_t) (cap - (int) strlen(out)), "%s%s", out[0] ? ", " : "", T(body_name[b]));
				break;
			}
}

static void draw_panel(void)
{
	KuiRect r = r_panel;
	kui_fill(r, 255);
	kui_fill((KuiRect){ 30, r.y, kui_width() - 60, 2 }, 0);
	char s[300], a[16], b[16];
	double rise, set;
	int x = 34, w = kui_width() - 68, y = r.y + 14, lh = 56;
	/* the Sun */
	if (is_day) {
		double ns = astro_next_cross(ASTRO_SUN, jd, city.lat, city.lon, -0.833, 0, 1.0);
		if (ns) {
			fmt_hhmm(ns, a, sizeof a);
			snprintf(s, sizeof s, T("Gündüz · Güneş batınca yıldızlar görünür (gün batımı %s)"), a);
		} else {
			snprintf(s, sizeof s, "%s", T("Gündüz · Güneş bugün batmıyor"));
		}
	} else {
		double ps = astro_next_cross(ASTRO_SUN, jd - 1, city.lat, city.lon, -0.833, 0, 1.0), nr = astro_next_cross(ASTRO_SUN, jd, city.lat, city.lon, -0.833, 1, 1.0);
		if (!nr) {
			snprintf(s, sizeof s, "%s", T("Gece · Güneş bugün doğmuyor"));
		} else {
			fmt_hhmm(ps, a, sizeof a);
			fmt_hhmm(nr, b, sizeof b);
			snprintf(s, sizeof s, T("Gece · gün batımı %s, gün doğumu %s"), a, b);
		}
	}
	kui_text((KuiRect){ x, y, w, lh }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 30, w), KUI_ALIGN_LEFT, 0);
	y += lh;
	/* the Moon */
	double age = astro_moon_age(jd);
	astro_pass(ASTRO_MOON, jd, city.lat, city.lon, &rise, &set);
	fmt_hhmm(rise, a, sizeof a);
	fmt_hhmm(set, b, sizeof b);
	snprintf(s, sizeof s, T("Ay: %s, %%%d aydınlık · doğuş %s · batış %s"), T(pano_moon_phase_name(age)), (int) lround(astro_moon_illum(jd, NULL) * 100), a, b);
	kui_text((KuiRect){ x, y, w, lh }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, w), KUI_ALIGN_LEFT, 0);
	y += lh;
	/* the planets */
	char pl[200];
	planet_list(pl, sizeof pl);
	if (pl[0]) snprintf(s, sizeof s, T("Bu gece görünen gezegenler: %s"), pl);
	else snprintf(s, sizeof s, "%s", T("Bu gece parlak gezegen görünmüyor"));
	kui_text((KuiRect){ x, y, w, lh }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, w), KUI_ALIGN_LEFT, 0);
	kui_mark_dirty(r);
}

static void draw_buttons(void)
{
	static const char *const lbl[N_BTN] = { /* N_ */ "−1 gün", "−1 sa", "Şimdi", "+1 sa", "+1 gün" };
	int gap = 14, bw = (kui_width() - 60 - gap * (N_BTN - 1)) / N_BTN, y = r_chart.y + r_chart.h + 6;
	for (int i = 0; i < N_BTN; i++) {
		btns[i] = (KuiButton){ { 30 + i * (bw + gap), y, bw, 84 }, T(lbl[i]), i + 1 };
		kui_button(&btns[i], i + 1 == BTN_NOW && live);
	}
}

/* ---- info box ---- */

static void sel_pos(double *x, double *y, double *r)
{
	if (sel_kind == SEL_BODY) {
		*x = bod[sel_idx].x;
		*y = bod[sel_idx].y;
		*r = bod[sel_idx].r + (sel_idx == ASTRO_SUN ? 10 : 2);
	} else {
		proj(sv[sel_idx], x, y);
		*r = star_r(sky_stars[sel_idx].mag);
	}
}

static void fmt_altaz(double alt, double az, char *out, int cap)
{
	snprintf(out, (size_t) cap, T("Yükseklik %d° · yön %s (%d°)"), (int) lround(alt), T(wx_wind_dir((int) lround(az) % 360)), (int) lround(az) % 360);
}

static void draw_box(void)
{
	char line[5][200];
	int n = 0;
	const char *title = "";
	double ox, oy, orad;
	char mag[24];
	if (sel_kind == SEL_BODY) {
		int b = sel_idx;
		double rise, set;
		char a[16], c[16];
		title = T(body_name[b]);
		if (b == ASTRO_MOON) {
			snprintf(line[n++], 200, T("%s · %%%d aydınlık"), T(pano_moon_phase_name(astro_moon_age(jd))), (int) lround(astro_moon_illum(jd, NULL) * 100));
		} else {
			kui_fmt_num(bod[b].p.mag, 1, mag, sizeof mag);
			if (b == ASTRO_SUN) snprintf(line[n++], 200, "%s", T("Yıldız"));
			else snprintf(line[n++], 200, T("Gezegen · parlaklık %s kadir"), mag);
		}
		fmt_altaz(bod[b].alt, bod[b].az, line[n++], 200);
		astro_pass(b, jd, city.lat, city.lon, &rise, &set);
		fmt_hhmm(rise, a, sizeof a);
		fmt_hhmm(set, c, sizeof c);
		snprintf(line[n++], 200, T("Doğuş %s · batış %s"), a, c);
		if (b >= ASTRO_MERCURY) {
			char dist[24];
			kui_fmt_num(bod[b].p.dist, 2, dist, sizeof dist);
			snprintf(line[n++], 200, T("Uzaklık %s AB (Güneş–Dünya = 1)"), dist);
		}
		ox = bod[b].x;
		oy = bod[b].y;
		orad = bod[b].r;
	} else if (sel_kind == SEL_STAR) {
		const SkyStar *st = &sky_stars[sel_idx];
		title = star_name(sel_idx);
		const char *cn = con_name(st->con);
		if (st->name) snprintf(line[n++], 200, T("Yıldız · %s · %s"), st->desig, cn);
		else snprintf(line[n++], 200, T("Yıldız · %s"), cn);
		kui_fmt_num(st->mag, 1, mag, sizeof mag);
		snprintf(line[n++], 200, T("Parlaklık %s kadir"), mag);
		double alt = asin(sv[sel_idx].u) / RAD, az = fmod(atan2(sv[sel_idx].e, sv[sel_idx].n) / RAD + 360, 360);
		fmt_altaz(alt, az, line[n++], 200);
		proj(sv[sel_idx], &ox, &oy);
		orad = star_r(st->mag);
	} else {
		const SkyCon *c = &sky_cons[sel_idx];
		title = con_name(sel_idx);
		snprintf(line[n++], 200, T("Takımyıldız · %s"), c->latin);
		int best = -1;
		for (int i = 0; i < sky_n_stars; i++)
			if (sky_stars[i].con == sel_idx) {
				best = i; /* brightest first */
				break;
			}
		if (best >= 0) snprintf(line[n++], 200, T("En parlak yıldızı: %s"), star_name(best));
		/* the label or the middle of the figure */
		ox = CX;
		oy = CY;
		orad = 0;
		for (int i = 0; i < n_labels; i++)
			if (labels[i].con == sel_idx) {
				ox = labels[i].r.x + labels[i].r.w / 2;
				oy = labels[i].r.y + labels[i].r.h / 2;
				orad = labels[i].r.h / 2;
			}
	}
	int w = 470, h = 74 + n * 42 + 14;
	for (int i = 0; i < n; i++) { /* widen for long lines */
		int lw = kui_text_width(line[i], KUI_FONT_REG, 26) + 44;
		if (lw > w) w = lw;
	}
	if (w > kui_width() - 40) w = kui_width() - 40;
	int gap = (int) orad + 26;
	int x = (int) ox + gap, y = (int) oy + gap;
	if (x + w > kui_width() - 16) x = (int) ox - gap - w;
	if (x < 16) x = 16;
	if (x + w > kui_width() - 16) x = kui_width() - 16 - w;
	if (y + h > r_chart.y + r_chart.h - 4) y = (int) oy - gap - h;
	if (y < r_chart.y + 4) y = r_chart.y + 4;
	r_box = (KuiRect){ x, y, w, h };
	kui_fill(r_box, 255);
	kui_frame(r_box, 3, 0);
	kui_fill((KuiRect){ x + 6, y + 6, w - 12, 1 }, 255);
	kui_text((KuiRect){ x + 22, y + 10, w - 44, 56 }, title, KUI_FONT_BOLD, kui_fit_px(title, KUI_FONT_BOLD, 36, w - 44), KUI_ALIGN_LEFT, 0);
	kui_fill((KuiRect){ x + 22, y + 66, w - 44, 2 }, 0);
	for (int i = 0; i < n; i++)
		kui_text((KuiRect){ x + 22, y + 74 + i * 42, w - 44, 42 }, line[i], KUI_FONT_REG, kui_fit_px(line[i], KUI_FONT_REG, 26, w - 44), KUI_ALIGN_LEFT, 0);
}

/* ---- hit test ---- */

static double seg_px_dist(double px, double py, double x0, double y0, double x1, double y1)
{
	double dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

static int hit(int x, int y, int *kind, int *idx)
{
	double best = 1e9;
	*kind = SEL_NONE;
	for (int b = 0; b < ASTRO_N_BODY; b++) {
		if (!bod[b].vis) continue;
		double d = hypot(x - bod[b].x, y - bod[b].y) - bod[b].r;
		if (d < 40 && d < best) {
			best = d;
			*kind = SEL_BODY;
			*idx = b;
		}
	}
	if (*kind) return 1;
	for (int i = 0; i < n_labels; i++)
		if (labels[i].con >= 0 && kui_rect_contains(labels[i].r, x, y)) {
			*kind = SEL_CON;
			*idx = labels[i].con;
			return 1;
		}
	for (int i = 0; i < sky_n_stars && i < MAX_STARS; i++) {
		if (sv[i].u <= 0 || sky_stars[i].mag > 4.5) continue;
		double sx, sy;
		proj(sv[i], &sx, &sy);
		double d = hypot(x - sx, y - sy);
		if (d > 38) continue;
		double score = d + 5 * sky_stars[i].mag; /* the brighter star wins a near tie */
		if (score < best) {
			best = score;
			*kind = SEL_STAR;
			*idx = i;
		}
	}
	if (*kind) return 1;
	best = 22;
	for (int s = 0; s < sky_n_segs; s++) {
		Vec a = sv[sky_segs[s].a], b = sv[sky_segs[s].b];
		if (a.u <= 0 || b.u <= 0) continue;
		double x0, y0, x1, y1;
		proj(a, &x0, &y0);
		proj(b, &x1, &y1);
		double d = seg_px_dist(x, y, x0, y0, x1, y1);
		if (d < best) {
			best = d;
			*kind = SEL_CON;
			*idx = sky_segs[s].con;
		}
	}
	return *kind != SEL_NONE;
}

/* ---- screens ---- */

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Gökyüzü"), NULL);
	compute();
	sel_kind = SEL_NONE;
	r_box = r_ring = r_fig = (KuiRect){ 0, 0, 0, 0 };
	render_chart();
	paint_chart(NULL, 0);
	draw_buttons();
	draw_panel();
	kui_flush(1); /* a new sky: one full refresh */
}

/* selection changed: repaint and refresh only where the old and the new box and ring are */
static void update_selection(void)
{
	KuiRect old_box = r_box, old_ring = r_ring, old_fig = r_fig;
	render_chart(); /* the new ring or figure */
	KuiRect rs[6] = { old_box, old_ring, old_fig, r_ring, r_fig, { 0, 0, 0, 0 } };
	paint_chart(rs, 5);
	r_box = (KuiRect){ 0, 0, 0, 0 };
	if (sel_kind != SEL_NONE) draw_box();
	rs[5] = r_box;
	for (int i = 0; i < 6; i++)
		if (rs[i].w > 0) kui_mark_dirty(rs[i]);
	kui_flush(0);
}

static void set_time(double t, int follow)
{
	t_view = t;
	live = follow;
	draw_all();
}

int main(void)
{
	if (kui_init("sky") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes only for a new chart */
	int w = kui_width(), h = kui_height();
	R = w * 43 / 100;
	CX = w / 2;
	CY = KUI_TOPBAR_H + 52 + R;
	r_chart = (KuiRect){ 0, KUI_TOPBAR_H, w, 2 * R + 112 };
	r_panel = (KuiRect){ 0, r_chart.y + r_chart.h + 100, w, h - (r_chart.y + r_chart.h + 100) };
	if (sky_n_stars > MAX_STARS || cv_init(&cv, r_chart) != 0) {
		kui_quit();
		return 1;
	}
	char cpath[400];
	WxCity cs[WX_MAX_CITIES];
	snprintf(cpath, sizeof cpath, "%s/cities.txt", kui_data_dir("weather"));
	wx_cities_load(cpath, cs, WX_MAX_CITIES);
	city = cs[0];
	const char *fake = getenv("SKY_TIME"); /* simulator and tests: a fixed Unix time */
	set_time(fake && *fake ? atof(fake) : (double) time(NULL), !(fake && *fake));
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) {
			set_time(live ? (double) time(NULL) : t_view, live);
			continue;
		}
		if (e.type == KUI_EV_SWIPE) { /* swipe left: later, right: earlier */
			if (e.dir == KUI_DIR_LEFT) set_time(t_view + 3600, 0);
			else if (e.dir == KUI_DIR_RIGHT) set_time(t_view - 3600, 0);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) break;
		switch (kui_buttons_hit(btns, N_BTN, e.x, e.y)) {
		case BTN_BACK_DAY: set_time(t_view - 86400, 0); continue;
		case BTN_BACK_HOUR: set_time(t_view - 3600, 0); continue;
		case BTN_NOW: set_time((double) time(NULL), 1); continue;
		case BTN_FWD_HOUR: set_time(t_view + 3600, 0); continue;
		case BTN_FWD_DAY: set_time(t_view + 86400, 0); continue;
		default: break;
		}
		if (!kui_rect_contains(r_chart, e.x, e.y)) continue;
		int kind, idx;
		if (r_box.w && kui_rect_contains(r_box, e.x, e.y)) kind = SEL_NONE; /* tapping the box closes it */
		else if (!hit(e.x, e.y, &kind, &idx)) kind = SEL_NONE;
		if (kind == SEL_NONE && sel_kind == SEL_NONE) continue;
		if (kind != SEL_NONE && kind == sel_kind && idx == sel_idx) kind = SEL_NONE; /* the same object again: close */
		sel_kind = kind;
		sel_idx = kind ? idx : 0;
		update_selection();
	}
	cv_free(&cv);
	kui_quit();
	return 0;
}

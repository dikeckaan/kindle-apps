#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "canvas.h"

int cv_init(SkyCanvas *c, KuiRect r)
{
	c->x = r.x;
	c->y = r.y;
	c->w = r.w;
	c->h = r.h;
	c->px = malloc((size_t) r.w * (size_t) r.h);
	return c->px ? 0 : -1;
}

void cv_free(SkyCanvas *c)
{
	free(c->px);
	c->px = NULL;
}

void cv_clear(SkyCanvas *c, unsigned char g) { memset(c->px, g, (size_t) c->w * (size_t) c->h); }

static void plot(SkyCanvas *c, int x, int y, double cov, unsigned char g)
{
	x -= c->x;
	y -= c->y;
	if (cov <= 0 || x < 0 || y < 0 || x >= c->w || y >= c->h) return;
	if (cov > 1) cov = 1;
	unsigned char *p = &c->px[y * c->w + x];
	*p = (unsigned char) lround(*p + (g - *p) * cov);
}

void cv_disc(SkyCanvas *c, double x, double y, double r, unsigned char g)
{
	int x0 = (int) floor(x - r - 1), x1 = (int) ceil(x + r + 1), y0 = (int) floor(y - r - 1), y1 = (int) ceil(y + r + 1);
	for (int py = y0; py <= y1; py++)
		for (int px = x0; px <= x1; px++) {
			double d = hypot(px + 0.5 - x, py + 0.5 - y);
			plot(c, px, py, r + 0.5 - d, g);
		}
}

void cv_ring(SkyCanvas *c, double x, double y, double r, double width, unsigned char g)
{
	double o = r + width / 2 + 1;
	int x0 = (int) floor(x - o), x1 = (int) ceil(x + o), y0 = (int) floor(y - o), y1 = (int) ceil(y + o);
	for (int py = y0; py <= y1; py++) {
		double dy = py + 0.5 - y;
		if (fabs(dy) > o) continue;
		/* only the pixels near the circle: skip the inside of big rings */
		double inner = r - width / 2 - 1, span = inner > fabs(dy) ? sqrt(inner * inner - dy * dy) : -1;
		for (int px = x0; px <= x1; px++) {
			double dx = px + 0.5 - x;
			if (span > 0 && fabs(dx) < span && (int) floor(x + span) - 1 > px) {
				px = (int) floor(x + span) - 1;
				continue;
			}
			plot(c, px, py, width / 2 + 0.5 - fabs(hypot(dx, dy) - r), g);
		}
	}
}

/* distance from (px, py) to the segment */
static double seg_dist(double px, double py, double x0, double y0, double x1, double y1)
{
	double dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

void cv_line(SkyCanvas *c, double x0, double y0, double x1, double y1, double width, unsigned char g)
{
	double hw = width / 2 + 1;
	int steep = fabs(y1 - y0) > fabs(x1 - x0);
	double a0 = steep ? y0 : x0, a1 = steep ? y1 : x1;
	if (a0 > a1) {
		double t = a0;
		a0 = a1;
		a1 = t;
	}
	/* walk the major axis; cover the few pixels across the line at each step */
	for (int a = (int) floor(a0 - hw); a <= (int) ceil(a1 + hw); a++) {
		double t = steep ? (y1 != y0 ? (a + 0.5 - y0) / (y1 - y0) : 0) : (x1 != x0 ? (a + 0.5 - x0) / (x1 - x0) : 0);
		if (t < 0) t = 0;
		if (t > 1) t = 1;
		double m = steep ? x0 + t * (x1 - x0) : y0 + t * (y1 - y0), slope = steep ? fabs(x1 - x0) / (fabs(y1 - y0) + 1e-9) : fabs(y1 - y0) / (fabs(x1 - x0) + 1e-9);
		int span = (int) ceil(hw * sqrt(1 + slope * slope)) + 1;
		for (int b = (int) floor(m) - span; b <= (int) floor(m) + span; b++) {
			int px = steep ? b : a, py = steep ? a : b;
			plot(c, px, py, width / 2 + 0.5 - seg_dist(px + 0.5, py + 0.5, x0, y0, x1, y1), g);
		}
	}
}

void cv_rect(SkyCanvas *c, KuiRect r, unsigned char g)
{
	for (int y = r.y; y < r.y + r.h; y++)
		for (int x = r.x; x < r.x + r.w; x++) plot(c, x, y, 1, g);
}

unsigned char cv_get(const SkyCanvas *c, int x, int y)
{
	x -= c->x;
	y -= c->y;
	return x < 0 || y < 0 || x >= c->w || y >= c->h ? 255 : c->px[y * c->w + x];
}

void cv_blit(const SkyCanvas *c, KuiRect r)
{
	int x0 = r.x > c->x ? r.x : c->x, y0 = r.y > c->y ? r.y : c->y;
	int x1 = r.x + r.w < c->x + c->w ? r.x + r.w : c->x + c->w, y1 = r.y + r.h < c->y + c->h ? r.y + r.h : c->y + c->h;
	if (x1 <= x0 || y1 <= y0) return;
	kui_fill((KuiRect){ x0, y0, x1 - x0, y1 - y0 }, 255);
	for (int y = y0; y < y1; y++) {
		const unsigned char *row = c->px + (y - c->y) * c->w - c->x;
		for (int x = x0; x < x1;) {
			unsigned char g = row[x];
			int e = x + 1;
			while (e < x1 && row[e] == g) e++;
			if (g != 255) kui_fill((KuiRect){ x, y, e - x, 1 }, g);
			x = e;
		}
	}
}

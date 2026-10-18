/* An 8-bit gray off-screen canvas with antialiased discs, rings and lines; blitted to the screen as runs of kui_fill. */
#ifndef SKY_CANVAS_H
#define SKY_CANVAS_H
#include "kui.h"

typedef struct { int x, y, w, h; unsigned char *px; } SkyCanvas; /* x, y: its place on the screen */

int  cv_init(SkyCanvas *c, KuiRect r); /* 0 = ok */
void cv_free(SkyCanvas *c);
void cv_clear(SkyCanvas *c, unsigned char g);
/* drawing paints towards gray g by each pixel's coverage; coordinates are screen pixels (centres at +0.5) */
void cv_disc(SkyCanvas *c, double x, double y, double r, unsigned char g);
void cv_ring(SkyCanvas *c, double x, double y, double r, double width, unsigned char g);
void cv_line(SkyCanvas *c, double x0, double y0, double x1, double y1, double width, unsigned char g);
void cv_rect(SkyCanvas *c, KuiRect r, unsigned char g); /* solid */
unsigned char cv_get(const SkyCanvas *c, int x, int y); /* 255 outside */
void cv_blit(const SkyCanvas *c, KuiRect r);           /* the part of the canvas inside r to the screen */
#endif

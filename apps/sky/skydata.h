/* Gökyüzü sky data (generated into skydata.c by tools/sky/gen_skydata.py). */
#ifndef SKYDATA_H
#define SKYDATA_H

/* a constellation: IAU abbreviation, Latin name, Turkish name (source text, show with T(); NULL: Latin only),
 * rank 1 … 3 (1 = most prominent) */
typedef struct { char abbr[4]; const char *latin, *tr; int rank; } SkyCon;
/* a star, J2000 position in degrees; con indexes sky_cons (-1: none); desig "α Ori"; name Turkish source text or NULL */
typedef struct { float ra, dec, mag; short con; const char *desig, *name; } SkyStar;
/* a line of a constellation figure between two stars */
typedef struct { unsigned short a, b; unsigned char con; } SkySeg;

extern const SkyCon sky_cons[];
extern const int sky_n_cons;
extern const SkyStar sky_stars[]; /* brightest first */
extern const int sky_n_stars;
extern const SkySeg sky_segs[];
extern const int sky_n_segs;
#endif

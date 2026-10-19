/* Not Defteri: pure logic — strokes and pages, smoothing and simplification, rasterising into an ink mask,
 * page backgrounds, the note file format, thumbnails and PNG export. No device calls (host-tested). */
#ifndef NOTLAR_H
#define NOTLAR_H
#include "kui.h"

/* tools: the three pens and the eraser (stored per stroke) */
enum { NT_PEN_THIN, NT_PEN_MED, NT_PEN_THICK, NT_ERASER, NT_TOOLS };
int nt_tool_radius(int tool); /* brush radius in px: 1, 2, 4, 24 */

typedef struct { short x, y; } NtPt;
typedef struct { unsigned char tool; int off, n; } NtStroke; /* points p[off .. off+n) of its page */
typedef struct { NtStroke *s; int ns, cap_s; NtPt *p; int np, cap_p; } NtPage;

#define NT_MAX_STROKES 20000
#define NT_MAX_POINTS  400000 /* per page */
#define NT_MAX_PAGES   200

/* strokes: the page is an undo stack — the last stroke (pen or eraser) is what "Geri al" takes back */
int  nt_page_begin(NtPage *pg, int tool, int x, int y); /* new stroke; its index, -1 when full */
int  nt_page_add(NtPage *pg, int x, int y);             /* point on the last stroke; 0 = ok, -1 when full */
void nt_page_end(NtPage *pg, double tol);               /* simplifies the last stroke (tolerance px) */
int  nt_page_undo(NtPage *pg);                          /* drops the last stroke: 1, or 0 when empty */
void nt_page_clear(NtPage *pg);
void nt_page_free(NtPage *pg);
int  nt_page_copy(NtPage *dst, const NtPage *src);      /* 0 = ok; dst is overwritten */

/* smoothing for live finger input: an exponential moving average over samples at least 2 px apart */
typedef struct { float x, y; int lx, ly, rx, ry, ex, ey; } NtSmooth;
void nt_smooth_start(NtSmooth *s, int x, int y);
int  nt_smooth_feed(NtSmooth *s, int x, int y, int *ox, int *oy); /* 1 when a new point (ox, oy) is emitted */
int  nt_smooth_end(NtSmooth *s, int *ox, int *oy);                /* the last raw sample, if not emitted yet */
/* Ramer–Douglas–Peucker: keeps the first and last point and those further than tol from the chord; returns n */
int  nt_simplify(NtPt *p, int n, double tol);

/* ink mask: one byte per canvas pixel, 1 = ink */
typedef struct { unsigned char *px; int w, h; } NtMask;
KuiRect nt_seg_bbox(int x0, int y0, int x1, int y1, int r, int w, int h); /* clipped to w×h; w == 0 when empty */
KuiRect nt_seg(NtMask *m, int x0, int y0, int x1, int y1, int tool); /* pen: sets ink, eraser: clears; dirty box */
KuiRect nt_stroke_draw(NtMask *m, const NtPage *pg, int si, int from);  /* segments from point `from` on */
void    nt_page_render(NtMask *m, const NtPage *pg);                    /* clears, then replays every stroke */

/* page backgrounds: Düz (plain), Kareli (grid), Çizgili (ruled) */
enum { NT_BG_PLAIN, NT_BG_GRID, NT_BG_LINES, NT_BGS };
#define NT_BG_GRAY 0xB0
#define NT_GRID_STEP 56
#define NT_LINE_STEP 64
int nt_bg_rects(int bg, KuiRect r, int w, int h, KuiRect *out, int max); /* gray line rects inside r */
unsigned char nt_bg_pixel(int bg, int x, int y);

/* notes: drawings (pages of strokes) or typed text (paragraphs separated by '\n') */
enum { NT_KIND_DRAW, NT_KIND_TEXT };
#define NT_TITLE_MAX 128
#define NT_TEXT_MAX  16000
typedef struct {
	int kind, bg;
	long created, modified;
	char title[NT_TITLE_MAX];
	char *text; /* NUL-terminated, malloc'd; may be NULL */
	NtPage *pages;
	int npages;
} NtNote;
int  nt_note_init(NtNote *n, int kind, long now); /* a drawing gets one empty page; 0 = ok */
void nt_note_free(NtNote *n);
int  nt_note_add_page(NtNote *n, int at);         /* new empty page at index at; its index, -1 when full */
int  nt_note_del_page(NtNote *n, int at);         /* the last page is kept (emptied); 0 = ok */
int  nt_note_strokes(const NtNote *n);            /* strokes on all pages */

/* text paragraphs */
int  nt_para_count(const char *text);
int  nt_para_get(const char *text, int i, char *out, int cap);  /* bytes copied, -1 if no paragraph i */
int  nt_para_set(char **text, int i, const char *s);            /* i == count appends; "" deletes; 0 = ok */

/* file format "NTL1" (little endian, see notlar.c); buffers are malloc'd */
int nt_note_serialize(const NtNote *n, unsigned char **out, int *len); /* 0 = ok */
int nt_note_parse(const unsigned char *buf, int len, NtNote *n);       /* 0 = ok, -1 corrupt */
int nt_note_save(const char *path, const NtNote *n);                   /* atomic; 0 = ok */
int nt_note_load(const char *path, NtNote *n);                         /* 0 = ok */

/* list summary: the title, else the first text line, else "" (the caller shows a default) */
void nt_note_label(const NtNote *n, char *out, int cap);

/* thumbnail: the mask scaled down to tw×th grays (ink coverage, darkened so thin lines show) */
void nt_thumb(const NtMask *m, unsigned char *out, int tw, int th);

/* PNG (8-bit gray: background + ink) of the mask; fixed-Huffman deflate with run lengths, no zlib needed */
int nt_png(const NtMask *m, int bg, unsigned char **out, int *len); /* 0 = ok */
unsigned long nt_crc32(const unsigned char *p, int n);

#endif

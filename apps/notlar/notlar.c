/* Not Defteri: pure logic (see notlar.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "notlar/notlar.h"

int nt_tool_radius(int tool)
{
	static const int r[NT_TOOLS] = { 1, 2, 4, 24 };
	return tool >= 0 && tool < NT_TOOLS ? r[tool] : 1;
}

/* ---- strokes ---- */

static int grow(void **p, int *cap, int need, int size)
{
	if (need <= *cap) return 0;
	int c = *cap ? *cap : 16;
	while (c < need) c *= 2;
	void *q = realloc(*p, (size_t) c * (size_t) size);
	if (!q) return -1;
	*p = q;
	*cap = c;
	return 0;
}

static int clamp_short(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

int nt_page_begin(NtPage *pg, int tool, int x, int y)
{
	if (pg->ns >= NT_MAX_STROKES || pg->np >= NT_MAX_POINTS) return -1;
	if (grow((void **) &pg->s, &pg->cap_s, pg->ns + 1, (int) sizeof *pg->s) < 0) return -1;
	if (grow((void **) &pg->p, &pg->cap_p, pg->np + 1, (int) sizeof *pg->p) < 0) return -1;
	NtStroke *s = &pg->s[pg->ns];
	s->tool = (unsigned char) (tool >= 0 && tool < NT_TOOLS ? tool : 0);
	s->off = pg->np;
	s->n = 1;
	pg->p[pg->np++] = (NtPt){ (short) clamp_short(x), (short) clamp_short(y) };
	return pg->ns++;
}

int nt_page_add(NtPage *pg, int x, int y)
{
	if (!pg->ns || pg->np >= NT_MAX_POINTS) return -1;
	if (grow((void **) &pg->p, &pg->cap_p, pg->np + 1, (int) sizeof *pg->p) < 0) return -1;
	pg->p[pg->np++] = (NtPt){ (short) clamp_short(x), (short) clamp_short(y) };
	pg->s[pg->ns - 1].n++;
	return 0;
}

void nt_page_end(NtPage *pg, double tol)
{
	if (!pg->ns) return;
	NtStroke *s = &pg->s[pg->ns - 1];
	s->n = nt_simplify(pg->p + s->off, s->n, tol);
	pg->np = s->off + s->n;
}

int nt_page_undo(NtPage *pg)
{
	if (!pg->ns) return 0;
	pg->np = pg->s[--pg->ns].off;
	return 1;
}

void nt_page_clear(NtPage *pg) { pg->ns = pg->np = 0; }

void nt_page_free(NtPage *pg)
{
	free(pg->s);
	free(pg->p);
	memset(pg, 0, sizeof *pg);
}

int nt_page_copy(NtPage *dst, const NtPage *src)
{
	nt_page_clear(dst);
	if (grow((void **) &dst->s, &dst->cap_s, src->ns, (int) sizeof *dst->s) < 0) return -1;
	if (grow((void **) &dst->p, &dst->cap_p, src->np, (int) sizeof *dst->p) < 0) return -1;
	if (src->ns) memcpy(dst->s, src->s, (size_t) src->ns * sizeof *src->s);
	if (src->np) memcpy(dst->p, src->p, (size_t) src->np * sizeof *src->p);
	dst->ns = src->ns;
	dst->np = src->np;
	return 0;
}

/* ---- smoothing and simplification ---- */

void nt_smooth_start(NtSmooth *s, int x, int y)
{
	s->x = (float) x;
	s->y = (float) y;
	s->lx = s->rx = s->ex = x;
	s->ly = s->ry = s->ey = y;
}

int nt_smooth_feed(NtSmooth *s, int x, int y, int *ox, int *oy)
{
	s->ex = x;
	s->ey = y;
	int dx = x - s->rx, dy = y - s->ry;
	if (dx * dx + dy * dy < 4) return 0; /* finger jitter */
	s->rx = x;
	s->ry = y;
	s->x += (x - s->x) * 0.5f;
	s->y += (y - s->y) * 0.5f;
	int nx = (int) lrintf(s->x), ny = (int) lrintf(s->y);
	if (nx == s->lx && ny == s->ly) return 0;
	*ox = s->lx = nx;
	*oy = s->ly = ny;
	return 1;
}

int nt_smooth_end(NtSmooth *s, int *ox, int *oy)
{
	if (s->ex == s->lx && s->ey == s->ly) return 0;
	*ox = s->lx = s->ex;
	*oy = s->ly = s->ey;
	return 1;
}

static double seg_dist(NtPt p, NtPt a, NtPt b)
{
	double vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
	double l2 = vx * vx + vy * vy;
	if (l2 == 0) return sqrt(wx * wx + wy * wy);
	double t = (wx * vx + wy * vy) / l2;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	double dx = wx - t * vx, dy = wy - t * vy;
	return sqrt(dx * dx + dy * dy);
}

int nt_simplify(NtPt *p, int n, double tol)
{
	if (n <= 2) {
		if (n == 2 && p[0].x == p[1].x && p[0].y == p[1].y) return 1;
		return n;
	}
	unsigned char *keep = calloc((size_t) n, 1);
	int *stack = malloc(sizeof(int) * 2 * (size_t) n);
	if (!keep || !stack) {
		free(keep);
		free(stack);
		return n;
	}
	keep[0] = keep[n - 1] = 1;
	int sp = 0;
	stack[sp++] = 0;
	stack[sp++] = n - 1;
	while (sp) {
		int b = stack[--sp], a = stack[--sp];
		double best = -1;
		int bi = -1;
		for (int i = a + 1; i < b; i++) {
			double d = seg_dist(p[i], p[a], p[b]);
			if (d > best) best = d, bi = i;
		}
		if (bi >= 0 && best > tol) {
			keep[bi] = 1;
			stack[sp++] = a;
			stack[sp++] = bi;
			stack[sp++] = bi;
			stack[sp++] = b;
		}
	}
	int m = 0;
	for (int i = 0; i < n; i++)
		if (keep[i]) p[m++] = p[i];
	free(keep);
	free(stack);
	if (m == 2 && p[0].x == p[1].x && p[0].y == p[1].y) m = 1; /* a dot */
	return m;
}

/* ---- rasterising ---- */

KuiRect nt_seg_bbox(int x0, int y0, int x1, int y1, int r, int w, int h)
{
	int l = (x0 < x1 ? x0 : x1) - r, t = (y0 < y1 ? y0 : y1) - r;
	int rr = (x0 > x1 ? x0 : x1) + r + 1, b = (y0 > y1 ? y0 : y1) + r + 1;
	if (l < 0) l = 0;
	if (t < 0) t = 0;
	if (rr > w) rr = w;
	if (b > h) b = h;
	if (rr <= l || b <= t) return (KuiRect){ 0, 0, 0, 0 };
	return (KuiRect){ l, t, rr - l, b - t };
}

static void stamp(NtMask *m, int cx, int cy, int r, const int *hw, unsigned char v)
{
	for (int dy = -r; dy <= r; dy++) {
		int y = cy + dy;
		if (y < 0 || y >= m->h) continue;
		int a = cx - hw[dy + r], b = cx + hw[dy + r];
		if (a < 0) a = 0;
		if (b >= m->w) b = m->w - 1;
		if (a <= b) memset(m->px + (size_t) y * (size_t) m->w + a, v, (size_t) (b - a + 1));
	}
}

KuiRect nt_seg(NtMask *m, int x0, int y0, int x1, int y1, int tool)
{
	int r = nt_tool_radius(tool), hw[2 * 32 + 1];
	if (r > 32) r = 32;
	for (int dy = -r; dy <= r; dy++) hw[dy + r] = (int) (sqrt((double) (r * r - dy * dy)) + 0.35);
	unsigned char v = tool == NT_ERASER ? 0 : 1;
	int dx = x1 - x0, dy = y1 - y0, len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
	int step = r / 2 > 1 ? r / 2 : 1, n = len / step + 1;
	for (int i = 0; i <= n; i++) {
		int x = x0 + (int) lrint((double) dx * i / n), y = y0 + (int) lrint((double) dy * i / n);
		stamp(m, x, y, r, hw, v);
	}
	return nt_seg_bbox(x0, y0, x1, y1, r, m->w, m->h);
}

KuiRect nt_stroke_draw(NtMask *m, const NtPage *pg, int si, int from)
{
	KuiRect box = { 0, 0, 0, 0 };
	if (si < 0 || si >= pg->ns) return box;
	const NtStroke *s = &pg->s[si];
	const NtPt *p = pg->p + s->off;
	if (from <= 0) box = nt_seg(m, p[0].x, p[0].y, p[0].x, p[0].y, s->tool);
	for (int i = from > 1 ? from : 1; i < s->n; i++) box = kui_rect_union(box, nt_seg(m, p[i - 1].x, p[i - 1].y, p[i].x, p[i].y, s->tool));
	return box;
}

void nt_page_render(NtMask *m, const NtPage *pg)
{
	memset(m->px, 0, (size_t) m->w * (size_t) m->h);
	for (int i = 0; i < pg->ns; i++) nt_stroke_draw(m, pg, i, 0);
}

/* ---- backgrounds ---- */

#define BG_LINE_W 2

unsigned char nt_bg_pixel(int bg, int x, int y)
{
	if (bg == NT_BG_GRID && ((x >= NT_GRID_STEP && x % NT_GRID_STEP < BG_LINE_W) || (y >= NT_GRID_STEP && y % NT_GRID_STEP < BG_LINE_W))) return NT_BG_GRAY;
	if (bg == NT_BG_LINES && y >= NT_LINE_STEP && y % NT_LINE_STEP < BG_LINE_W) return NT_BG_GRAY;
	return 255;
}

static int clip_add(KuiRect a, KuiRect r, KuiRect *out, int n, int max)
{
	int l = a.x > r.x ? a.x : r.x, t = a.y > r.y ? a.y : r.y;
	int rr = a.x + a.w < r.x + r.w ? a.x + a.w : r.x + r.w, b = a.y + a.h < r.y + r.h ? a.y + a.h : r.y + r.h;
	if (rr <= l || b <= t || n >= max) return n;
	out[n] = (KuiRect){ l, t, rr - l, b - t };
	return n + 1;
}

int nt_bg_rects(int bg, KuiRect r, int w, int h, KuiRect *out, int max)
{
	int n = 0;
	if (bg == NT_BG_GRID) {
		for (int x = (r.x / NT_GRID_STEP) * NT_GRID_STEP; x < r.x + r.w && x < w; x += NT_GRID_STEP)
			if (x >= NT_GRID_STEP) n = clip_add((KuiRect){ x, 0, BG_LINE_W, h }, r, out, n, max);
	}
	if (bg == NT_BG_GRID || bg == NT_BG_LINES) {
		int step = bg == NT_BG_GRID ? NT_GRID_STEP : NT_LINE_STEP;
		for (int y = (r.y / step) * step; y < r.y + r.h && y < h; y += step)
			if (y >= step) n = clip_add((KuiRect){ 0, y, w, BG_LINE_W }, r, out, n, max);
	}
	return n;
}

/* ---- notes ---- */

int nt_note_init(NtNote *n, int kind, long now)
{
	memset(n, 0, sizeof *n);
	n->kind = kind;
	n->bg = kind == NT_KIND_DRAW ? NT_BG_GRID : NT_BG_PLAIN;
	n->created = n->modified = now;
	n->text = calloc(1, 1);
	if (!n->text) return -1;
	if (kind == NT_KIND_DRAW && nt_note_add_page(n, 0) < 0) return -1;
	return 0;
}

void nt_note_free(NtNote *n)
{
	for (int i = 0; i < n->npages; i++) nt_page_free(&n->pages[i]);
	free(n->pages);
	free(n->text);
	memset(n, 0, sizeof *n);
}

int nt_note_add_page(NtNote *n, int at)
{
	if (n->npages >= NT_MAX_PAGES) return -1;
	NtPage *p = realloc(n->pages, sizeof *p * (size_t) (n->npages + 1));
	if (!p) return -1;
	n->pages = p;
	if (at < 0 || at > n->npages) at = n->npages;
	memmove(p + at + 1, p + at, sizeof *p * (size_t) (n->npages - at));
	memset(&p[at], 0, sizeof *p);
	n->npages++;
	return at;
}

int nt_note_del_page(NtNote *n, int at)
{
	if (at < 0 || at >= n->npages) return -1;
	if (n->npages == 1) {
		nt_page_clear(&n->pages[0]);
		return 0;
	}
	nt_page_free(&n->pages[at]);
	memmove(n->pages + at, n->pages + at + 1, sizeof *n->pages * (size_t) (n->npages - at - 1));
	n->npages--;
	return 0;
}

int nt_note_strokes(const NtNote *n)
{
	int s = 0;
	for (int i = 0; i < n->npages; i++) s += n->pages[i].ns;
	return s;
}

/* ---- paragraphs ---- */

int nt_para_count(const char *text)
{
	if (!text || !*text) return 0;
	int n = 1;
	for (const char *p = text; *p; p++) n += *p == '\n';
	return n;
}

static const char *para_at(const char *text, int i, int *len)
{
	if (!text || i < 0 || i >= nt_para_count(text)) return NULL;
	const char *p = text;
	while (i-- > 0) p = strchr(p, '\n') + 1;
	const char *e = strchr(p, '\n');
	*len = e ? (int) (e - p) : (int) strlen(p);
	return p;
}

int nt_para_get(const char *text, int i, char *out, int cap)
{
	int len;
	const char *p = para_at(text, i, &len);
	if (!p || cap <= 0) return -1;
	if (len > cap - 1) {
		len = cap - 1;
		while (len > 0 && ((unsigned char) p[len] & 0xC0) == 0x80) len--; /* whole code points */
	}
	memcpy(out, p, (size_t) len);
	out[len] = '\0';
	return len;
}

int nt_para_set(char **text, int i, const char *s)
{
	const char *t = *text ? *text : "";
	int count = nt_para_count(t);
	if (i < 0 || i > count) return -1;
	int sl = (int) strlen(s), tl = (int) strlen(t);
	if (tl + sl + 2 > NT_TEXT_MAX) return -1;
	char *out = malloc((size_t) (tl + sl + 2));
	if (!out) return -1;
	int o = 0, first = 1;
	for (int k = 0; k <= count; k++) {
		const char *src;
		int len;
		if (k == i) {
			if (!sl) continue; /* "" deletes */
			src = s, len = sl;
		} else if (k < count) {
			src = para_at(t, k, &len);
		} else {
			continue;
		}
		if (!first) out[o++] = '\n';
		first = 0;
		for (int j = 0; j < len; j++) out[o++] = src[j] == '\n' ? ' ' : src[j];
	}
	out[o] = '\0';
	free(*text);
	*text = out;
	return 0;
}

/* ---- file format ----
 * "NTL1", u8 kind, u8 bg, u16 0, u32 created, u32 modified, u16 title length + bytes, u32 text length + bytes,
 * u16 pages; per page u32 strokes; per stroke u8 tool, varint points, then each point as zigzag-varint deltas
 * (dx, dy) from the previous one (the first from 0,0). A simplified handwritten stroke takes ~2-3 bytes a point. */

typedef struct { unsigned char *p; int len, cap, err; } Buf;

static void put(Buf *b, const void *d, int n)
{
	if (b->err) return;
	if (grow((void **) &b->p, &b->cap, b->len + n, 1) < 0) {
		b->err = 1;
		return;
	}
	memcpy(b->p + b->len, d, (size_t) n);
	b->len += n;
}

static void put_u8(Buf *b, unsigned v)
{
	unsigned char c = (unsigned char) v;
	put(b, &c, 1);
}

static void put_le(Buf *b, unsigned long v, int bytes)
{
	for (int i = 0; i < bytes; i++) put_u8(b, (unsigned) (v >> (8 * i)) & 0xFF);
}

static void put_varint(Buf *b, unsigned long v)
{
	while (v >= 0x80) {
		put_u8(b, (unsigned) (v & 0x7F) | 0x80);
		v >>= 7;
	}
	put_u8(b, (unsigned) v);
}

static unsigned long zz(long v) { return v < 0 ? ((unsigned long) (-v) << 1) - 1 : (unsigned long) v << 1; }
static long unzz(unsigned long u) { return u & 1 ? -(long) ((u + 1) >> 1) : (long) (u >> 1); }

int nt_note_serialize(const NtNote *n, unsigned char **out, int *len)
{
	Buf b = { 0 };
	put(&b, "NTL1", 4);
	put_u8(&b, (unsigned) n->kind);
	put_u8(&b, (unsigned) n->bg);
	put_le(&b, 0, 2);
	put_le(&b, (unsigned long) n->created, 4);
	put_le(&b, (unsigned long) n->modified, 4);
	int tl = (int) strnlen(n->title, NT_TITLE_MAX - 1);
	put_le(&b, (unsigned long) tl, 2);
	put(&b, n->title, tl);
	int xl = n->text ? (int) strlen(n->text) : 0;
	put_le(&b, (unsigned long) xl, 4);
	if (xl) put(&b, n->text, xl);
	put_le(&b, (unsigned long) n->npages, 2);
	for (int i = 0; i < n->npages; i++) {
		const NtPage *pg = &n->pages[i];
		put_le(&b, (unsigned long) pg->ns, 4);
		for (int k = 0; k < pg->ns; k++) {
			const NtStroke *s = &pg->s[k];
			put_u8(&b, s->tool);
			put_varint(&b, (unsigned long) s->n);
			int px = 0, py = 0;
			for (int j = 0; j < s->n; j++) {
				NtPt p = pg->p[s->off + j];
				put_varint(&b, zz(p.x - px));
				put_varint(&b, zz(p.y - py));
				px = p.x, py = p.y;
			}
		}
	}
	if (b.err) {
		free(b.p);
		return -1;
	}
	*out = b.p;
	*len = b.len;
	return 0;
}

typedef struct { const unsigned char *p; int len, pos, err; } Rd;

static unsigned long get_le(Rd *r, int bytes)
{
	if (r->pos + bytes > r->len) {
		r->err = 1;
		return 0;
	}
	unsigned long v = 0;
	for (int i = 0; i < bytes; i++) v |= (unsigned long) r->p[r->pos++] << (8 * i);
	return v;
}

static unsigned long get_varint(Rd *r)
{
	unsigned long v = 0;
	for (int s = 0; s < 35; s += 7) {
		if (r->pos >= r->len) break;
		unsigned c = r->p[r->pos++];
		v |= (unsigned long) (c & 0x7F) << s;
		if (!(c & 0x80)) return v;
	}
	r->err = 1;
	return 0;
}

int nt_note_parse(const unsigned char *buf, int len, NtNote *n)
{
	memset(n, 0, sizeof *n);
	if (len < 4 || memcmp(buf, "NTL1", 4) != 0) return -1;
	Rd r = { buf, len, 4, 0 };
	n->kind = (int) get_le(&r, 1);
	n->bg = (int) get_le(&r, 1);
	get_le(&r, 2);
	n->created = (long) get_le(&r, 4);
	n->modified = (long) get_le(&r, 4);
	int tl = (int) get_le(&r, 2);
	if (r.err || tl >= NT_TITLE_MAX || r.pos + tl > len || (n->kind != NT_KIND_DRAW && n->kind != NT_KIND_TEXT) || n->bg < 0 || n->bg >= NT_BGS) return -1;
	memcpy(n->title, buf + r.pos, (size_t) tl);
	n->title[tl] = '\0';
	r.pos += tl;
	unsigned long xl = get_le(&r, 4);
	if (r.err || xl > NT_TEXT_MAX || r.pos + (long) xl > len) return -1;
	n->text = malloc(xl + 1);
	if (!n->text) return -1;
	memcpy(n->text, buf + r.pos, xl);
	n->text[xl] = '\0';
	r.pos += (int) xl;
	int np = (int) get_le(&r, 2);
	if (r.err || np > NT_MAX_PAGES) goto bad;
	for (int i = 0; i < np; i++) {
		if (nt_note_add_page(n, i) < 0) goto bad;
		NtPage *pg = &n->pages[i];
		unsigned long ns = get_le(&r, 4);
		if (r.err || ns > NT_MAX_STROKES) goto bad;
		for (unsigned long k = 0; k < ns; k++) {
			int tool = (int) get_le(&r, 1);
			unsigned long cnt = get_varint(&r);
			if (r.err || tool >= NT_TOOLS || cnt < 1 || cnt > (unsigned long) (NT_MAX_POINTS - pg->np)) goto bad;
			long x = 0, y = 0;
			for (unsigned long j = 0; j < cnt; j++) {
				x += unzz(get_varint(&r));
				y += unzz(get_varint(&r));
				if (r.err || x < -32768 || x > 32767 || y < -32768 || y > 32767) goto bad;
				if (j == 0 ? nt_page_begin(pg, tool, (int) x, (int) y) < 0 : nt_page_add(pg, (int) x, (int) y) < 0) goto bad;
			}
		}
	}
	if (n->kind == NT_KIND_DRAW && n->npages == 0 && nt_note_add_page(n, 0) < 0) goto bad;
	return 0;
bad:
	nt_note_free(n);
	return -1;
}

int nt_note_save(const char *path, const NtNote *n)
{
	unsigned char *b;
	int len;
	if (nt_note_serialize(n, &b, &len) < 0) return -1;
	int rc = kui_write_atomic(path, b, len);
	free(b);
	return rc;
}

int nt_note_load(const char *path, NtNote *n)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > 64L * 1024 * 1024) {
		fclose(f);
		return -1;
	}
	unsigned char *b = malloc((size_t) sz);
	int ok = b && fread(b, 1, (size_t) sz, f) == (size_t) sz;
	fclose(f);
	int rc = ok ? nt_note_parse(b, (int) sz, n) : -1;
	free(b);
	return rc;
}

void nt_note_label(const NtNote *n, char *out, int cap)
{
	if (cap <= 0) return;
	out[0] = '\0';
	const char *s = n->title;
	while (*s == ' ') s++;
	if (!*s && n->text) {
		int len;
		for (int i = 0; i < nt_para_count(n->text); i++) { /* the first non-blank paragraph */
			const char *p = para_at(n->text, i, &len);
			int k = 0;
			while (k < len && p[k] == ' ') k++;
			if (k < len) {
				nt_para_get(n->text, i, out, cap);
				return;
			}
		}
		return;
	}
	int len = (int) strlen(s);
	if (len > cap - 1) {
		len = cap - 1;
		while (len > 0 && ((unsigned char) s[len] & 0xC0) == 0x80) len--;
	}
	memcpy(out, s, (size_t) len);
	out[len] = '\0';
}

/* ---- thumbnails ---- */

void nt_thumb(const NtMask *m, unsigned char *out, int tw, int th)
{
	for (int ty = 0; ty < th; ty++) {
		int y0 = ty * m->h / th, y1 = (ty + 1) * m->h / th;
		for (int tx = 0; tx < tw; tx++) {
			int x0 = tx * m->w / tw, x1 = (tx + 1) * m->w / tw, ink = 0, area = (x1 - x0) * (y1 - y0);
			for (int y = y0; y < y1; y++) {
				const unsigned char *row = m->px + (size_t) y * (size_t) m->w;
				for (int x = x0; x < x1; x++) ink += row[x];
			}
			int g = area ? 255 - ink * 255 * 3 / area : 255;
			out[ty * tw + tx] = (unsigned char) (g < 0 ? 0 : g);
		}
	}
}

/* ---- PNG ---- */

unsigned long nt_crc32(const unsigned char *p, int n)
{
	static unsigned long table[256];
	if (!table[1]) {
		for (unsigned long i = 0; i < 256; i++) {
			unsigned long c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	}
	unsigned long c = 0xFFFFFFFFUL;
	for (int i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFUL;
}

typedef struct { Buf b; unsigned long bits; int nbits; } Bits;

static void bits_put(Bits *w, unsigned v, int n) /* LSB first */
{
	w->bits |= (unsigned long) v << w->nbits;
	w->nbits += n;
	while (w->nbits >= 8) {
		put_u8(&w->b, (unsigned) (w->bits & 0xFF));
		w->bits >>= 8;
		w->nbits -= 8;
	}
}

static void huff_put(Bits *w, unsigned code, int n) /* Huffman codes go MSB first */
{
	unsigned r = 0;
	for (int i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i);
	bits_put(w, r, n);
}

static void lit_put(Bits *w, unsigned v)
{
	if (v < 144) huff_put(w, 0x30 + v, 8);
	else if (v < 256) huff_put(w, 0x190 + (v - 144), 9);
	else if (v < 280) huff_put(w, v - 256, 7);
	else huff_put(w, 0xC0 + (v - 280), 8);
}

static void match_put(Bits *w, int len, int dist)
{
	static const int dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
				       4097, 6145, 8193, 12289, 16385, 24577 };
	static const int base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const int extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	int i = 28;
	while (base[i] > len) i--;
	lit_put(w, 257 + (unsigned) i);
	if (extra[i]) bits_put(w, (unsigned) (len - base[i]), extra[i]);
	int d = 29;
	while (dbase[d] > dist) d--;
	huff_put(w, (unsigned) d, 5);
	int dx = d < 4 ? 0 : d / 2 - 1;
	if (dx) bits_put(w, (unsigned) (dist - dbase[d]), dx);
}

/* a copy of `run` bytes from `dist` back, in matches of 3..258 bytes; returns the bytes left (0..2) */
static int run_put(Bits *w, int run, int dist)
{
	while (run >= 3) {
		int l = run > 258 ? 258 : run;
		if (run - l > 0 && run - l < 3) l = run - 3; /* leave a tail a match can still take */
		match_put(w, l, dist);
		run -= l;
	}
	return run;
}

static void chunk(Buf *o, const char *type, const unsigned char *d, int n)
{
	unsigned char h[4] = { (unsigned char) (n >> 24), (unsigned char) (n >> 16), (unsigned char) (n >> 8), (unsigned char) n };
	put(o, h, 4);
	int start = o->len;
	put(o, type, 4);
	if (n) put(o, d, n);
	unsigned long c = o->err ? 0 : nt_crc32(o->p + start, n + 4);
	unsigned char cb[4] = { (unsigned char) (c >> 24), (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c };
	put(o, cb, 4);
}

int nt_png(const NtMask *m, int bg, unsigned char **out, int *len)
{
	Bits w = { { 0 }, 0, 0 };
	unsigned long a1 = 1, a2 = 0; /* Adler-32 of the raw scanlines */
	unsigned char *row = malloc((size_t) m->w + 1), *prev = calloc((size_t) m->w + 1, 1);
	if (!row || !prev) {
		free(row);
		free(prev);
		return -1;
	}
	put_u8(&w.b, 0x78);
	put_u8(&w.b, 0x01);
	bits_put(&w, 1, 1); /* BFINAL */
	bits_put(&w, 1, 2); /* fixed Huffman */
	for (int y = 0; y < m->h; y++) {
		row[0] = 0; /* filter: none */
		const unsigned char *mk = m->px + (size_t) y * (size_t) m->w;
		for (int x = 0; x < m->w; x++) row[x + 1] = mk[x] ? 0 : nt_bg_pixel(bg, x, y);
		int n = m->w + 1;
		for (int i = 0; i < n; i++) {
			a1 = (a1 + row[i]) % 65521;
			a2 = (a2 + a1) % 65521;
		}
		/* greedy: the same bytes as the row above (distance one row), or a literal and its run (distance 1) */
		for (int i = 0; i < n;) {
			int up = 0;
			if (y > 0)
				while (i + up < n && row[i + up] == prev[i + up]) up++;
			int run = 0;
			while (i + 1 + run < n && row[i + 1 + run] == row[i]) run++;
			if (up >= 3 && up > run) {
				i += up - run_put(&w, up, n);
				continue;
			}
			lit_put(&w, row[i]);
			i++;
			i += run - run_put(&w, run, 1);
		}
		memcpy(prev, row, (size_t) n);
	}
	free(row);
	free(prev);
	lit_put(&w, 256);
	if (w.nbits) bits_put(&w, 0, 8 - w.nbits);
	unsigned long ad = (a2 << 16) | a1;
	unsigned char ab[4] = { (unsigned char) (ad >> 24), (unsigned char) (ad >> 16), (unsigned char) (ad >> 8), (unsigned char) ad };
	put(&w.b, ab, 4);
	if (w.b.err) {
		free(w.b.p);
		return -1;
	}
	Buf o = { 0 };
	put(&o, "\x89PNG\r\n\x1a\n", 8);
	unsigned char ihdr[13] = { (unsigned char) (m->w >> 24), (unsigned char) (m->w >> 16), (unsigned char) (m->w >> 8), (unsigned char) m->w,
				   (unsigned char) (m->h >> 24), (unsigned char) (m->h >> 16), (unsigned char) (m->h >> 8), (unsigned char) m->h,
				   8, 0, 0, 0, 0 };
	chunk(&o, "IHDR", ihdr, 13);
	chunk(&o, "IDAT", w.b.p, w.b.len);
	chunk(&o, "IEND", NULL, 0);
	free(w.b.p);
	if (o.err) {
		free(o.p);
		return -1;
	}
	*out = o.p;
	*len = o.len;
	return 0;
}

#include <stdlib.h>
#include <string.h>
#include "notlar/notlar.h"
#include "t.h"

#define CW 400
#define CH 300
static unsigned char px[CW * CH], px2[CW * CH];

static NtMask mask(unsigned char *p) { return (NtMask){ p, CW, CH }; }

static void test_page_is_an_undo_stack(void)
{
	NtPage pg = { 0 };
	T_EQ_INT(nt_page_begin(&pg, NT_PEN_MED, 10, 10), 0);
	T_EQ_INT(nt_page_add(&pg, 20, 10), 0);
	T_EQ_INT(nt_page_begin(&pg, NT_ERASER, 15, 10), 1);
	T_EQ_INT(nt_page_add(&pg, 15, 20), 0);
	T_EQ_INT(pg.ns, 2);
	T_EQ_INT(pg.np, 4);
	T_EQ_INT(pg.s[1].tool, NT_ERASER);
	T_EQ_INT(pg.s[1].off, 2);
	T_EQ_INT(nt_page_undo(&pg), 1);
	T_EQ_INT(pg.ns, 1);
	T_EQ_INT(pg.np, 2);
	T_EQ_INT(nt_page_undo(&pg), 1);
	T_EQ_INT(nt_page_undo(&pg), 0);
	T_EQ_INT(nt_page_add(&pg, 1, 1), -1); /* no stroke to add to */
	nt_page_free(&pg);
}

static void test_smoothing(void)
{
	NtSmooth s;
	int x, y;
	nt_smooth_start(&s, 100, 100);
	T_EQ_INT(nt_smooth_feed(&s, 101, 100, &x, &y), 0); /* jitter below 2 px */
	T_EQ_INT(nt_smooth_feed(&s, 110, 100, &x, &y), 1);
	T_EQ_INT(x, 105); /* halfway: the average lags behind the finger */
	T_EQ_INT(y, 100);
	T_EQ_INT(nt_smooth_feed(&s, 120, 100, &x, &y), 1);
	T_ASSERT(x > 105 && x < 120);
	T_EQ_INT(nt_smooth_end(&s, &x, &y), 1); /* the stroke ends where the finger lifted */
	T_EQ_INT(x, 120);
	T_EQ_INT(nt_smooth_end(&s, &x, &y), 0);
}

static void test_simplify(void)
{
	NtPt line[50];
	for (int i = 0; i < 50; i++) line[i] = (NtPt){ (short) (i * 2), (short) (i % 2) }; /* 1 px zigzag */
	T_EQ_INT(nt_simplify(line, 50, 1.0), 2);
	T_EQ_INT(line[1].x, 98);
	NtPt el[21];
	for (int i = 0; i <= 10; i++) el[i] = (NtPt){ (short) (i * 10), 0 };
	for (int i = 1; i <= 10; i++) el[10 + i] = (NtPt){ 100, (short) (i * 10) };
	int n = nt_simplify(el, 21, 1.0);
	T_EQ_INT(n, 3); /* the corner stays */
	T_EQ_INT(el[1].x, 100);
	T_EQ_INT(el[1].y, 0);
	NtPt dot[2] = { { 5, 5 }, { 5, 5 } };
	T_EQ_INT(nt_simplify(dot, 2, 1.0), 1);
}

static void test_segment_raster_and_bbox(void)
{
	NtMask m = mask(px);
	memset(px, 0, sizeof px);
	KuiRect b = nt_seg(&m, 50, 100, 150, 100, NT_PEN_MED); /* radius 2 */
	T_EQ_INT(b.x, 48);
	T_EQ_INT(b.y, 98);
	T_EQ_INT(b.w, 105);
	T_EQ_INT(b.h, 5);
	for (int x = 50; x <= 150; x++) T_EQ_INT(px[100 * CW + x], 1);
	T_EQ_INT(px[100 * CW + 47], 0);
	T_EQ_INT(px[103 * CW + 100], 0);
	int outside = 0; /* every ink pixel lies inside the reported box */
	for (int y = 0; y < CH; y++)
		for (int x = 0; x < CW; x++)
			if (px[y * CW + x] && !kui_rect_contains(b, x, y)) outside++;
	T_EQ_INT(outside, 0);
	b = nt_seg(&m, 100, 80, 100, 120, NT_ERASER);
	T_EQ_INT(px[100 * CW + 100], 0);
	T_EQ_INT(px[100 * CW + 150], 1);
	b = nt_seg(&m, -10, -10, 5, 5, NT_PEN_THICK); /* clipped at the edge */
	T_EQ_INT(b.x, 0);
	T_EQ_INT(b.y, 0);
	T_EQ_INT(px[0], 1);
	b = nt_seg_bbox(-100, -100, -50, -50, 4, CW, CH);
	T_EQ_INT(b.w, 0);
}

static void test_undo_replays(void)
{
	NtPage pg = { 0 };
	NtMask m = mask(px), m2 = mask(px2);
	nt_page_begin(&pg, NT_PEN_THIN, 10, 10);
	nt_page_add(&pg, 200, 150);
	nt_page_end(&pg, 1.0);
	nt_page_render(&m2, &pg); /* the page after stroke 1 */
	nt_page_begin(&pg, NT_PEN_THICK, 10, 150);
	nt_page_add(&pg, 300, 20);
	nt_page_begin(&pg, NT_ERASER, 100, 60);
	nt_page_add(&pg, 120, 90);
	nt_page_render(&m, &pg);
	T_ASSERT(memcmp(px, px2, sizeof px) != 0);
	nt_page_undo(&pg);
	nt_page_undo(&pg);
	nt_page_render(&m, &pg);
	T_ASSERT(memcmp(px, px2, sizeof px) == 0);
	/* incremental drawing equals the replay */
	memset(px, 0, sizeof px);
	nt_page_clear(&pg);
	nt_page_begin(&pg, NT_PEN_MED, 30, 30);
	nt_stroke_draw(&m, &pg, 0, 0);
	int pts[][2] = { { 60, 40 }, { 90, 80 }, { 150, 85 } };
	for (int i = 0; i < 3; i++) {
		nt_page_add(&pg, pts[i][0], pts[i][1]);
		nt_stroke_draw(&m, &pg, 0, pg.s[0].n - 1);
	}
	nt_page_render(&m2, &pg);
	T_ASSERT(memcmp(px, px2, sizeof px) == 0);
	nt_page_free(&pg);
}

static void test_backgrounds(void)
{
	for (int bg = 0; bg < NT_BGS; bg++) {
		KuiRect r = { 30, 40, 200, 150 }, rs[64];
		int n = nt_bg_rects(bg, r, CW, CH, rs, 64), bad = 0, gray = 0;
		for (int y = r.y; y < r.y + r.h; y++)
			for (int x = r.x; x < r.x + r.w; x++) {
				int in = 0;
				for (int i = 0; i < n; i++) in |= kui_rect_contains(rs[i], x, y);
				int g = nt_bg_pixel(bg, x, y) != 255;
				gray += g;
				bad += in != g;
			}
		T_EQ_INT(bad, 0);
		T_EQ_INT(gray > 0, bg != NT_BG_PLAIN);
		for (int i = 0; i < n; i++) T_ASSERT(rs[i].x >= r.x && rs[i].x + rs[i].w <= r.x + r.w && rs[i].y >= r.y && rs[i].y + rs[i].h <= r.y + r.h);
	}
}

static void test_paragraphs(void)
{
	char *t = NULL, buf[64];
	T_EQ_INT(nt_para_count(t), 0);
	T_EQ_INT(nt_para_set(&t, 0, "Alışveriş"), 0);
	T_EQ_INT(nt_para_set(&t, 1, "süt\nekmek"), 0); /* newlines inside a paragraph become spaces */
	T_EQ_STR(t, "Alışveriş\nsüt ekmek");
	T_EQ_INT(nt_para_count(t), 2);
	T_EQ_INT(nt_para_get(t, 1, buf, sizeof buf), 10); /* ü is two bytes */
	T_EQ_STR(buf, "süt ekmek");
	T_EQ_INT(nt_para_get(t, 2, buf, sizeof buf), -1);
	T_EQ_INT(nt_para_get(t, 0, buf, 4), 2); /* cut between code points: "Al" */
	T_EQ_STR(buf, "Al");
	T_EQ_INT(nt_para_set(&t, 0, "Liste"), 0);
	T_EQ_STR(t, "Liste\nsüt ekmek");
	T_EQ_INT(nt_para_set(&t, 0, ""), 0);
	T_EQ_STR(t, "süt ekmek");
	T_EQ_INT(nt_para_set(&t, 5, "x"), -1);
	free(t);
}

static void test_serialize_roundtrip(void)
{
	NtNote n, m;
	T_EQ_INT(nt_note_init(&n, NT_KIND_DRAW, 1760000000L), 0);
	n.bg = NT_BG_LINES;
	snprintf(n.title, sizeof n.title, "Ev çizimi");
	nt_para_set(&n.text, 0, "not");
	nt_page_begin(&n.pages[0], NT_PEN_THICK, 10, 1000);
	nt_page_add(&n.pages[0], 1071, 3);
	nt_page_add(&n.pages[0], 0, 1197);
	T_EQ_INT(nt_note_add_page(&n, 1), 1);
	nt_page_begin(&n.pages[1], NT_ERASER, 5, 6);
	T_EQ_INT(nt_note_add_page(&n, 2), 2); /* an empty page survives too */
	unsigned char *b;
	int len;
	T_EQ_INT(nt_note_serialize(&n, &b, &len), 0);
	T_EQ_INT(nt_note_parse(b, len, &m), 0);
	T_EQ_INT(m.kind, NT_KIND_DRAW);
	T_EQ_INT(m.bg, NT_BG_LINES);
	T_EQ_INT(m.created, 1760000000L);
	T_EQ_STR(m.title, "Ev çizimi");
	T_EQ_STR(m.text, "not");
	T_EQ_INT(m.npages, 3);
	T_EQ_INT(m.pages[0].ns, 1);
	T_EQ_INT(m.pages[0].s[0].tool, NT_PEN_THICK);
	T_EQ_INT(m.pages[0].s[0].n, 3);
	T_EQ_INT(m.pages[0].p[1].x, 1071);
	T_EQ_INT(m.pages[0].p[2].y, 1197);
	T_EQ_INT(m.pages[1].s[0].tool, NT_ERASER);
	T_EQ_INT(m.pages[2].ns, 0);
	T_EQ_INT(nt_note_strokes(&m), 2);
	nt_note_free(&m);
	for (int cut = 0; cut < len; cut++) { /* every truncation is rejected, none crashes */
		T_EQ_INT(nt_note_parse(b, cut, &m), -1);
	}
	b[4] = 9; /* unknown kind */
	T_EQ_INT(nt_note_parse(b, len, &m), -1);
	free(b);
	nt_note_free(&n);
}

static void test_compact_format(void)
{
	NtNote n;
	nt_note_init(&n, NT_KIND_DRAW, 0);
	nt_page_begin(&n.pages[0], NT_PEN_THIN, 500, 600);
	for (int i = 1; i < 200; i++) nt_page_add(&n.pages[0], 500 + i * 3, 600 + (i % 7));
	unsigned char *b;
	int len;
	nt_note_serialize(&n, &b, &len);
	T_ASSERT(len < 40 + 200 * 2 + 8); /* small deltas: one byte per coordinate */
	free(b);
	nt_note_free(&n);
}

static void test_label_and_pages(void)
{
	NtNote n;
	char s[16];
	nt_note_init(&n, NT_KIND_TEXT, 0);
	T_EQ_INT(n.npages, 0);
	nt_note_label(&n, s, sizeof s);
	T_EQ_STR(s, "");
	nt_para_set(&n.text, 0, "  ");
	nt_para_set(&n.text, 1, "Yapılacaklar listesi uzun");
	nt_note_label(&n, s, sizeof s);
	T_EQ_STR(s, "Yapılacaklar l");
	snprintf(n.title, sizeof n.title, "Başlık");
	nt_note_label(&n, s, sizeof s);
	T_EQ_STR(s, "Başlık");
	nt_note_free(&n);
	nt_note_init(&n, NT_KIND_DRAW, 0);
	nt_page_begin(&n.pages[0], 0, 1, 1);
	T_EQ_INT(nt_note_del_page(&n, 0), 0); /* the only page is emptied, not removed */
	T_EQ_INT(n.npages, 1);
	T_EQ_INT(n.pages[0].ns, 0);
	nt_note_add_page(&n, 1);
	nt_page_begin(&n.pages[1], 0, 2, 2);
	nt_note_add_page(&n, 0);
	T_EQ_INT(n.pages[2].ns, 1);
	T_EQ_INT(nt_note_del_page(&n, 0), 0);
	T_EQ_INT(n.npages, 2);
	T_EQ_INT(n.pages[1].ns, 1);
	nt_note_free(&n);
}

static void test_thumb(void)
{
	NtMask m = mask(px);
	memset(px, 0, sizeof px);
	nt_seg(&m, 0, 150, 399, 150, NT_PEN_THIN);
	unsigned char th[40 * 30];
	nt_thumb(&m, th, 40, 30);
	T_EQ_INT(th[0], 255);
	T_ASSERT(th[15 * 40 + 20] < 128); /* a 3 px line still shows at 1/10 */
	T_EQ_INT(th[5 * 40 + 20], 255);
}

/* ---- a minimal inflater (fixed Huffman only) to check nt_png's output ---- */
typedef struct { const unsigned char *p; int len, pos, bit; } Bin;
static int rbit(Bin *b)
{
	if (b->pos >= b->len) return -1;
	int v = (b->p[b->pos] >> b->bit) & 1;
	if (++b->bit == 8) b->bit = 0, b->pos++;
	return v;
}
static int rbits(Bin *b, int n)
{
	int v = 0;
	for (int i = 0; i < n; i++) v |= rbit(b) << i;
	return v;
}
static int rsym(Bin *b)
{
	int code = 0;
	for (int len = 1; len <= 9; len++) {
		code = (code << 1) | rbit(b);
		if (len == 7 && code <= 23) return 256 + code;
		if (len == 8 && code >= 0x30 && code <= 0xBF) return code - 0x30;
		if (len == 8 && code >= 0xC0 && code <= 0xC7) return 280 + code - 0xC0;
		if (len == 9 && code >= 0x190) return 144 + code - 0x190;
	}
	return -1;
}
static int inflate_fixed(const unsigned char *z, int zl, unsigned char *out, int cap)
{
	static const int base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const int extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	Bin b = { z + 2, zl - 6, 0, 0 };
	if (rbits(&b, 1) != 1 || rbits(&b, 2) != 1) return -1;
	int o = 0;
	for (;;) {
		int s = rsym(&b);
		if (s < 0) return -1;
		if (s < 256) {
			if (o >= cap) return -1;
			out[o++] = (unsigned char) s;
		} else if (s == 256) {
			return o;
		} else {
			int i = s - 257, l = base[i] + rbits(&b, extra[i]);
			static const int dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
			int dc = 0;
			for (int k = 0; k < 5; k++) dc = (dc << 1) | rbit(&b);
			if (dc > 29) return -1;
			int dist = dbase[dc] + (dc < 4 ? 0 : rbits(&b, dc / 2 - 1));
			if (dist > o || o + l > cap) return -1;
			for (int k = 0; k < l; k++, o++) out[o] = out[o - dist];
		}
	}
}
static unsigned long be32(const unsigned char *p) { return (unsigned long) p[0] << 24 | (unsigned long) p[1] << 16 | (unsigned long) p[2] << 8 | p[3]; }

static void test_png(void)
{
	NtMask m = mask(px);
	memset(px, 0, sizeof px);
	nt_seg(&m, 20, 20, 380, 280, NT_PEN_THICK);
	nt_seg(&m, 20, 280, 380, 20, NT_PEN_THIN);
	unsigned char *png;
	int len;
	T_EQ_INT(nt_png(&m, NT_BG_GRID, &png, &len), 0);
	T_ASSERT(memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0);
	T_EQ_INT(be32(png + 16), CW);
	T_EQ_INT(be32(png + 20), CH);
	T_EQ_INT(png[24], 8); /* 8-bit */
	T_EQ_INT(png[25], 0); /* gray */
	/* chunk CRCs */
	int pos = 8, chunks = 0, idat = 0, idat_len = 0;
	while (pos + 12 <= len) {
		int n = (int) be32(png + pos);
		T_EQ_INT(be32(png + pos + 8 + n), nt_crc32(png + pos + 4, n + 4));
		if (!memcmp(png + pos + 4, "IDAT", 4)) idat = pos + 8, idat_len = n;
		pos += 12 + n;
		chunks++;
	}
	T_EQ_INT(pos, len);
	T_EQ_INT(chunks, 3);
	T_ASSERT(idat_len < (CW + 1) * CH / 10); /* mostly runs */
	static unsigned char raw[(CW + 1) * CH + 16];
	int rl = inflate_fixed(png + idat, idat_len, raw, (int) sizeof raw);
	T_EQ_INT(rl, (CW + 1) * CH);
	unsigned long a1 = 1, a2 = 0;
	for (int i = 0; i < rl; i++) a1 = (a1 + raw[i]) % 65521, a2 = (a2 + a1) % 65521;
	T_EQ_INT(be32(png + idat + idat_len - 4), (a2 << 16) | a1);
	int bad = 0;
	for (int y = 0; y < CH; y++) {
		bad += raw[y * (CW + 1)] != 0;
		for (int x = 0; x < CW; x++) {
			unsigned char want = px[y * CW + x] ? 0 : nt_bg_pixel(NT_BG_GRID, x, y);
			bad += raw[y * (CW + 1) + 1 + x] != want;
		}
	}
	T_EQ_INT(bad, 0);
	free(png);
	T_EQ_INT(nt_crc32((const unsigned char *) "IEND", 4), 0xAE426082UL);
}

int main(void)
{
	T_RUN(test_page_is_an_undo_stack);
	T_RUN(test_smoothing);
	T_RUN(test_simplify);
	T_RUN(test_segment_raster_and_bbox);
	T_RUN(test_undo_replays);
	T_RUN(test_backgrounds);
	T_RUN(test_paragraphs);
	T_RUN(test_serialize_roundtrip);
	T_RUN(test_compact_format);
	T_RUN(test_label_and_pages);
	T_RUN(test_thumb);
	T_RUN(test_png);
	return T_DONE();
}

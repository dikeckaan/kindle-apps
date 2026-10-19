/* Host simulator for kui apps: replaces screen.c + input.c. Draws into an 8-bit 1072x1448 buffer with
 * stb_truetype (same DejaVu fonts as the device) and writes a PNG at every kui_flush to $KUI_SIM_OUT, named after
 * the refresh the device would do (same policy, core/kui/refresh.c, same settings keys):
 *   <app>_NNN.png             partial update (dirty rects, no flash)
 *   <app>_NNN_full.png        quiet full-screen GC16 update (no flash)
 *   <app>_NNN_full_flash.png  full-screen flash (after a wake, KUI_FLUSH_FLASH, or the ghosting counters)
 * Input comes from $KUI_SIM, a ';'-separated script:
 *   tap X Y | long X Y | swipe left|right|up|down | wake | none | asleep 0|1
 *   draw X1 Y1 X2 Y2 … — a finger dragged along the polyline (samples every 6 px, 8 ms apart): with
 *   kui_set_drag_events(1) on, KUI_EV_DOWN / MOVE… / UP and then the gesture; otherwise only the gesture.
 *   In drag mode tap and long also arrive as DOWN, UP, then TAP / LONGPRESS (as on the device).
 * kui_flush_fast writes a "<app>_NNN_ink.png" screenshot.
 * When the script is exhausted the app receives KUI_EV_QUIT.
 * Glyphs missing from the font are drawn as a crossed box so they stand out in screenshots. */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "kui.h"

#define W 1072
#define H 1448
static unsigned char fb[W * H];
static stbtt_fontinfo fonts[4];
static unsigned char *font_data[4];
static int shot, asleep;
static const char *script;
static KuiDirty dirty;
static char app_name[64];
static KuiRefreshPolicy policy;
static int drag_mode;
#define QN 4096
static KuiEvent queue[QN];
static int q_head, q_len;
static long sim_t = 100000; /* synthetic clock for drag samples (ms) */

void kui_set_drag_events(int on)
{
	drag_mode = on != 0;
	q_head = q_len = 0;
}

static void q_push(const KuiEvent *e, int n)
{
	for (int i = 0; i < n && q_len < QN; i++) queue[(q_head + q_len++) % QN] = e[i];
}

/* a touch along pts (n points): queued as the device would report it; returns the gesture event */
static KuiEvent sim_touch(const int *pts, int n, long hold_ms)
{
	KuiDrag d = { 0 };
	KuiEvent evs[KUI_DRAG_MAX_OUT], gest = { KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
	int k = kui_drag_feed(&d, 0, pts[0], pts[1], sim_t, evs), base = q_len;
	q_push(evs, k);
	for (int i = 1; i < n; i++) {
		int x0 = pts[2 * i - 2], y0 = pts[2 * i - 1], x1 = pts[2 * i], y1 = pts[2 * i + 1];
		int dx = x1 - x0, dy = y1 - y0, len = abs(dx) > abs(dy) ? abs(dx) : abs(dy), steps = len / 6 + 1;
		for (int s = 1; s <= steps; s++) {
			sim_t += 8;
			k = kui_drag_feed(&d, 0, x0 + dx * s / steps, y0 + dy * s / steps, sim_t, evs);
			q_push(evs, k);
		}
	}
	sim_t += hold_ms + 8;
	k = kui_drag_feed(&d, -1, 0, 0, sim_t, evs);
	for (int i = 0; i < k; i++)
		if (evs[i].type != KUI_EV_UP) gest = evs[i];
	if (drag_mode) q_push(evs, k);
	else q_len = base; /* without drag events only the gesture is delivered */
	sim_t += 50;
	return gest;
}

static unsigned char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc((size_t) n);
	if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

int kui_init(const char *app)
{
	kui_i18n_load_saved(); /* language chosen in the Uygulamalar menu */
	char path[400];
	const char *root = getenv("KUI_ROOT");
	if (!root || !*root) root = "build/simroot";
	snprintf(app_name, sizeof app_name, "%s", app);
	const char *names[4] = { "DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSerif.ttf", "DejaVuSerif-Bold.ttf" };
	for (int i = 0; i < 4; i++) {
		if (font_data[i]) continue;
		snprintf(path, sizeof path, "%s/fonts/%s", root, names[i]);
		font_data[i] = slurp(path);
		if (!font_data[i] || !stbtt_InitFont(&fonts[i], font_data[i], 0)) {
			fprintf(stderr, "sim: cannot load %s\n", path);
			return -1;
		}
	}
	if (!script) script = getenv("KUI_SIM");
	if (!script) script = "";
	memset(fb, 255, sizeof fb);
	snprintf(path, sizeof path, "%s/settings", kui_data_dir(app));
	kui_policy_init(&policy, kui_cfg_get_int(path, "full_refresh_every", 25),
			kui_cfg_get_int(path, "flash_every", KUI_FLASH_EVERY_DEFAULT));
	return 0;
}

void kui_quit(void) {}
int kui_width(void) { return W; }
int kui_height(void) { return H; }
void kui_set_refresh_every(int n) { kui_policy_set_every(&policy, n); }
void kui_request_flash(void) { kui_policy_request_flash(&policy); }
int kui_input_asleep(void) { return asleep; }

void kui_fill(KuiRect r, unsigned char g)
{
	for (int y = r.y < 0 ? 0 : r.y; y < r.y + r.h && y < H; y++)
		for (int x = r.x < 0 ? 0 : r.x; x < r.x + r.w && x < W; x++) fb[y * W + x] = g;
}

void kui_frame(KuiRect r, int t, unsigned char g)
{
	kui_fill((KuiRect){ r.x, r.y, r.w, t }, g);
	kui_fill((KuiRect){ r.x, r.y + r.h - t, r.w, t }, g);
	kui_fill((KuiRect){ r.x, r.y, t, r.h }, g);
	kui_fill((KuiRect){ r.x + r.w - t, r.y, t, r.h }, g);
}

static float scale_for(KuiFont f, int px) { return stbtt_ScaleForPixelHeight(&fonts[f], (float) px); }

static int line_h(KuiFont f, int px)
{
	int a, d, g;
	stbtt_GetFontVMetrics(&fonts[f], &a, &d, &g);
	return (int) ((a - d) * scale_for(f, px) + 0.5f);
}

static int glyph_adv(KuiFont f, int px, int cp)
{
	int adv, lsb;
	if (!stbtt_FindGlyphIndex(&fonts[f], cp)) return px / 2;
	stbtt_GetCodepointHMetrics(&fonts[f], cp, &adv, &lsb);
	return (int) (adv * scale_for(f, px) + 0.5f);
}

static int span_width(const char *s, int n, KuiFont f, int px)
{
	int w = 0;
	for (int i = 0; i < n;) {
		int cp;
		i += kui_utf8_decode(s + i, &cp);
		w += glyph_adv(f, px, cp);
	}
	return w;
}

static int measure_cb(const char *s, int n, void *ctx)
{
	const int *fp = ctx;
	return span_width(s, n, (KuiFont) fp[0], fp[1]);
}

int kui_text_width(const char *utf8, KuiFont f, int px)
{
	/* widest line, like FBInk's bbox */
	int best = 0;
	for (const char *p = utf8;;) {
		const char *nl = strchr(p, '\n');
		int n = nl ? (int) (nl - p) : (int) strlen(p);
		int w = span_width(p, n, f, px);
		if (w > best) best = w;
		if (!nl) break;
		p = nl + 1;
	}
	return best;
}

int kui_wrap_text(const char *text, int width, KuiFont f, int px, KuiLine *out, int max)
{
	int fp[2] = { (int) f, px };
	return kui_wrap(text, width, measure_cb, fp, out, max);
}

int kui_line_height(KuiFont f, int px)
{
	(void) f;
	return px * 13 / 10;
}

static void draw_glyph(KuiFont f, int px, int cp, int x, int baseline, unsigned char fg)
{
	float sc = scale_for(f, px);
	if (!stbtt_FindGlyphIndex(&fonts[f], cp)) { /* tofu: crossed box */
		int bw = px / 2 - 4, bh = px * 2 / 3;
		kui_frame((KuiRect){ x + 2, baseline - bh, bw, bh }, 2, 0);
		for (int i = 0; i < bw && i < bh; i++) {
			if (baseline - bh + i >= 0 && baseline - bh + i < H && x + 2 + i < W) fb[(baseline - bh + i) * W + x + 2 + i] = 0;
		}
		return;
	}
	int gw, gh, xo, yo;
	unsigned char *bm = stbtt_GetCodepointBitmap(&fonts[f], sc, sc, cp, &gw, &gh, &xo, &yo);
	for (int j = 0; j < gh; j++)
		for (int i = 0; i < gw; i++) {
			int X = x + xo + i, Y = baseline + yo + j;
			if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
			int a = bm[j * gw + i];
			fb[Y * W + X] = (unsigned char) ((fb[Y * W + X] * (255 - a) + fg * a) / 255);
		}
	stbtt_FreeBitmap(bm, NULL);
}

/* FBInk print_ot: wraps inside the margins; the block is vertically centred in the box like screen.c */
static int a11y_width(const char *s, KuiFont f, int px, void *ctx)
{
	(void) ctx;
	return kui_text_width(s, f, px);
}

static void draw_text(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign al, unsigned char fg, int bg)
{
	if (!utf8 || !*utf8) return;
	/* Kolay okuma: bold, black, larger — still fitting the box (light text on dark keeps its colour) */
	kui_a11y_adjust(kui_easy(), utf8, box.w, box.h, &f, &px, &fg, fg >= 200 || (bg >= 0 && bg < 128), a11y_width, NULL);
	KuiLine lines[64];
	int fp[2] = { (int) f, px };
	int n = kui_wrap(utf8, box.w, measure_cb, fp, lines, 64), lh = line_h(f, px);
	int a, d, g;
	stbtt_GetFontVMetrics(&fonts[f], &a, &d, &g);
	int asc = (int) (a * scale_for(f, px) + 0.5f), th = n * lh;
	int top = box.y + (th < box.h ? (box.h - th) / 2 : 0);
	int bw = 0;
	for (int i = 0; i < n; i++) {
		int w = span_width(utf8 + lines[i].off, lines[i].len, f, px);
		if (w > bw) bw = w;
	}
	for (int i = 0; i < n; i++) {
		const char *s = utf8 + lines[i].off;
		int w = span_width(s, lines[i].len, f, px);
		/* screen.c aligns the whole block by its widest line; FBInk then left-aligns lines inside it */
		int left = box.x;
		if (al == KUI_ALIGN_CENTER && bw < box.w) left = box.x + (box.w - bw) / 2;
		if (al == KUI_ALIGN_RIGHT && bw < box.w) left = box.x + box.w - bw;
		int y = top + i * lh;
		if (bg >= 0) kui_fill((KuiRect){ left, y, w, lh }, (unsigned char) bg);
		int x = left;
		for (int k = 0; k < lines[i].len;) {
			int cp;
			k += kui_utf8_decode(s + k, &cp);
			draw_glyph(f, px, cp, x, y + asc, fg);
			x += glyph_adv(f, px, cp);
		}
	}
}

void kui_text(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg) { draw_text(box, utf8, f, px, a, fg, -1); }

void kui_text_on(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg, unsigned char bg)
{
	draw_text(box, utf8, f, px, a, fg, bg);
}

void kui_mark_dirty(KuiRect r) { kui_dirty_add(&dirty, r); }

static void put32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char) (v >> 24);
	p[1] = (unsigned char) (v >> 16);
	p[2] = (unsigned char) (v >> 8);
	p[3] = (unsigned char) v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, unsigned len)
{
	unsigned char hdr[8];
	put32(hdr, len);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	if (len) fwrite(data, 1, len, f);
	unsigned long crc = crc32(0, (const unsigned char *) type, 4);
	crc = crc32(crc, data, len);
	unsigned char c[4];
	put32(c, (unsigned) crc);
	fwrite(c, 1, 4, f);
}

static void write_png(const char *path, int full, const KuiDirty *d)
{
	/* grayscale PNG; partial refresh regions are outlined in mid-gray in a second "-dirty" image */
	static unsigned char raw[(W + 1) * H];
	for (int y = 0; y < H; y++) {
		raw[y * (W + 1)] = 0;
		memcpy(raw + y * (W + 1) + 1, fb + y * W, W);
	}
	(void) full;
	(void) d;
	uLongf zl = compressBound(sizeof raw);
	unsigned char *z = malloc(zl);
	compress2(z, &zl, raw, sizeof raw, 6);
	FILE *f = fopen(path, "wb");
	if (!f) {
		free(z);
		return;
	}
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	unsigned char ihdr[13];
	put32(ihdr, W);
	put32(ihdr + 4, H);
	ihdr[8] = 8;
	ihdr[9] = 0;
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, (unsigned) zl);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(z);
}

void kui_flush(int screen_changed)
{
	if (!screen_changed && dirty.n == 0) return;
	if (asleep) {
		dirty.n = 0;
		return;
	}
	KuiUpdKind kind = kui_policy_next(&policy, screen_changed);
	const char *out = getenv("KUI_SIM_OUT");
	if (out && *out) {
		static const char *const suffix[] = { "", "_full", "_full_flash" };
		static const char *const label[] = { "partial", "quiet full", "FLASH" };
		char path[512];
		snprintf(path, sizeof path, "%s/%s_%03d%s.png", out, app_name, shot, suffix[kind]);
		write_png(path, kind != KUI_UPD_PARTIAL, &dirty);
		fprintf(stderr, "sim: flush %d (%s, %d dirty) -> %s\n", shot, label[kind], dirty.n, path);
	}
	shot++;
	dirty.n = 0;
}

KuiEvent kui_next_event(int timeout_ms)
{
	(void) timeout_ms;
	KuiEvent e = { KUI_EV_QUIT, 0, 0, KUI_DIR_NONE };
	for (;;) {
		if (q_len) {
			KuiEvent q = queue[q_head];
			q_head = (q_head + 1) % QN;
			q_len--;
			return q;
		}
		while (*script == ';' || *script == ' ') script++;
		if (!*script) return e;
		char cmd[16] = "", arg[16] = "";
		int x = 0, y = 0, used = 0;
		int k = sscanf(script, "%15s %n", cmd, &used);
		if (k != 1) return e;
		script += used;
		cmd[strcspn(cmd, ";")] = '\0';
		arg[0] = '\0';
		if (!strcmp(cmd, "tap") || !strcmp(cmd, "long")) {
			sscanf(script, "%d %d %n", &x, &y, &used);
			script += used;
			fprintf(stderr, "sim: %s %d %d\n", cmd, x, y);
			if (drag_mode) {
				int pt[2] = { x, y };
				sim_touch(pt, 1, cmd[0] == 't' ? 80 : KUI_LONGPRESS_MS + 100);
				continue;
			}
			return (KuiEvent){ cmd[0] == 't' ? KUI_EV_TAP : KUI_EV_LONGPRESS, x, y, KUI_DIR_NONE };
		}
		if (!strcmp(cmd, "swipe")) {
			sscanf(script, "%15[a-z] %n", arg, &used);
			script += used;
			KuiDir dir = !strcmp(arg, "left") ? KUI_DIR_LEFT : !strcmp(arg, "right") ? KUI_DIR_RIGHT : !strcmp(arg, "up") ? KUI_DIR_UP : KUI_DIR_DOWN;
			return (KuiEvent){ KUI_EV_SWIPE, 0, 0, dir };
		}
		if (!strcmp(cmd, "draw")) {
			static int pts[2 * 1024];
			int n = 0, v;
			while (n < 2 * 1024 && sscanf(script, "%d %n", &v, &used) == 1) {
				pts[n++] = v;
				script += used;
			}
			n /= 2;
			if (n < 1) continue;
			fprintf(stderr, "sim: draw (%d points)\n", n);
			KuiEvent g = sim_touch(pts, n, 0);
			if (!drag_mode && g.type != KUI_EV_NONE) return g;
			continue;
		}
		if (!strcmp(cmd, "wake")) {
			asleep = 0;
			kui_policy_request_flash(&policy); /* as input.c: the redraw after a wake flashes */
			return (KuiEvent){ KUI_EV_WAKE, 0, 0, KUI_DIR_NONE };
		}
		if (!strcmp(cmd, "asleep")) {
			int was = asleep;
			sscanf(script, "%d %n", &asleep, &used);
			script += used;
			if (asleep && !was) kui_sleep_screen(); /* as input.c on goingToScreenSaver */
			continue;
		}
		if (!strcmp(cmd, "none")) return (KuiEvent){ KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
		fprintf(stderr, "sim: unknown command %s\n", cmd);
	}
}

void kui_flush_sleep(void)
{
	const char *out = getenv("KUI_SIM_OUT");
	if (out && *out) {
		char path[512];
		snprintf(path, sizeof path, "%s/%s_%03d_sleep.png", out, app_name, shot);
		write_png(path, 1, &dirty);
		fprintf(stderr, "sim: flush %d (sleep screen) -> %s\n", shot, path);
	}
	shot++;
	dirty.n = 0;
}

void kui_flush_fast(KuiRect r)
{
	if (asleep || r.w <= 0 || r.h <= 0) return;
	const char *out = getenv("KUI_SIM_OUT");
	if (out && *out) {
		char path[512];
		snprintf(path, sizeof path, "%s/%s_%03d_ink.png", out, app_name, shot);
		write_png(path, 0, &dirty);
		fprintf(stderr, "sim: flush %d (ink %dx%d) -> %s\n", shot, r.w, r.h, path);
	}
	shot++;
}

void kui_wait_refresh(void) {}
int kui_quit_pending(void) { return 0; }
int kui_grab_power_key(int on) { (void) on; return -1; }
void kui_drain_power_key(void) {}

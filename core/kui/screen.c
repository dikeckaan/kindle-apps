/* Screen layer: every draw goes to the framebuffer with no_refresh; kui_flush() is the single refresh point. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fbink.h"
#include "kui.h"

int kui_input_open(void);
void kui_input_close(void);
int kui_input_asleep(void);

static int fb = -1;
static FBInkConfig cfg;
static FBInkState st;
static KuiRefreshPolicy policy;
static KuiDirty dirty;
static WFM_MODE_INDEX_T partial_wfm = WFM_GL16;

int kui_width(void) { return (int) st.screen_width; }
int kui_height(void) { return (int) st.screen_height; }

static void show_message(const char *msg)
{
	FBInkOTConfig ot = { 0 };
	ot.size_px = 40;
	ot.is_centered = true;
	ot.margins.top = (short) (kui_height() / 2 - 40);
	FBInkConfig c = cfg;
	c.no_refresh = false;
	c.wfm_mode = WFM_GC16;
	c.is_flashing = true;
	fbink_cls(fb, &c, NULL, false);
	fbink_print_ot(fb, msg, &ot, &c, NULL);
}

int kui_init(const char *app)
{
	char path[300];
	const char *root = getenv("KUI_ROOT");
	if (!root || !*root) root = "/mnt/us/kindle-apps";
	fb = fbink_open();
	cfg = (FBInkConfig){ 0 };
	cfg.is_quiet = true;
	if (fb < 0 || fbink_init(fb, &cfg) < 0) return -1;
	fbink_get_state(&cfg, &st);
	snprintf(path, sizeof path, "%s/fonts/DejaVuSans.ttf", root);
	if (fbink_add_ot_font(path, FNT_REGULAR) < 0) return -1;
	snprintf(path, sizeof path, "%s/fonts/DejaVuSans-Bold.ttf", root);
	if (fbink_add_ot_font(path, FNT_BOLD) < 0) return -1;
	cfg.no_refresh = true;
	cfg.is_bgless = true;

	snprintf(path, sizeof path, "%s/settings", kui_data_dir(app));
	kui_policy_init(&policy, kui_cfg_get_int(path, "full_refresh_every", 25));
	/* partial_wfm: 0 = GL16 (grays, default), 1 = DU (faster, black/white); KUI_WFM env overrides */
	const char *w = getenv("KUI_WFM");
	int du = w ? strcmp(w, "DU") == 0 : kui_cfg_get_int(path, "partial_wfm", 0) == 1;
	partial_wfm = du ? WFM_DU : WFM_GL16;
	dirty.n = 0;

	int in = kui_input_open();
	if (in == KUI_ERR_TOUCH_BUSY) {
		show_message("Dokunmatik meşgul: önce KOReader'ı kapatın");
		return KUI_ERR_TOUCH_BUSY;
	}
	return in < 0 ? -1 : 0;
}

void kui_quit(void)
{
	kui_input_close();
	if (fb >= 0) {
		fbink_free_ot_fonts();
		fbink_close(fb);
		fb = -1;
	}
}

void kui_set_refresh_every(int n) { kui_policy_init(&policy, n); }

void kui_fill(KuiRect r, unsigned char gray)
{
	if (r.w <= 0 || r.h <= 0) return;
	FBInkRect fr = { (unsigned short) r.x, (unsigned short) r.y, (unsigned short) r.w, (unsigned short) r.h };
	fbink_fill_rect_gray(fb, &cfg, &fr, false, gray);
}

void kui_frame(KuiRect r, int t, unsigned char gray)
{
	kui_fill((KuiRect){ r.x, r.y, r.w, t }, gray);
	kui_fill((KuiRect){ r.x, r.y + r.h - t, r.w, t }, gray);
	kui_fill((KuiRect){ r.x, r.y, t, r.h }, gray);
	kui_fill((KuiRect){ r.x + r.w - t, r.y, t, r.h }, gray);
}

static void measure(const char *utf8, KuiFont f, int px, int *w, int *h)
{
	FBInkOTConfig ot = { 0 };
	ot.size_px = (unsigned short) px;
	ot.style = f == KUI_FONT_BOLD ? FNT_BOLD : FNT_REGULAR;
	ot.compute_only = true;
	FBInkOTFit fit = { 0 };
	fbink_print_ot(fb, utf8, &ot, &cfg, &fit);
	*w = fit.bbox.width;
	*h = fit.bbox.height;
}

int kui_text_width(const char *utf8, KuiFont f, int px)
{
	int w, h;
	measure(utf8, f, px, &w, &h);
	return w;
}

static void draw_text(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg, int bg)
{
	int w, h;
	measure(utf8, f, px, &w, &h);
	int left = box.x;
	if (a == KUI_ALIGN_CENTER && w < box.w) left = box.x + (box.w - w) / 2;
	if (a == KUI_ALIGN_RIGHT && w < box.w) left = box.x + box.w - w;
	FBInkOTConfig ot = { 0 };
	ot.size_px = (unsigned short) px;
	ot.style = f == KUI_FONT_BOLD ? FNT_BOLD : FNT_REGULAR;
	ot.margins.top = (short) (box.y + (h < box.h ? (box.h - h) / 2 : 0));
	ot.margins.left = (short) left;
	ot.margins.right = (short) (kui_width() - (box.x + box.w));
	/* print_ot takes its colours from the config's palette indices (FG_BLACK = 0 … FG_WHITE = 15,
	 * BG_WHITE = 0 … BG_BLACK = 15). Its "bgless" blend is wrong for light text on 8bpp panels,
	 * so light text is drawn with an explicit background instead. */
	FBInkConfig c = cfg;
	/* FBInk's pure black/white shortcut (|fg - bg| == 0xFF) always paints dark-on-light, so white on black
	 * is drawn with an almost-white foreground to take the general (correct) path. */
	if (bg >= 0 && fg - bg == 0xFF) fg = 0xEE;
	c.fg_color = (FG_COLOR_INDEX_T) (fg / 0x11);
	if (bg >= 0) {
		c.bg_color = (BG_COLOR_INDEX_T) ((255 - bg) / 0x11);
		c.is_bgless = false;
	}
	fbink_update_pen_colors(&c);
	fbink_print_ot(fb, utf8, &ot, &c, NULL);
}

void kui_text(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg)
{
	draw_text(box, utf8, f, px, a, fg, -1);
}

void kui_text_on(KuiRect box, const char *utf8, KuiFont f, int px, KuiAlign a, unsigned char fg, unsigned char bg)
{
	draw_text(box, utf8, f, px, a, fg, bg);
}

void kui_mark_dirty(KuiRect r) { kui_dirty_add(&dirty, r); }

void kui_flush(int screen_changed)
{
	if (!screen_changed && dirty.n == 0) return;
	if (kui_input_asleep()) { /* never draw over the screensaver; KUI_EV_WAKE redraws everything */
		dirty.n = 0;
		return;
	}
	FBInkConfig c = cfg;
	if (kui_policy_next(&policy, screen_changed) == KUI_UPD_FULL) {
		c.wfm_mode = WFM_GC16;
		c.is_flashing = true;
		fbink_refresh(fb, 0, 0, 0, 0, &c);
	} else {
		c.wfm_mode = partial_wfm;
		c.is_flashing = false;
		for (int i = 0; i < dirty.n; i++) {
			KuiRect r = dirty.r[i];
			fbink_refresh(fb, (uint32_t) r.y, (uint32_t) r.x, (uint32_t) r.w, (uint32_t) r.h, &c);
		}
	}
	dirty.n = 0;
}

static int fb_measure(const char *s, int n, void *ctx)
{
	const int *fp = ctx; /* { font, px } */
	char buf[1024];
	if (n >= (int) sizeof buf) n = (int) sizeof buf - 1;
	memcpy(buf, s, (size_t) n);
	buf[n] = '\0';
	return kui_text_width(buf, (KuiFont) fp[0], fp[1]);
}

int kui_wrap_text(const char *text, int width, KuiFont f, int px, KuiLine *out, int max)
{
	int fp[2] = { (int) f, px };
	return kui_wrap(text, width, fb_measure, fp, out, max);
}

int kui_line_height(KuiFont f, int px)
{
	(void) f;
	return px * 13 / 10;
}

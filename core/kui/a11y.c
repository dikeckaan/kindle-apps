/* Kolay okuma and Basit mod: the user's reading settings (data/ui) and the text rule Kolay okuma applies. */
#include <stdio.h>
#include <stdlib.h>
#include "kui.h"

#define EASY_SCALE_PCT 130 /* larger text … */
#define EASY_MIN_PX 30     /* … never smaller than this */

void kui_a11y_adjust(int easy, const char *s, int box_w, int box_h, KuiFont *f, int *px, unsigned char *fg, int light,
		     int (*width)(const char *, KuiFont, int, void *), void *ctx)
{
	if (!easy) return;
	KuiFont f0 = *f;
	if (*f == KUI_FONT_REG) *f = KUI_FONT_BOLD;
	else if (*f == KUI_FONT_SERIF) *f = KUI_FONT_SERIF_BOLD;
	if (!light) *fg = 0; /* gray text is hard to read: black */
	int px0 = *px, want = (px0 * EASY_SCALE_PCT + 50) / 100;
	if (want < EASY_MIN_PX) want = EASY_MIN_PX;
	if (want <= px0) want = px0;
	/* a line that fit its box (in its own font) must stay one line in bold: shrink as needed, at most to 80 % */
	if (s && box_w > 0 && width && width(s, f0, px0, ctx) <= box_w)
		while (want > px0 * 8 / 10 && width(s, *f, want, ctx) > box_w) want--;
	/* and not taller than its box (an old size that already fit stays) */
	int cap = box_h * 9 / 10;
	if (box_h > 0 && want > cap) want = cap > px0 ? cap : px0;
	*px = want;
}

static int ui_loaded, ui_easy, ui_simple;

static const char *ui_path(void)
{
	static char p[300];
	const char *root = getenv("KUI_ROOT");
	snprintf(p, sizeof p, "%s/data/ui", root && *root ? root : "/mnt/us/kindle-apps");
	return p;
}

static void ui_load(void)
{
	if (ui_loaded) return;
	ui_loaded = 1;
	ui_easy = kui_cfg_get_int(ui_path(), "easy", 0) == 1;
	ui_simple = kui_cfg_get_int(ui_path(), "simple", 0) == 1;
}

int kui_easy(void)
{
	ui_load();
	return ui_easy;
}

int kui_simple(void)
{
	ui_load();
	return ui_simple;
}

void kui_ui_set(const char *key, int v)
{
	ui_load();
	char dir[300];
	const char *root = getenv("KUI_ROOT");
	snprintf(dir, sizeof dir, "%s/data", root && *root ? root : "/mnt/us/kindle-apps");
	kui_data_dir("hub"); /* makes sure data/ exists */
	kui_cfg_set_int(ui_path(), key, v ? 1 : 0);
	if (key[0] == 'e') ui_easy = v ? 1 : 0;
	else ui_simple = v ? 1 : 0;
}

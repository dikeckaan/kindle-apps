#include <math.h>
#include <stdio.h>
#include "wxdraw.h"

void wx_draw_icon(KuiRect r, WxIcon ic)
{
	int px = r.h * 4 / 5;
	static const char *const glyph[] = { "☀", "☽", "", "☁", "≡", "☂", "❄", "⚡" };
	if (ic == WX_ICON_PARTLY || ic == WX_ICON_PARTLY_NIGHT) { /* DejaVu has no ⛅: sun (or moon) behind a cloud */
		kui_text((KuiRect){ r.x + r.w / 3, r.y, r.w * 2 / 3, r.h * 2 / 3 }, ic == WX_ICON_PARTLY ? "☀" : "☽", KUI_FONT_REG, px * 4 / 5, KUI_ALIGN_CENTER, 0);
		kui_text((KuiRect){ r.x, r.y + r.h / 4, r.w * 4 / 5, r.h * 3 / 4 }, "☁", KUI_FONT_REG, px, KUI_ALIGN_CENTER, 0);
		return;
	}
	if ((int) ic < 0 || (int) ic >= (int) (sizeof glyph / sizeof glyph[0])) ic = WX_ICON_CLOUD;
	kui_text(r, glyph[ic], KUI_FONT_REG, px, KUI_ALIGN_CENTER, 0);
}

void wx_fmt_temp(double t, char *out, int cap)
{
	if (isnan(t)) snprintf(out, (size_t) cap, "—");
	else snprintf(out, (size_t) cap, "%ld°", lround(t) == 0 ? 0L : lround(t));
}

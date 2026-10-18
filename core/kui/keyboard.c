/* Turkish Q on-screen keyboard: pure layout and hit-testing. */
#include <string.h>
#include "kui.h"

#define GAP 8

static const int tr_rows[3][12] = {
	{ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0x11E, 0xDC },
	{ 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0x15E, 0x130, 0 },
	{ KUI_KEY_SHIFT, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0xD6, 0xC7, KUI_KEY_BACK, 0 },
};
static const int sym_rows[3][12] = {
	{ '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0, 0 },
	{ '-', '_', '.', '@', '#', '!', '?', '*', '+', '=', '/', 0 },
	{ '(', ')', ':', ';', ',', '\'', '"', '&', '%', KUI_KEY_BACK, 0, 0 },
};

static void set_label(KuiKey *k)
{
	switch (k->cp) {
	case KUI_KEY_SHIFT: strcpy(k->label, "⇧"); return;
	case KUI_KEY_BACK: strcpy(k->label, "⌫"); return;
	case KUI_KEY_ENTER: strcpy(k->label, "Tamam"); return;
	case ' ': strcpy(k->label, "boşluk"); return;
	default: break;
	}
	if (k->cp == KUI_KEY_PAGE) return; /* label set by caller */
	int n = kui_utf8_encode(k->cp, k->label);
	k->label[n] = '\0';
}

int kui_kb_layout(KuiRect area, KuiKbPage page, int shift, KuiKey *out, int max)
{
	const int(*rows)[12] = page == KUI_KB_TR_LETTERS ? tr_rows : sym_rows;
	int n = 0, rh = (area.h - GAP * 5) / 4, kw = (area.w - GAP * 13) / 12;
	for (int r = 0; r < 3; r++) {
		int cnt = 0;
		while (cnt < 12 && rows[r][cnt]) cnt++;
		int x = area.x + (area.w - (cnt * kw + (cnt - 1) * GAP)) / 2, y = area.y + GAP + r * (rh + GAP);
		for (int i = 0; i < cnt && n < max; i++, x += kw + GAP) {
			KuiKey *k = &out[n++];
			int cp = rows[r][i];
			k->cp = (page == KUI_KB_TR_LETTERS && !shift && cp > 0) ? kui_tr_lower(cp) : cp;
			k->r = (KuiRect){ x, y, kw, rh };
			set_label(k);
		}
	}
	/* bottom row: page switch | space | Tamam */
	int y = area.y + GAP + 3 * (rh + GAP), side = 2 * kw + GAP, space = area.w - 2 * side - 4 * GAP;
	const struct { int cp, x, w; } bottom[3] = {
		{ KUI_KEY_PAGE, area.x + GAP, side },
		{ ' ', area.x + 2 * GAP + side, space },
		{ KUI_KEY_ENTER, area.x + 3 * GAP + side + space, side },
	};
	for (int i = 0; i < 3 && n < max; i++) {
		KuiKey *k = &out[n++];
		k->cp = bottom[i].cp;
		k->r = (KuiRect){ bottom[i].x, y, bottom[i].w, rh };
		if (k->cp == KUI_KEY_PAGE) strcpy(k->label, page == KUI_KB_TR_LETTERS ? "123" : "ABC");
		else set_label(k);
	}
	return n;
}

int kui_kb_hit(const KuiKey *keys, int n, int x, int y)
{
	for (int i = 0; i < n; i++)
		if (kui_rect_contains(keys[i].r, x, y)) return i;
	return -1;
}

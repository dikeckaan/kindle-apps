/* Shared widgets: top bar, buttons, modal dialog. */
#include <stdio.h>
#include <string.h>
#include "kui.h"

int kui_fit_px(const char *text, KuiFont f, int px, int max_w)
{
	while (px > 20 && kui_text_width(text, f, px) > max_w) px -= 2;
	return px;
}

void kui_topbar(const char *title, const char *right_label)
{
	int w = kui_width();
	KuiRect bar = { 0, 0, w, KUI_TOPBAR_H };
	kui_fill(bar, 255);
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H - 4, w, 4 }, 0);
	kui_text(kui_topbar_exit_rect(), "✕", KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 200, 0, w - 2 * 200, KUI_TOPBAR_H - 4 }, title, KUI_FONT_BOLD, kui_fit_px(title, KUI_FONT_BOLD, 52, w - 400), KUI_ALIGN_CENTER, 0);
	if (right_label) {
		KuiRect m = kui_topbar_menu_rect(w);
		kui_text((KuiRect){ m.x, m.y, m.w - 24, m.h - 4 }, right_label, KUI_FONT_REG, kui_fit_px(right_label, KUI_FONT_REG, 44, m.w - 24), KUI_ALIGN_RIGHT, 0);
	}
	kui_mark_dirty(bar);
}

void kui_button(const KuiButton *b, int pressed)
{
	kui_fill(b->r, pressed ? 0 : 255);
	kui_frame(b->r, 4, 0);
	int px = kui_fit_px(b->label, KUI_FONT_BOLD, b->r.h > 140 ? 52 : 40, b->r.w - 24);
	kui_text_on(b->r, b->label, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, pressed ? 255 : 0, pressed ? 0 : 255);
	kui_mark_dirty(b->r);
}

int kui_dialog(const char *title, const char *msg, const char **choices, int n, void (*redraw_bg)(void))
{
	int woke = 0;
	int w = kui_width(), h = kui_height();
	KuiRect box = { 60, h / 2 - 330, w - 120, 660 };
	KuiButton bs[4];
	if (n > 4) n = 4;
	int bw = (box.w - 40 * (n + 1)) / (n ? n : 1);
	for (int i = 0; i < n; i++)
		bs[i] = (KuiButton){ { box.x + 40 + i * (bw + 40), box.y + box.h - 180, bw, 130 }, choices[i], i + 1 };
	for (;;) {
		kui_fill(box, 255);
		kui_frame(box, 6, 0);
		kui_text((KuiRect){ box.x + 30, box.y + 30, box.w - 60, 110 }, title, KUI_FONT_BOLD, 60, KUI_ALIGN_CENTER, 0);
		kui_text((KuiRect){ box.x + 30, box.y + 150, box.w - 60, 300 }, msg, KUI_FONT_REG, 46, KUI_ALIGN_CENTER, 0);
		for (int i = 0; i < n; i++) kui_button(&bs[i], 0);
		kui_mark_dirty(box);
		kui_flush(woke); /* after a wake the screensaver covered everything: full refresh */
		woke = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) {
				if (redraw_bg) redraw_bg();
				woke = 1;
				break;
			}
			if (e.type != KUI_EV_TAP) continue;
			if (kui_rect_contains(kui_topbar_exit_rect(), e.x, e.y)) return -1;
			int id = kui_buttons_hit(bs, n, e.x, e.y);
			if (id) return id - 1;
		}
	}
}

/* ---- full-screen text entry ---- */

static void draw_field(KuiRect field, const char *buf, int secret, const char *hint)
{
	char shown[256];
	if (secret) {
		int n = kui_utf8_len(buf), o = 0;
		for (int i = 0; i < n && o + 4 < (int) sizeof shown; i++) o += snprintf(shown + o, sizeof shown - (size_t) o, "●");
		shown[o] = '\0';
	} else {
		snprintf(shown, sizeof shown, "%s", buf);
	}
	kui_fill(field, 255);
	kui_frame(field, 4, 0);
	char line[264];
	snprintf(line, sizeof line, "%s_", shown);
	kui_text((KuiRect){ field.x + 24, field.y, field.w - 48, field.h }, line, KUI_FONT_REG, 56, KUI_ALIGN_LEFT, 0);
	KuiRect h = { field.x, field.y + field.h + 10, field.w, 60 };
	kui_fill(h, 255);
	if (hint) kui_text(h, hint, KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(field);
	kui_mark_dirty(h);
}

static void draw_keys(const KuiKey *keys, int n, KuiRect area)
{
	kui_fill(area, 255);
	for (int i = 0; i < n; i++) {
		kui_frame(keys[i].r, 3, 0);
		int px = kui_fit_px(keys[i].label, KUI_FONT_BOLD, keys[i].r.w > 200 ? 40 : 48, keys[i].r.w - 16);
		kui_text(keys[i].r, keys[i].label, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty(area);
}

int kui_input_text(const char *title, char *buf, int cap, int secret, int min_len, void (*redraw_bg)(void))
{
	(void) redraw_bg; /* full-screen: nothing behind it to repaint */
	int w = kui_width(), h = kui_height(), shift = 1, page = KUI_KB_TR_LETTERS;
	KuiRect field = { 40, KUI_TOPBAR_H + 60, w - 80, 140 }, area = { 0, h - 580, w, 560 };
	KuiKey keys[64];
	int n = kui_kb_layout(area, (KuiKbPage) page, shift, keys, 64);
	char hint[64] = "";
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(title, NULL);
			draw_field(field, buf, secret, hint[0] ? hint : NULL);
			draw_keys(keys, n, area);
			kui_flush(1);
			full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return 0;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
		int k = kui_kb_hit(keys, n, e.x, e.y);
		if (k < 0) continue;
		int cp = keys[k].cp, len = (int) strlen(buf);
		hint[0] = '\0';
		if (cp == KUI_KEY_ENTER) {
			if (kui_utf8_len(buf) >= min_len) return 1;
			snprintf(hint, sizeof hint, "En az %d karakter girin", min_len);
		} else if (cp == KUI_KEY_BACK) {
			while (len > 0 && (buf[len - 1] & 0xC0) == 0x80) len--; /* drop continuation bytes */
			if (len > 0) len--;
			buf[len] = '\0';
		} else if (cp == KUI_KEY_SHIFT || cp == KUI_KEY_PAGE) {
			if (cp == KUI_KEY_SHIFT) shift = !shift;
			else page = page == KUI_KB_TR_LETTERS ? KUI_KB_SYMBOLS : KUI_KB_TR_LETTERS;
			n = kui_kb_layout(area, (KuiKbPage) page, shift, keys, 64);
			draw_keys(keys, n, area);
		} else {
			char enc[4];
			int bl = kui_utf8_encode(cp, enc);
			if (len + bl < cap) {
				memcpy(buf + len, enc, (size_t) bl);
				buf[len + bl] = '\0';
			}
		}
		draw_field(field, buf, secret, hint[0] ? hint : NULL);
		kui_flush(0);
	}
}

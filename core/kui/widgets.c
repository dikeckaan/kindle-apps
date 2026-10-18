/* Shared widgets: top bar, buttons, modal dialog. */
#include "kui.h"

void kui_topbar(const char *title, const char *right_label)
{
	int w = kui_width();
	KuiRect bar = { 0, 0, w, KUI_TOPBAR_H };
	kui_fill(bar, 255);
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H - 4, w, 4 }, 0);
	kui_text(kui_topbar_exit_rect(), "✕", KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 200, 0, w - 2 * 200, KUI_TOPBAR_H - 4 }, title, KUI_FONT_BOLD, 52, KUI_ALIGN_CENTER, 0);
	if (right_label) {
		KuiRect m = kui_topbar_menu_rect(w);
		kui_text((KuiRect){ m.x, m.y, m.w - 24, m.h - 4 }, right_label, KUI_FONT_REG, 44, KUI_ALIGN_RIGHT, 0);
	}
	kui_mark_dirty(bar);
}

void kui_button(const KuiButton *b, int pressed)
{
	kui_fill(b->r, pressed ? 0 : 255);
	kui_frame(b->r, 4, 0);
	kui_text(b->r, b->label, KUI_FONT_BOLD, b->r.h > 140 ? 52 : 40, KUI_ALIGN_CENTER, pressed ? 255 : 0);
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

/* Temporary device check for kui: tap draws a square, swipe left inverts, top-left corner exits. */
#include <stdio.h>
#include "kui.h"

static int inverted;

static void draw_all(void)
{
	KuiRect all = { 0, 0, kui_width(), kui_height() };
	kui_fill(all, inverted ? 0 : 255);
	kui_text((KuiRect){ 0, 0, 120, 120 }, "✕", KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, inverted ? 255 : 0);
	kui_text((KuiRect){ 0, kui_height() / 2 - 60, kui_width(), 120 }, "Dokun · Kaydır · İıĞğŞş", KUI_FONT_REG, 56,
		 KUI_ALIGN_CENTER, inverted ? 255 : 0);
	kui_flush(1);
}

int main(void)
{
	int rc = kui_init("kuidemo");
	if (rc != 0) {
		fprintf(stderr, "kui_init failed: %d\n", rc);
		return 1;
	}
	draw_all();
	int squares = 0;
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		printf("event %d at %d,%d dir %d\n", e.type, e.x, e.y, e.dir);
		fflush(stdout);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_TAP && kui_rect_contains((KuiRect){ 0, 0, 120, 120 }, e.x, e.y)) break;
		if (e.type == KUI_EV_TAP) {
			KuiRect r = { e.x - 20, e.y - 20, 40, 40 };
			kui_fill(r, inverted ? 255 : 0);
			kui_mark_dirty(r);
			kui_flush(0);
			printf("square %d\n", ++squares);
		} else if (e.type == KUI_EV_SWIPE && e.dir == KUI_DIR_LEFT) {
			inverted = !inverted;
			draw_all();
		} else if (e.type == KUI_EV_WAKE) {
			draw_all();
		}
	}
	kui_quit();
	return 0;
}

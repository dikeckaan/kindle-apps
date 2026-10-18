#include <stdlib.h>
#include "kui.h"

/* Movement < KUI_LONGPRESS_SLOP → tap (short) or long-press (long);
 * dominant-axis movement >= KUI_SWIPE_MIN_PX → swipe; anything in between is ambiguous → no event. */
KuiEvent kui_gesture_feed(KuiGesture *g, int down, int x, int y, long t_ms)
{
	KuiEvent ev = { KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
	if (down) {
		if (!g->down) {
			g->down = 1;
			g->x0 = x;
			g->y0 = y;
			g->t0_ms = t_ms;
		}
		g->x = x;
		g->y = y;
		return ev;
	}
	if (!g->down) return ev;
	g->down = 0;
	int dx = x - g->x0, dy = y - g->y0;
	int adx = abs(dx), ady = abs(dy);
	ev.x = g->x0;
	ev.y = g->y0;
	if (adx < KUI_LONGPRESS_SLOP && ady < KUI_LONGPRESS_SLOP) {
		ev.type = t_ms - g->t0_ms >= KUI_LONGPRESS_MS ? KUI_EV_LONGPRESS : KUI_EV_TAP;
	} else if (adx >= KUI_SWIPE_MIN_PX || ady >= KUI_SWIPE_MIN_PX) {
		ev.type = KUI_EV_SWIPE;
		if (adx >= ady) ev.dir = dx < 0 ? KUI_DIR_LEFT : KUI_DIR_RIGHT;
		else ev.dir = dy < 0 ? KUI_DIR_UP : KUI_DIR_DOWN;
	}
	return ev;
}

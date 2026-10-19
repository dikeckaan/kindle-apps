/* Drag stream: raw touch samples → KUI_EV_DOWN / MOVE / UP plus the touch's gesture event (pure, host-tested). */
#include "kui.h"

static int push(KuiEvent *out, int n, KuiEvType t, int x, int y)
{
	out[n] = (KuiEvent){ t, x, y, KUI_DIR_NONE };
	return n + 1;
}

/* ends the current touch at its last position: UP, then the gesture (if any) */
static int release(KuiDrag *d, long t_ms, KuiEvent *out, int n)
{
	n = push(out, n, KUI_EV_UP, d->x, d->y);
	KuiEvent g = kui_gesture_feed(&d->g, 0, d->x, d->y, t_ms);
	if (g.type != KUI_EV_NONE) out[n++] = g;
	d->down = 0;
	d->id = -1;
	return n;
}

int kui_drag_feed(KuiDrag *d, int contact, int x, int y, long t_ms, KuiEvent *out)
{
	int n = 0;
	if (contact < 0) return d->down ? release(d, t_ms, out, n) : 0;
	if (d->down && contact != d->id) n = release(d, t_ms, out, n); /* another finger took slot 0 */
	if (!d->down) {
		d->down = 1;
		d->id = contact;
		d->x = x;
		d->y = y;
		kui_gesture_feed(&d->g, 1, x, y, t_ms);
		return push(out, n, KUI_EV_DOWN, x, y);
	}
	kui_gesture_feed(&d->g, 1, x, y, t_ms);
	if (x == d->x && y == d->y) return n;
	d->x = x;
	d->y = y;
	return push(out, n, KUI_EV_MOVE, x, y);
}

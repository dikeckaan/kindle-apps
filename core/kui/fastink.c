/* Fast ink: an immediate black/white refresh of a small rectangle (pen strokes), next to kui_flush's dirty list.
 * DU by default: "any → black/white", no flash, roughly a quarter of GL16's time, and pixels that are not black or
 * white in the region (e.g. a gray grid) are left alone on the panel. KUI_INK_WFM=A2 tries the even faster A2
 * (black/white only: grays in the region are forced to black or white). screen.c is not involved. */
#include <stdlib.h>
#include <string.h>
#include "fbink.h"
#include "kui.h"

int kui_input_asleep(void);

static int ink_fd = -1;
static int ink_wfm = -1;

void kui_flush_fast(KuiRect r)
{
	if (kui_input_asleep()) return; /* never draw over the screensaver */
	int w = kui_width(), h = kui_height();
	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > w) r.w = w - r.x;
	if (r.y + r.h > h) r.h = h - r.y;
	if (r.w <= 0 || r.h <= 0) return;
	if (ink_fd < 0) ink_fd = fbink_open(); /* FBInk's state is process-wide: screen.c's fbink_init covers this fd */
	if (ink_fd < 0) return;
	if (ink_wfm < 0) {
		const char *e = getenv("KUI_INK_WFM");
		ink_wfm = e && strcmp(e, "A2") == 0 ? WFM_A2 : WFM_DU;
	}
	FBInkConfig c = { 0 };
	c.is_quiet = true;
	c.wfm_mode = (WFM_MODE_INDEX_T) ink_wfm;
	c.is_flashing = false;
	fbink_refresh(ink_fd, (uint32_t) r.y, (uint32_t) r.x, (uint32_t) r.w, (uint32_t) r.h, &c);
}

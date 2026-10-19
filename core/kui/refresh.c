#include "kui.h"

void kui_policy_init(KuiRefreshPolicy *p, int every, int flash_every)
{
	p->every = every < 0 ? 0 : every;
	p->count = 0;
	p->flash_every = flash_every < 0 ? 0 : flash_every;
	p->changes = 0;
	p->flash_pending = 0;
}

void kui_policy_set_every(KuiRefreshPolicy *p, int every)
{
	p->every = every < 0 ? 0 : every;
	p->count = 0;
}

void kui_policy_request_flash(KuiRefreshPolicy *p) { p->flash_pending = 1; }

static KuiUpdKind flash(KuiRefreshPolicy *p)
{
	p->count = p->changes = p->flash_pending = 0; /* a flash drives every pixel: no ghosts left to count */
	return KUI_UPD_FULL_FLASH;
}

KuiUpdKind kui_policy_next(KuiRefreshPolicy *p, int screen_changed)
{
	if (screen_changed >= KUI_FLUSH_FLASH || p->flash_pending) return flash(p);
	if (screen_changed) {
		/* a quiet full update drives only the changed pixels: the partials' ghosts elsewhere stay, count is kept */
		if (p->flash_every > 0 && ++p->changes >= p->flash_every) return flash(p);
		return KUI_UPD_FULL_QUIET;
	}
	if (p->every > 0 && ++p->count >= p->every) return flash(p);
	return KUI_UPD_PARTIAL;
}

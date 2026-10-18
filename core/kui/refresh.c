#include "kui.h"

void kui_policy_init(KuiRefreshPolicy *p, int every)
{
	p->every = every < 0 ? 0 : every;
	p->count = 0;
}

KuiUpdKind kui_policy_next(KuiRefreshPolicy *p, int screen_changed)
{
	if (screen_changed) {
		p->count = 0;
		return KUI_UPD_FULL;
	}
	if (p->every == 0) return KUI_UPD_PARTIAL;
	if (++p->count >= p->every) {
		p->count = 0;
		return KUI_UPD_FULL;
	}
	return KUI_UPD_PARTIAL;
}

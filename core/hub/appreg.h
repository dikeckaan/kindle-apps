/* App registry: every installed app has $KUI_ROOT/apps/<id>/app.ini (name=, exec=, order=). */
#ifndef APPREG_H
#define APPREG_H

typedef struct {
	char id[32];
	char name[48];
	char exec[256]; /* absolute path */
	int order;
} HubApp;

/* Reads <root>/apps/<id>/app.ini; skips entries without exec=; sorted by order, then name; at most max. */
int hub_scan_apps(const char *root, HubApp *out, int max);
/* <root>/data/hub/order (one app id per line, Dikeç Store → Sırala): the apps it names first, in its order */
void hub_apply_order(const char *root, HubApp *a, int n);
int  hub_save_order(const char *root, const HubApp *a, int n); /* 0 = saved */
#endif

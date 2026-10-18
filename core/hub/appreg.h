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
#endif

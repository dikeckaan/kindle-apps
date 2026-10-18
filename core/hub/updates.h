/* updates — the kindle-apps package repository (kpm manifest v2) against what is installed here. */
#ifndef UPDATES_H
#define UPDATES_H

#define UPD_MAX 40

typedef struct {
	char id[48];        /* kindle-apps-<x>: [a-z0-9-] only */
	char name[80];
	char desc[200];
	char ver[24];       /* newest kindlehf artifact, "Y.M.P" */
	char url[200];      /* relative to the repo: packages/<id>/<file>.kpkg */
	char installed[24]; /* "" not installed, "?" installed before versions were recorded */
} UpdPkg;

enum { UPD_CURRENT, UPD_UPDATE, UPD_NEW };

/* packages with a safe id and url and a kindlehf artifact, the core package first; -1 if not a repo */
int upd_parse_repo(const char *json, UpdPkg *out, int max);
int upd_ver_cmp(const char *a, const char *b); /* <0, 0, >0 by numeric parts */
/* fills .installed from <root>/versions/<id> (written by install.sh), else "?" when the app's files exist */
void upd_read_installed(const char *root, UpdPkg *p, int n);
int upd_state(const UpdPkg *p);
/* where a package's app lives: <root>/apps/<x> (kindle-apps-2048 → game2048); core → <root>/bin/hub */
void upd_app_dir(const char *root, const char *id, char *out, int cap);
#endif

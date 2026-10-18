#include <stdlib.h>
#include "hub/updates.h"
#include "t.h"

#define D "build/host/updtest"

static const char *REPO =
	"{\"manifest_version\":2,\"id\":\"kindle-apps\",\"packages\":{"
	"\"kindle-apps-core\":{\"name\":\"Uygulamalar (çekirdek)\",\"artifacts\":["
	"{\"url\":\"packages/kindle-apps-core/kindle-apps-core_2026.10.9_kindlehf.kpkg\",\"version\":[2026,10,9],\"supported_platforms\":[\"kindlehf\"]},"
	"{\"url\":\"packages/kindle-apps-core/kindle-apps-core_2026.10.12_kindlehf.kpkg\",\"version\":[2026,10,12],\"supported_platforms\":[\"kindlehf\"]}]},"
	"\"kindle-apps-2048\":{\"name\":\"2048\",\"description\":\"Klasik 2048\",\"artifacts\":["
	"{\"url\":\"packages/kindle-apps-2048/kindle-apps-2048_2026.10.11_kindlehf.kpkg\",\"version\":[2026,10,11],\"supported_platforms\":[\"kindlehf\"]}]},"
	"\"kindle-apps-pano\":{\"name\":\"Masa Panosu\",\"artifacts\":["
	"{\"url\":\"packages/kindle-apps-pano/kindle-apps-pano_2026.10.12_kindlehf.kpkg\",\"version\":[2026,10,12],\"supported_platforms\":[\"kindlehf\"]}]},"
	"\"kindle-apps-old\":{\"name\":\"Old\",\"artifacts\":["
	"{\"url\":\"packages/x.kpkg\",\"version\":[1,0,0],\"supported_platforms\":[\"kindlepw2\"]}]},"
	"\"evil;rm\":{\"name\":\"Bad id\",\"artifacts\":["
	"{\"url\":\"packages/e.kpkg\",\"version\":[9,0,0],\"supported_platforms\":[\"kindlehf\"]}]},"
	"\"kindle-apps-dots\":{\"name\":\"Bad url\",\"artifacts\":["
	"{\"url\":\"../../etc/x.kpkg\",\"version\":[9,0,0],\"supported_platforms\":[\"kindlehf\"]}]},"
	"\"kindle-apps-abs\":{\"name\":\"Abs url\",\"artifacts\":["
	"{\"url\":\"https://evil.example/x.kpkg\",\"version\":[9,0,0],\"supported_platforms\":[\"kindlehf\"]}]}"
	"}}";

static UpdPkg *find(UpdPkg *p, int n, const char *id)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(p[i].id, id)) return &p[i];
	return NULL;
}

static void test_parse_picks_newest_kindlehf_artifact(void)
{
	UpdPkg p[16];
	int n = upd_parse_repo(REPO, p, 16);
	T_EQ_INT(n, 3); /* core, 2048, pano; wrong platform, bad id, unsafe urls dropped */
	UpdPkg *c = find(p, n, "kindle-apps-core");
	T_ASSERT(c != NULL);
	if (c) {
		T_EQ_STR(c->ver, "2026.10.12");
		T_EQ_STR(c->url, "packages/kindle-apps-core/kindle-apps-core_2026.10.12_kindlehf.kpkg");
		T_EQ_STR(c->name, "Uygulamalar (çekirdek)");
	}
	T_ASSERT(p[0].id[0] && !strcmp(p[0].id, "kindle-apps-core")); /* core first: the runtime goes in before apps */
	T_ASSERT(find(p, n, "kindle-apps-old") == NULL);
	UpdPkg *g = find(p, n, "kindle-apps-2048");
	T_ASSERT(g && !strcmp(g->desc, "Klasik 2048"));
	char dir[200];
	upd_app_dir("/r", "kindle-apps-2048", dir, sizeof dir);
	T_EQ_STR(dir, "/r/apps/game2048");
	upd_app_dir("/r", "kindle-apps-pano", dir, sizeof dir);
	T_EQ_STR(dir, "/r/apps/pano");
	T_ASSERT(find(p, n, "evil;rm") == NULL);
	T_ASSERT(find(p, n, "kindle-apps-dots") == NULL);
	T_ASSERT(find(p, n, "kindle-apps-abs") == NULL);
}

static void test_parse_rejects_garbage(void)
{
	UpdPkg p[4];
	T_EQ_INT(upd_parse_repo("not json", p, 4), -1);
	T_EQ_INT(upd_parse_repo("{\"packages\":[]}", p, 4), -1);
	T_EQ_INT(upd_parse_repo(REPO, p, 2), 2); /* bounded */
}

static void test_ver_cmp(void)
{
	T_ASSERT(upd_ver_cmp("2026.10.12", "2026.10.11") > 0);
	T_ASSERT(upd_ver_cmp("2026.10.9", "2026.10.10") < 0); /* numeric, not lexical */
	T_ASSERT(upd_ver_cmp("2026.10.12", "2026.10.12") == 0);
	T_ASSERT(upd_ver_cmp("", "2026.1.1") < 0);
}

static void test_installed_state(void)
{
	system("rm -rf " D " && mkdir -p " D "/versions " D "/apps/game2048 " D "/bin && : > " D "/bin/hub"
	       " && echo 2026.10.12 > " D "/versions/kindle-apps-core");
	UpdPkg p[16];
	int n = upd_parse_repo(REPO, p, 16);
	upd_read_installed(D, p, n);
	UpdPkg *c = find(p, n, "kindle-apps-core"), *g = find(p, n, "kindle-apps-2048"), *pa = find(p, n, "kindle-apps-pano");
	T_ASSERT(c && g && pa);
	if (!c || !g || !pa) return;
	T_EQ_STR(c->installed, "2026.10.12");
	T_EQ_INT(upd_state(c), UPD_CURRENT);
	T_EQ_STR(g->installed, "?"); /* present (apps/game2048) but installed before versions were recorded */
	T_EQ_INT(upd_state(g), UPD_UPDATE);
	T_EQ_STR(pa->installed, "");
	T_EQ_INT(upd_state(pa), UPD_NEW);
	system("echo 2026.10.13 > " D "/versions/kindle-apps-pano");
	upd_read_installed(D, p, n);
	T_EQ_INT(upd_state(pa), UPD_NEW); /* a version left behind by an uninstall: the files are gone */
	system("mkdir -p " D "/apps/pano");
	upd_read_installed(D, p, n);
	T_EQ_INT(upd_state(pa), UPD_CURRENT); /* newer than the repo (a local build): nothing to do */
}

int main(void)
{
	T_RUN(test_parse_picks_newest_kindlehf_artifact);
	T_RUN(test_parse_rejects_garbage);
	T_RUN(test_ver_cmp);
	T_RUN(test_installed_state);
	return T_DONE();
}

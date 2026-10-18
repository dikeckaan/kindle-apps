#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "hub/appreg.h"
#include "t.h"

static char root[] = "/tmp/kui-hub-XXXXXX";

static void make_app(const char *id, const char *ini)
{
	char p[256];
	snprintf(p, sizeof p, "%s/apps", root);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/apps/%s", root, id);
	mkdir(p, 0755);
	if (!ini) return;
	snprintf(p, sizeof p, "%s/apps/%s/app.ini", root, id);
	FILE *f = fopen(p, "w");
	fputs(ini, f);
	fclose(f);
}

static void test_scan_sorts_by_order_then_name(void)
{
	HubApp a[8];
	int n = hub_scan_apps(root, a, 8);
	T_EQ_INT(n, 3);
	T_EQ_STR(a[0].name, "Satranç");
	T_EQ_STR(a[1].name, "2048");
	T_EQ_STR(a[2].name, "Wordle");
}

static void test_scan_skips_dir_without_ini(void)
{
	HubApp a[8];
	int n = hub_scan_apps(root, a, 8);
	for (int i = 0; i < n; i++) T_ASSERT(strcmp(a[i].id, "empty") != 0);
}

static void test_scan_skips_ini_without_exec(void)
{
	HubApp a[8];
	int n = hub_scan_apps(root, a, 8);
	for (int i = 0; i < n; i++) T_ASSERT(strcmp(a[i].id, "noexec") != 0);
}

static void test_scan_resolves_exec_relative_to_app_dir(void)
{
	HubApp a[8];
	hub_scan_apps(root, a, 8);
	char want[256];
	snprintf(want, sizeof want, "%s/apps/game2048/game2048", root);
	T_EQ_STR(a[1].exec, want);
	T_EQ_STR(a[1].id, "game2048");
}

static void test_scan_caps_at_max(void)
{
	HubApp a[2];
	T_EQ_INT(hub_scan_apps(root, a, 2), 2);
}

static void test_scan_missing_root_is_empty(void)
{
	HubApp a[2];
	T_EQ_INT(hub_scan_apps("/tmp/kui-no-such-root", a, 2), 0);
}

int main(void)
{
	if (!mkdtemp(root)) return 2;
	make_app("game2048", "name=2048\nexec=game2048\norder=20\n");
	make_app("wordle", "name=Wordle\nexec=wordle\norder=20\n");
	make_app("chess", "order=10\nname=Satranç\nexec=chess\n");
	make_app("noexec", "name=Bozuk\norder=1\n");
	make_app("empty", NULL);
	T_RUN(test_scan_sorts_by_order_then_name);
	T_RUN(test_scan_skips_dir_without_ini);
	T_RUN(test_scan_skips_ini_without_exec);
	T_RUN(test_scan_resolves_exec_relative_to_app_dir);
	T_RUN(test_scan_caps_at_max);
	T_RUN(test_scan_missing_root_is_empty);
	return T_DONE();
}

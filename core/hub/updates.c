/* updates — reads the kpm repo manifest and compares it with <root>/versions/<id> written by install.sh. */
#include "updates.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "vendor/cjson/cJSON.h"

static int safe_id(const char *s)
{
	if (!*s || strlen(s) >= sizeof ((UpdPkg *) 0)->id) return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-')) return 0;
	return 1;
}

/* relative path inside the repo: packages/…kpkg, no "..", no scheme, no shell or URL specials */
static int safe_url(const char *s)
{
	size_t n = strlen(s);
	if (strncmp(s, "packages/", 9) != 0 || n >= sizeof ((UpdPkg *) 0)->url || n < 14 || strcmp(s + n - 5, ".kpkg") != 0) return 0;
	if (strstr(s, "..")) return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr("._-/", *s))) return 0;
	return 1;
}

static int has_platform(const cJSON *art)
{
	const cJSON *pl, *ps = cJSON_GetObjectItemCaseSensitive(art, "supported_platforms");
	cJSON_ArrayForEach(pl, ps)
		if (cJSON_IsString(pl) && !strcmp(pl->valuestring, "kindlehf")) return 1;
	return 0;
}

/* [Y,M,P] → "Y.M.P"; 0 if not a short list of small non-negative integers */
static int fmt_version(const cJSON *v, char *out, int cap)
{
	const cJSON *x;
	int k = 0, len = 0;
	out[0] = '\0';
	if (!cJSON_IsArray(v)) return 0;
	cJSON_ArrayForEach(x, v) {
		if (!cJSON_IsNumber(x) || x->valuedouble < 0 || x->valuedouble > 999999 || ++k > 4) return 0;
		int w = snprintf(out + len, (size_t) (cap - len), "%s%d", len ? "." : "", (int) x->valuedouble);
		if (w < 0 || w >= cap - len) return 0;
		len += w;
	}
	return k > 0;
}

int upd_ver_cmp(const char *a, const char *b)
{
	while (*a || *b) {
		long x = strtol(a, (char **) &a, 10), y = strtol(b, (char **) &b, 10);
		if (x != y) return x < y ? -1 : 1;
		if (*a == '.') a++;
		if (*b == '.') b++;
		if (!*a && !*b) break;
		if ((*a && (*a < '0' || *a > '9')) || (*b && (*b < '0' || *b > '9'))) break; /* garbage: stop */
	}
	return 0;
}

int upd_parse_repo(const char *json, UpdPkg *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *pkgs = root ? cJSON_GetObjectItemCaseSensitive(root, "packages") : NULL, *pk;
	if (!cJSON_IsObject(pkgs)) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	cJSON_ArrayForEach(pk, pkgs) {
		if (n >= max) break;
		if (!pk->string || !safe_id(pk->string)) continue;
		UpdPkg p;
		memset(&p, 0, sizeof p);
		const cJSON *art, *nm = cJSON_GetObjectItemCaseSensitive(pk, "name"), *ds = cJSON_GetObjectItemCaseSensitive(pk, "description");
		cJSON_ArrayForEach(art, cJSON_GetObjectItemCaseSensitive(pk, "artifacts")) {
			const cJSON *u = cJSON_GetObjectItemCaseSensitive(art, "url");
			char v[24];
			if (!cJSON_IsString(u) || !safe_url(u->valuestring) || !has_platform(art) ||
			    !fmt_version(cJSON_GetObjectItemCaseSensitive(art, "version"), v, sizeof v))
				continue;
			if (p.ver[0] && upd_ver_cmp(v, p.ver) <= 0) continue;
			snprintf(p.ver, sizeof p.ver, "%s", v);
			snprintf(p.url, sizeof p.url, "%s", u->valuestring);
		}
		if (!p.ver[0]) continue;
		snprintf(p.id, sizeof p.id, "%s", pk->string);
		snprintf(p.name, sizeof p.name, "%s", cJSON_IsString(nm) ? nm->valuestring : p.id);
		if (cJSON_IsString(ds)) snprintf(p.desc, sizeof p.desc, "%s", ds->valuestring);
		if (!strcmp(p.id, "kindle-apps-core")) { /* the runtime first */
			memmove(&out[1], &out[0], (size_t) n * sizeof *out);
			out[0] = p;
		} else {
			out[n] = p;
		}
		n++;
	}
	cJSON_Delete(root);
	return n;
}

/* where an installed package's files live (to recognise installs made before versions/ existed) */
void upd_app_dir(const char *root, const char *id, char *out, int cap)
{
	const char *x = strncmp(id, "kindle-apps-", 12) ? id : id + 12;
	if (!strcmp(x, "core")) snprintf(out, (size_t) cap, "%s/bin/hub", root);
	else if (!strcmp(x, "2048")) snprintf(out, (size_t) cap, "%s/apps/game2048", root);
	else snprintf(out, (size_t) cap, "%s/apps/%s", root, x);
}

void upd_read_installed(const char *root, UpdPkg *p, int n)
{
	for (int i = 0; i < n; i++) {
		char path[400], v[sizeof p->installed] = "";
		snprintf(path, sizeof path, "%s/versions/%s", root, p[i].id);
		FILE *f = fopen(path, "r");
		if (f) {
			if (!fgets(v, sizeof v, f)) v[0] = '\0';
			fclose(f);
			v[strcspn(v, "\r\n")] = '\0';
		}
		struct stat st;
		upd_app_dir(root, p[i].id, path, sizeof path);
		if (stat(path, &st) != 0) v[0] = '\0';  /* uninstalled (its version file may be left behind) */
		else if (!v[0]) snprintf(v, sizeof v, "?");
		snprintf(p[i].installed, sizeof p[i].installed, "%s", v);
	}
}

int upd_state(const UpdPkg *p)
{
	if (!p->installed[0]) return UPD_NEW;
	if (!strcmp(p->installed, "?") || upd_ver_cmp(p->installed, p->ver) < 0) return UPD_UPDATE;
	return UPD_CURRENT;
}

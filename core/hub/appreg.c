#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appreg.h"

static void ini_value(const char *text, const char *key, char *out, size_t cap)
{
	size_t kl = strlen(key);
	out[0] = '\0';
	for (const char *line = text; *line;) {
		const char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t) (nl - line) : strlen(line);
		if (len > kl && strncmp(line, key, kl) == 0 && line[kl] == '=') {
			size_t vl = len - kl - 1;
			if (vl >= cap) vl = cap - 1;
			memcpy(out, line + kl + 1, vl);
			out[vl] = '\0';
			while (vl && (out[vl - 1] == '\r' || out[vl - 1] == ' ')) out[--vl] = '\0';
			return;
		}
		if (!nl) break;
		line = nl + 1;
	}
}

static int cmp_app(const void *a, const void *b)
{
	const HubApp *x = a, *y = b;
	if (x->order != y->order) return x->order < y->order ? -1 : 1;
	return strcmp(x->name, y->name);
}

int hub_scan_apps(const char *root, HubApp *out, int max)
{
	char dir[256];
	snprintf(dir, sizeof dir, "%s/apps", root);
	DIR *d = opendir(dir);
	if (!d) return 0;
	int n = 0;
	struct dirent *de;
	HubApp all[64];
	while ((de = readdir(d)) && n < 64) {
		size_t idlen = strlen(de->d_name);
		if (de->d_name[0] == '.' || idlen >= sizeof all[0].id) continue;
		char path[512], text[2048], exec[200], order[16];
		if (snprintf(path, sizeof path, "%s/%s/app.ini", dir, de->d_name) >= (int) sizeof path) continue;
		FILE *f = fopen(path, "r");
		if (!f) continue;
		size_t len = fread(text, 1, sizeof text - 1, f);
		fclose(f);
		text[len] = '\0';
		ini_value(text, "exec", exec, sizeof exec);
		if (!exec[0] || strchr(exec, '/')) continue; /* exec must name a file inside the app dir */
		HubApp *a = &all[n];
		memcpy(a->id, de->d_name, idlen + 1);
		ini_value(text, "name", a->name, sizeof a->name);
		if (!a->name[0]) memcpy(a->name, a->id, idlen + 1); /* id (< 32) always fits name (48) */
		if (snprintf(a->exec, sizeof a->exec, "%s/%s/%s", dir, de->d_name, exec) >= (int) sizeof a->exec) continue;
		ini_value(text, "order", order, sizeof order);
		a->order = order[0] ? atoi(order) : 100;
		n++;
	}
	closedir(d);
	qsort(all, (size_t) n, sizeof all[0], cmp_app);
	if (n > max) n = max;
	memcpy(out, all, (size_t) n * sizeof all[0]);
	return n;
}

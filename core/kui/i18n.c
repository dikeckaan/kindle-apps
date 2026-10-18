/* Run-time translation: catalogs $KUI_ROOT/lang/<code>.txt hold "source<TAB>translation" lines (\n, \t, \\ escaped).
 * Turkish is the source language; a missing text falls back to English, then to the Turkish source. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"

typedef struct { char *buf; const char **src, **dst; unsigned *slot; unsigned cap; int n; } Catalog;

static Catalog primary, fallback;
static char cur_lang[8] = "tr";

static const KuiLang langs[] = {
	{ "tr", "Türkçe" },   { "en", "English" },   { "de", "Deutsch" },    { "fr", "Français" },    { "es", "Español" },
	{ "it", "Italiano" }, { "pt", "Português" }, { "nl", "Nederlands" }, { "pl", "Polski" },      { "cs", "Čeština" },
	{ "ro", "Română" },   { "hu", "Magyar" },    { "sv", "Svenska" },    { "nb", "Norsk bokmål" }, { "da", "Dansk" },
	{ "fi", "Suomi" },    { "el", "Ελληνικά" },  { "ru", "Русский" },    { "uk", "Українська" },  { "az", "Azərbaycanca" },
};

const KuiLang *kui_languages(int *n)
{
	*n = (int) (sizeof langs / sizeof langs[0]);
	return langs;
}

static int known(const char *code)
{
	for (size_t i = 0; i < sizeof langs / sizeof langs[0]; i++)
		if (!strcmp(langs[i].code, code)) return 1;
	return 0;
}

static unsigned hash(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) h = (h ^ (unsigned char) *s) * 16777619u;
	return h;
}

static void cat_free(Catalog *c)
{
	free(c->buf);
	free((void *) c->src);
	free((void *) c->dst);
	free(c->slot);
	memset(c, 0, sizeof *c);
}

/* in-place unescape of \n \t \\ */
static void unescape(char *s)
{
	char *o = s;
	for (; *s; s++) {
		if (*s == '\\' && s[1]) {
			s++;
			*o++ = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s;
		} else {
			*o++ = *s;
		}
	}
	*o = '\0';
}

static const char *cat_get(const Catalog *c, const char *src)
{
	if (!c->n) return NULL;
	for (unsigned i = hash(src) & (c->cap - 1);; i = (i + 1) & (c->cap - 1)) {
		unsigned k = c->slot[i];
		if (!k) return NULL;
		if (!strcmp(c->src[k - 1], src)) return c->dst[k - 1];
	}
}

static void cat_load(Catalog *c, const char *code)
{
	cat_free(c);
	const char *root = getenv("KUI_ROOT");
	char path[300];
	snprintf(path, sizeof path, "%s/lang/%s.txt", root && *root ? root : "/mnt/us/kindle-apps", code);
	FILE *f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 4 * 1024 * 1024 || !(c->buf = malloc((size_t) size + 1)) || fread(c->buf, 1, (size_t) size, f) != (size_t) size) {
		fclose(f);
		cat_free(c);
		return;
	}
	fclose(f);
	c->buf[size] = '\0';
	int lines = 1;
	for (long i = 0; i < size; i++) lines += c->buf[i] == '\n';
	c->cap = 16;
	while (c->cap < (unsigned) lines * 2) c->cap <<= 1;
	c->src = malloc(sizeof *c->src * (size_t) lines);
	c->dst = malloc(sizeof *c->dst * (size_t) lines);
	c->slot = calloc(c->cap, sizeof *c->slot);
	if (!c->src || !c->dst || !c->slot) {
		cat_free(c);
		return;
	}
	for (char *p = c->buf; p && *p;) {
		char *nl = strchr(p, '\n'), *line = p;
		if (nl) *nl = '\0';
		p = nl ? nl + 1 : NULL;
		size_t len = strlen(line);
		if (len && line[len - 1] == '\r') line[len - 1] = '\0';
		char *tab = strchr(line, '\t');
		if (line[0] == '#' || !tab || tab == line || !tab[1]) continue;
		*tab = '\0';
		unescape(line);
		unescape(tab + 1);
		if (cat_get(c, line)) continue; /* first definition wins */
		unsigned i = hash(line) & (c->cap - 1);
		while (c->slot[i]) i = (i + 1) & (c->cap - 1);
		c->src[c->n] = line;
		c->dst[c->n] = tab + 1;
		c->slot[i] = (unsigned) ++c->n;
	}
}

const char *kui_t(const char *src)
{
	const char *t = cat_get(&primary, src);
	if (!t) t = cat_get(&fallback, src);
	return t ? t : src;
}

const char *kui_lang(void) { return cur_lang; }

void kui_i18n_set(const char *code)
{
	if (!code || !known(code)) code = "tr";
	snprintf(cur_lang, sizeof cur_lang, "%s", code);
	cat_free(&primary);
	cat_free(&fallback);
	if (strcmp(code, "tr") != 0) {
		cat_load(&primary, code);
		if (strcmp(code, "en") != 0) cat_load(&fallback, "en");
	}
	/* number format of the language: decimal mark and thousands separator */
	static const char *const comma_dot[] = { "tr", "de", "it", "es", "pt", "nl", "ro", "da", "el", "az", NULL };
	const char *dec = ",", *th = " ";
	if (!strcmp(code, "en")) dec = ".", th = ",";
	for (int i = 0; comma_dot[i]; i++)
		if (!strcmp(code, comma_dot[i])) th = ".";
	kui_set_num_format(dec, th);
	kui_set_text_hook(strcmp(code, "tr") ? kui_t : NULL);
}

static void lang_file(char *out, int cap)
{
	const char *root = getenv("KUI_ROOT");
	snprintf(out, (size_t) cap, "%s/data/lang", root && *root ? root : "/mnt/us/kindle-apps");
}

void kui_i18n_load_saved(void)
{
	char path[300], buf[16];
	lang_file(path, sizeof path);
	if (kui_read_file(path, buf, sizeof buf) > 0) buf[strcspn(buf, "\r\n ")] = '\0';
	else buf[0] = '\0';
	kui_i18n_set(buf[0] ? buf : "tr");
}

int kui_i18n_save(const char *code)
{
	char path[300], buf[16];
	if (!known(code)) return -1;
	lang_file(path, sizeof path);
	snprintf(buf, sizeof buf, "%s\n", code);
	return kui_write_atomic(path, buf, (int) strlen(buf));
}

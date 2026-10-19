/* Tarifler: pure logic (no drawing). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "tarif.h"
#include "vendor/cjson/cJSON.h"

/* ---- recipe sets ---- */

static int split_tabs(char *s, char **f, int max)
{
	int n = 0;
	f[n++] = s;
	for (char *p = s; *p && n < max; p++)
		if (*p == '\t') {
			*p = '\0';
			f[n++] = p + 1;
		}
	return n;
}

int tarif_parse(char *buf, TarifDb *db)
{
	memset(db, 0, sizeof *db);
	int nr = 0, nl = 0;
	for (char *p = buf; *p; p++)
		if (p == buf || p[-1] == '\n') {
			if (p[0] == '@' && p[1] == 'R') nr++;
			else if (*p == '+' || *p == '-' || *p == '*') nl++;
		}
	if (!nr) return -1;
	db->r = calloc((size_t) nr, sizeof *db->r);
	db->line = calloc((size_t) nl + 1, sizeof *db->line);
	db->kind = calloc((size_t) nl + 1, 1);
	if (!db->r || !db->line || !db->kind) goto bad;
	TarifRecipe *cur = NULL;
	for (char *p = buf; *p;) {
		char *e = strchr(p, '\n');
		char *next = e ? e + 1 : p + strlen(p);
		if (e) *e = '\0';
		if (e && e > p && e[-1] == '\r') e[-1] = '\0';
		if (p[0] == '@' && p[1] == 'R' && p[2] == '\t') {
			char *f[9];
			if (split_tabs(p + 2, f, 9) != 8) goto bad;
			cur = &db->r[db->n++];
			*cur = (TarifRecipe){ f[1], f[2], f[3], f[4], f[6], f[7], "", atoi(f[5]), db->n_line, 0, 0, db->n_line, 0 };
		} else if (*p == '#' && !cur) {
			/* comment */
		} else if (*p && cur) {
			char k = *p;
			if (k == '=') cur->intro = p + 1;
			else if (k == '+' || k == '-' || k == '*') {
				if (k != '*' && cur->n_steps) goto bad; /* ingredients come before the steps */
				db->line[db->n_line] = p + 1;
				db->kind[db->n_line++] = k;
				if (k == '*') cur->n_steps++;
				else {
					cur->n_lines++;
					cur->step0 = db->n_line;
					cur->n_ing += k == '-';
				}
			} else goto bad;
		} else if (*p) goto bad;
		p = next;
	}
	db->buf = buf;
	return db->n;
bad:
	free(db->r);
	free(db->line);
	free(db->kind);
	memset(db, 0, sizeof *db);
	return -1;
}

int tarif_load(const char *path, TarifDb *db)
{
	memset(db, 0, sizeof *db);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = n >= 0 ? malloc((size_t) n + 1) : NULL;
	if (!buf || fread(buf, 1, (size_t) n, f) != (size_t) n) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[n] = '\0';
	int r = tarif_parse(buf, db);
	if (r < 0) free(buf);
	return r;
}

void tarif_free(TarifDb *db)
{
	free(db->buf);
	free(db->r);
	free(db->line);
	free(db->kind);
	memset(db, 0, sizeof *db);
}

int tarif_find_url(const TarifDb *db, const char *url)
{
	for (int i = 0; i < db->n; i++)
		if (!strcmp(db->r[i].url, url)) return i;
	return -1;
}

int tarif_has_tag(const TarifRecipe *r, char tag) { return tag && strchr(r->tags, tag) != NULL; }
int tarif_base_servings(const TarifRecipe *r) { return r->servings > 0 ? r->servings : 4; }

/* ---- writing records ---- */

void tarif_buf_add(TarifBuf *b, const char *s, int n)
{
	if (n < 0) n = (int) strlen(s);
	if (b->len + n + 1 > b->cap) {
		int cap = b->cap ? b->cap : 256;
		while (cap < b->len + n + 1) cap *= 2;
		char *p = realloc(b->s, (size_t) cap);
		if (!p) return;
		b->s = p;
		b->cap = cap;
	}
	memcpy(b->s + b->len, s, (size_t) n);
	b->len += n;
	b->s[b->len] = '\0';
}

void tarif_buf_free(TarifBuf *b)
{
	free(b->s);
	memset(b, 0, sizeof *b);
}

static void add_field(TarifBuf *b, const char *s, int n)
{
	if (n < 0) n = (int) strlen(s);
	for (int i = 0; i < n; i++) {
		char c = s[i] == '\t' || s[i] == '\n' || s[i] == '\r' ? ' ' : s[i];
		tarif_buf_add(b, &c, 1);
	}
}

void tarif_rec_head(TarifBuf *b, const char *lang, const char *title, const char *cuisine, const char *tags, int servings, const char *url,
		    const char *licence)
{
	char num[16];
	snprintf(num, sizeof num, "%d", servings);
	const char *f[7] = { lang, title, cuisine, tags, num, url, licence };
	tarif_buf_add(b, "@R", 2);
	for (int i = 0; i < 7; i++) {
		tarif_buf_add(b, "\t", 1);
		add_field(b, f[i], -1);
	}
	tarif_buf_add(b, "\n", 1);
}

void tarif_rec_line(TarifBuf *b, char kind, const char *text, int n)
{
	tarif_buf_add(b, &kind, 1);
	add_field(b, text, n);
	tarif_buf_add(b, "\n", 1);
}

/* ---- folding ---- */

static int fold_cp(int cp)
{
	if (cp < 0x80) return tolower(cp);
	switch (cp) {
	case 0xE7: case 0xC7: return 'c';
	case 0x11F: case 0x11E: return 'g';
	case 0x131: case 0x130: return 'i';
	case 0xF6: case 0xD6: case 0xF4: case 0xD4: case 0xF3: case 0xF2: case 0xF5: case 0xF8: return 'o';
	case 0x15F: case 0x15E: return 's';
	case 0xFC: case 0xDC: case 0xFB: case 0xDB: case 0xFA: case 0xF9: return 'u';
	case 0xE2: case 0xC2: case 0xE0: case 0xE1: case 0xE4: case 0xE3: case 0xE5: case 0xC0: case 0xC1: case 0xC4: return 'a';
	case 0xE9: case 0xE8: case 0xEA: case 0xEB: case 0xC9: case 0xC8: return 'e';
	case 0xEE: case 0xCE: case 0xED: case 0xEC: case 0xEF: return 'i';
	case 0xF1: case 0xD1: return 'n';
	case 0x307: return -1; /* combining dot (i̇) */
	}
	return cp;
}

void tarif_fold(const char *in, char *out, int cap)
{
	int o = 0, space = 1;
	if (cap <= 0) return;
	for (const char *p = in; *p && o < cap - 5;) {
		int cp;
		p += kui_utf8_decode(p, &cp);
		int f = fold_cp(cp);
		if (f < 0) continue;
		if (f < 0x80 && !isalnum(f)) {
			if (!space) out[o++] = ' ';
			space = 1;
			continue;
		}
		if (f >= 0x80 && (f == 0x2019 || f == 0x2013 || f == 0x2014 || f == 0xBD || f == 0xBC || f == 0xBE || (f >= 0x2150 && f <= 0x215E) ||
				  f == 0xB0 || f == 0xA0 || f == 0x201C || f == 0x201D)) {
			if (!space) out[o++] = ' ';
			space = 1;
			continue;
		}
		if (f < 0x80) out[o++] = (char) f;
		else o += kui_utf8_encode(f, out + o);
		space = 0;
	}
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = '\0';
}

/* word w (folded, no spaces) starts a word of folded text h */
static int has_word_prefix(const char *h, const char *w)
{
	size_t n = strlen(w);
	if (!n) return 0;
	for (const char *p = h; (p = strstr(p, w)); p++)
		if (p == h || p[-1] == ' ') return 1;
	return 0;
}

#define MAX_WORDS 10
static int split_words(char *s, char **w, int max) /* in place, at spaces */
{
	int n = 0;
	for (char *p = strtok(s, " "); p && n < max; p = strtok(NULL, " ")) w[n++] = p;
	return n;
}

/* ---- Turkish → English ingredient names (folded) ---- */
static const char *const SYN[][2] = {
	{ "domates", "tomato" }, { "sogan", "onion" }, { "sarimsak", "garlic" }, { "patates", "potato" }, { "havuc", "carrot" },
	{ "biber", "pepper" }, { "patlican", "eggplant aubergine" }, { "kabak", "zucchini courgette squash pumpkin" }, { "ispanak", "spinach" },
	{ "mercimek", "lentil" }, { "nohut", "chickpea garbanzo" }, { "fasulye", "bean" }, { "pirinc", "rice" }, { "bulgur", "bulgur" },
	{ "un", "flour" }, { "seker", "sugar" }, { "tuz", "salt" }, { "yumurta", "egg" }, { "sut", "milk" }, { "yogurt", "yogurt yoghurt" },
	{ "peynir", "cheese" }, { "tereyag", "butter" }, { "zeytinyag", "olive oil" }, { "yag", "oil" }, { "tavuk", "chicken" },
	{ "et", "meat beef" }, { "kiyma", "ground minced mince" }, { "kuzu", "lamb" }, { "dana", "beef veal" }, { "balik", "fish" },
	{ "limon", "lemon" }, { "elma", "apple" }, { "muz", "banana" }, { "cilek", "strawberr" }, { "ceviz", "walnut" }, { "findik", "hazelnut" },
	{ "badem", "almond" }, { "bal", "honey" }, { "makarna", "pasta noodle spaghetti macaroni" }, { "ekmek", "bread" },
	{ "mantar", "mushroom" }, { "misir", "corn maize" }, { "bezelye", "pea" }, { "lahana", "cabbage" }, { "karnabahar", "cauliflower" },
	{ "karnibahar", "cauliflower" }, { "brokoli", "broccoli" }, { "salatalik", "cucumber" }, { "marul", "lettuce" },
	{ "maydanoz", "parsley" }, { "nane", "mint" }, { "dereotu", "dill" }, { "kekik", "thyme oregano" }, { "tarcin", "cinnamon" },
	{ "kimyon", "cumin" }, { "zencefil", "ginger" }, { "cikolata", "chocolate" }, { "kakao", "cocoa" }, { "krema", "cream" },
	{ "irmik", "semolina" }, { "portakal", "orange" }, { "karides", "shrimp prawn" }, { "somon", "salmon" }, { "ton", "tuna" },
	{ "sucuk", "sausage" }, { "hindi", "turkey" }, { "ordek", "duck" }, { "nisasta", "starch" }, { "maya", "yeast" },
	{ "sirke", "vinegar" }, { "salca", "paste" }, { "hardal", "mustard" }, { "zeytin", "olive" }, { "avokado", "avocado" },
	{ "bamya", "okra" }, { "enginar", "artichoke" }, { "pirasa", "leek" }, { "kereviz", "celery" }, { "turp", "radish" },
	{ "pancar", "beet" }, { "armut", "pear" }, { "kayisi", "apricot" }, { "uzum", "grape raisin" }, { "incir", "fig" },
	{ "nar", "pomegranate" }, { "seftali", "peach" }, { "visne", "cherr" }, { "kiraz", "cherr" }, { "hindistancevizi", "coconut" },
	{ "susam", "sesame" }, { "tahin", "tahini" }, { "fistik", "pistachio peanut" }, { "kabartma", "baking" }, { "vanilya", "vanilla" },
	{ "karabiber", "black pepper" }, { "pul", "chili flakes" }, { "kirmizi", "red" }, { "yesil", "green" }, { "taze", "fresh" },
	{ "corba", "soup" }, { "tatli", "dessert sweet" }, { "salata", "salad" }, { "kek", "cake" }, { "kurabiye", "cookie biscuit" },
	{ "pilav", "pilaf rice" }, { "kofte", "meatball" }, { "borek", "pastry pie" }, { "tost", "toast" }, { "sandvic", "sandwich" },
	{ "omlet", "omelet omelette" }, { "pasta", "cake" }, { "sos", "sauce" }, { "kizartma", "fried" }, { "firin", "baked" },
	{ "kuru", "dried" }, { "su", "water" }, { "kahve", "coffee" }, { "cay", "tea" }, { "erik", "plum" }, { "ananas", "pineapple" },
	{ "mango", "mango" }, { "tofu", "tofu" }, { "kinoa", "quinoa" }, { "yulaf", "oat" }, { "arpa", "barley" }, { "mercan", "bream" },
};
#define N_SYN ((int) (sizeof SYN / sizeof SYN[0]))

/* English words for a folded Turkish word (the longest table key the word starts with), or NULL */
static const char *syn_for(const char *w)
{
	int best = -1;
	size_t bl = 0;
	for (int i = 0; i < N_SYN; i++) {
		size_t kl = strlen(SYN[i][0]);
		if (!strncmp(w, SYN[i][0], kl) && kl > bl && (kl >= 4 || strlen(w) == kl)) best = i, bl = kl;
	}
	return best >= 0 ? SYN[best][1] : NULL;
}

int tarif_translate_query(const char *query, char *out, int cap)
{
	char f[256], *w[MAX_WORDS];
	tarif_fold(query, f, sizeof f);
	int n = split_words(f, w, MAX_WORDS), any = 0, o = 0;
	out[0] = '\0';
	for (int i = 0; i < n; i++) {
		const char *s = syn_for(w[i]);
		if (s) any = 1;
		o += snprintf(out + o, (size_t) (cap - o > 0 ? cap - o : 0), "%s%s", o ? " " : "", s ? s : w[i]);
		if (o >= cap) break;
	}
	return any;
}

/* ---- search ---- */

static const TarifDb *sort_db;
static const int *sort_rank;

static int cmp_title(const void *a, const void *b)
{
	int i = *(const int *) a, j = *(const int *) b;
	const TarifRecipe *x = &sort_db->r[i], *y = &sort_db->r[j];
	if (sort_rank && sort_rank[i] != sort_rank[j]) return sort_rank[i] - sort_rank[j];
	int lx = strcmp(x->lang, "tr") != 0, ly = strcmp(y->lang, "tr") != 0;
	if (lx != ly) return lx - ly;
	char fx[200], fy[200];
	tarif_fold(x->title, fx, sizeof fx);
	tarif_fold(y->title, fy, sizeof fy);
	int c = strcmp(fx, fy);
	return c ? c : i - j;
}

static void sort_ids(const TarifDb *db, int *ids, int n, const int *rank)
{
	sort_db = db;
	sort_rank = rank;
	qsort(ids, (size_t) n, sizeof *ids, cmp_title);
	sort_rank = NULL;
}

int tarif_search(const TarifDb *db, const char *query, int *out, int max)
{
	char q[256], *w[MAX_WORDS];
	tarif_fold(query, q, sizeof q);
	int nw = split_words(q, w, MAX_WORDS), n = 0;
	if (!nw || !db->n) return 0;
	int *rank = calloc((size_t) db->n, sizeof *rank);
	if (!rank) return 0;
	for (int i = 0; i < db->n && n < max; i++) {
		char h[600], t[300], c[200];
		tarif_fold(db->r[i].title, t, sizeof t);
		tarif_fold(db->r[i].cuisine, c, sizeof c);
		snprintf(h, sizeof h, "%s %s", t, c);
		int ok = 1;
		for (int k = 0; k < nw && ok; k++) ok = has_word_prefix(h, w[k]);
		if (!ok) continue;
		rank[i] = strncmp(t, w[0], strlen(w[0])) != 0;
		out[n++] = i;
	}
	sort_ids(db, out, n, rank);
	free(rank);
	return n;
}

static const int *ing_matched, *ing_total;
static int cmp_ing(const void *a, const void *b)
{
	int i = *(const int *) a, j = *(const int *) b;
	if (ing_matched[i] != ing_matched[j]) return ing_matched[j] - ing_matched[i];
	int lx = strcmp(sort_db->r[i].lang, "tr") != 0, ly = strcmp(sort_db->r[j].lang, "tr") != 0;
	if (lx != ly) return lx - ly;
	int ox = ing_total[i] - ing_matched[i], oy = ing_total[j] - ing_matched[j];
	return ox != oy ? ox - oy : i - j;
}

int tarif_by_ingredients(const TarifDb *db, const char *query, int *out, int *matched, int max, int *n_words)
{
	char q[256], *w[MAX_WORDS], *alt[MAX_WORDS][6], syn[MAX_WORDS][80];
	int nalt[MAX_WORDS];
	tarif_fold(query, q, sizeof q);
	int nw = 0;
	for (char *p = strtok(q, " "); p && nw < MAX_WORDS; p = strtok(NULL, " "))
		if (strlen(p) >= 2 && strcmp(p, "ve") && strcmp(p, "and") && strcmp(p, "ile")) w[nw++] = p;
	if (n_words) *n_words = nw;
	if (!nw || !db->n) return 0;
	for (int k = 0; k < nw; k++) {
		alt[k][0] = w[k];
		nalt[k] = 1;
		const char *s = syn_for(w[k]);
		if (!s) continue;
		snprintf(syn[k], sizeof syn[k], "%s", s);
		/* multi-word translations ("olive oil", "black pepper") are kept whole; others split into alternatives */
		if (!strcmp(s, "olive oil") || !strcmp(s, "black pepper") || !strcmp(s, "chili flakes")) {
			alt[k][nalt[k]++] = syn[k];
			continue;
		}
		for (char *t = strtok(syn[k], " "); t && nalt[k] < 6; t = strtok(NULL, " ")) alt[k][nalt[k]++] = t;
	}
	int *m = calloc((size_t) db->n, sizeof *m), *tot = calloc((size_t) db->n, sizeof *tot), n = 0;
	if (!m || !tot) {
		free(m);
		free(tot);
		return 0;
	}
	char *h = malloc(16384);
	for (int i = 0; h && i < db->n; i++) {
		const TarifRecipe *r = &db->r[i];
		int o = 0;
		for (int l = r->line0; l < r->line0 + r->n_lines && o < 15000; l++) {
			if (db->kind[l] != '-') continue;
			tarif_fold(db->line[l], h + o, 16384 - o - 2);
			o += (int) strlen(h + o);
			h[o++] = ' ';
			h[o] = '\0';
		}
		h[o] = '\0';
		for (int k = 0; k < nw; k++) {
			int hit = 0;
			for (int a = 0; a < nalt[k] && !hit; a++)
				hit = strchr(alt[k][a], ' ') ? strstr(h, alt[k][a]) != NULL : has_word_prefix(h, alt[k][a]);
			m[i] += hit;
		}
		tot[i] = r->n_ing;
		if (m[i] && n < max) out[n++] = i;
	}
	free(h);
	sort_db = db;
	ing_matched = m;
	ing_total = tot;
	qsort(out, (size_t) n, sizeof *out, cmp_ing);
	if (matched)
		for (int i = 0; i < n; i++) matched[i] = m[out[i]];
	free(m);
	free(tot);
	return n;
}

int tarif_filter(const TarifDb *db, const char *cuisine, char tag, int *out, int max)
{
	int n = 0, neg = cuisine && cuisine[0] == '!';
	const char *c = neg ? cuisine + 1 : cuisine;
	for (int i = 0; i < db->n && n < max; i++) {
		const TarifRecipe *r = &db->r[i];
		if (c && (strcmp(r->cuisine, c) == 0) == neg) continue;
		if (tag && !tarif_has_tag(r, tag)) continue;
		out[n++] = i;
	}
	sort_ids(db, out, n, NULL);
	return n;
}

int tarif_cuisines(const TarifDb *db, const char **names, int *counts, int max)
{
	int n = 0;
	for (int i = 0; i < db->n; i++) {
		const char *c = db->r[i].cuisine;
		if (!*c || !strcmp(c, "Türkiye")) continue;
		int k = 0;
		while (k < n && strcmp(names[k], c)) k++;
		if (k == n) {
			if (n == max) continue;
			names[n] = c;
			counts[n++] = 0;
		}
		counts[k]++;
	}
	for (int i = 1; i < n; i++) /* insertion sort: count desc, then name */
		for (int j = i; j > 0; j--) {
			char a[100], b[100];
			tarif_fold(names[j - 1], a, sizeof a);
			tarif_fold(names[j], b, sizeof b);
			if (counts[j - 1] > counts[j] || (counts[j - 1] == counts[j] && strcmp(a, b) <= 0)) break;
			const char *t = names[j];
			names[j] = names[j - 1];
			names[j - 1] = t;
			int x = counts[j];
			counts[j] = counts[j - 1];
			counts[j - 1] = x;
		}
	return n;
}

/* ---- quantities ---- */

static int frac_glyph(const char *s, double *v)
{
	static const struct { const char *g; double v; } G[] = {
		{ "½", 0.5 }, { "¼", 0.25 }, { "¾", 0.75 }, { "⅓", 1.0 / 3 }, { "⅔", 2.0 / 3 }, { "⅛", 0.125 },
	};
	for (size_t i = 0; i < sizeof G / sizeof G[0]; i++) {
		size_t n = strlen(G[i].g);
		if (!strncmp(s, G[i].g, n)) {
			*v = G[i].v;
			return (int) n;
		}
	}
	return 0;
}

static int word_end(const char *s) /* s is followed by a non-letter */
{
	unsigned char c = (unsigned char) *s;
	return !c || (c < 0x80 && !isalpha(c));
}

/* one number at s: digits with a decimal part, a/b, a glyph, digits + glyph, digits + a/b, or a Turkish number word */
static int one_number(const char *s, double *v, int *frac)
{
	const char *p = s;
	double g;
	int k;
	if ((k = frac_glyph(p, &g))) {
		*v = g;
		*frac = 1;
		return k;
	}
	if (!isdigit((unsigned char) *p)) {
		static const struct { const char *w; double v; } W[] = {
			{ "yarım", 0.5 }, { "Yarım", 0.5 }, { "çeyrek", 0.25 }, { "Çeyrek", 0.25 }, { "bir", 1 }, { "Bir", 1 }, { "iki", 2 }, { "İki", 2 },
			{ "üç", 3 }, { "Üç", 3 }, { "dört", 4 }, { "Dört", 4 }, { "beş", 5 }, { "Beş", 5 }, { "altı", 6 }, { "Altı", 6 },
		};
		for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
			size_t n = strlen(W[i].w);
			if (!strncmp(p, W[i].w, n) && p[n] == ' ') {
				*v = W[i].v;
				*frac = W[i].v < 1;
				return (int) n;
			}
		}
		return 0;
	}
	double x = 0;
	while (isdigit((unsigned char) *p)) x = x * 10 + (*p++ - '0');
	*frac = 0;
	if ((*p == ',' || *p == '.') && isdigit((unsigned char) p[1])) {
		double d = 0.1;
		for (p++; isdigit((unsigned char) *p); p++, d /= 10) x += (*p - '0') * d;
	} else if (*p == '/' && isdigit((unsigned char) p[1])) {
		int den = 0;
		for (p++; isdigit((unsigned char) *p); p++) den = den * 10 + (*p - '0');
		if (!den) return 0;
		x /= den;
		*frac = 1;
	} else if ((k = frac_glyph(p, &g))) { /* 1½ */
		x += g;
		p += k;
		*frac = 1;
	} else if (*p == ' ' && (k = frac_glyph(p + 1, &g))) { /* 1 ½ */
		x += g;
		p += 1 + k;
		*frac = 1;
	} else if (*p == ' ' && isdigit((unsigned char) p[1])) { /* 1 1/2 */
		const char *q = p + 1;
		int a = 0, b = 0;
		while (isdigit((unsigned char) *q)) a = a * 10 + (*q++ - '0');
		if (*q == '/' && isdigit((unsigned char) q[1])) {
			for (q++; isdigit((unsigned char) *q); q++) b = b * 10 + (*q - '0');
			if (b && a < b) {
				x += (double) a / b;
				p = q;
				*frac = 1;
			}
		}
	}
	*v = x;
	return (int) (p - s);
}

int tarif_qty_parse(const char *s, double *lo, double *hi, int *frac)
{
	double a, b;
	int fa, fb;
	int n = one_number(s, &a, &fa);
	if (!n) return 0;
	if (!strncmp(s + n, " buçuk", 7) && word_end(s + n + 7)) {
		a += 0.5;
		n += 7;
	}
	*lo = *hi = a;
	*frac = fa;
	/* a range: "2-3", "1 - 1,5", "1–2" */
	const char *p = s + n;
	while (*p == ' ') p++;
	int dash = *p == '-' ? 1 : !strncmp(p, "–", 3) ? 3 : 0;
	if (dash) {
		const char *q = p + dash;
		while (*q == ' ') q++;
		int m = isdigit((unsigned char) *q) || frac_glyph(q, &b) ? one_number(q, &b, &fb) : 0;
		if (m && b > a) {
			*hi = b;
			*frac = fa || fb;
			n = (int) (q + m - s);
		}
	}
	return n;
}

void tarif_qty_format(double v, int frac, int comma, char *out, int cap)
{
	static const struct { double v; const char *g; } G[] = {
		{ 0.5, "½" }, { 0.25, "¼" }, { 0.75, "¾" }, { 1.0 / 3, "⅓" }, { 2.0 / 3, "⅔" },
	};
	double whole = floor(v + 1e-9), rem = v - whole;
	if (rem < 0.02) {
		snprintf(out, (size_t) cap, "%.0f", whole);
		return;
	}
	if (rem > 0.98) {
		snprintf(out, (size_t) cap, "%.0f", whole + 1);
		return;
	}
	if (frac || v < 1)
		for (size_t i = 0; i < sizeof G / sizeof G[0]; i++)
			if (fabs(rem - G[i].v) < 0.02) {
				if (whole > 0) snprintf(out, (size_t) cap, "%.0f%s", whole, G[i].g);
				else snprintf(out, (size_t) cap, "%s", G[i].g);
				return;
			}
	if (v < 1) { /* small amounts read better as kitchen fractions: the nearest quarter, at least ¼ */
		int q = (int) floor(v * 4 + 0.5);
		snprintf(out, (size_t) cap, "%s", q <= 1 ? "¼" : q == 2 ? "½" : q == 3 ? "¾" : "1");
		return;
	}
	if (v >= 10) {
		snprintf(out, (size_t) cap, "%.0f", floor(v + 0.5));
		return;
	}
	double r = floor(v * 10 + 0.5) / 10;
	if (r < 0.1) r = 0.1;
	if (fabs(r - floor(r + 1e-9)) < 1e-9) snprintf(out, (size_t) cap, "%.0f", r);
	else {
		snprintf(out, (size_t) cap, "%.1f", r);
		char *d = strchr(out, '.');
		if (d && comma) *d = ',';
	}
}

static const char *const UNITS[] = { "gr", "g", "gram", "kg", "ml", "lt", "l", "litre", "cl", "oz", "lb", "lbs", "cup", "cups", NULL };

static int unit_follows(const char *p)
{
	while (*p == ' ') p++;
	for (int i = 0; UNITS[i]; i++) {
		size_t n = strlen(UNITS[i]);
		if (!strncmp(p, UNITS[i], n) && (word_end(p + n) || p[n] == '.')) return 1;
	}
	return 0;
}

static void emit_qty(char *out, int cap, int *o, double lo, double hi, int frac, double f, int comma)
{
	char a[32], b[32];
	tarif_qty_format(lo * f, frac, comma, a, sizeof a);
	if (hi > lo) {
		tarif_qty_format(hi * f, frac, comma, b, sizeof b);
		*o += snprintf(out + *o, (size_t) (cap - *o), "%s-%s", a, b);
	} else
		*o += snprintf(out + *o, (size_t) (cap - *o), "%s", a);
	if (*o > cap - 1) *o = cap - 1;
}

int tarif_scale_line(const char *line, double f, int comma, char *out, int cap)
{
	double lo, hi;
	int frac, n, o = 0;
	const char *s = line;
	while (*s == ' ') s++;
	if (fabs(f - 1) > 1e-9 && (n = tarif_qty_parse(s, &lo, &hi, &frac)) > 0 && lo > 0) {
		o = (int) (s - line);
		if (o > cap - 1) o = cap - 1;
		memcpy(out, line, (size_t) o);
		emit_qty(out, cap, &o, lo, hi, frac, f, comma);
		snprintf(out + o, (size_t) (cap - o), "%s", s + n);
		return 1;
	}
	if (fabs(f - 1) > 1e-9)
		for (const char *p = s; *p; p++) { /* "Mercimek 30gr", "Tuz 2 g" */
			if (!isdigit((unsigned char) *p) || (p > line && p[-1] != ' ')) continue;
			n = tarif_qty_parse(p, &lo, &hi, &frac);
			if (n <= 0 || lo <= 0 || !unit_follows(p + n)) continue;
			o = (int) (p - line);
			if (o > cap - 1) o = cap - 1;
			memcpy(out, line, (size_t) o);
			emit_qty(out, cap, &o, lo, hi, frac, f, comma);
			snprintf(out + o, (size_t) (cap - o), "%s", p + n);
			return 1;
		}
	snprintf(out, (size_t) cap, "%s", line);
	return 0;
}

/* ---- step timers ---- */

/* seconds per unit when the folded word w names a time unit, else 0 */
static int unit_seconds(const char *w)
{
	static const struct { const char *u; int s; } U[] = {
		{ "dk", 60 }, { "dak", 60 }, { "dakika", 60 }, { "dakikada", 60 }, { "dakikalik", 60 }, { "min", 60 }, { "mins", 60 }, { "minute", 60 },
		{ "minutes", 60 }, { "saat", 3600 }, { "saatte", 3600 }, { "sa", 3600 }, { "hour", 3600 }, { "hours", 3600 }, { "hr", 3600 },
		{ "hrs", 3600 }, { "saniye", 1 }, { "sn", 1 }, { "second", 1 }, { "seconds", 1 }, { "sec", 1 }, { "secs", 1 },
	};
	for (size_t i = 0; i < sizeof U / sizeof U[0]; i++)
		if (!strcmp(w, U[i].u)) return U[i].s;
	if (!strncmp(w, "dakika", 6)) return 60; /* dakikadan, dakikası … */
	if (!strncmp(w, "saat", 4)) return 3600;
	if (!strncmp(w, "saniye", 6)) return 1;
	return 0;
}

/* a quantity + time unit at p: seconds (upper end of a range) and *len; 0 when not a time */
static int time_at(const char *p, int *len)
{
	double lo, hi;
	int frac, n = tarif_qty_parse(p, &lo, &hi, &frac);
	if (n <= 0 || hi <= 0) return 0;
	const char *q = p + n;
	if (*q == '\'' || *q == '-') q++; /* 10'' / 30-dakika */
	while (*q == ' ') q++;
	char w[32];
	int k = 0;
	while (q[k] && q[k] != ' ' && q[k] != '.' && q[k] != ',' && q[k] != ')' && q[k] != ';' && k < 24) k++;
	char raw[32];
	memcpy(raw, q, (size_t) k);
	raw[k] = '\0';
	tarif_fold(raw, w, sizeof w);
	char *sp = strchr(w, ' ');
	if (sp) *sp = '\0';
	int u = unit_seconds(w);
	if (!u) return 0;
	*len = (int) (q + k - p);
	return (int) (hi * u + 0.5);
}

int tarif_step_seconds(const char *step)
{
	for (const char *p = step; *p; p++) {
		if (p > step && p[-1] != ' ' && p[-1] != '(' && p[-1] != '\n') continue;
		int len, s = time_at(p, &len);
		if (!s) continue;
		if (s >= 3600) { /* "1 saat 30 dakika" */
			const char *q = p + len;
			while (*q == ' ') q++;
			if (!strncmp(q, "ve ", 3)) q += 3;
			else if (!strncmp(q, "and ", 4)) q += 4;
			int l2, s2 = time_at(q, &l2);
			if (s2 && s2 < 3600) s += s2;
		}
		return s;
	}
	return 0;
}

int tarif_timer_add(TarifTimers *ts, const char *name, int seconds, long now)
{
	if (ts->n >= TARIF_MAX_TIMERS || seconds <= 0) return -1;
	TarifTimer *t = &ts->t[ts->n];
	memset(t, 0, sizeof *t);
	snprintf(t->name, sizeof t->name, "%s", name);
	for (char *c = t->name; *c; c++)
		if (*c == '\n' || *c == '\t') *c = ' ';
	t->dur = seconds;
	t->end = now + seconds;
	return ts->n++;
}

void tarif_timer_remove(TarifTimers *ts, int i)
{
	if (i < 0 || i >= ts->n) return;
	memmove(&ts->t[i], &ts->t[i + 1], (size_t) (ts->n - i - 1) * sizeof ts->t[0]);
	ts->n--;
}

long tarif_timer_left(const TarifTimer *t, long now) { return t->end > now ? t->end - now : 0; }

int tarif_timers_ringing(const TarifTimers *ts, long now)
{
	for (int i = 0; i < ts->n; i++)
		if (!ts->t[i].done && ts->t[i].end <= now) return i;
	return -1;
}

void tarif_fmt_clock(long s, char *out, int cap)
{
	if (s < 0) s = 0;
	if (s >= 3600) snprintf(out, (size_t) cap, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
	else snprintf(out, (size_t) cap, "%ld:%02ld", s / 60, s % 60);
}

int tarif_timers_format(const TarifTimers *ts, char *out, int cap)
{
	int o = 0;
	out[0] = '\0';
	for (int i = 0; i < ts->n; i++) {
		int k = snprintf(out + o, (size_t) (cap - o), "%ld %d %d %s\n", ts->t[i].end, ts->t[i].dur, ts->t[i].done, ts->t[i].name);
		if (k < 0 || o + k >= cap) return -1;
		o += k;
	}
	return o;
}

void tarif_timers_parse(const char *buf, TarifTimers *ts)
{
	memset(ts, 0, sizeof *ts);
	for (const char *p = buf; p && *p && ts->n < TARIF_MAX_TIMERS;) {
		const char *e = strchr(p, '\n');
		char line[160];
		int len = e ? (int) (e - p) : (int) strlen(p);
		if (len > (int) sizeof line - 1) len = (int) sizeof line - 1;
		memcpy(line, p, (size_t) len);
		line[len] = '\0';
		long end;
		int dur, done, used = 0;
		if (sscanf(line, "%ld %d %d %n", &end, &dur, &done, &used) == 3 && dur > 0) {
			TarifTimer *t = &ts->t[ts->n++];
			memset(t, 0, sizeof *t);
			t->end = end;
			t->dur = dur;
			t->done = done != 0;
			snprintf(t->name, sizeof t->name, "%s", line + used);
		}
		p = e ? e + 1 : NULL;
	}
}

/* ---- key lists ---- */

void tarif_keys_parse(const char *buf, TarifKeys *k)
{
	tarif_keys_free(k);
	for (const char *p = buf; p && *p;) {
		const char *e = strchr(p, '\n');
		int len = e ? (int) (e - p) : (int) strlen(p);
		while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
		if (len > 0 && k->n < TARIF_MAX_KEYS) {
			char *s = malloc((size_t) len + 1);
			if (s) {
				memcpy(s, p, (size_t) len);
				s[len] = '\0';
				if (tarif_keys_has(k, s)) free(s);
				else k->key[k->n++] = s;
			}
		}
		p = e ? e + 1 : NULL;
	}
}

static int key_index(const TarifKeys *k, const char *key)
{
	for (int i = 0; i < k->n; i++)
		if (!strcmp(k->key[i], key)) return i;
	return -1;
}

int tarif_keys_has(const TarifKeys *k, const char *key) { return key_index(k, key) >= 0; }

int tarif_keys_toggle(TarifKeys *k, const char *key)
{
	int i = key_index(k, key);
	if (i >= 0) {
		free(k->key[i]);
		memmove(&k->key[i], &k->key[i + 1], (size_t) (k->n - i - 1) * sizeof k->key[0]);
		k->n--;
		return 0;
	}
	if (k->n >= TARIF_MAX_KEYS) return -1;
	char *s = strdup(key);
	if (!s) return -1;
	k->key[k->n++] = s;
	return 1;
}

void tarif_keys_touch(TarifKeys *k, const char *key, int keep)
{
	int i = key_index(k, key);
	char *s = i >= 0 ? k->key[i] : strdup(key);
	if (!s) return;
	if (i >= 0) {
		memmove(&k->key[i], &k->key[i + 1], (size_t) (k->n - i - 1) * sizeof k->key[0]);
		k->n--;
	}
	if (keep > TARIF_MAX_KEYS) keep = TARIF_MAX_KEYS;
	while (k->n >= keep && k->n > 0) { /* drop the oldest */
		free(k->key[0]);
		memmove(&k->key[0], &k->key[1], (size_t) (k->n - 1) * sizeof k->key[0]);
		k->n--;
	}
	k->key[k->n++] = s;
}

int tarif_keys_format(const TarifKeys *k, char *out, int cap)
{
	int o = 0;
	out[0] = '\0';
	for (int i = 0; i < k->n; i++) {
		int n = snprintf(out + o, (size_t) (cap - o), "%s\n", k->key[i]);
		if (n < 0 || o + n >= cap) return -1;
		o += n;
	}
	return o;
}

void tarif_keys_free(TarifKeys *k)
{
	for (int i = 0; i < k->n; i++) free(k->key[i]);
	k->n = 0;
}

/* ---- cuisines: English adjective (TheMealDB area, Wikibooks "<X> recipes") → Turkish country name ---- */
static const char *const AREAS[][2] = {
	{ "Turkish", "Türkiye" }, { "Italian", "İtalya" }, { "French", "Fransa" }, { "Indian", "Hindistan" }, { "Chinese", "Çin" },
	{ "Japanese", "Japonya" }, { "Mexican", "Meksika" }, { "Thai", "Tayland" }, { "Greek", "Yunanistan" }, { "Spanish", "İspanya" },
	{ "German", "Almanya" }, { "Austrian", "Avusturya" }, { "Egyptian", "Mısır" }, { "Ethiopian", "Etiyopya" }, { "Nigerian", "Nijerya" },
	{ "Moroccan", "Fas" }, { "Algerian", "Cezayir" }, { "Tunisian", "Tunus" }, { "Lebanese", "Lübnan" }, { "Iranian", "İran" },
	{ "Israeli", "İsrail" }, { "Palestinian", "Filistin" }, { "Afghan", "Afganistan" }, { "Armenian", "Ermenistan" },
	{ "Azerbaijani", "Azerbaycan" }, { "Georgian", "Gürcistan" }, { "Korean", "Kore" }, { "Vietnamese", "Vietnam" },
	{ "Indonesian", "Endonezya" }, { "Filipino", "Filipinler" }, { "Malaysian", "Malezya" }, { "Pakistani", "Pakistan" },
	{ "Russian", "Rusya" }, { "Ukrainian", "Ukrayna" }, { "Polish", "Polonya" }, { "Hungarian", "Macaristan" }, { "Croatian", "Hırvatistan" },
	{ "Bulgarian", "Bulgaristan" }, { "Romanian", "Romanya" }, { "Albanian", "Arnavutluk" }, { "Swedish", "İsveç" },
	{ "Norwegian", "Norveç" }, { "Danish", "Danimarka" }, { "Finnish", "Finlandiya" }, { "Dutch", "Hollanda" }, { "Belgian", "Belçika" },
	{ "Swiss", "İsviçre" }, { "Portuguese", "Portekiz" }, { "British", "İngiltere" }, { "English", "İngiltere" }, { "Scottish", "İskoçya" },
	{ "Irish", "İrlanda" }, { "Welsh", "Galler" }, { "American", "ABD" }, { "United States", "ABD" }, { "Canadian", "Kanada" },
	{ "Brazilian", "Brezilya" }, { "Argentine", "Arjantin" }, { "Argentinian", "Arjantin" }, { "Peruvian", "Peru" },
	{ "Colombian", "Kolombiya" }, { "Venezuelan", "Venezuela" }, { "Uruguayan", "Uruguay" }, { "Cuban", "Küba" }, { "Jamaican", "Jamaika" },
	{ "Puerto Rican", "Porto Riko" }, { "Australian", "Avustralya" }, { "New Zealand", "Yeni Zelanda" }, { "Kenyan", "Kenya" },
	{ "South African", "Güney Afrika" }, { "Libyan", "Libya" }, { "Czech", "Çekya" }, { "Cypriot", "Kıbrıs" }, { "Syrian", "Suriye" },
	{ "Saudi Arabian", "Suudi Arabistan" }, { "Slovakian", "Slovakya" }, { "Venezulan", "Venezuela" },
	/* country names (TheMealDB's strCountry) */
	{ "Turkey", "Türkiye" }, { "Türkiye", "Türkiye" }, { "Italy", "İtalya" }, { "France", "Fransa" }, { "India", "Hindistan" },
	{ "China", "Çin" }, { "Japan", "Japonya" }, { "Mexico", "Meksika" }, { "Thailand", "Tayland" }, { "Greece", "Yunanistan" },
	{ "Spain", "İspanya" }, { "Germany", "Almanya" }, { "United States", "ABD" }, { "United States of America", "ABD" },
	{ "United Kingdom", "İngiltere" }, { "Argentina", "Arjantin" }, { "Brazil", "Brezilya" }, { "Morocco", "Fas" }, { "Egypt", "Mısır" },
	{ "Russia", "Rusya" }, { "Poland", "Polonya" }, { "Portugal", "Portekiz" }, { "Vietnam", "Vietnam" }, { "Malaysia", "Malezya" },
	{ "Philippines", "Filipinler" }, { "Canada", "Kanada" }, { "Jamaica", "Jamaika" }, { "Kenya", "Kenya" }, { "Croatia", "Hırvatistan" },
	{ "Netherlands", "Hollanda" }, { "Ireland", "İrlanda" }, { "Tunisia", "Tunus" }, { "Ukraine", "Ukrayna" }, { "Uruguay", "Uruguay" },
	{ "Syria", "Suriye" }, { "Saudi Arabia", "Suudi Arabistan" }, { "Lebanon", "Lübnan" }, { "Iran", "İran" }, { "Algeria", "Cezayir" },
	{ "Peru", "Peru" }, { "Colombia", "Kolombiya" }, { "Venezuela", "Venezuela" }, { "Cuba", "Küba" }, { "Australia", "Avustralya" },
	{ "Nigeria", "Nijerya" }, { "Ethiopia", "Etiyopya" }, { "Indonesia", "Endonezya" }, { "South Korea", "Kore" }, { "Korea", "Kore" },
	{ "Pakistan", "Pakistan" }, { "Hungary", "Macaristan" }, { "Austria", "Avusturya" }, { "Sweden", "İsveç" }, { "Norway", "Norveç" },
	{ "Denmark", "Danimarka" }, { "Finland", "Finlandiya" }, { "Belgium", "Belçika" }, { "Switzerland", "İsviçre" }, { "Israel", "İsrail" },
	{ "Afghanistan", "Afganistan" }, { "Armenia", "Ermenistan" }, { "Azerbaijan", "Azerbaycan" }, { "Georgia", "Gürcistan" },
	{ "Albania", "Arnavutluk" }, { "Bulgaria", "Bulgaristan" }, { "Romania", "Romanya" }, { "Czech Republic", "Çekya" }, { "Cyprus", "Kıbrıs" },
};

const char *tarif_area_tr(const char *area)
{
	for (size_t i = 0; i < sizeof AREAS / sizeof AREAS[0]; i++)
		if (!strcmp(area, AREAS[i][0])) return AREAS[i][1];
	return area;
}

/* ---- wikitext ---- */

static int starts_ci(const char *s, const char *w)
{
	for (; *w; s++, w++)
		if (tolower((unsigned char) *s) != tolower((unsigned char) *w)) return 0;
	return 1;
}

/* copies s with templates, comments, references and tables removed (newlines inside them dropped) */
static char *strip_blocks(const char *s)
{
	size_t n = strlen(s);
	char *out = malloc(n + 1), *o = out;
	if (!out) return NULL;
	int tdepth = 0;
	for (const char *p = s; *p;) {
		if (p[0] == '{' && p[1] == '{') { tdepth++; p += 2; continue; }
		if (tdepth && p[0] == '}' && p[1] == '}') { tdepth--; p += 2; continue; }
		if (tdepth) { p++; continue; }
		if (!strncmp(p, "<!--", 4)) {
			const char *e = strstr(p + 4, "-->");
			p = e ? e + 3 : p + strlen(p);
			continue;
		}
		if (starts_ci(p, "<ref")) {
			const char *gt = strchr(p, '>');
			if (!gt) break;
			if (gt[-1] == '/') { p = gt + 1; continue; }
			const char *e = strstr(gt, "</ref>");
			if (!e) e = strstr(gt, "</REF>");
			p = e ? e + 6 : gt + 1;
			continue;
		}
		if (p[0] == '{' && p[1] == '|' && (p == s || p[-1] == '\n')) { /* a table, to its closing |} line */
			const char *e = strstr(p, "\n|}");
			p = e ? e + 3 : p + strlen(p);
			continue;
		}
		*o++ = *p++;
	}
	*o = '\0';
	return out;
}

/* one line of wiki markup → plain text (links keep their label; files and categories dropped) */
static void clean_inline(const char *s, int n, char *out, int cap)
{
	int o = 0, space = 1;
	const char *e = s + n;
#define PUT(c) do { char c_ = (c); if (c_ == ' ' || c_ == '\t') { if (!space && o < cap - 1) out[o++] = ' '; space = 1; } \
		else if (o < cap - 1) { out[o++] = c_; space = 0; } } while (0)
	for (const char *p = s; p < e;) {
		if (p[0] == '[' && p + 1 < e && p[1] == '[') {
			const char *q = p + 2, *close = NULL;
			int depth = 1;
			for (const char *r = q; r + 1 < e; r++) {
				if (r[0] == '[' && r[1] == '[') depth++, r++;
				else if (r[0] == ']' && r[1] == ']' && !--depth) { close = r; break; }
			}
			if (!close) { p += 2; continue; }
			static const char *const drop[] = { "File:", "Image:", "Dosya:", "Resim:", "Category:", "Kategori:", "Media:", NULL };
			int skip = 0;
			const char *t = q;
			while (t < close && *t == ' ') t++;
			if (*t == ':') t++;
			for (int i = 0; drop[i] && !skip; i++) skip = starts_ci(t, drop[i]);
			if (!skip) {
				const char *bar = NULL;
				for (const char *r = q; r < close; r++)
					if (*r == '|') bar = r;
				const char *a = bar ? bar + 1 : q;
				if (!bar) /* [[Cookbook:Egg]] → Egg */
					for (const char *r = q; r < close; r++)
						if (*r == ':') a = r + 1;
				for (const char *r = a; r < close; r++) PUT(*r);
			}
			p = close + 2;
			continue;
		}
		if (p[0] == '[' && (starts_ci(p + 1, "http://") || starts_ci(p + 1, "https://"))) {
			const char *close = memchr(p, ']', (size_t) (e - p)), *sp = memchr(p, ' ', (size_t) (e - p));
			if (close) {
				if (sp && sp < close)
					for (const char *r = sp + 1; r < close; r++) PUT(*r);
				p = close + 1;
				continue;
			}
		}
		if (p[0] == '\'' && p + 1 < e && p[1] == '\'') {
			while (p < e && *p == '\'') p++;
			continue;
		}
		if (p[0] == '_' && p + 1 < e && p[1] == '_') {
			const char *q = p + 2;
			while (q < e && isalpha((unsigned char) *q)) q++;
			if (q + 1 < e && q[0] == '_' && q[1] == '_') { p = q + 2; continue; }
		}
		if (*p == '<') {
			const char *gt = memchr(p, '>', (size_t) (e - p));
			if (gt && (isalpha((unsigned char) p[1]) || p[1] == '/')) {
				if (starts_ci(p, "<br")) PUT(' ');
				p = gt + 1;
				continue;
			}
		}
		if (*p == '&') {
			static const struct { const char *e, *t; } ENT[] = {
				{ "&nbsp;", " " }, { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&#39;", "'" },
				{ "&ndash;", "–" }, { "&mdash;", "—" }, { "&frac12;", "½" }, { "&frac14;", "¼" }, { "&frac34;", "¾" }, { "&deg;", "°" },
			};
			int hit = 0;
			for (size_t i = 0; i < sizeof ENT / sizeof ENT[0] && !hit; i++) {
				size_t l = strlen(ENT[i].e);
				if ((size_t) (e - p) >= l && !strncmp(p, ENT[i].e, l)) {
					for (const char *r = ENT[i].t; *r; r++) PUT(*r);
					p += l;
					hit = 1;
				}
			}
			if (hit) continue;
		}
		if ((unsigned char) p[0] == 0xC2 && p + 1 < e && (unsigned char) p[1] == 0xA0) { /* no-break space */
			PUT(' ');
			p += 2;
			continue;
		}
		PUT(*p);
		p++;
	}
#undef PUT
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = '\0';
}

static int is_upper_start(const char *p)
{
	unsigned char c = (unsigned char) p[0], d = (unsigned char) p[1];
	return isupper(c) || isdigit(c) || (c == 0xC3 && (d == 0x87 || d == 0x96 || d == 0x9C)) || (c == 0xC4 && (d == 0x9E || d == 0xB0)) ||
	       (c == 0xC5 && d == 0x9E);
}

/* a long method paragraph → steps of about two sentences */
static void add_paragraph_steps(TarifBuf *b, const char *p)
{
	int len = (int) strlen(p);
	if (len < 320) {
		tarif_rec_line(b, '*', p, len);
		return;
	}
	const char *start = p;
	for (const char *q = p; *q; q++) {
		int boundary = (*q == '.' || *q == '!' || *q == '?') && q[1] == ' ' && is_upper_start(q + 2);
		if (boundary && q + 1 - start > 200) {
			tarif_rec_line(b, '*', start, (int) (q + 1 - start));
			start = q + 2;
		}
	}
	if (*start) tarif_rec_line(b, '*', start, -1);
}

static int heading_matches(const char *folded, const char *const *keys)
{
	for (int i = 0; keys[i]; i++)
		if (strstr(folded, keys[i])) return 1;
	return 0;
}

static void tags_from_category(const char *c, char *tags)
{
	static const char *const T[][2] = {
		{ "soup", "c" }, { "corba", "c" }, { "dessert", "t" }, { "tatli", "t" }, { "cake", "t" }, { "cookie", "t" }, { "kurabiye", "t" },
		{ "salad", "s" }, { "salata", "s" }, { "meze", "s" }, { "appetizer", "s" }, { "main course", "a" }, { "et yemek", "a" },
		{ "ana yemek", "a" }, { "stew", "a" }, { "bread", "h" }, { "ekmek", "h" }, { "borek", "h" }, { "pastr", "h" }, { "breakfast", "k" },
		{ "kahvalti", "k" }, { "beverage", "i" }, { "icecek", "i" }, { "vegan", "vg" }, { "vegetarian", "g" }, { "vejetaryen", "g" },
	};
	char f[200];
	tarif_fold(c, f, sizeof f);
	for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
		if (strstr(f, T[i][0]))
			for (const char *t = T[i][1]; *t; t++)
				if (!strchr(tags, *t) && strlen(tags) < 15) strncat(tags, t, 1);
}

int tarif_wiki_record(TarifBuf *b, const char *wikitext, const char *lang, const char *title, const char *url)
{
	static const char *const ING[] = { "malzeme", "gerekli", "ihtiya", "ingredient", NULL };
	static const char *const STEP[] = { "hazirlan", "yapilis", "yapimi", "tarif", "pisir", "procedure", "direction", "method", "preparation",
					    "instruction", "steps", NULL };
	int serv = 0;
	const char *m;
	if ((m = strstr(wikitext, "servings"))) {
		m += 8;
		while (*m == ' ') m++;
		if (*m == '=') serv = atoi(m + 1 + strspn(m + 1, " "));
	}
	if (!serv && (m = strstr(wikitext, " kişilik"))) {
		const char *d = m;
		while (d > wikitext && isdigit((unsigned char) d[-1])) d--;
		if (d < m) serv = atoi(d);
	}
	if (!serv && (m = strstr(wikitext, "Serves "))) serv = atoi(m + 7);
	if (serv < 0 || serv > 30) serv = 0;
	char tags[16] = "", cuisine[160] = "";
	/* categories → tags and cuisine */
	for (const char *p = wikitext; (p = strstr(p, "[[")); p += 2) {
		const char *q = p + 2;
		int en = starts_ci(q, "Category:"), tr = starts_ci(q, "Kategori:");
		if (!en && !tr) continue;
		q += 9;
		const char *e = strpbrk(q, "|]");
		if (!e) break;
		char c[160];
		int n = (int) (e - q) < (int) sizeof c - 1 ? (int) (e - q) : (int) sizeof c - 1;
		memcpy(c, q, (size_t) n);
		c[n] = '\0';
		tags_from_category(c, tags);
		size_t cl = strlen(c);
		if (cl > 8 && !strcmp(c + cl - 8, " recipes")) {
			c[cl - 8] = '\0';
			const char *t = tarif_area_tr(c);
			if (t != c && !cuisine[0]) snprintf(cuisine, sizeof cuisine, "%s", t);
		} else if (cl > strlen(" Mutfağı") && !strcmp(c + cl - strlen(" Mutfağı"), " Mutfağı")) {
			c[cl - strlen(" Mutfağı")] = 0;
			snprintf(cuisine, sizeof cuisine, "%s", !strcmp(c, "Türk") ? "Türkiye" : c);
		}
	}
	if (!cuisine[0]) snprintf(cuisine, sizeof cuisine, "%s", !strcmp(lang, "tr") ? "Türkiye" : "Dünya");
	char *text = strip_blocks(wikitext);
	if (!text) return -1;
	TarifBuf ing = { 0 }, steps = { 0 }, intro = { 0 };
	int n_ing = 0, n_steps = 0;
	enum { S_INTRO, S_ING, S_STEP, S_OTHER } sect = S_INTRO;
	static char line[8192];
	for (char *p = text; *p;) {
		char *e = strchr(p, '\n');
		int len = e ? (int) (e - p) : (int) strlen(p);
		char *next = e ? e + 1 : p + len;
		while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\r' || p[len - 1] == '\t')) len--;
		while (len > 0 && (*p == ' ' || *p == '\t')) p++, len--;
		if (len >= 4 && p[0] == '=' && p[1] == '=' && p[len - 1] == '=') {
			int lv = 0;
			while (lv < len && p[lv] == '=') lv++;
			int rv = 0;
			while (rv < len - lv && p[len - 1 - rv] == '=') rv++;
			char h[300], f[300];
			clean_inline(p + lv, len - lv - rv, h, sizeof h);
			tarif_fold(h, f, sizeof f);
			if (heading_matches(f, ING) && (lv == 2 || sect != S_ING)) sect = S_ING;
			else if (heading_matches(f, STEP) && (lv == 2 || sect != S_STEP)) sect = S_STEP;
			else if (sect == S_ING && lv > 2) {
				size_t hl = strlen(h);
				if (hl && h[hl - 1] == ':') h[hl - 1] = '\0';
				if (*h) tarif_rec_line(&ing, '+', h, -1);
			} else if (lv == 2 || sect != S_ING) sect = S_OTHER;
			p = next;
			continue;
		}
		if (!len) { p = next; continue; }
		int bullet = 0;
		while (bullet < len && strchr("*#:;", p[bullet])) bullet++;
		clean_inline(p + bullet, len - bullet, line, sizeof line);
		size_t ll = strlen(line);
		if (!ll) { p = next; continue; }
		if (sect == S_INTRO) {
			if (!bullet && line[0] != '<' && line[0] != '|' && !strstr(line, "listesine dön") && intro.len < 400) {
				if (intro.len) tarif_buf_add(&intro, " ", 1);
				tarif_buf_add(&intro, line, -1);
			}
		} else if (sect == S_ING) {
			if (bullet) {
				while (ll && strchr(",;. ", line[ll - 1])) line[--ll] = '\0';
				if (ll > 1) tarif_rec_line(&ing, '-', line, (int) (ll < 300 ? ll : 300)), n_ing++;
			} else if (strstr(line, "kişilik") && ll < 20) {
				/* "4 kişilik:" */
			} else if (line[ll - 1] == ':' && ll < 60) {
				line[ll - 1] = '\0';
				tarif_rec_line(&ing, '+', line, -1);
			} else { /* "Un, şeker, …" in one paragraph: split at commas outside parentheses */
				int depth = 0;
				char *s = line;
				for (char *q = line;; q++) {
					if (*q == '(') depth++;
					else if (*q == ')' && depth) depth--;
					if (*q == '\0' || (*q == ',' && !depth)) {
						char *a = s, *z = q;
						while (*a == ' ') a++;
						while (z > a && strchr(",;. ", z[-1])) z--;
						if (z - a > 1) tarif_rec_line(&ing, '-', a, (int) (z - a)), n_ing++;
						if (!*q) break;
						s = q + 1;
					}
				}
			}
		} else if (sect == S_STEP) {
			if (ll > 2) {
				if (ll > 1200) line[1200] = '\0';
				if (bullet) tarif_rec_line(&steps, '*', line, -1);
				else add_paragraph_steps(&steps, line);
				n_steps++;
			}
		}
		p = next;
	}
	free(text);
	int ok = n_ing >= 2 && n_steps > 0;
	if (ok) {
		if (strlen(tags) < 15 && !strchr(tags, 'c')) { /* soups by name */
			char f[200];
			tarif_fold(title, f, sizeof f);
			if (strstr(f, "corba") || strstr(f, "soup")) strcat(tags, "c");
		}
		tarif_rec_head(b, lang, title, cuisine, tags, serv, url, "CC BY-SA 4.0");
		if (intro.len) tarif_rec_line(b, '=', intro.s, intro.len > 500 ? 500 : intro.len);
		if (ing.len) tarif_buf_add(b, ing.s, ing.len);
		if (steps.len) tarif_buf_add(b, steps.s, steps.len);
	}
	tarif_buf_free(&ing);
	tarif_buf_free(&steps);
	tarif_buf_free(&intro);
	return ok ? 0 : -1;
}

int tarif_wiki_search_titles(const char *json, char (*titles)[160], int max)
{
	cJSON *root = cJSON_Parse(json);
	int n = 0;
	cJSON *q = cJSON_GetObjectItem(root, "query"), *s = cJSON_GetObjectItem(q, "search"), *it;
	cJSON_ArrayForEach(it, s)
	{
		cJSON *t = cJSON_GetObjectItem(it, "title");
		if (n < max && cJSON_IsString(t)) snprintf(titles[n++], 160, "%s", t->valuestring);
	}
	cJSON_Delete(root);
	return n;
}

static const char *jstr(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItem(o, k);
	return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

int tarif_mealdb_records(TarifBuf *b, const char *json)
{
	cJSON *root = cJSON_Parse(json), *meals = cJSON_GetObjectItem(root, "meals"), *m;
	int n = 0;
	cJSON_ArrayForEach(m, meals)
	{
		const char *id = jstr(m, "idMeal"), *name = jstr(m, "strMeal"), *cat = jstr(m, "strCategory");
		if (!*id || !*name) continue;
		char url[96], tags[16] = "";
		snprintf(url, sizeof url, "https://www.themealdb.com/meal/%s", id);
		static const char *const CT[][2] = { { "Dessert", "t" }, { "Vegan", "vg" }, { "Vegetarian", "g" }, { "Breakfast", "k" },
						     { "Starter", "s" }, { "Side", "s" }, { "Soup", "c" } };
		int known = 0;
		for (size_t i = 0; i < sizeof CT / sizeof CT[0]; i++)
			if (!strcmp(cat, CT[i][0])) snprintf(tags, sizeof tags, "%s", CT[i][1]), known = 1;
		if (!known && *cat) snprintf(tags, sizeof tags, "a");
		char f[200];
		tarif_fold(name, f, sizeof f);
		if (strstr(f, "soup") && !strchr(tags, 'c')) strcat(tags, "c");
		const char *area = *jstr(m, "strCountry") ? jstr(m, "strCountry") : jstr(m, "strArea");
		tarif_rec_head(b, "en", name, *area && strcmp(area, "Unknown") ? tarif_area_tr(area) : "Dünya", tags, 0, url, "TheMealDB");
		for (int i = 1; i <= 20; i++) {
			char k1[24], k2[24], ln[400];
			snprintf(k1, sizeof k1, "strIngredient%d", i);
			snprintf(k2, sizeof k2, "strMeasure%d", i);
			const char *ing = jstr(m, k1), *mea = jstr(m, k2);
			while (*ing == ' ') ing++;
			while (*mea == ' ') mea++;
			if (!*ing) continue;
			char ms[160];
			snprintf(ms, sizeof ms, "%s", mea);
			for (size_t l = strlen(ms); l && ms[l - 1] == ' ';) ms[--l] = '\0';
			snprintf(ln, sizeof ln, "%s%s%s", ms, *ms ? " " : "", ing);
			size_t l = strlen(ln);
			while (l && ln[l - 1] == ' ') ln[--l] = '\0';
			tarif_rec_line(b, '-', ln, -1);
		}
		/* instructions: one step per line ("STEP 3" lines dropped), long lines split at sentences */
		const char *ins = jstr(m, "strInstructions");
		int steps = 0;
		for (const char *p = ins; *p;) {
			const char *e = strchr(p, '\n');
			int len = e ? (int) (e - p) : (int) strlen(p);
			char ln[4096];
			int l = len < (int) sizeof ln - 1 ? len : (int) sizeof ln - 1;
			memcpy(ln, p, (size_t) l);
			ln[l] = '\0';
			while (l && (ln[l - 1] == '\r' || ln[l - 1] == ' ')) ln[--l] = '\0';
			char *s = ln;
			while (*s == ' ') s++;
			char fs[64];
			tarif_fold(s, fs, sizeof fs);
			int label = !strncmp(fs, "step", 4) && strlen(fs) <= 8;
			if (strlen(s) > 2 && !label) {
				if (!strncmp(fs, "step ", 5) && isdigit((unsigned char) fs[5])) { /* "STEP 1 Heat the oil" */
					s += 4;
					while (*s == ' ' || isdigit((unsigned char) *s) || *s == '.' || *s == ':') s++;
				}
				add_paragraph_steps(b, s);
				steps++;
			}
			p = e ? e + 1 : p + len;
		}
		if (!steps) tarif_rec_line(b, '*', "—", -1);
		n++;
	}
	cJSON_Delete(root);
	return n;
}

int tarif_mealdb_list(const char *json, char (*ids)[16], char (*names)[160], int max)
{
	cJSON *root = cJSON_Parse(json), *meals = cJSON_GetObjectItem(root, "meals"), *m;
	int n = 0;
	cJSON_ArrayForEach(m, meals)
	{
		const char *id = jstr(m, "idMeal"), *name = jstr(m, "strMeal");
		if (!*id || !*name || n >= max) continue;
		snprintf(ids[n], 16, "%s", id);
		snprintf(names[n], 160, "%s", name);
		n++;
	}
	cJSON_Delete(root);
	return n;
}

void tarif_wiki_url(const char *host, const char *title, char *out, int cap)
{
	int o = snprintf(out, (size_t) cap, "https://%s/wiki/", host);
	for (const unsigned char *p = (const unsigned char *) title; *p && o < cap - 4; p++) {
		if (*p == ' ') out[o++] = '_';
		else if (isalnum(*p) || strchr("-._~:/(),'", *p)) out[o++] = (char) *p;
		else o += snprintf(out + o, (size_t) (cap - o), "%%%02X", *p);
	}
	out[o < cap ? o : cap - 1] = '\0';
}

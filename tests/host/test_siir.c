#include <stdlib.h>
#include <string.h>
#include "siir/siir.h"
#include "t.h"

static const char *SAMPLE =
	"# comment\n"
	"# another\n"
	"@A\tyunus\tYunus Emre\t1240–1321\tTasavvuf şairi.\n"
	"@P\tyunus\tAşk Ver Bana\thttps://tr.wikisource.org/wiki/A\n"
	"İlahi bir aşk ver bana\n"
	"Yavı kılayım ben beni\n"
	"\n"
	"Bülbül olup öteyim\n"
	"@P\tyunus\tİkinci\thttps://tr.wikisource.org/wiki/B\n"
	"Tek satır\n"
	"@A\tbaki\tBâkî\t1526–1600\tDivan şairi.\n"
	"@P\tbaki\tGazel\thttps://tr.wikisource.org/wiki/C\n"
	"Nâz ile\n"
	"Hey elâ gözlüm\n";

static void test_parse(void)
{
	char *buf = strdup(SAMPLE);
	SiirDb db;
	T_EQ_INT(siir_parse(buf, &db), 3);
	T_EQ_INT(db.n_authors, 2);
	T_EQ_STR(db.authors[0].id, "yunus");
	T_EQ_STR(db.authors[0].name, "Yunus Emre");
	T_EQ_STR(db.authors[0].years, "1240–1321");
	T_EQ_STR(db.authors[0].note, "Tasavvuf şairi.");
	T_EQ_INT(db.authors[0].first, 0);
	T_EQ_INT(db.authors[0].count, 2);
	T_EQ_INT(db.authors[1].first, 2);
	T_EQ_INT(db.authors[1].count, 1);
	T_EQ_STR(db.poems[0].title, "Aşk Ver Bana");
	T_EQ_STR(db.poems[0].url, "https://tr.wikisource.org/wiki/A");
	T_EQ_STR(db.poems[0].text, "İlahi bir aşk ver bana\nYavı kılayım ben beni\n\nBülbül olup öteyim");
	T_EQ_STR(db.poems[1].text, "Tek satır");
	T_EQ_INT(db.poems[2].author, 1);
	T_EQ_STR(db.poems[2].text, "Nâz ile\nHey elâ gözlüm"); /* trailing newline dropped */
	T_EQ_INT(siir_find_url(&db, "https://tr.wikisource.org/wiki/C"), 2);
	T_EQ_INT(siir_find_url(&db, "https://x"), -1);
	siir_free(&db);
}

static void test_parse_bad(void)
{
	SiirDb db;
	char *a = strdup("@P\tyunus\tT\tu\nsatır\n"); /* poem before any poet */
	T_EQ_INT(siir_parse(a, &db), -1);
	char *b = strdup("@A\tyunus\tYunus\n"); /* too few fields */
	T_EQ_INT(siir_parse(b, &db), -1);
	char *c = strdup("@A\ta\tA\t1\tn\n@P\tb\tT\tu\nx\n"); /* poem of another poet than the last @A */
	T_EQ_INT(siir_parse(c, &db), -1);
	char *d = strdup("# nothing\n");
	T_EQ_INT(siir_parse(d, &db), -1);
	free(a);
	free(b);
	free(c);
	free(d);
	char *e = strdup("@A\ta\tA\t1\tn\r\n@P\ta\tT\tu\r\nbir\r\niki\r\n"); /* CRLF tolerated */
	T_EQ_INT(siir_parse(e, &db), 1);
	T_EQ_STR(db.authors[0].note, "n");
	T_EQ_STR(db.poems[0].url, "u");
	T_EQ_STR(db.poems[0].text, "bir\niki");
	siir_free(&db);
	T_EQ_INT(siir_load("build/host/no-such-file", &db), -1);
}

static void test_real_file(void)
{
	SiirDb db;
	int n = siir_load("apps/siir/data/poems.txt", &db);
	T_ASSERT(n >= 150 && n <= 400);
	T_ASSERT(db.n_authors >= 20);
	for (int a = 0; a < db.n_authors; a++) {
		T_ASSERT(db.authors[a].count > 0);
		T_ASSERT(db.authors[a].name[0] && db.authors[a].note[0]);
	}
	for (int i = 0; i < n; i++) {
		const SiirPoem *p = &db.poems[i];
		T_ASSERT(p->title[0] && p->text[0] && !strncmp(p->url, "https://tr.wikisource.org/wiki/", 31));
		if (strstr(p->text, "\n\n\n") || strstr(p->text, "{{") || strstr(p->text, "<br") || strstr(p->text, "[[")) {
			T_FAIL_("poem %s has markup or triple breaks", p->title);
			break;
		}
		if (siir_find_url(&db, p->url) != i) {
			T_FAIL_("poem %s: url is not unique", p->title);
			break;
		}
	}
	siir_free(&db);
}

static void test_days(void)
{
	T_EQ_INT(siir_days(1970, 1, 1), 0);
	T_EQ_INT(siir_days(1970, 1, 2), 1);
	T_EQ_INT(siir_days(2000, 3, 1), 11017);
	T_EQ_INT(siir_days(2026, 10, 19), 20745);
	T_EQ_INT(siir_days(2024, 12, 31) + 1, siir_days(2025, 1, 1));
	T_EQ_INT(siir_days(2024, 2, 29) + 1, siir_days(2024, 3, 1));
}

static void test_daily(void)
{
	T_EQ_INT(siir_daily(100, 0), -1);
	T_EQ_INT(siir_daily(5, 1), 0);
	enum { N = 411 };
	static char seen[N];
	memset(seen, 0, sizeof seen);
	long d0 = siir_days(2026, 10, 19);
	int near = 0;
	for (long d = d0; d < d0 + N; d++) { /* a full cycle covers every poem once */
		int i = siir_daily(d, N);
		T_ASSERT(i >= 0 && i < N);
		if (i >= 0 && i < N) seen[i]++;
		int j = siir_daily(d + 1, N);
		int dist = abs(i - j);
		if (dist < 15 || dist > N - 15) near++;
	}
	for (int i = 0; i < N; i++)
		if (seen[i] != 1) {
			T_FAIL_("poem %d chosen %d times in a cycle", i, seen[i]);
			break;
		}
	T_ASSERT(near < N / 10); /* the next day is usually far away in the file (another poet) */
	T_EQ_INT(siir_daily(d0, N), siir_daily(d0, N));
	for (int n = 2; n < 60; n++) { /* every size: a permutation */
		char s[60] = { 0 };
		for (long d = 0; d < n; d++) s[siir_daily(d0 + d, n)]++;
		for (int i = 0; i < n; i++)
			if (s[i] != 1) {
				T_FAIL_("n=%d: not a permutation", n);
				i = n;
			}
	}
	T_ASSERT(siir_daily(-5, 7) >= 0); /* before 1970: still in range */
}

static void test_random(void)
{
	for (unsigned s = 0; s < 200; s++) {
		int r = siir_random(s, 10, 3);
		T_ASSERT(r >= 0 && r < 10 && r != 3);
	}
	T_EQ_INT(siir_random(7, 1, 0), 0);
	T_EQ_INT(siir_random(7, 0, -1), -1);
}

static void paginate(const char *kinds, int h, int first_h, int page_h, const char *want)
{
	SiirItem it[64];
	int n = (int) strlen(kinds), starts[16];
	for (int i = 0; i < n; i++) it[i] = (SiirItem){ kinds[i] == 'L' ? SIIR_LINE : kinds[i] == 'c' ? SIIR_CONT : SIIR_GAP, kinds[i] == '_' ? h / 2 : h };
	int p = siir_paginate(it, n, first_h, page_h, starts, 16);
	char got[64] = "";
	for (int k = 0; k < p; k++) snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d", k ? "," : "", starts[k]);
	T_EQ_STR(got, want);
}

static void test_paginate(void)
{
	paginate("LLLL", 10, 100, 100, "0");                   /* fits */
	paginate("LLLLLL", 10, 30, 100, "0,3");                /* short first page */
	paginate("LLLLLLLLLL", 10, 50, 50, "0,5");             /* exactly full */
	paginate("LLLcL", 10, 30, 30, "0,2");                  /* a wrapped line moves whole */
	paginate("LLLL_LLLL", 10, 55, 55, "0,5");              /* stanza does not fit: break at the stanza break */
	paginate("LL_LLLLLLLL", 10, 100, 100, "0,9");          /* 20 % full at the gap: split, but no widow */
	paginate("LLLL_LLL", 10, 65, 65, "0,6");               /* the stanza's last line takes the one before along */
	paginate("LLLL_LcL", 10, 70, 70, "0,5");               /* … a wrapped line counts as one */
	paginate("LLLLLLL_LLLL", 10, 100, 100, "0,8");         /* 70 % full at the gap: the stanza moves */
	paginate("LLLLLL_LLLLL", 10, 100, 100, "0,10");        /* 60 %: the stanza is split instead */
	paginate("LLLLL_LLLLLLLLLL", 10, 100, 100, "0,10");
	paginate("LLLLLLLL_LLLL", 10, 100, 100, "0,9");        /* break at the gap: it would open a page, so skip it */
	paginate("LLLLLLLLLL_LL", 10, 100, 100, "0,10");       /* gap exactly at the page end */
	paginate("Lcccccccc", 10, 50, 50, "0,5");              /* a line longer than a page is split */
	paginate("LLLLLLLLLLLLLLLLLLLL", 10, 50, 50, "0,5,10,15");
	paginate("", 10, 50, 50, "");
	paginate("L", 10, 5, 50, "0");                         /* never an empty first page */
	SiirItem it[40];
	int starts[3];
	for (int i = 0; i < 40; i++) it[i] = (SiirItem){ SIIR_LINE, 10 };
	T_EQ_INT(siir_paginate(it, 40, 50, 50, starts, 3), 3); /* capped at max */
}

static void test_favs(void)
{
	static SiirFavs f;
	siir_favs_parse("u1\n\nu2\nu1\n  \nu3", &f);
	T_EQ_INT(f.n, 3);
	T_EQ_INT(siir_favs_has(&f, "u2"), 1);
	T_EQ_INT(siir_favs_has(&f, "u4"), 0);
	T_EQ_INT(siir_favs_toggle(&f, "u2"), 0);
	T_EQ_INT(siir_favs_has(&f, "u2"), 0);
	T_EQ_INT(siir_favs_toggle(&f, "u4"), 1);
	char buf[64];
	T_EQ_INT(siir_favs_format(&f, buf, sizeof buf), 9);
	T_EQ_STR(buf, "u1\nu3\nu4\n");
	T_EQ_INT(siir_favs_format(&f, buf, 9), -1); /* no room for the NUL */
	static SiirFavs g;
	siir_favs_parse(buf, &g);
	T_EQ_INT(g.n, 3);
	T_EQ_STR(g.key[2], "u4");
	siir_favs_free(&g);
	siir_favs_parse("", &f); /* resets (and frees) */
	T_EQ_INT(f.n, 0);
	for (int i = 0; i < SIIR_MAX_FAVS; i++) {
		char k[16];
		snprintf(k, sizeof k, "k%d", i);
		siir_favs_toggle(&f, k);
	}
	T_EQ_INT(siir_favs_toggle(&f, "one-more"), -1);
	T_EQ_INT(siir_favs_toggle(&f, "k5"), 0);
	siir_favs_free(&f);
	siir_favs_parse("a\r\nb\r\n", &f); /* CRLF */
	T_EQ_INT(siir_favs_has(&f, "a"), 1);
	T_EQ_INT(siir_favs_has(&f, "b"), 1);
	siir_favs_free(&f);
}

int main(void)
{
	T_RUN(test_parse);
	T_RUN(test_parse_bad);
	T_RUN(test_real_file);
	T_RUN(test_days);
	T_RUN(test_daily);
	T_RUN(test_random);
	T_RUN(test_paginate);
	T_RUN(test_favs);
	return T_DONE();
}

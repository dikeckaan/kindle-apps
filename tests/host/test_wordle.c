#include <stdlib.h>
#include <unistd.h>
#include "kui.h"
#include "wordle/wordle.h"
#include "t.h"

static void marks_are(const char *answer, const char *guess, const char *want)
{
	WlMark m[WL_LEN];
	wl_score(answer, guess, m);
	char got[WL_LEN + 1];
	for (int i = 0; i < WL_LEN; i++) got[i] = m[i] == WL_CORRECT ? 'C' : m[i] == WL_PRESENT ? 'P' : '-';
	got[WL_LEN] = '\0';
	T_EQ_STR(got, want);
}

static void test_score_duplicates(void)
{
	marks_are("kalem", "kakao", "CC---");
	marks_are("kalem", "kalem", "CCCCC");
	marks_are("abaca", "aaaaa", "C-C-C");
	marks_are("kitap", "pakit", "PPPPP");
	marks_are("elmas", "eeeee", "C----");   /* one e in the answer: only the matching one */
	marks_are("teyze", "eeeee", "-C--C");
	marks_are("sezen", "eseee", "PP-C-");  /* two e's left after the correct one */
	marks_are("ağaçı", "ağaca", "CCC--");  /* multi-byte letters; c is not ç */
	marks_are("ıslak", "islak", "-CCCC");  /* ı and i are different letters */
}

static void test_normalize(void)
{
	char out[WL_BYTES];
	T_EQ_INT(wl_normalize("KIRIK", out), 5);
	T_EQ_STR(out, "kırık");
	T_EQ_INT(wl_normalize("İĞNEM", out), 5);
	T_EQ_STR(out, "iğnem");
	T_EQ_INT(wl_normalize("kâğıt", out), 5);
	T_EQ_STR(out, "kağıt");
	T_EQ_INT(wl_normalize("Hûzur", out), 5);
	T_EQ_STR(out, "huzur");
	T_EQ_INT(wl_normalize("abc", out), 3);
	T_EQ_INT(wl_normalize("abcdefgh", out), -1); /* longer than a word */
	/* wide or invalid characters must not overflow the output (heap buffer of exactly WL_BYTES under ASan) */
	char *heap = malloc(WL_BYTES);
	T_EQ_INT(wl_normalize("\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xFF\xFF", heap), -1);
	T_EQ_INT(wl_normalize("\xFF\xFF\xFF\xFF\xFF", heap), -1); /* 5 × U+FFFD = 15 bytes: not a word either */
	free(heap);
}

static void test_list(void)
{
	WlList l;
	T_ASSERT(wl_load("apps/wordle/words/guesses.txt", &l) > 5000);
	T_EQ_INT(wl_contains(&l, "kalem"), 1);
	T_EQ_INT(wl_contains(&l, "anlat"), 1); /* verb root */
	T_EQ_INT(wl_contains(&l, "evler"), 0); /* inflected */
	T_EQ_INT(wl_contains(&l, "gitti"), 0);
	T_EQ_INT(wl_contains(&l, "zzzzz"), 0);
	T_EQ_INT(wl_contains(&l, ""), 0);
	wl_free(&l);
	WlList a;
	T_ASSERT(wl_load("apps/wordle/words/answers.txt", &a) > 1000);
	WlList g;
	wl_load("apps/wordle/words/guesses.txt", &g);
	for (int i = 0; i < a.n; i++) {
		if (!wl_contains(&g, a.w[i])) {
			T_FAIL_("answer %s is not a valid guess", a.w[i]);
			break;
		}
		char n[WL_BYTES];
		if (wl_normalize(a.w[i], n) != WL_LEN || strcmp(n, a.w[i]) != 0) {
			T_FAIL_("answer %s is not a normalized 5-letter word", a.w[i]);
			break;
		}
	}
	T_ASSERT(wl_pick(&a, 12345) >= 0 && wl_pick(&a, 12345) < a.n);
	T_EQ_INT(wl_pick(&a, 7), wl_pick(&a, 7));
	wl_free(&a);
	wl_free(&g);
	T_EQ_INT(wl_load("build/host/no-such-file", &l), -1);
}

static void test_keyboard_keeps_best(void)
{
	WlKb kb = { 0 };
	WlMark m[WL_LEN];
	wl_score("kalem", "lamba", m); /* l P, a C, m P, b -, a - */
	wl_kb_update(&kb, "lamba", m);
	T_EQ_INT(wl_kb_get(&kb, 'a'), WL_CORRECT); /* the second a is absent but the key stays correct */
	T_EQ_INT(wl_kb_get(&kb, 'l'), WL_PRESENT);
	T_EQ_INT(wl_kb_get(&kb, 'b'), WL_ABSENT);
	T_EQ_INT(wl_kb_get(&kb, 'z'), WL_UNKNOWN);
	wl_score("kalem", "kalem", m);
	wl_kb_update(&kb, "kalem", m);
	T_EQ_INT(wl_kb_get(&kb, 'l'), WL_CORRECT);
	wl_score("kalem", "ağaçı", m);
	wl_kb_update(&kb, "ağaçı", m);
	T_EQ_INT(wl_kb_get(&kb, 0x11F), WL_ABSENT); /* ğ */
}

static void test_stats(void)
{
	WlStats s = { 0 };
	wl_stats_record(&s, 1, 3);
	wl_stats_record(&s, 1, 4);
	T_EQ_INT(s.streak, 2);
	T_EQ_INT(s.best, 2);
	wl_stats_record(&s, 0, 6);
	T_EQ_INT(s.played, 3);
	T_EQ_INT(s.won, 2);
	T_EQ_INT(s.streak, 0);
	T_EQ_INT(s.best, 2);
	T_EQ_INT(s.dist[2], 1);
	T_EQ_INT(s.dist[3], 1);
	char buf[128];
	T_ASSERT(wl_stats_format(&s, buf, sizeof buf) > 0);
	WlStats t;
	T_EQ_INT(wl_stats_parse(buf, &t), 0);
	T_EQ_INT(t.played, 3);
	T_EQ_INT(t.dist[3], 1);
	T_EQ_INT(wl_stats_parse("v1 3 2 0 2 0 -1 0 0 0 0\n", &t), -1);      /* negative count */
	T_EQ_INT(wl_stats_parse("v1 3 2 0 2 0 5 0 0 0 0\n", &t), -1);       /* more wins in the chart than games won */
	T_EQ_INT(wl_stats_parse("v1 2000000000 2000000000 0 0 2000000000 0 0 0 0 0\n", &t), -1); /* won*100 would overflow */
	T_EQ_INT(wl_stats_parse("garbage", &t), -1);
	T_EQ_INT(t.played, 0); /* reset on garbage */
}

static void test_game_save(void)
{
	WlGame g = { 0 };
	strcpy(g.answer, "kalem");
	strcpy(g.guess[0], "lamba");
	strcpy(g.guess[1], "ağaçı");
	g.n = 2;
	char buf[256];
	T_ASSERT(wl_game_format(&g, buf, sizeof buf) > 0);
	WlGame h;
	T_EQ_INT(wl_game_parse(buf, &h), 0);
	T_EQ_STR(h.answer, "kalem");
	T_EQ_INT(h.n, 2);
	T_EQ_STR(h.guess[1], "ağaçı");
	T_EQ_INT(wl_game_parse("v1\nkalem\n7\n", &h), -1);
	T_EQ_INT(wl_game_parse("v1\nkal\n0\n", &h), -1);
	T_EQ_INT(wl_game_parse("nope", &h), -1);
}

int main(void)
{
	T_RUN(test_score_duplicates);
	T_RUN(test_normalize);
	T_RUN(test_list);
	T_RUN(test_keyboard_keeps_best);
	T_RUN(test_stats);
	T_RUN(test_game_save);
	return T_DONE();
}

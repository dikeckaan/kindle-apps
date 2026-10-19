#include <stdlib.h>
#include <string.h>
#include "kelime/kelime.h"
#include "t.h"

static const char *SAMPLE =
	"# Kelime word list: de\n"
	"Hund\tder\tnoun\t1\tein Haustier, das bellt\tDer {Hund} bellt.\ttr=köpek|en=dog\ttr=Köpek havlıyor.|en=The dog barks.\n"
	"Katze\tdie\tnoun\t1\tein Haustier, das miaut\tDie {Katze} schläft.\ttr=kedi|en=cat\t\n"
	"Haus\tdas\tnoun\t1\tein Gebäude zum Wohnen\t\ttr=ev|en=house\t\n"
	"Baum\tder\tnoun\t1\teine große Pflanze\tDer {Baum} ist alt.\ttr=ağaç|en=tree\t\n"
	"Wasser\tdas\tnoun\t1\tdurchsichtige Flüssigkeit\t\ttr=su|en=water\t\n"
	"gehen\t\tverb\t1\tsich zu Fuß fortbewegen\tWir {gehen} nach Hause.\ttr=gitmek|en=to go\t\n"
	"kommen\t\tverb\t1\tsich nähern\t\ttr=gelmek|en=to come\t\n"
	"laufen\t\tverb\t2\tschnell gehen\t\ttr=koşmak\t\n"
	"Eau\tl'\tnoun\t2\t\t\tde=Wasser\t\n"
	"bad line without fields\n"
	"Fehler\tder\tnoun\t9\tbad level\t\t\t\n"
	"Tier\tdas\tnoun\t2\tLebewesen\t\ttr=hayvan|en=animal\t\n";

static KlDb load_sample(void)
{
	KlDb db;
	char *buf = strdup(SAMPLE);
	T_EQ_INT(kl_parse(buf, &db), 10);
	return db;
}

static void test_parse(void)
{
	KlDb db = load_sample();
	T_EQ_STR(db.w[0].word, "Hund");
	T_EQ_STR(db.w[0].art, "der");
	T_EQ_INT(db.w[0].pos, KL_POS_NOUN);
	T_EQ_INT(db.w[0].level, 1);
	T_EQ_STR(db.w[0].def, "ein Haustier, das bellt");
	T_EQ_STR(db.w[5].word, "gehen");
	T_EQ_INT(db.w[5].pos, KL_POS_VERB);
	T_EQ_INT(db.level_n[1], 7);
	T_EQ_INT(db.level_n[2], 3);
	T_EQ_INT(db.level_n[0], 10);
	T_EQ_STR(db.w[9].word, "Tier");
	kl_free(&db);
	char empty[] = "# nothing\n";
	T_EQ_INT(kl_parse(empty, &db), -1);
	T_EQ_STR(kl_level_name(1), "A1");
	T_EQ_STR(kl_level_name(6), "C2");
	T_EQ_STR(kl_level_name(7), "");
	T_EQ_INT(kl_pos_parse("adj"), KL_POS_ADJ);
	T_EQ_INT(kl_pos_parse("xyz"), KL_POS_OTHER);
}

static void test_fields(void)
{
	char out[64];
	T_EQ_INT(kl_field("tr=köpek|en=dog", "en", out, sizeof out), 3);
	T_EQ_STR(out, "dog");
	T_EQ_INT(kl_field("tr=köpek|en=dog", "tr", out, sizeof out), 6);
	T_EQ_STR(out, "köpek");
	T_EQ_INT(kl_field("tr=köpek|en=dog", "de", out, sizeof out), 0);
	T_EQ_STR(out, "");
	T_EQ_INT(kl_field("tre=x|tr=y", "tr", out, sizeof out), 1);
	T_EQ_STR(out, "y");
	T_EQ_INT(kl_field("", "tr", out, sizeof out), 0);
	T_EQ_INT(kl_field("tr=", "tr", out, sizeof out), 0);
	T_EQ_INT(kl_field("en=abcdefgh", "en", out, 4), 3);
	T_EQ_STR(out, "abc");
	T_ASSERT(kl_has_field("tr=a", "tr"));
	T_ASSERT(!kl_has_field("tr=a", "en"));
}

static void test_cloze(void)
{
	char out[128];
	T_EQ_INT(kl_cloze("Der {Hund} bellt.", "____", out, sizeof out), 1);
	T_EQ_STR(out, "Der ____ bellt.");
	T_EQ_INT(kl_cloze("{Ev}de kimse yok.", "____", out, sizeof out), 1);
	T_EQ_STR(out, "____de kimse yok.");
	T_EQ_INT(kl_cloze("A {dog} is a pet; {dogs} bark.", "___", out, sizeof out), 1);
	T_EQ_STR(out, "A ___ is a pet; ___ bark.");
	T_EQ_INT(kl_cloze("Odd {brace", "___", out, sizeof out), 0);
	T_EQ_STR(out, "Odd brace");
	T_EQ_INT(kl_cloze("No mark here.", "____", out, sizeof out), 0);
	T_EQ_STR(out, "No mark here.");
	kl_unmark("Der {Hund} bellt.", out, sizeof out);
	T_EQ_STR(out, "Der Hund bellt.");
	T_EQ_INT(kl_cloze("Der {Hund} bellt.", "____", out, 8), 1);
	T_EQ_STR(out, "Der ___");
	KlWord a = { "Hund", "der", "", "", "", "", 1, 1, "", "", "" }, b = { "eau", "l'", "", "", "", "", 1, 1, "", "", "" }, c = { "go", "", "", "", "", "", 2, 1, "", "", "" };
	kl_display(&a, out, sizeof out);
	T_EQ_STR(out, "der Hund");
	kl_display(&b, out, sizeof out);
	T_EQ_STR(out, "l'eau");
	kl_display(&c, out, sizeof out);
	T_EQ_STR(out, "go");
}

static void test_deck(void)
{
	KlDeck d = { 0 };
	kl_deck_parse("Hund\t2\t100\t3\t2\nbad\nKatze\t1\t95\t1\t0\n\tx\nHund\t3\t101\t4\t3\nX\t9\t1\t1\t1\n", &d);
	T_EQ_INT(d.n, 2);
	KlCard *c = kl_deck_find(&d, "Hund");
	T_ASSERT(c != NULL);
	if (c) {
		T_EQ_INT(c->box, 3); /* a later line for the same word wins */
		T_EQ_INT(c->due, 101);
	}
	T_ASSERT(kl_deck_find(&d, "Baum") == NULL);
	char buf[256];
	int n = kl_deck_format(&d, buf, sizeof buf);
	T_ASSERT(n > 0);
	/* the first version's lines (no flags): an answered word counts as taught */
	T_EQ_STR(buf, "Hund\t3\t101\t4\t3\t1\nKatze\t1\t95\t1\t0\t1\n");
	T_EQ_INT(kl_deck_format(&d, buf, 10), -1);
	KlDeck m = { 0 };
	kl_deck_parse("a\t0\t5\t0\t0\nb\t0\t5\t0\t0\t1\nc\t4\t9\t2\t2\t3\nd\t2\t9\t2\t2\t99\n", &m);
	T_EQ_INT(m.n, 4);
	T_ASSERT(!kl_card_taught(kl_deck_find(&m, "a")));
	T_ASSERT(kl_card_taught(kl_deck_find(&m, "b")));
	T_EQ_INT(kl_deck_find(&m, "c")->flags, KL_F_TAUGHT | KL_F_KNOWN);
	T_EQ_INT(kl_deck_find(&m, "d")->flags, KL_F_TAUGHT | KL_F_KNOWN); /* unknown bits dropped */
	T_EQ_INT(kl_deck_format(&m, buf, sizeof buf), (int) strlen(buf));
	T_EQ_STR(buf, "b\t0\t5\t0\t0\t1\nc\t4\t9\t2\t2\t3\nd\t2\t9\t2\t2\t3\n"); /* a never-taught card is not saved */
	kl_deck_free(&m);
	/* many keys: growth keeps every lookup working */
	char key[16];
	for (int i = 0; i < 1000; i++) {
		snprintf(key, sizeof key, "w%d", i);
		KlCard *k = kl_deck_get(&d, key);
		T_ASSERT(k != NULL);
		if (k) k->due = i;
	}
	T_EQ_INT(d.n, 1002);
	for (int i = 0; i < 1000; i += 37) {
		snprintf(key, sizeof key, "w%d", i);
		KlCard *k = kl_deck_find(&d, key);
		T_ASSERT(k && k->due == i);
	}
	T_ASSERT(kl_deck_get(&d, "Hund") == kl_deck_find(&d, "Hund"));
	T_EQ_INT(d.n, 1002);
	kl_deck_free(&d);
	T_EQ_INT(d.n, 0);
}

static void test_leitner(void)
{
	KlCard c = { "x", 0, 0, 0, 0, 0 };
	T_ASSERT(!kl_card_due(&c, 10)); /* never seen */
	kl_card_answer(&c, 1, 10);
	T_EQ_INT(c.box, 1);
	T_EQ_INT(c.due, 11);
	T_ASSERT(!kl_card_due(&c, 10));
	T_ASSERT(kl_card_due(&c, 11));
	kl_card_answer(&c, 1, 11);
	T_EQ_INT(c.box, 2);
	T_EQ_INT(c.due, 13);
	for (int i = 0; i < 10; i++) kl_card_answer(&c, 1, 20);
	T_EQ_INT(c.box, KL_BOX_MAX);
	T_EQ_INT(c.due, 20 + kl_interval(KL_BOX_MAX));
	kl_card_answer(&c, 0, 30);
	T_EQ_INT(c.box, 1);
	T_EQ_INT(c.due, 30);
	T_ASSERT(kl_card_due(&c, 30));
	T_EQ_INT(c.seen, 13);
	T_EQ_INT(c.right, 12);
	for (int b = 1; b < KL_BOX_MAX; b++) T_ASSERT(kl_interval(b) < kl_interval(b + 1));
	T_EQ_INT(kl_interval(99), kl_interval(KL_BOX_MAX));
	/* teaching, a second answer in the same lesson, "I know it", forgetting */
	KlCard t = { "y", 0, 0, 0, 0, 0 };
	T_ASSERT(!kl_card_taught(&t));
	kl_card_teach(&t, 40);
	T_ASSERT(kl_card_taught(&t));
	T_ASSERT(kl_card_due(&t, 40)); /* taught, not practised yet: due right away */
	T_EQ_INT(t.box, 0);
	kl_card_answer(&t, 1, 40);
	T_EQ_INT(t.box, 1);
	kl_card_again(&t, 1, 40); /* right again in the same lesson: counted, no box up */
	T_EQ_INT(t.box, 1);
	T_EQ_INT(t.seen, 2);
	T_EQ_INT(t.right, 2);
	kl_card_again(&t, 0, 40);
	T_EQ_INT(t.box, 1);
	T_EQ_INT(t.due, 40);
	kl_card_teach(&t, 45); /* teaching again keeps the schedule */
	T_EQ_INT(t.due, 40);
	kl_card_know(&t, 50);
	T_EQ_INT(t.box, KL_KNOWN_BOX);
	T_EQ_INT(t.due, 50 + kl_interval(KL_KNOWN_BOX));
	T_ASSERT(t.flags & KL_F_KNOWN);
	T_ASSERT(t.seen >= 2);
	kl_card_forget(&t);
	T_ASSERT(!kl_card_taught(&t));
	T_ASSERT(!kl_card_due(&t, 99));
}

static void test_progress(void)
{
	KlDb db = load_sample();
	KlDeck d = { 0 };
	kl_deck_parse("Hund\t3\t100\t3\t3\nKatze\t1\t50\t2\t1\nTier\t2\t200\t2\t2\nunknown\t5\t1\t9\t9\n", &d);
	int l, s, du;
	kl_progress(&db, &d, 1, 60, &l, &s, &du);
	T_EQ_INT(l, 1);
	T_EQ_INT(s, 2);
	T_EQ_INT(du, 1);
	kl_progress(&db, &d, 0, 60, &l, &s, &du);
	T_EQ_INT(l, 2);
	T_EQ_INT(s, 3);
	kl_progress(&db, &d, 2, 300, &l, &s, &du);
	T_EQ_INT(du, 1);
	kl_deck_free(&d);
	kl_free(&db);
}

static void test_stats(void)
{
	KlStats s;
	kl_stats_parse("", &s);
	T_EQ_INT(s.goal, 50);
	T_EQ_INT(s.xp, 0);
	int d0 = kl_days(2026, 10, 19);
	T_EQ_INT(kl_days(1970, 1, 1), 0);
	T_EQ_INT(kl_days(2000, 3, 1) - kl_days(2000, 2, 28), 2); /* leap year */
	T_EQ_INT(kl_days(2026, 1, 1) - kl_days(2025, 12, 31), 1);
	kl_stats_answer(&s, 1, d0);
	kl_stats_answer(&s, 0, d0);
	T_EQ_INT(s.xp, KL_XP_RIGHT);
	T_EQ_INT(s.answers, 2);
	T_EQ_INT(s.right, 1);
	T_EQ_INT(kl_today_xp(&s, d0), KL_XP_RIGHT);
	T_EQ_INT(kl_today_xp(&s, d0 + 1), 0);
	T_EQ_INT(kl_streak_now(&s, d0), 0);
	kl_streak_touch(&s, d0);
	T_EQ_INT(s.streak, 1);
	kl_streak_touch(&s, d0); /* a second lesson the same day */
	T_EQ_INT(s.streak, 1);
	T_EQ_INT(s.lessons, 2);
	kl_streak_touch(&s, d0 + 1);
	T_EQ_INT(s.streak, 2);
	T_EQ_INT(kl_streak_now(&s, d0 + 2), 2); /* alive until the end of the next day */
	T_EQ_INT(kl_streak_now(&s, d0 + 3), 0);
	kl_streak_touch(&s, d0 + 4);
	T_EQ_INT(s.streak, 1);
	T_EQ_INT(s.best, 2);
	kl_stats_bonus(&s, 20, d0 + 4);
	T_EQ_INT(kl_today_xp(&s, d0 + 4), 20);
	char buf[512];
	T_ASSERT(kl_stats_format(&s, buf, sizeof buf) > 0);
	KlStats r;
	kl_stats_parse(buf, &r);
	T_EQ_INT(r.xp, s.xp);
	T_EQ_INT(r.streak, 1);
	T_EQ_INT(r.best, 2);
	T_EQ_INT(r.last_day, d0 + 4);
	T_EQ_INT(r.today_xp, 20);
	T_EQ_INT(r.lessons, 4);
	T_EQ_INT(kl_stats_format(&s, buf, 8), -1);
}

static const char *opt(const KlDb *db, const KlQuestion *q, int i, char *out, int cap)
{
	if (q->type == KL_Q_MEANING || q->type == KL_Q_TF) kl_field(db->w[q->opts[i]].tr, "tr", out, cap);
	else kl_display(&db->w[q->opts[i]], out, cap);
	return out;
}

static void test_make(void)
{
	KlDb db = load_sample();
	unsigned rng = 12345;
	KlQuestion q;
	for (int round = 0; round < 200; round++) {
		int type = round % KL_Q_N;
		int word = round % db.n;
		int ok = kl_make(&db, word, type, "tr", &rng, &q);
		T_EQ_INT(ok, kl_can_ask(&db.w[word], type, "tr") ? 1 : 0);
		if (!ok) continue;
		T_EQ_INT(q.word, word);
		if (type == KL_Q_TF) {
			T_EQ_INT(q.n_opts, 1);
			T_EQ_INT(q.answer, q.opts[0] == word);
			continue;
		}
		T_ASSERT(q.n_opts >= 2 && q.n_opts <= 4);
		T_EQ_INT(q.opts[q.answer], word);
		char a[64], b[64];
		for (int i = 0; i < q.n_opts; i++) {
			if (i != q.answer) T_ASSERT(q.opts[i] != word);
			for (int j = i + 1; j < q.n_opts; j++) T_ASSERT(strcmp(opt(&db, &q, i, a, sizeof a), opt(&db, &q, j, b, sizeof b)) != 0);
			/* every option of a translation question has a Turkish translation */
			if (type == KL_Q_MEANING || type == KL_Q_WORD) T_ASSERT(kl_has_field(db.w[q.opts[i]].tr, "tr"));
		}
	}
	/* the distractors share the part of speech when the list allows it */
	rng = 99;
	T_ASSERT(kl_make(&db, 0, KL_Q_MEANING, "tr", &rng, &q));
	for (int i = 0; i < q.n_opts; i++) T_EQ_INT(db.w[q.opts[i]].pos, KL_POS_NOUN);
	/* no translation into this language */
	T_ASSERT(!kl_make(&db, 0, KL_Q_MEANING, "fr", &rng, &q));
	T_ASSERT(kl_make(&db, 0, KL_Q_DEFINE, "fr", &rng, &q));
	T_ASSERT(!kl_make(&db, 2, KL_Q_CLOZE, "tr", &rng, &q)); /* Haus: no marked example */
	T_ASSERT(kl_make(&db, 0, KL_Q_CLOZE, "tr", &rng, &q));
	T_ASSERT(!kl_make(&db, -1, KL_Q_DEFINE, "tr", &rng, &q));
	kl_free(&db);
}

/* a course-sized list: starter themes, then nouns, verbs, an adjective and a small word in frequency order */
static KlDb load_course(void)
{
	static const char *order[] = { "N0", "sein", "N1", "N2", "W0", "N3", "haben", "N4", "N5", "A0", "N6", "N7", "gehen", "N8", "N9", "X0", "N10", "N11", "N12" };
	size_t cap = 8192, o = 0;
	char *buf = malloc(cap);
	o += (size_t) snprintf(buf + o, cap - o,
			       "# course\n"
			       "hallo\t\tintj\t1\tGruß\t{Hallo}, Tom!\ttr=merhaba|en=hello\ttr=Merhaba Tom!\t/haˈloː/\tha-LOO\tgreet\n"
			       "danke\t\tintj\t1\tDank\t{Danke}!\ttr=teşekkürler|en=thanks\t\t/ˈdaŋkə/\tDAN-ke\tgreet\n"
			       "ich\t\tpron\t1\tdie eigene Person\t{Ich} bin Tom.\ttr=ben|en=I\t\t\t\tpron\n"
			       "du\t\tpron\t1\tdie andere Person\t{Du} bist Tom.\ttr=sen|en=you\t\t\t\tpron\n"
			       "eins\t\tnum\t1\tdie Zahl 1\t\ttr=bir|en=one\t\t\t\tnum\n"
			       "zwei\t\tnum\t1\tdie Zahl 2\t\ttr=iki|en=two\t\t\t\tnum\n");
	for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
		const char *w = order[i];
		const char *pos = w[0] == 'N' || w[0] == 'X' ? "noun" : w[0] == 'A' ? "adj" : w[0] == 'W' ? "conj" : "verb";
		char tr[32];
		snprintf(tr, sizeof tr, "t%s", w);
		if (!strcmp(w, "sein")) snprintf(tr, sizeof tr, "olmak");
		if (!strcmp(w, "haben")) snprintf(tr, sizeof tr, "sahip olmak");
		if (!strcmp(w, "gehen")) snprintf(tr, sizeof tr, "gitmek");
		if (w[0] == 'X') o += (size_t) snprintf(buf + o, cap - o, "%s\t\t%s\t1\tdef %s\t\ten=%s\t\n", w, pos, w, w); /* no Turkish */
		else
			o += (size_t) snprintf(buf + o, cap - o, "%s\t%s\t%s\t1\tein Wort %s\tDas ist {%s}.\ttr=%s|en=e%s\t\n", w, w[0] == 'N' ? "der" : "", pos, w, w,
					       tr, w);
	}
	for (int i = 0; i < 4; i++) o += (size_t) snprintf(buf + o, cap - o, "M%d\tdie\tnoun\t2\tWort M%d\tDie {M%d} ist da.\ttr=m%d\t\n", i, i, i, i);
	KlDb db;
	T_EQ_INT(kl_parse(buf, &db), 6 + 19 + 4);
	return db;
}

static void test_parse_extra(void)
{
	KlDb db = load_course();
	T_EQ_STR(db.w[0].ipa, "/haˈloː/");
	T_EQ_STR(db.w[0].say, "ha-LOO");
	T_EQ_STR(db.w[0].theme, "greet");
	T_EQ_STR(db.w[2].ipa, "");
	T_EQ_STR(db.w[2].theme, "pron");
	T_EQ_STR(db.w[7].theme, ""); /* sein: 8 fields only */
	T_EQ_INT(kl_find(&db, "haben"), 12);
	T_EQ_INT(kl_find(&db, "nope"), -1);
	T_EQ_INT(kl_theme_kind("num"), KL_U_NUM);
	T_EQ_INT(kl_theme_kind(""), -1);
	T_EQ_INT(kl_gender(&db.w[6]), 'm'); /* der N0 */
	T_EQ_INT(kl_gender(&db.w[0]), 0);
	KlWord f = { "casa", "la", "", "", "", "", 1, 1, "", "", "" }, n = { "Haus", "das", "", "", "", "", 1, 1, "", "", "" };
	T_EQ_INT(kl_gender(&f), 'f');
	T_EQ_INT(kl_gender(&n), 'n');
	kl_free(&db);
}

static void test_course(void)
{
	KlDb db = load_course();
	KlCourse c;
	T_EQ_INT(kl_course_build(&db, "tr", 6, &c), 0);
	T_EQ_INT(c.lv_start[1], 0);
	T_EQ_INT(c.lv_start[2], 24); /* X0 has no Turkish translation: not taught to a Turkish speaker */
	T_EQ_INT(c.lv_start[3], 28);
	T_EQ_INT(c.n, 28);
	T_EQ_INT(c.at[kl_find(&db, "X0")], -1);
	T_EQ_STR(db.w[c.word[0]].word, "hallo");
	T_EQ_STR(db.w[c.word[2]].word, "ich");
	/* units: the three themes, then nouns 1, verbs 1, small words 1, adjectives 1, nouns 2 (by their first word) */
	static const int kinds[] = { KL_U_GREET, KL_U_PRON, KL_U_NUM, KL_U_NOUNS, KL_U_VERBS, KL_U_WORDS, KL_U_ADJS, KL_U_NOUNS, KL_U_NOUNS };
	T_EQ_INT(c.n_unit, 9);
	for (int i = 0; i < c.n_unit && i < 9; i++) T_EQ_INT(c.unit[i].kind, kinds[i]);
	T_EQ_INT(c.unit[3].num, 1);
	T_EQ_INT(c.unit[3].n, 6);
	T_EQ_INT(c.unit[7].num, 2);
	T_EQ_INT(c.unit[7].n, 7); /* 13 nouns in units of 6: the one left over joins the second unit */
	T_EQ_INT(c.unit[8].level, 2);
	T_EQ_INT(c.unit[0].num, 0);
	T_EQ_STR(db.w[c.word[c.unit[4].start]].word, "sein");
	for (int i = 0; i < c.n; i++) {
		T_EQ_INT(c.at[c.word[i]], i);
		int u = kl_course_unit(&c, i);
		T_ASSERT(u >= 0 && i >= c.unit[u].start && i < c.unit[u].start + c.unit[u].n);
	}
	T_EQ_INT(kl_course_unit(&c, c.n), -1);
	KlDeck d = { 0 };
	int taught, total;
	kl_course_count(&c, &db, &d, 1, &taught, &total);
	T_EQ_INT(taught, 0);
	T_EQ_INT(total, 24);
	kl_card_teach(kl_deck_get(&d, "hallo"), 5);
	kl_course_count(&c, &db, &d, 1, &taught, &total);
	T_EQ_INT(taught, 1);
	kl_course_free(&c);
	/* an English speaker gets X0 too */
	T_EQ_INT(kl_course_build(&db, "en", 6, &c), 0);
	T_EQ_INT(c.lv_start[2], 25);
	kl_course_free(&c);
	kl_deck_free(&d);
	kl_free(&db);
}

static int in(const int *a, int n, int x)
{
	for (int i = 0; i < n; i++)
		if (a[i] == x) return 1;
	return 0;
}

static void test_plan(void)
{
	KlDb db = load_course();
	KlCourse c;
	kl_course_build(&db, "tr", 6, &c);
	KlDeck d = { 0 };
	KlPlan p;
	int today = 500;
	kl_plan(&db, &c, &d, 1, today, 5, 10, &p);
	/* greetings (2) and pronouns (2): a unit with fewer than three words left is joined by the next one */
	T_EQ_INT(p.n_new, 4);
	T_EQ_INT(p.level, 1);
	T_EQ_INT(p.unit, 0);
	T_EQ_STR(db.w[p.new_w[0]].word, "hallo");
	T_EQ_STR(db.w[p.new_w[3]].word, "du");
	T_EQ_INT(p.n_rev, 0);
	T_EQ_INT(p.due_all, 0);
	for (int i = 0; i < p.n_new; i++) kl_card_teach(kl_deck_get(&d, db.w[p.new_w[i]].word), today);
	/* taught but not practised yet: due, so they come back as reviews */
	kl_plan(&db, &c, &d, 1, today, 5, 10, &p);
	T_EQ_INT(p.n_rev, 4);
	T_EQ_STR(db.w[p.new_w[0]].word, "eins");
	T_EQ_INT(p.n_new, 5); /* eins zwei, then N0 N1 N2 of the nouns */
	T_EQ_STR(db.w[p.new_w[2]].word, "N0");
	for (int i = 0; i < 4; i++) kl_card_answer(kl_deck_get(&d, db.w[p.rev[i]].word), 1, today);
	/* a lesson of 3 keeps to the unit */
	kl_plan(&db, &c, &d, 1, today, 3, 10, &p);
	T_EQ_INT(p.n_new, 3);
	T_EQ_INT(p.n_rev, 0);
	/* overdue words first, then the lowest box */
	kl_deck_get(&d, "hallo")->due = today - 3;
	kl_deck_get(&d, "ich")->due = today - 1;
	kl_deck_get(&d, "du")->due = today - 1;
	kl_deck_get(&d, "ich")->box = 2;
	kl_plan(&db, &c, &d, 0, today, 5, 2, &p);
	T_EQ_INT(p.n_new, 0); /* review only */
	T_EQ_INT(p.n_rev, 2);
	T_EQ_INT(p.due_all, 3);
	T_EQ_STR(db.w[p.rev[0]].word, "hallo");
	T_EQ_STR(db.w[p.rev[1]].word, "du");
	/* level 1 done: the new words come from level 2 */
	for (int i = 0; i < c.lv_start[2]; i++) kl_card_know(kl_deck_get(&d, db.w[c.word[i]].word), today);
	kl_plan(&db, &c, &d, 1, today, 5, 0, &p);
	T_EQ_INT(p.level, 2);
	T_EQ_INT(p.n_new, 4);
	T_EQ_STR(db.w[p.new_w[0]].word, "M0");
	for (int i = 0; i < db.n; i++) kl_card_know(kl_deck_get(&d, db.w[i].word), today);
	kl_plan(&db, &c, &d, 1, today, 5, 5, &p);
	T_EQ_INT(p.n_new, 0); /* everything taught */
	kl_deck_free(&d);
	kl_course_free(&c);
	kl_free(&db);
}

static void test_qtype(void)
{
	KlDb db = load_course();
	const KlWord *n0 = &db.w[kl_find(&db, "N0")], *eins = &db.w[kl_find(&db, "eins")], *x0 = &db.w[kl_find(&db, "X0")];
	KlCard c = { "N0", 0, 0, 0, 0, 0 };
	T_EQ_INT(kl_qtype(n0, NULL, "tr"), KL_Q_MEANING);
	T_EQ_INT(kl_qtype(n0, &c, "tr"), KL_Q_MEANING);
	c.flags = KL_F_TAUGHT, c.box = 1, c.seen = 1;
	T_EQ_INT(kl_qtype(n0, &c, "tr"), KL_Q_MEANING);
	c.box = 2, c.seen = 2;
	T_EQ_INT(kl_qtype(n0, &c, "tr"), KL_Q_WORD);
	c.box = 3;
	T_EQ_INT(kl_qtype(n0, &c, "tr"), KL_Q_CLOZE);
	c.box = 4, c.seen = 1; /* never the definition before two answers */
	T_EQ_INT(kl_qtype(n0, &c, "tr"), KL_Q_CLOZE);
	c.seen = 2;
	T_EQ_INT(kl_qtype(n0, &c, "tr"), KL_Q_DEFINE);
	T_EQ_INT(kl_qtype(eins, &c, "tr"), KL_Q_DEFINE);
	c.box = 3;
	T_EQ_INT(kl_qtype(eins, &c, "tr"), KL_Q_WORD); /* no example to blank: one step easier */
	/* no Turkish translation: only the gap, and the definition after two answers */
	c.box = 1, c.seen = 1;
	T_EQ_INT(kl_qtype(x0, &c, "tr"), -1);
	c.seen = 2;
	T_EQ_INT(kl_qtype(x0, &c, "tr"), KL_Q_DEFINE);
	kl_free(&db);
}

static void test_practice(void)
{
	KlDb db = load_course();
	KlCourse c;
	kl_course_build(&db, "tr", 6, &c);
	KlDeck d = { 0 };
	KlPlan p;
	KlQuestion q[40];
	int today = 900;
	kl_plan(&db, &c, &d, 1, today, 5, 10, &p);
	int n = kl_practice(&db, &d, "tr", &p, 77, q, 40);
	T_EQ_INT(n, 2 * p.n_new);
	for (int i = 0; i < n; i++) {
		T_ASSERT(in(p.new_w, p.n_new, q[i].word)); /* only the words just taught */
		T_EQ_INT(q[i].type, i < p.n_new ? KL_Q_MEANING : KL_Q_WORD);
		for (int k = 0; k < q[i].n_opts; k++) T_ASSERT(in(p.new_w, p.n_new, q[i].opts[k])); /* options: this lesson's words */
		T_EQ_INT(q[i].opts[q[i].answer], q[i].word);
		if (i) T_ASSERT(q[i].word != q[i - 1].word);
	}
	for (int i = 0; i < p.n_new; i++) {
		int a = 0, b = 0;
		for (int k = 0; k < n; k++) a += q[k].word == p.new_w[i] && q[k].type == KL_Q_MEANING, b += q[k].word == p.new_w[i] && q[k].type == KL_Q_WORD;
		T_EQ_INT(a, 1);
		T_EQ_INT(b, 1);
	}
	/* reviews come between the two rounds, at the type their box allows; never a definition before two answers */
	for (unsigned seed = 1; seed < 40; seed++) {
		KlDeck r = { 0 };
		unsigned rng = seed;
		for (int i = 0; i < c.lv_start[2]; i++) {
			KlCard *k = kl_deck_get(&r, db.w[c.word[i]].word);
			if (kl_rand(&rng) % 3 == 0) continue;
			kl_card_teach(k, today);
			k->box = (int) (kl_rand(&rng) % 6);
			k->seen = (int) (kl_rand(&rng) % 4);
			k->due = today - (int) (kl_rand(&rng) % 3);
		}
		kl_plan(&db, &c, &r, 1, today, 3, 8, &p);
		n = kl_practice(&db, &r, "tr", &p, seed, q, 40);
		T_ASSERT(n >= 2 * p.n_new);
		for (int i = 0; i < n; i++) {
			const KlCard *k = kl_deck_find(&r, db.w[q[i].word].word);
			T_ASSERT(in(p.new_w, p.n_new, q[i].word) || in(p.rev, p.n_rev, q[i].word));
			if (q[i].type == KL_Q_DEFINE) T_ASSERT(k && k->seen >= 2 && k->box >= 4);
			if (i >= p.n_new && i < n - p.n_new) T_ASSERT(in(p.rev, p.n_rev, q[i].word));
		}
		kl_deck_free(&r);
	}
	/* easy recognition: no option whose translation shares a word with the answer's */
	unsigned rng = 3;
	int sein = kl_find(&db, "sein"), haben = kl_find(&db, "haben"), pref[1] = { haben };
	for (int i = 0; i < 30; i++) {
		KlQuestion e;
		T_ASSERT(kl_make_ex(&db, sein, KL_Q_MEANING, "tr", &rng, pref, 1, 1, &e));
		T_ASSERT(!in(e.opts, e.n_opts, haben));
		T_ASSERT(kl_make_ex(&db, sein, KL_Q_MEANING, "tr", &rng, pref, 1, 0, &e));
		T_ASSERT(in(e.opts, e.n_opts, haben)); /* not easy: the known word is offered first */
	}
	/* a missed question comes back later in the lesson */
	KlQuestion again, first;
	T_ASSERT(kl_make(&db, sein, KL_Q_MEANING, "tr", &rng, &first));
	T_ASSERT(kl_again(&db, &first, "tr", &rng, &again));
	T_EQ_INT(again.word, sein);
	T_EQ_INT(again.type, KL_Q_MEANING);
	first.type = KL_Q_DEFINE;
	T_ASSERT(kl_again(&db, &first, "tr", &rng, &again));
	T_EQ_INT(again.type, KL_Q_WORD); /* a hard question comes back one step easier */
	KlQuestion qs[5];
	int nq = 0;
	for (int i = 0; i < 4; i++) {
		qs[nq] = first;
		qs[nq++].word = i;
	}
	again.word = 9;
	T_EQ_INT(kl_insert(qs, &nq, 5, 1, 2, &again), 3);
	T_EQ_INT(nq, 5);
	T_EQ_INT(qs[3].word, 9);
	T_EQ_INT(qs[4].word, 3);
	T_EQ_INT(kl_insert(qs, &nq, 5, 0, 1, &again), -1); /* full */
	nq = 2;
	T_EQ_INT(kl_insert(qs, &nq, 5, 1, 3, &again), 2); /* past the end: appended */
	kl_deck_free(&d);
	kl_course_free(&c);
	kl_free(&db);
}

static void test_place(void)
{
	int asked[KL_LEVELS + 1] = { 0 }, right[KL_LEVELS + 1] = { 0 }, more;
	T_EQ_INT(kl_place_level(asked, right, &more), 1);
	T_EQ_INT(more, 1);
	asked[1] = 5, right[1] = 5;
	T_EQ_INT(kl_place_level(asked, right, &more), 2);
	T_EQ_INT(more, 1);
	asked[2] = 5, right[2] = 3; /* 3 of 5: not passed, the course starts at A2 */
	T_EQ_INT(kl_place_level(asked, right, &more), 2);
	T_EQ_INT(more, 0);
	right[2] = 4;
	T_EQ_INT(kl_place_level(asked, right, &more), 3);
	asked[1] = 5, right[1] = 2;
	T_EQ_INT(kl_place_level(asked, right, &more), 1);
	T_EQ_INT(more, 0);
	for (int lv = 1; lv <= KL_LEVELS; lv++) asked[lv] = right[lv] = 5;
	T_EQ_INT(kl_place_level(asked, right, &more), KL_LEVELS);
	T_EQ_INT(more, 0);
	T_ASSERT(kl_place_passed(5, 4));
	T_ASSERT(!kl_place_passed(5, 3));
	T_ASSERT(!kl_place_passed(0, 0));
	KlDb db = load_course();
	KlCourse c;
	kl_course_build(&db, "tr", 6, &c);
	unsigned rng = 11;
	int out[KL_PLACE_PER_LEVEL];
	T_EQ_INT(kl_place_pick(&c, 1, &rng, out, KL_PLACE_PER_LEVEL), KL_PLACE_PER_LEVEL);
	for (int i = 0; i < KL_PLACE_PER_LEVEL; i++) {
		T_EQ_INT(db.w[out[i]].level, 1);
		T_ASSERT(c.at[out[i]] >= 0);
		for (int j = 0; j < i; j++) T_ASSERT(out[i] != out[j]);
	}
	T_EQ_INT(kl_place_pick(&c, 2, &rng, out, KL_PLACE_PER_LEVEL), 4); /* only four words at A2 */
	T_EQ_INT(kl_place_pick(&c, 3, &rng, out, KL_PLACE_PER_LEVEL), 0);
	kl_course_free(&c);
	kl_free(&db);
}

/* the shipped word lists: every language starts with its starter themes, with a Turkish translation */
static void test_data(void)
{
	static const char *langs[] = { "tr", "en", "de", "fr", "es", "it", "pt" };
	for (size_t l = 0; l < sizeof langs / sizeof langs[0]; l++) {
		char path[64];
		snprintf(path, sizeof path, "apps/kelime/data/%s.txt", langs[l]);
		KlDb db;
		if (kl_load(path, &db) < 0) {
			fprintf(stderr, "skip %s (not found)\n", path);
			continue;
		}
		const char *native = !strcmp(langs[l], "tr") ? "en" : "tr";
		int themed = 0, ipa = 0;
		for (int i = 0; i < db.n; i++) {
			if (db.w[i].theme[0]) {
				themed++;
				T_EQ_INT(db.w[i].level, 1);
				T_ASSERT(kl_theme_kind(db.w[i].theme) >= 0);
				T_ASSERT(kl_has_field(db.w[i].tr, native));
			}
			ipa += db.w[i].ipa[0] != 0;
		}
		if (themed < 35) fprintf(stderr, "%s: %d starter words\n", langs[l], themed);
		T_ASSERT(themed >= 35);
		T_ASSERT(ipa * 2 > db.n);
		KlCourse c;
		T_EQ_INT(kl_course_build(&db, native, 15, &c), 0);
		T_EQ_INT(c.unit[0].kind, KL_U_GREET);
		T_ASSERT(c.lv_start[2] > 200);
		kl_course_free(&c);
		kl_free(&db);
	}
}

int main(void)
{
	T_RUN(test_parse);
	T_RUN(test_parse_extra);
	T_RUN(test_fields);
	T_RUN(test_cloze);
	T_RUN(test_deck);
	T_RUN(test_leitner);
	T_RUN(test_progress);
	T_RUN(test_stats);
	T_RUN(test_make);
	T_RUN(test_course);
	T_RUN(test_plan);
	T_RUN(test_qtype);
	T_RUN(test_practice);
	T_RUN(test_place);
	T_RUN(test_data);
	return T_DONE();
}

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
	KlWord a = { "Hund", "der", "", "", "", "", 1, 1 }, b = { "eau", "l'", "", "", "", "", 1, 1 }, c = { "go", "", "", "", "", "", 2, 1 };
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
	T_EQ_STR(buf, "Hund\t3\t101\t4\t3\nKatze\t1\t95\t1\t0\n");
	T_EQ_INT(kl_deck_format(&d, buf, 10), -1);
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
	KlCard c = { "x", 0, 0, 0, 0 };
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

static void test_lesson(void)
{
	KlDb db = load_sample();
	KlDeck d = { 0 };
	KlQuestion q[10];
	int today = 1000;
	int n = kl_lesson(&db, &d, "tr", 1, today, 7, q, 10);
	T_EQ_INT(n, 7); /* seven A1 words, all new */
	for (int i = 0; i < n; i++) {
		T_EQ_INT(db.w[q[i].word].level, 1);
		T_EQ_INT(q[i].type, KL_Q_MEANING); /* new words are introduced word → meaning */
		for (int j = i + 1; j < n; j++) T_ASSERT(q[i].word != q[j].word);
	}
	/* review with nothing seen: empty */
	T_EQ_INT(kl_lesson(&db, &d, "tr", 0, today, 7, q, 10), 0);
	/* answer everything once; Katze wrong */
	for (int i = 0; i < n; i++) kl_card_answer(kl_deck_get(&d, db.w[q[i].word].word), strcmp(db.w[q[i].word].word, "Katze") != 0, today);
	n = kl_lesson(&db, &d, "tr", 0, today, 7, q, 10);
	T_ASSERT(n >= 1);
	T_EQ_STR(db.w[q[0].word].word, "Katze"); /* due today comes first (the others are due tomorrow but listed after) */
	int has_katze = 0;
	n = kl_lesson(&db, &d, "tr", 1, today, 3, q, 4);
	T_EQ_INT(n, 4);
	for (int i = 0; i < n; i++) has_katze |= !strcmp(db.w[q[i].word].word, "Katze");
	T_ASSERT(has_katze); /* due reviews are part of a level lesson */
	/* a missed question comes back as another type */
	unsigned rng = 5;
	KlQuestion again, first;
	T_ASSERT(kl_make(&db, 0, KL_Q_MEANING, "tr", &rng, &first));
	T_ASSERT(kl_again(&db, &first, "tr", &rng, &again));
	T_EQ_INT(again.word, 0);
	T_ASSERT(again.type != KL_Q_MEANING);
	/* native language without translations: only definition and cloze questions */
	KlDeck empty = { 0 };
	n = kl_lesson(&db, &empty, "it", 1, today, 1, q, 10);
	T_EQ_INT(n, 7);
	for (int i = 0; i < n; i++) T_ASSERT(q[i].type == KL_Q_DEFINE || q[i].type == KL_Q_CLOZE);
	kl_deck_free(&d);
	kl_free(&db);
}

int main(void)
{
	T_RUN(test_parse);
	T_RUN(test_fields);
	T_RUN(test_cloze);
	T_RUN(test_deck);
	T_RUN(test_leitner);
	T_RUN(test_progress);
	T_RUN(test_stats);
	T_RUN(test_make);
	T_RUN(test_lesson);
	return T_DONE();
}

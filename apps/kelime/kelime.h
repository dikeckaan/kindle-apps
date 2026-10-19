/* Kelime Öğren: pure logic (no drawing) — word lists, the course (units, lessons that teach new words before
 * practising them), quiz questions with distractors, Leitner boxes, placement test, XP and streak. */
#ifndef KELIME_H
#define KELIME_H

#define KL_LEVELS 6 /* A1 A2 B1 B2 C1 C2 → 1..6 */

enum { KL_POS_OTHER, KL_POS_NOUN, KL_POS_VERB, KL_POS_ADJ, KL_POS_ADV, KL_POS_PRON, KL_POS_PREP, KL_POS_CONJ, KL_POS_NUM, KL_POS_DET,
       KL_POS_INTJ, KL_POS_PHRASE, KL_POS_N };

/* One word of the language being learnt. tr and extr hold one text per native language: "tr=köpek, it|en=dog".
 * ex marks the word inside the sentence with braces ("Der {Hund} bellt.") when it can be blanked out.
 * ipa: "/ˈhaːbən/", say: a reading guide in Turkish letters ("HAA-ben"), theme: the starter theme ("greet", "num" …)
 * of a beginner's first words; all three may be "" (never NULL after kl_parse). */
typedef struct {
	const char *word, *art, *def, *ex, *tr, *extr;
	unsigned char pos, level;
	const char *ipa, *say, *theme;
} KlWord;
typedef struct {
	char *buf;
	KlWord *w;
	int n, level_n[KL_LEVELS + 1];
} KlDb;

/* The word list: '#' lines are comments, then one word per line, 8 to 11 tab-separated fields:
 *   word  article  pos  level(1-6)  definition  example  translations  example-translations  [ipa  reading  theme]
 * Parsed in place (buf is kept and freed by kl_free). Returns the word count, -1 when nothing could be read. */
int  kl_parse(char *buf, KlDb *db);
int  kl_load(const char *path, KlDb *db);
void kl_free(KlDb *db);
int  kl_find(const KlDb *db, const char *word); /* index of the word, -1 when it is not in the list */
int  kl_pos_parse(const char *s);  /* "noun" → KL_POS_NOUN; unknown → KL_POS_OTHER */
const char *kl_level_name(int lv); /* 1 → "A1"; "" out of range */

/* "tr=köpek|en=dog", "en" → "dog" into out; returns the length, 0 when there is no text for lang */
int  kl_field(const char *multi, const char *lang, char *out, int cap);
int  kl_has_field(const char *multi, const char *lang);
/* the text with every marked word replaced by blank (1), or 0 when nothing is marked (out gets the plain text) */
int  kl_cloze(const char *ex, const char *blank, char *out, int cap);
void kl_unmark(const char *ex, char *out, int cap); /* the example without braces */
void kl_display(const KlWord *w, char *out, int cap); /* article and word: "der Hund", "l'eau" (an article ending in ' is joined) */
/* the grammatical gender an article shows: 'm', 'f', 'n' (der/die/das, le/la/un/une, el/la, il/lo/la, o/a), 0 none */
int  kl_gender(const KlWord *w);

/* ---- Leitner boxes: box 0 = new, 1 = just missed, … KL_BOX_MAX = mastered ---- */
#define KL_BOX_MAX 5
#define KL_LEARNED_BOX 2 /* answered right at least twice since the last miss */
#define KL_KNOWN_BOX 4   /* "Bunu biliyorum": a word the learner already knew */
enum { KL_F_TAUGHT = 1, KL_F_KNOWN = 2 };
typedef struct {
	char *key; /* the word */
	int box, due, seen, right; /* seen/right: answers given, right ones */
	int flags; /* KL_F_*: its card was shown (taught), marked as already known */
} KlCard;
typedef struct {
	KlCard *c;
	int n, cap;
	int *hash, hcap; /* open addressing: index + 1, 0 = empty */
} KlDeck;
/* "word<TAB>box<TAB>due<TAB>seen<TAB>right[<TAB>flags]" lines; d (zeroed or used before) is reset. Bad lines are
 * skipped. Files of the first version have no flags: a word answered before counts as taught. */
void kl_deck_parse(const char *buf, KlDeck *d);
int  kl_deck_format(const KlDeck *d, char *buf, int cap); /* bytes written, -1 if cap is too small */
KlCard *kl_deck_find(const KlDeck *d, const char *key);
KlCard *kl_deck_get(KlDeck *d, const char *key); /* finds or adds (box 0); NULL when out of memory */
void kl_deck_free(KlDeck *d);
int  kl_interval(int box);                              /* days until the next review once a word is in box */
void kl_card_answer(KlCard *c, int correct, int today); /* right: one box up, wrong: back to box 1, due today */
/* a further answer in the same lesson: counted, a miss sends the word back to box 1, a right one moves no box */
void kl_card_again(KlCard *c, int correct, int today);
void kl_card_teach(KlCard *c, int today); /* its "Yeni kelime" card was shown: taught, practised today */
void kl_card_know(KlCard *c, int today);  /* "Bunu biliyorum": into KL_KNOWN_BOX */
void kl_card_forget(KlCard *c);           /* back to a word that was never taught */
int  kl_card_taught(const KlCard *c);
int  kl_card_due(const KlCard *c, int today); /* taught and due on or before today */
/* words of level lv (0 = all) in the deck: learned (box >= KL_LEARNED_BOX), taught, due today */
void kl_progress(const KlDb *db, const KlDeck *deck, int lv, int today, int *learned, int *seen, int *due);

/* ---- the course: per level, the words that can be taught to this native language (they have a translation), in
 * units: first the starter themes, then up to unit_size words of one kind (nouns, verbs, …) in frequency order ---- */
enum { KL_U_GREET, KL_U_PRON, KL_U_NUM, KL_U_VERB, KL_U_PEOPLE, KL_U_THINGS, KL_U_ADJ, /* starter themes */
       KL_U_NOUNS, KL_U_VERBS, KL_U_ADJS, KL_U_NUMS, KL_U_WORDS, KL_U_N };
typedef struct {
	int level, kind, num; /* num: 1, 2 … per kind and level; 0 for a starter theme */
	int start, n;         /* course.word[start .. start + n) */
} KlUnit;
typedef struct {
	int *word, n;            /* word indexes in teaching order, level by level */
	int lv_start[KL_LEVELS + 2]; /* the words of level lv: [lv_start[lv], lv_start[lv + 1]) */
	KlUnit *unit;
	int n_unit;
	int *at; /* db index → position in word[], -1 when the word is not taught to this native language */
} KlCourse;
int  kl_theme_kind(const char *theme); /* "greet" → KL_U_GREET …, -1 none */
int  kl_course_build(const KlDb *db, const char *native, int unit_size, KlCourse *c); /* 0 ok, -1 out of memory */
void kl_course_free(KlCourse *c);
int  kl_course_unit(const KlCourse *c, int pos); /* the unit holding word[pos], -1 */
/* taught words of level lv and how many it has */
void kl_course_count(const KlCourse *c, const KlDb *db, const KlDeck *deck, int lv, int *taught, int *total);

/* ---- a lesson: teach the next new words of a level, then practise them with the due reviews ---- */
#define KL_NEW_MAX 8
#define KL_REV_MAX 12
typedef struct {
	int level;                    /* the level the new words come from (the next one when lv is finished) */
	int unit;                     /* the unit of the first new word, -1 */
	int new_w[KL_NEW_MAX], n_new; /* words to teach, in course order */
	int rev[KL_REV_MAX], n_rev;   /* due reviews: most overdue, then the lowest box first */
	int due_all;                  /* every due word (more than n_rev when there are many) */
} KlPlan;
/* new_n new words (≤ KL_NEW_MAX; a lesson keeps to one unit unless fewer than 3 words are left in it), up to
 * rev_max reviews. lv 0: reviews only. */
void kl_plan(const KlDb *db, const KlCourse *c, const KlDeck *deck, int lv, int today, int new_n, int rev_max, KlPlan *p);

/* ---- XP, daily goal and streak; days are counted from 1970-01-01 ---- */
#define KL_XP_RIGHT 10
#define KL_XP_PERFECT 20 /* bonus for a lesson without mistakes */
typedef struct {
	long xp;
	int streak, best, last_day; /* last_day: the day of the last finished lesson */
	int lessons, answers, right;
	int today_xp, today_day, goal;
} KlStats;
int  kl_days(int year, int mon, int mday); /* mon 1..12 */
void kl_stats_parse(const char *buf, KlStats *s); /* key=value lines; missing keys keep defaults */
int  kl_stats_format(const KlStats *s, char *buf, int cap);
void kl_stats_answer(KlStats *s, int correct, int today); /* counts the answer and its XP */
void kl_stats_bonus(KlStats *s, int xp, int today);
void kl_streak_touch(KlStats *s, int today);           /* a lesson was finished today */
int  kl_streak_now(const KlStats *s, int today);       /* the streak still alive today (0 once a day was missed) */
int  kl_today_xp(const KlStats *s, int today);

/* ---- questions ---- */
enum {
	KL_Q_MEANING, /* word → pick its translation (recognition: the easiest) */
	KL_Q_WORD,    /* translation → pick the word */
	KL_Q_DEFINE,  /* definition in the language itself → pick the word */
	KL_Q_CLOZE,   /* example with a blank → pick the word */
	KL_Q_TF,      /* word = translation? opts[0] is the word whose translation is shown; answer 1 = true */
	KL_Q_N
};
typedef struct {
	int type, word, n_opts, answer; /* answer: index into opts (KL_Q_TF: 1 true, 0 false) */
	int opts[4];
} KlQuestion;
unsigned kl_rand(unsigned *state); /* xorshift32; state != 0 */
int  kl_can_ask(const KlWord *w, int type, const char *native);
/* Builds question q of type for db->w[word]: the distractors share the part of speech and level (then a
 * neighbouring level, then any) and never repeat the answer's text. 0 when the type cannot be asked. */
int  kl_make(const KlDb *db, int word, int type, const char *native, unsigned *rng, KlQuestion *q);
/* the same, the distractors drawn first from pref (words the learner knows: this lesson's, taught ones); easy:
 * a translation sharing a word with the answer's is not offered (no "olmak" next to "sahip olmak") */
int  kl_make_ex(const KlDb *db, int word, int type, const char *native, unsigned *rng, const int *pref, int n_pref, int easy, KlQuestion *q);
/* The question type a word is ready for: recognition (word → meaning) until it is in box 2, then meaning → word,
 * then the gap in an example, and the definition in the language itself only from box 4 and after two answers.
 * Falls back to an easier type the word can be asked as; -1 when none. */
int  kl_qtype(const KlWord *w, const KlCard *c, const char *native);
/* The practice part of a lesson: every new word first as word → meaning among the lesson's words, the reviews at
 * the type their strength allows, then every new word again as meaning → word. Returns the question count. */
int  kl_practice(const KlDb *db, const KlDeck *deck, const char *native, const KlPlan *p, unsigned seed, KlQuestion *q, int max);
/* the same word again (a missed question comes back later in the lesson), as word → meaning when it is new */
int  kl_again(const KlDb *db, const KlQuestion *prev, const char *native, unsigned *rng, KlQuestion *q);
/* puts nq at q[at + gap] (or the end) moving the rest back; 0 when q is full */
int  kl_insert(KlQuestion *q, int *n, int max, int at, int gap, const KlQuestion *nq);

/* ---- placement: a few words per level, level by level; the first level not passed is where the course starts ---- */
#define KL_PLACE_PER_LEVEL 5
int  kl_place_pick(const KlCourse *c, int lv, unsigned *rng, int *out, int k); /* k random words of level lv */
int  kl_place_passed(int asked, int right); /* at least 4 of 5 */
/* asked/right per level (index 1..KL_LEVELS); the start level; *more: whether the test goes on to the next level */
int  kl_place_level(const int *asked, const int *right, int *more);
#endif

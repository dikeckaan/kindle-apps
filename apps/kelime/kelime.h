/* Kelime Öğren: pure logic (no drawing) — word lists, quiz questions with distractors, Leitner boxes, XP and streak. */
#ifndef KELIME_H
#define KELIME_H

#define KL_LEVELS 6 /* A1 A2 B1 B2 C1 C2 → 1..6 */

enum { KL_POS_OTHER, KL_POS_NOUN, KL_POS_VERB, KL_POS_ADJ, KL_POS_ADV, KL_POS_PRON, KL_POS_PREP, KL_POS_CONJ, KL_POS_NUM, KL_POS_DET,
       KL_POS_INTJ, KL_POS_PHRASE, KL_POS_N };

/* One word of the language being learnt. tr and extr hold one text per native language: "tr=köpek, it|en=dog".
 * ex marks the word inside the sentence with braces ("Der {Hund} bellt.") when it can be blanked out. */
typedef struct {
	const char *word, *art, *def, *ex, *tr, *extr;
	unsigned char pos, level;
} KlWord;
typedef struct {
	char *buf;
	KlWord *w;
	int n, level_n[KL_LEVELS + 1];
} KlDb;

/* The word list: '#' lines are comments, then one word per line, 8 tab-separated fields:
 *   word  article  pos  level(1-6)  definition  example  translations  example-translations
 * Parsed in place (buf is kept and freed by kl_free). Returns the word count, -1 when nothing could be read. */
int  kl_parse(char *buf, KlDb *db);
int  kl_load(const char *path, KlDb *db);
void kl_free(KlDb *db);
int  kl_pos_parse(const char *s);  /* "noun" → KL_POS_NOUN; unknown → KL_POS_OTHER */
const char *kl_level_name(int lv); /* 1 → "A1"; "" out of range */

/* "tr=köpek|en=dog", "en" → "dog" into out; returns the length, 0 when there is no text for lang */
int  kl_field(const char *multi, const char *lang, char *out, int cap);
int  kl_has_field(const char *multi, const char *lang);
/* the text with every marked word replaced by blank (1), or 0 when nothing is marked (out gets the plain text) */
int  kl_cloze(const char *ex, const char *blank, char *out, int cap);
void kl_unmark(const char *ex, char *out, int cap); /* the example without braces */
void kl_display(const KlWord *w, char *out, int cap); /* article and word: "der Hund", "l'eau" (an article ending in ' is joined) */

/* ---- Leitner boxes: box 0 = new, 1 = just missed, … KL_BOX_MAX = mastered ---- */
#define KL_BOX_MAX 5
#define KL_LEARNED_BOX 2 /* answered right at least twice since the last miss */
typedef struct {
	char *key; /* the word */
	int box, due, seen, right;
} KlCard;
typedef struct {
	KlCard *c;
	int n, cap;
	int *hash, hcap; /* open addressing: index + 1, 0 = empty */
} KlDeck;
/* "word<TAB>box<TAB>due<TAB>seen<TAB>right" lines; d (zeroed or used before) is reset. Bad lines are skipped. */
void kl_deck_parse(const char *buf, KlDeck *d);
int  kl_deck_format(const KlDeck *d, char *buf, int cap); /* bytes written, -1 if cap is too small */
KlCard *kl_deck_find(const KlDeck *d, const char *key);
KlCard *kl_deck_get(KlDeck *d, const char *key); /* finds or adds (box 0); NULL when out of memory */
void kl_deck_free(KlDeck *d);
int  kl_interval(int box);                              /* days until the next review once a word is in box */
void kl_card_answer(KlCard *c, int correct, int today); /* right: one box up, wrong: back to box 1, due today */
int  kl_card_due(const KlCard *c, int today);           /* seen and due on or before today */
/* words of level lv (0 = all) in the deck: learned (box >= KL_LEARNED_BOX), seen at least once, due today */
void kl_progress(const KlDb *db, const KlDeck *deck, int lv, int today, int *learned, int *seen, int *due);

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
	KL_Q_MEANING, /* word → pick its translation */
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
/* A lesson of up to max questions: lv 1..6 mixes due reviews, new words (in list order, i.e. most frequent first)
 * and practice; lv 0 = review only (due words, then the weakest seen ones). A new word is first asked as
 * KL_Q_MEANING. Returns the question count (0: nothing to ask). */
int  kl_lesson(const KlDb *db, const KlDeck *deck, const char *native, int lv, int today, unsigned seed, KlQuestion *q, int max);
/* the same word again (a missed question comes back at the end of the lesson), as another type when possible */
int  kl_again(const KlDb *db, const KlQuestion *prev, const char *native, unsigned *rng, KlQuestion *q);
#endif

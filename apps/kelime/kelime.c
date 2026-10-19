/* Kelime Öğren: pure logic (no drawing). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kelime.h"

/* ---- word list ---- */

static const char *POS_NAMES[KL_POS_N] = { "other", "noun", "verb", "adj", "adv", "pron", "prep", "conj", "num", "det", "intj", "phrase" };

int kl_pos_parse(const char *s)
{
	for (int i = 1; i < KL_POS_N; i++)
		if (!strcmp(s, POS_NAMES[i])) return i;
	return KL_POS_OTHER;
}

const char *kl_level_name(int lv)
{
	static const char *n[] = { "", "A1", "A2", "B1", "B2", "C1", "C2" };
	return lv >= 1 && lv <= KL_LEVELS ? n[lv] : "";
}

int kl_parse(char *buf, KlDb *db)
{
	memset(db, 0, sizeof *db);
	int lines = 0;
	for (char *p = buf; *p; p++) lines += *p == '\n';
	db->w = calloc((size_t) lines + 1, sizeof *db->w);
	if (!db->w) return -1;
	for (char *p = buf; *p;) {
		char *nl = strchr(p, '\n'), *next = nl ? nl + 1 : p + strlen(p);
		if (nl) *nl = '\0';
		size_t len = strlen(p);
		if (len && p[len - 1] == '\r') p[len - 1] = '\0';
		if (*p && *p != '#') {
			char *f[11];
			int k = 0;
			f[k++] = p;
			for (char *q = p; *q && k < 11; q++)
				if (*q == '\t') {
					*q = '\0';
					f[k++] = q + 1;
				}
			int lv = k >= 4 ? atoi(f[3]) : 0;
			if (k >= 8 && *f[0] && lv >= 1 && lv <= KL_LEVELS && !strchr(f[k - 1], '\t')) {
				KlWord *w = &db->w[db->n++];
				*w = (KlWord){ f[0], f[1], f[4], f[5], f[6], f[7], (unsigned char) kl_pos_parse(f[2]), (unsigned char) lv,
					       k > 8 ? f[8] : "", k > 9 ? f[9] : "", k > 10 ? f[10] : "" };
				db->level_n[lv]++;
				db->level_n[0]++;
			}
		}
		p = next;
	}
	if (!db->n) {
		free(db->w);
		db->w = NULL;
		return -1;
	}
	db->buf = buf;
	return db->n;
}

int kl_load(const char *path, KlDb *db)
{
	memset(db, 0, sizeof *db);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = sz > 0 ? malloc((size_t) sz + 1) : NULL;
	if (!buf || fread(buf, 1, (size_t) sz, f) != (size_t) sz) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[sz] = '\0';
	int n = kl_parse(buf, db);
	if (n < 0) free(buf);
	return n;
}

void kl_free(KlDb *db)
{
	free(db->w);
	free(db->buf);
	memset(db, 0, sizeof *db);
}

int kl_field(const char *multi, const char *lang, char *out, int cap)
{
	size_t ll = strlen(lang);
	if (cap > 0) out[0] = '\0';
	for (const char *p = multi; p && *p;) {
		const char *e = strchr(p, '|');
		size_t len = e ? (size_t) (e - p) : strlen(p);
		if (len > ll && !strncmp(p, lang, ll) && p[ll] == '=') {
			int n = (int) (len - ll - 1);
			if (n > cap - 1) n = cap - 1;
			if (n < 0) n = 0;
			memcpy(out, p + ll + 1, (size_t) n);
			out[n] = '\0';
			return n;
		}
		p = e ? e + 1 : NULL;
	}
	return 0;
}

int kl_has_field(const char *multi, const char *lang)
{
	char tmp[4];
	return kl_field(multi, lang, tmp, sizeof tmp) > 0;
}

/* copies src[0..n) into out at *o (bounded) */
static void put(char *out, int cap, int *o, const char *src, int n)
{
	if (n > cap - 1 - *o) n = cap - 1 - *o;
	if (n > 0) {
		memcpy(out + *o, src, (size_t) n);
		*o += n;
	}
	out[*o] = '\0';
}

int kl_cloze(const char *ex, const char *blank, char *out, int cap)
{
	int o = 0, marked = 0, bl = (int) strlen(blank);
	if (cap < 1) return 0;
	out[0] = '\0';
	for (const char *p = ex; *p;) {
		const char *b = *p == '{' ? strchr(p, '}') : NULL;
		if (b) { /* every marked word becomes the blank */
			put(out, cap, &o, blank, bl);
			marked = 1;
			p = b + 1;
		} else {
			if (*p != '{' && *p != '}') put(out, cap, &o, p, 1);
			p++;
		}
	}
	return marked;
}

void kl_unmark(const char *ex, char *out, int cap)
{
	int o = 0;
	if (cap > 0) out[0] = '\0';
	for (const char *p = ex; *p && o < cap - 1; p++)
		if (*p != '{' && *p != '}') out[o++] = *p;
	if (cap > 0) out[o] = '\0';
}

int kl_find(const KlDb *db, const char *word)
{
	for (int i = 0; i < db->n; i++)
		if (!strcmp(db->w[i].word, word)) return i;
	return -1;
}

int kl_gender(const KlWord *w)
{
	static const struct { const char *art; char g; } A[] = { { "der", 'm' }, { "die", 'f' }, { "das", 'n' }, { "le", 'm' }, { "un", 'm' },
								  { "une", 'f' }, { "el", 'm' }, { "la", 'f' },  { "il", 'm' }, { "lo", 'm' },
								  { "o", 'm' },   { "a", 'f' } };
	for (size_t i = 0; i < sizeof A / sizeof A[0]; i++)
		if (!strcmp(w->art, A[i].art)) return A[i].g;
	return 0;
}

void kl_display(const KlWord *w, char *out, int cap)
{
	size_t al = strlen(w->art);
	if (!al) snprintf(out, (size_t) cap, "%s", w->word);
	else if (w->art[al - 1] == '\'') snprintf(out, (size_t) cap, "%s%s", w->art, w->word);
	else snprintf(out, (size_t) cap, "%s %s", w->art, w->word);
}

/* ---- Leitner deck ---- */

static unsigned hash_str(const char *s)
{
	unsigned h = 2166136261u;
	for (; *s; s++) h = (h ^ (unsigned char) *s) * 16777619u;
	return h;
}

static int rehash(KlDeck *d, int hcap)
{
	int *h = calloc((size_t) hcap, sizeof *h);
	if (!h) return -1;
	free(d->hash);
	d->hash = h;
	d->hcap = hcap;
	for (int i = 0; i < d->n; i++) {
		unsigned k = hash_str(d->c[i].key) & (unsigned) (hcap - 1);
		while (h[k]) k = (k + 1) & (unsigned) (hcap - 1);
		h[k] = i + 1;
	}
	return 0;
}

KlCard *kl_deck_find(const KlDeck *d, const char *key)
{
	if (!d->hcap) return NULL;
	unsigned k = hash_str(key) & (unsigned) (d->hcap - 1);
	while (d->hash[k]) {
		KlCard *c = &d->c[d->hash[k] - 1];
		if (!strcmp(c->key, key)) return c;
		k = (k + 1) & (unsigned) (d->hcap - 1);
	}
	return NULL;
}

KlCard *kl_deck_get(KlDeck *d, const char *key)
{
	KlCard *c = kl_deck_find(d, key);
	if (c) return c;
	if (d->n == d->cap) {
		int nc = d->cap ? d->cap * 2 : 64;
		KlCard *m = realloc(d->c, (size_t) nc * sizeof *m);
		if (!m) return NULL;
		d->c = m;
		d->cap = nc;
	}
	char *k = strdup(key);
	if (!k) return NULL;
	d->c[d->n++] = (KlCard){ k, 0, 0, 0, 0, 0 };
	if (d->n * 2 > d->hcap) { /* grow: rehash inserts every key, the new one too */
		if (rehash(d, d->hcap ? d->hcap * 2 : 128) != 0) {
			free(k);
			d->n--;
			return NULL;
		}
	} else {
		unsigned h = hash_str(key) & (unsigned) (d->hcap - 1);
		while (d->hash[h]) h = (h + 1) & (unsigned) (d->hcap - 1);
		d->hash[h] = d->n;
	}
	return &d->c[d->n - 1];
}

void kl_deck_free(KlDeck *d)
{
	for (int i = 0; i < d->n; i++) free(d->c[i].key);
	free(d->c);
	free(d->hash);
	memset(d, 0, sizeof *d);
}

void kl_deck_parse(const char *buf, KlDeck *d)
{
	kl_deck_free(d);
	for (const char *p = buf; p && *p;) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t) (e - p) : strlen(p);
		char line[512];
		if (len < sizeof line) {
			memcpy(line, p, len);
			line[len] = '\0';
			char *t = strchr(line, '\t');
			int box, due, seen, right, flags = 0;
			if (t && t != line) {
				*t = '\0';
				int got = sscanf(t + 1, "%d\t%d\t%d\t%d\t%d", &box, &due, &seen, &right, &flags);
				if (got >= 4 && box >= 0 && box <= KL_BOX_MAX) {
					if (got < 5) flags = seen > 0 ? KL_F_TAUGHT : 0; /* first version: answered = taught */
					KlCard *c = kl_deck_get(d, line);
					if (c) *c = (KlCard){ c->key, box, due, seen, right, flags & (KL_F_TAUGHT | KL_F_KNOWN) };
				}
			}
		}
		p = e ? e + 1 : NULL;
	}
}

int kl_deck_format(const KlDeck *d, char *buf, int cap)
{
	int o = 0;
	if (cap < 1) return -1;
	buf[0] = '\0';
	for (int i = 0; i < d->n; i++) {
		const KlCard *c = &d->c[i];
		if (!kl_card_taught(c)) continue; /* a forgotten word needs no line */
		int n = snprintf(buf + o, (size_t) (cap - o), "%s\t%d\t%d\t%d\t%d\t%d\n", c->key, c->box, c->due, c->seen, c->right, c->flags);
		if (n < 0 || n >= cap - o) return -1;
		o += n;
	}
	return o;
}

int kl_interval(int box)
{
	static const int days[KL_BOX_MAX + 1] = { 0, 1, 2, 4, 8, 21 };
	return box < 0 ? 0 : box > KL_BOX_MAX ? days[KL_BOX_MAX] : days[box];
}

void kl_card_answer(KlCard *c, int correct, int today)
{
	c->seen++;
	c->flags |= KL_F_TAUGHT;
	if (correct) {
		c->right++;
		if (c->box < KL_BOX_MAX) c->box++;
		c->due = today + kl_interval(c->box);
	} else {
		c->box = 1;
		c->due = today;
	}
}

void kl_card_again(KlCard *c, int correct, int today)
{
	if (correct) {
		c->seen++;
		c->right++;
	} else {
		kl_card_answer(c, 0, today);
	}
}

void kl_card_teach(KlCard *c, int today)
{
	if (!kl_card_taught(c)) c->due = today;
	c->flags |= KL_F_TAUGHT;
}

void kl_card_know(KlCard *c, int today)
{
	c->flags |= KL_F_TAUGHT | KL_F_KNOWN;
	if (c->box < KL_KNOWN_BOX) c->box = KL_KNOWN_BOX;
	c->due = today + kl_interval(c->box);
	if (c->seen < 2) c->seen = 2;
	if (c->right < c->seen) c->right = c->seen;
}

void kl_card_forget(KlCard *c) { c->box = c->due = c->seen = c->right = c->flags = 0; }

int kl_card_taught(const KlCard *c) { return c && ((c->flags & KL_F_TAUGHT) || c->seen > 0); }

int kl_card_due(const KlCard *c, int today) { return kl_card_taught(c) && c->due <= today; }

void kl_progress(const KlDb *db, const KlDeck *deck, int lv, int today, int *learned, int *seen, int *due)
{
	int l = 0, s = 0, du = 0;
	for (int i = 0; i < db->n; i++) {
		if (lv && db->w[i].level != lv) continue;
		const KlCard *c = kl_deck_find(deck, db->w[i].word);
		if (!kl_card_taught(c)) continue;
		s++;
		l += c->box >= KL_LEARNED_BOX;
		du += kl_card_due(c, today);
	}
	if (learned) *learned = l;
	if (seen) *seen = s;
	if (due) *due = du;
}

/* ---- stats ---- */

int kl_days(int y, int m, int d)
{
	/* days from civil (Howard Hinnant) */
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	int yoe = y - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void kl_stats_parse(const char *buf, KlStats *s)
{
	memset(s, 0, sizeof *s);
	s->goal = 50;
	s->last_day = s->today_day = -1000;
	for (const char *p = buf; p && *p;) {
		const char *e = strchr(p, '\n');
		char key[32];
		long v;
		if (sscanf(p, "%31[a-z_]=%ld", key, &v) == 2) {
			if (!strcmp(key, "xp")) s->xp = v;
			else if (!strcmp(key, "streak")) s->streak = (int) v;
			else if (!strcmp(key, "best")) s->best = (int) v;
			else if (!strcmp(key, "last_day")) s->last_day = (int) v;
			else if (!strcmp(key, "lessons")) s->lessons = (int) v;
			else if (!strcmp(key, "answers")) s->answers = (int) v;
			else if (!strcmp(key, "right")) s->right = (int) v;
			else if (!strcmp(key, "today_xp")) s->today_xp = (int) v;
			else if (!strcmp(key, "today_day")) s->today_day = (int) v;
			else if (!strcmp(key, "goal") && v > 0) s->goal = (int) v;
		}
		p = e ? e + 1 : NULL;
	}
}

int kl_stats_format(const KlStats *s, char *buf, int cap)
{
	int n = snprintf(buf, (size_t) cap, "xp=%ld\nstreak=%d\nbest=%d\nlast_day=%d\nlessons=%d\nanswers=%d\nright=%d\ntoday_xp=%d\ntoday_day=%d\ngoal=%d\n",
			 s->xp, s->streak, s->best, s->last_day, s->lessons, s->answers, s->right, s->today_xp, s->today_day, s->goal);
	return n < 0 || n >= cap ? -1 : n;
}

void kl_stats_bonus(KlStats *s, int xp, int today)
{
	if (s->today_day != today) {
		s->today_day = today;
		s->today_xp = 0;
	}
	s->xp += xp;
	s->today_xp += xp;
}

void kl_stats_answer(KlStats *s, int correct, int today)
{
	s->answers++;
	s->right += correct != 0;
	kl_stats_bonus(s, correct ? KL_XP_RIGHT : 0, today);
}

void kl_streak_touch(KlStats *s, int today)
{
	if (s->last_day == today) {
		/* already counted today */
	} else if (s->last_day == today - 1) {
		s->streak++;
	} else {
		s->streak = 1;
	}
	if (s->streak < 1) s->streak = 1;
	s->last_day = today;
	s->lessons++;
	if (s->streak > s->best) s->best = s->streak;
}

int kl_streak_now(const KlStats *s, int today) { return s->last_day == today || s->last_day == today - 1 ? s->streak : 0; }

int kl_today_xp(const KlStats *s, int today) { return s->today_day == today ? s->today_xp : 0; }

/* ---- questions ---- */

unsigned kl_rand(unsigned *st)
{
	unsigned x = *st ? *st : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*st = x;
	return x;
}

int kl_can_ask(const KlWord *w, int type, const char *native)
{
	switch (type) {
	case KL_Q_MEANING:
	case KL_Q_WORD:
	case KL_Q_TF: return kl_has_field(w->tr, native);
	case KL_Q_DEFINE: return w->def[0] != '\0';
	case KL_Q_CLOZE: return strchr(w->ex, '{') != NULL;
	}
	return 0;
}

/* the text an option shows: a translation (KL_Q_MEANING, KL_Q_TF) or the word */
static void opt_text(const KlDb *db, int word, int type, const char *native, char *out, int cap)
{
	if (type == KL_Q_MEANING || type == KL_Q_TF) kl_field(db->w[word].tr, native, out, cap);
	else kl_display(&db->w[word], out, cap);
}

static int ci_eq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		int x = (unsigned char) *a, y = (unsigned char) *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return 0;
	}
	return *a == *b;
}

/* would cand be a fair wrong option for the answer word? */
static int word_byte(int c) { return (c & 0x80) || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z'); }

/* does text contain the word s[0..len) as a whole word (ASCII case-insensitive)? */
static int has_word(const char *text, const char *s, size_t len)
{
	for (const char *p = text; *p;) {
		while (*p && !word_byte((unsigned char) *p)) p++;
		const char *b = p;
		while (*p && word_byte((unsigned char) *p)) p++;
		if ((size_t) (p - b) != len) continue;
		size_t i = 0;
		for (; i < len; i++) {
			int x = (unsigned char) b[i], y = (unsigned char) s[i];
			if (x >= 'A' && x <= 'Z') x += 32;
			if (y >= 'A' && y <= 'Z') y += 32;
			if (x != y) break;
		}
		if (i == len) return 1;
	}
	return 0;
}

/* do two translations share a word of three or more bytes ("olmak" / "sahip olmak")? */
static int share_word(const char *a, const char *b)
{
	for (const char *p = a; *p;) {
		while (*p && !word_byte((unsigned char) *p)) p++;
		const char *s = p;
		while (*p && word_byte((unsigned char) *p)) p++;
		if (p - s >= 3 && has_word(b, s, (size_t) (p - s))) return 1;
	}
	return 0;
}

/* would cand be a fair wrong option for the answer word? */
static int fair(const KlDb *db, int answer, int cand, int type, const char *native, const int *taken, int n_taken, int easy)
{
	if (cand == answer || cand < 0 || cand >= db->n) return 0;
	const KlWord *a = &db->w[answer], *c = &db->w[cand];
	if (ci_eq(a->word, c->word)) return 0;
	if (type != KL_Q_DEFINE && type != KL_Q_CLOZE && !kl_has_field(c->tr, native)) return 0;
	char ta[256], tc[256], tt[256];
	/* a word with the same translation would be a second right answer */
	if (kl_field(a->tr, native, ta, sizeof ta) && kl_field(c->tr, native, tc, sizeof tc)) {
		if (ci_eq(ta, tc)) return 0;
		if (easy && share_word(ta, tc)) return 0;
	}
	opt_text(db, cand, type, native, tc, sizeof tc);
	if (!tc[0]) return 0;
	for (int i = 0; i < n_taken; i++) {
		if (taken[i] == cand) return 0;
		opt_text(db, taken[i], type, native, tt, sizeof tt);
		if (ci_eq(tt, tc)) return 0;
	}
	return 1;
}

/* adds distractors for answer to taken[n_taken..]; returns how many were found */
static int distractors(const KlDb *db, int answer, int type, const char *native, unsigned *rng, int *taken, int n_taken, int want,
		       const int *pref, int n_pref, int easy)
{
	const KlWord *a = &db->w[answer];
	int got = 0;
	int *pool = malloc((size_t) (db->n > n_pref ? db->n : n_pref) * sizeof *pool);
	if (!pool) return 0;
	/* passes: the preferred (known) words, then same POS and level, same POS ±1 level, ±1 level, anything */
	for (int pass = pref && n_pref ? -1 : 0; pass < 4 && got < want; pass++) {
		int np = 0;
		if (pass < 0) {
			memcpy(pool, pref, (size_t) n_pref * sizeof *pool);
			np = n_pref;
		} else {
			for (int c = 0; c < db->n; c++) {
				const KlWord *w = &db->w[c];
				int dl = w->level > a->level ? w->level - a->level : a->level - w->level;
				int ok = pass == 0 ? (w->pos == a->pos && dl == 0) : pass == 1 ? (w->pos == a->pos && dl == 1) : pass == 2 ? (w->pos != a->pos && dl <= 1) : dl > 1;
				if (ok) pool[np++] = c;
			}
		}
		/* random order without repeats: a partial Fisher–Yates shuffle, stopped once enough fair words were found */
		for (int i = 0; i < np && got < want; i++) {
			int j = i + (int) (kl_rand(rng) % (unsigned) (np - i)), c = pool[j];
			pool[j] = pool[i];
			pool[i] = c;
			if (fair(db, answer, c, type, native, taken, n_taken + got, easy)) taken[n_taken + got++] = c;
		}
	}
	free(pool);
	return got;
}

int kl_make_ex(const KlDb *db, int word, int type, const char *native, unsigned *rng, const int *pref, int n_pref, int easy, KlQuestion *q)
{
	if (word < 0 || word >= db->n || type < 0 || type >= KL_Q_N || !kl_can_ask(&db->w[word], type, native)) return 0;
	memset(q, 0, sizeof *q);
	q->type = type;
	q->word = word;
	if (type == KL_Q_TF) {
		int t[2] = { word, -1 };
		q->opts[0] = word;
		q->answer = 1;
		if ((kl_rand(rng) & 1) && distractors(db, word, KL_Q_MEANING, native, rng, t, 1, 1, pref, n_pref, easy) == 1) {
			q->opts[0] = t[1]; /* the shown translation belongs to another word */
			q->answer = 0;
		}
		q->n_opts = 1;
		return 1;
	}
	int t[4] = { word, -1, -1, -1 };
	int got = distractors(db, word, type, native, rng, t, 1, 3, pref, n_pref, easy);
	if (got < 1) return 0;
	q->n_opts = got + 1;
	q->answer = (int) (kl_rand(rng) % (unsigned) q->n_opts);
	for (int i = 0, k = 1; i < q->n_opts; i++) q->opts[i] = i == q->answer ? word : t[k++];
	return 1;
}

int kl_make(const KlDb *db, int word, int type, const char *native, unsigned *rng, KlQuestion *q)
{
	return kl_make_ex(db, word, type, native, rng, NULL, 0, 0, q);
}

int kl_qtype(const KlWord *w, const KlCard *c, const char *native)
{
	/* from the hardest to the easiest */
	static const int ladder[] = { KL_Q_DEFINE, KL_Q_CLOZE, KL_Q_WORD, KL_Q_MEANING };
	int box = c ? c->box : 0, seen = c ? c->seen : 0, start;
	if (!kl_card_taught(c) || box < 2) start = 3;
	else if (box == 2) start = 2;
	else if (box == 3 || seen < 2) start = 1;
	else start = 0;
	for (int i = start; i < 4; i++)
		if (kl_can_ask(w, ladder[i], native)) return ladder[i];
	/* no translation into this language: a harder type is all there is */
	for (int i = start - 1 >= 0 ? start - 1 : 0; i >= 0; i--)
		if (kl_can_ask(w, ladder[i], native) && (ladder[i] != KL_Q_DEFINE || seen >= 2)) return ladder[i];
	if (kl_can_ask(w, KL_Q_CLOZE, native)) return KL_Q_CLOZE;
	return seen >= 2 && kl_can_ask(w, KL_Q_DEFINE, native) ? KL_Q_DEFINE : -1;
}

static void shuffle(int *a, int n, unsigned *rng)
{
	for (int i = n - 1; i > 0; i--) {
		int j = (int) (kl_rand(rng) % (unsigned) (i + 1)), t = a[i];
		a[i] = a[j];
		a[j] = t;
	}
}

/* ---- the course ---- */

int kl_theme_kind(const char *theme)
{
	static const char *T[] = { "greet", "pron", "num", "verb", "people", "things", "adj" };
	for (int i = 0; theme && i < (int) (sizeof T / sizeof T[0]); i++)
		if (!strcmp(theme, T[i])) return KL_U_GREET + i;
	return -1;
}

static int pos_kind(int pos)
{
	switch (pos) {
	case KL_POS_NOUN: return KL_U_NOUNS;
	case KL_POS_VERB: return KL_U_VERBS;
	case KL_POS_ADJ: return KL_U_ADJS;
	case KL_POS_NUM: return KL_U_NUMS;
	}
	return KL_U_WORDS;
}

void kl_course_free(KlCourse *c)
{
	free(c->word);
	free(c->unit);
	free(c->at);
	memset(c, 0, sizeof *c);
}

typedef struct {
	int kind, num, first, off, n; /* first: db index of its first word; off: into the kind's list */
} Chunk;

int kl_course_build(const KlDb *db, const char *native, int unit_size, KlCourse *c)
{
	memset(c, 0, sizeof *c);
	if (unit_size < 2) unit_size = 2;
	int n = db->n > 0 ? db->n : 1;
	c->word = malloc((size_t) n * sizeof *c->word);
	c->at = malloc((size_t) n * sizeof *c->at);
	c->unit = malloc((size_t) n * sizeof *c->unit);
	int *list = malloc((size_t) n * sizeof *list);
	Chunk *ch = malloc((size_t) n * sizeof *ch);
	if (!c->word || !c->at || !c->unit || !list || !ch) {
		free(list);
		free(ch);
		kl_course_free(c);
		return -1;
	}
	for (int i = 0; i < db->n; i++) c->at[i] = -1;
	for (int lv = 1; lv <= KL_LEVELS; lv++) {
		c->lv_start[lv] = c->n;
		/* the starter themes, in the order of their kinds */
		for (int k = KL_U_GREET; k <= KL_U_ADJ; k++) {
			int start = c->n;
			for (int i = 0; i < db->n; i++)
				if (db->w[i].level == lv && kl_theme_kind(db->w[i].theme) == k && kl_has_field(db->w[i].tr, native)) {
					c->at[i] = c->n;
					c->word[c->n++] = i;
				}
			if (c->n > start) c->unit[c->n_unit++] = (KlUnit){ lv, k, 0, start, c->n - start };
		}
		/* the rest by kind, in list (frequency) order, cut into units */
		int n_ch = 0, nl = 0;
		for (int k = KL_U_NOUNS; k <= KL_U_WORDS; k++) {
			int off = nl;
			for (int i = 0; i < db->n; i++) {
				const KlWord *w = &db->w[i];
				if (w->level == lv && kl_theme_kind(w->theme) < 0 && pos_kind(w->pos) == k && kl_has_field(w->tr, native)) list[nl++] = i;
			}
			int len = nl - off, units = (len + unit_size - 1) / unit_size;
			if (units > 1 && len % unit_size && len % unit_size < unit_size / 3) units--; /* a few words left: into the last unit */
			for (int u = 0; u < units; u++) {
				int s = u * unit_size, e = u == units - 1 ? len : s + unit_size;
				ch[n_ch++] = (Chunk){ k, u + 1, list[off + s], off + s, e - s };
			}
		}
		/* units in the order of their first (most frequent) word: nouns, verbs and small words take turns */
		for (int i = 1; i < n_ch; i++)
			for (int j = i; j > 0 && ch[j - 1].first > ch[j].first; j--) {
				Chunk t = ch[j];
				ch[j] = ch[j - 1];
				ch[j - 1] = t;
			}
		for (int i = 0; i < n_ch; i++) {
			c->unit[c->n_unit++] = (KlUnit){ lv, ch[i].kind, ch[i].num, c->n, ch[i].n };
			for (int k = 0; k < ch[i].n; k++) {
				int w = list[ch[i].off + k];
				c->at[w] = c->n;
				c->word[c->n++] = w;
			}
		}
	}
	c->lv_start[KL_LEVELS + 1] = c->n;
	free(list);
	free(ch);
	return 0;
}

int kl_course_unit(const KlCourse *c, int pos)
{
	int a = 0, b = c->n_unit - 1;
	while (a <= b) {
		int m = (a + b) / 2;
		const KlUnit *u = &c->unit[m];
		if (pos < u->start) b = m - 1;
		else if (pos >= u->start + u->n) a = m + 1;
		else return m;
	}
	return -1;
}

void kl_course_count(const KlCourse *c, const KlDb *db, const KlDeck *deck, int lv, int *taught, int *total)
{
	int t = 0, a = lv >= 1 && lv <= KL_LEVELS ? c->lv_start[lv] : 0, b = lv >= 1 && lv <= KL_LEVELS ? c->lv_start[lv + 1] : c->n;
	for (int i = a; i < b; i++) t += kl_card_taught(kl_deck_find(deck, db->w[c->word[i]].word));
	if (taught) *taught = t;
	if (total) *total = b - a;
}

/* ---- a lesson ---- */

typedef struct {
	int due, box, word;
} DueItem;

static int due_cmp(const void *a, const void *b)
{
	const DueItem *x = a, *y = b;
	if (x->due != y->due) return x->due < y->due ? -1 : 1;
	if (x->box != y->box) return x->box < y->box ? -1 : 1;
	return x->word - y->word;
}

void kl_plan(const KlDb *db, const KlCourse *c, const KlDeck *deck, int lv, int today, int new_n, int rev_max, KlPlan *p)
{
	memset(p, 0, sizeof *p);
	p->level = lv;
	p->unit = -1;
	if (new_n > KL_NEW_MAX) new_n = KL_NEW_MAX;
	if (rev_max > KL_REV_MAX) rev_max = KL_REV_MAX;
	if (lv >= 1 && lv <= KL_LEVELS && new_n > 0) {
		int first = -1;
		for (int l = lv; l <= KL_LEVELS && first < 0; l++)
			for (int i = c->lv_start[l]; i < c->lv_start[l + 1]; i++)
				if (!kl_card_taught(kl_deck_find(deck, db->w[c->word[i]].word))) {
					first = i;
					p->level = l;
					break;
				}
		if (first >= 0) {
			int cur = kl_course_unit(c, first);
			p->unit = cur;
			for (int i = first; i < c->lv_start[p->level + 1] && p->n_new < new_n; i++) {
				if (kl_card_taught(kl_deck_find(deck, db->w[c->word[i]].word))) continue;
				int u = kl_course_unit(c, i);
				if (u != cur && p->n_new >= 3) break; /* a lesson keeps to its unit */
				cur = u;
				p->new_w[p->n_new++] = c->word[i];
			}
		}
	}
	DueItem *d = malloc((size_t) (db->n > 0 ? db->n : 1) * sizeof *d);
	if (!d) return;
	int nd = 0;
	for (int i = 0; i < db->n; i++) {
		const KlCard *k = kl_deck_find(deck, db->w[i].word);
		if (kl_card_due(k, today)) d[nd++] = (DueItem){ k->due, k->box, i };
	}
	qsort(d, (size_t) nd, sizeof *d, due_cmp);
	p->due_all = nd;
	for (int i = 0; i < nd && p->n_rev < rev_max; i++) p->rev[p->n_rev++] = d[i].word;
	free(d);
}

int kl_practice(const KlDb *db, const KlDeck *deck, const char *native, const KlPlan *p, unsigned seed, KlQuestion *q, int max)
{
	unsigned rng = seed ? seed : 1;
	int n = 0, fresh[KL_NEW_MAX], all[KL_NEW_MAX + KL_REV_MAX], n_all = 0;
	for (int i = 0; i < p->n_new; i++) fresh[i] = all[n_all++] = p->new_w[i];
	for (int i = 0; i < p->n_rev; i++) all[n_all++] = p->rev[i];
	/* 1: recognition of every new word, among this lesson's new words */
	shuffle(fresh, p->n_new, &rng);
	for (int i = 0; i < p->n_new && n < max; i++)
		if (kl_make_ex(db, fresh[i], KL_Q_MEANING, native, &rng, p->new_w, p->n_new, 1, &q[n])) n++;
	/* 2: the reviews, as hard as their box allows */
	int rev[KL_REV_MAX];
	memcpy(rev, p->rev, (size_t) p->n_rev * sizeof *rev);
	shuffle(rev, p->n_rev, &rng);
	for (int i = 0; i < p->n_rev && n < max; i++) {
		const KlWord *w = &db->w[rev[i]];
		int t = kl_qtype(w, kl_deck_find(deck, w->word), native);
		if (t >= 0 && kl_make_ex(db, rev[i], t, native, &rng, all, n_all, t == KL_Q_MEANING, &q[n])) n++;
	}
	/* 3: every new word again, now meaning → word (or the gap in its example) */
	int last = n ? q[n - 1].word : -1;
	shuffle(fresh, p->n_new, &rng);
	if (p->n_new > 1 && fresh[0] == last) { /* not the same word twice in a row */
		fresh[0] = fresh[p->n_new - 1];
		fresh[p->n_new - 1] = last;
	}
	for (int i = 0; i < p->n_new && n < max; i++)
		if (kl_make_ex(db, fresh[i], KL_Q_WORD, native, &rng, p->new_w, p->n_new, 0, &q[n]) ||
		    kl_make_ex(db, fresh[i], KL_Q_CLOZE, native, &rng, p->new_w, p->n_new, 0, &q[n]))
			n++;
	return n;
}

int kl_again(const KlDb *db, const KlQuestion *prev, const char *native, unsigned *rng, KlQuestion *q)
{
	/* a missed word comes back at the same type, a hard one one step easier (meaning → word) */
	int t = prev->type == KL_Q_MEANING || prev->type == KL_Q_WORD ? prev->type : KL_Q_WORD;
	return kl_make(db, prev->word, t, native, rng, q) || kl_make(db, prev->word, KL_Q_MEANING, native, rng, q) ||
	       kl_make(db, prev->word, prev->type == KL_Q_TF ? KL_Q_CLOZE : prev->type, native, rng, q);
}

int kl_insert(KlQuestion *q, int *n, int max, int at, int gap, const KlQuestion *nq)
{
	if (*n >= max) return -1;
	int pos = at + gap;
	if (pos > *n) pos = *n;
	if (pos < 0) pos = 0;
	memmove(&q[pos + 1], &q[pos], (size_t) (*n - pos) * sizeof *q);
	q[pos] = *nq;
	(*n)++;
	return pos;
}

/* ---- placement ---- */

int kl_place_pick(const KlCourse *c, int lv, unsigned *rng, int *out, int k)
{
	if (lv < 1 || lv > KL_LEVELS) return 0;
	int a = c->lv_start[lv], m = c->lv_start[lv + 1] - a;
	if (m <= 0) return 0;
	int *idx = malloc((size_t) m * sizeof *idx);
	if (!idx) return 0;
	for (int i = 0; i < m; i++) idx[i] = a + i;
	if (k > m) k = m;
	for (int i = 0; i < k; i++) {
		int j = i + (int) (kl_rand(rng) % (unsigned) (m - i)), t = idx[j];
		idx[j] = idx[i];
		idx[i] = t;
		out[i] = c->word[t];
	}
	free(idx);
	return k;
}

int kl_place_passed(int asked, int right) { return asked > 0 && right * 5 >= asked * 4; }

int kl_place_level(const int *asked, const int *right, int *more)
{
	for (int lv = 1; lv <= KL_LEVELS; lv++) {
		if (!asked[lv]) {
			if (more) *more = 1;
			return lv;
		}
		if (!kl_place_passed(asked[lv], right[lv])) {
			if (more) *more = 0;
			return lv;
		}
	}
	if (more) *more = 0;
	return KL_LEVELS;
}

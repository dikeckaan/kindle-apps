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
			char *f[8];
			int k = 0;
			f[k++] = p;
			for (char *q = p; *q && k < 8; q++)
				if (*q == '\t') {
					*q = '\0';
					f[k++] = q + 1;
				}
			int lv = k >= 4 ? atoi(f[3]) : 0;
			if (k == 8 && *f[0] && lv >= 1 && lv <= KL_LEVELS && !strchr(f[7], '\t')) {
				KlWord *w = &db->w[db->n++];
				*w = (KlWord){ f[0], f[1], f[4], f[5], f[6], f[7], (unsigned char) kl_pos_parse(f[2]), (unsigned char) lv };
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
	d->c[d->n++] = (KlCard){ k, 0, 0, 0, 0 };
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
			int box, due, seen, right;
			if (t && t != line) {
				*t = '\0';
				if (sscanf(t + 1, "%d\t%d\t%d\t%d", &box, &due, &seen, &right) == 4 && box >= 0 && box <= KL_BOX_MAX) {
					KlCard *c = kl_deck_get(d, line);
					if (c) *c = (KlCard){ c->key, box, due, seen, right };
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
		int n = snprintf(buf + o, (size_t) (cap - o), "%s\t%d\t%d\t%d\t%d\n", c->key, c->box, c->due, c->seen, c->right);
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
	if (correct) {
		c->right++;
		if (c->box < KL_BOX_MAX) c->box++;
		c->due = today + kl_interval(c->box);
	} else {
		c->box = 1;
		c->due = today;
	}
}

int kl_card_due(const KlCard *c, int today) { return c && c->seen > 0 && c->box > 0 && c->due <= today; }

void kl_progress(const KlDb *db, const KlDeck *deck, int lv, int today, int *learned, int *seen, int *due)
{
	int l = 0, s = 0, du = 0;
	for (int i = 0; i < db->n; i++) {
		if (lv && db->w[i].level != lv) continue;
		const KlCard *c = kl_deck_find(deck, db->w[i].word);
		if (!c || !c->seen) continue;
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
static int fair(const KlDb *db, int answer, int cand, int type, const char *native, const int *taken, int n_taken)
{
	if (cand == answer) return 0;
	const KlWord *a = &db->w[answer], *c = &db->w[cand];
	if (ci_eq(a->word, c->word)) return 0;
	if (type != KL_Q_DEFINE && type != KL_Q_CLOZE && !kl_has_field(c->tr, native)) return 0;
	char ta[256], tc[256], tt[256];
	/* a word with the same translation would be a second right answer */
	if (kl_field(a->tr, native, ta, sizeof ta) && kl_field(c->tr, native, tc, sizeof tc) && ci_eq(ta, tc)) return 0;
	opt_text(db, cand, type, native, tc, sizeof tc);
	if (!tc[0]) return 0;
	for (int i = 0; i < n_taken; i++) {
		if (taken[i] == cand) return 0;
		opt_text(db, taken[i], type, native, tt, sizeof tt);
		if (ci_eq(tt, tc)) return 0;
	}
	return 1;
}

/* fills out[0..n) with distractors for answer; returns how many were found */
static int distractors(const KlDb *db, int answer, int type, const char *native, unsigned *rng, int *taken, int n_taken, int want)
{
	const KlWord *a = &db->w[answer];
	int got = 0;
	/* passes: same POS and level, same POS ±1 level, ±1 level, anything; each pass draws from the matching words */
	int *pool = malloc((size_t) db->n * sizeof *pool);
	if (!pool) return 0;
	for (int pass = 0; pass < 4 && got < want; pass++) {
		int np = 0;
		for (int c = 0; c < db->n; c++) {
			const KlWord *w = &db->w[c];
			int dl = w->level > a->level ? w->level - a->level : a->level - w->level;
			int ok = pass == 0 ? (w->pos == a->pos && dl == 0) : pass == 1 ? (w->pos == a->pos && dl == 1) : pass == 2 ? (w->pos != a->pos && dl <= 1) : dl > 1;
			if (ok) pool[np++] = c;
		}
		/* random order without repeats: a partial Fisher–Yates shuffle, stopped once enough fair words were found */
		for (int i = 0; i < np && got < want; i++) {
			int j = i + (int) (kl_rand(rng) % (unsigned) (np - i)), c = pool[j];
			pool[j] = pool[i];
			pool[i] = c;
			if (fair(db, answer, c, type, native, taken, n_taken + got)) taken[n_taken + got++] = c;
		}
	}
	free(pool);
	return got;
}

int kl_make(const KlDb *db, int word, int type, const char *native, unsigned *rng, KlQuestion *q)
{
	if (word < 0 || word >= db->n || !kl_can_ask(&db->w[word], type, native)) return 0;
	memset(q, 0, sizeof *q);
	q->type = type;
	q->word = word;
	if (type == KL_Q_TF) {
		int t[2] = { word, -1 };
		if (kl_rand(rng) & 1) { /* shown translation belongs to another word */
			if (distractors(db, word, KL_Q_MEANING, native, rng, t, 1, 1) == 1) {
				q->opts[0] = t[1];
				q->answer = 0;
			} else {
				q->opts[0] = word;
				q->answer = 1;
			}
		} else {
			q->opts[0] = word;
			q->answer = 1;
		}
		q->n_opts = 1;
		return 1;
	}
	int t[4] = { word, -1, -1, -1 };
	int got = distractors(db, word, type, native, rng, t, 1, 3);
	if (got < 1) return 0;
	q->n_opts = got + 1;
	q->answer = (int) (kl_rand(rng) % (unsigned) q->n_opts);
	for (int i = 0, k = 1; i < q->n_opts; i++) q->opts[i] = i == q->answer ? word : t[k++];
	return 1;
}

/* a question type for a word, given how well it is known */
static int pick_type(const KlDb *db, int word, const KlCard *c, const char *native, unsigned *rng)
{
	const KlWord *w = &db->w[word];
	if (!c || !c->seen) {
		if (kl_can_ask(w, KL_Q_MEANING, native)) return KL_Q_MEANING;
	}
	static const int pool[] = { KL_Q_MEANING, KL_Q_WORD, KL_Q_DEFINE, KL_Q_CLOZE, KL_Q_TF, KL_Q_WORD, KL_Q_DEFINE, KL_Q_CLOZE };
	int n = (int) (sizeof pool / sizeof pool[0]);
	int start = (int) (kl_rand(rng) % (unsigned) n);
	for (int i = 0; i < n; i++) {
		int t = pool[(start + i) % n];
		if (kl_can_ask(w, t, native)) return t;
	}
	return -1;
}

static int askable(const KlWord *w, const char *native)
{
	return kl_can_ask(w, KL_Q_MEANING, native) || kl_can_ask(w, KL_Q_DEFINE, native) || kl_can_ask(w, KL_Q_CLOZE, native);
}

static void shuffle(int *a, int n, unsigned *rng)
{
	for (int i = n - 1; i > 0; i--) {
		int j = (int) (kl_rand(rng) % (unsigned) (i + 1)), t = a[i];
		a[i] = a[j];
		a[j] = t;
	}
}

int kl_lesson(const KlDb *db, const KlDeck *deck, const char *native, int lv, int today, unsigned seed, KlQuestion *q, int max)
{
	if (!db->n || max < 1) return 0;
	unsigned rng = seed ? seed : 1;
	int *due = malloc((size_t) db->n * sizeof *due), *fresh = malloc((size_t) db->n * sizeof *fresh), *prac = malloc((size_t) db->n * sizeof *prac);
	int nd = 0, nf = 0, np = 0, pick[64], n = 0;
	if (max > 64) max = 64;
	if (!due || !fresh || !prac) goto out;
	for (int i = 0; i < db->n; i++) {
		const KlWord *w = &db->w[i];
		if ((lv && w->level != lv) || !askable(w, native)) continue;
		const KlCard *c = kl_deck_find(deck, w->word);
		if (kl_card_due(c, today)) due[nd++] = i;
		else if (!c || !c->seen) fresh[nf++] = i;
		else prac[np++] = i;
	}
	/* most overdue first, then the lowest box */
	for (int i = 1; i < nd; i++)
		for (int j = i; j > 0; j--) {
			const KlCard *a = kl_deck_find(deck, db->w[due[j - 1]].word), *b = kl_deck_find(deck, db->w[due[j]].word);
			if (a->due < b->due || (a->due == b->due && a->box <= b->box)) break;
			int t = due[j];
			due[j] = due[j - 1];
			due[j - 1] = t;
		}
	if (lv == 0) {
		for (int i = 0; i < nd && n < max; i++) pick[n++] = due[i];
		/* then the weakest words seen so far */
		for (int box = 1; box < KL_BOX_MAX && n < max; box++) {
			int cand[64], nc = 0;
			for (int i = 0; i < np && nc < 64; i++)
				if (kl_deck_find(deck, db->w[prac[i]].word)->box == box) cand[nc++] = prac[i];
			shuffle(cand, nc, &rng);
			for (int i = 0; i < nc && n < max; i++) pick[n++] = cand[i];
		}
	} else {
		int review = max * 3 / 10, new_n = max / 2, id = 0, jf = 0;
		while (id < nd && n < review) pick[n++] = due[id++];
		while (jf < nf && n < review + new_n) pick[n++] = fresh[jf++];
		shuffle(prac, np, &rng);
		for (int i = 0; i < np && n < max; i++) pick[n++] = prac[i];
		while (id < nd && n < max) pick[n++] = due[id++];
		while (jf < nf && n < max) pick[n++] = fresh[jf++];
	}
	if (lv) shuffle(pick, n, &rng); /* a review keeps the most overdue words first */
	int k = 0;
	for (int i = 0; i < n; i++) {
		const KlCard *c = kl_deck_find(deck, db->w[pick[i]].word);
		int t = pick_type(db, pick[i], c, native, &rng);
		if (t >= 0 && kl_make(db, pick[i], t, native, &rng, &q[k])) k++;
		else if (kl_make(db, pick[i], KL_Q_DEFINE, native, &rng, &q[k]) || kl_make(db, pick[i], KL_Q_CLOZE, native, &rng, &q[k])) k++;
	}
	n = k;
out:
	free(due);
	free(fresh);
	free(prac);
	return n;
}

int kl_again(const KlDb *db, const KlQuestion *prev, const char *native, unsigned *rng, KlQuestion *q)
{
	static const int order[] = { KL_Q_WORD, KL_Q_MEANING, KL_Q_DEFINE, KL_Q_CLOZE };
	int start = (int) (kl_rand(rng) % 4u);
	for (int i = 0; i < 4; i++) {
		int t = order[(start + i) % 4];
		if (t != prev->type && kl_make(db, prev->word, t, native, rng, q)) return 1;
	}
	return kl_make(db, prev->word, prev->type == KL_Q_TF ? KL_Q_MEANING : prev->type, native, rng, q);
}

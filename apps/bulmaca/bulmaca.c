/* Bulmaca: pure crossword logic (see bulmaca.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "bulmaca.h"

/* ---- letters ---- */

int bm_fold(int cp)
{
	return kui_tr_upper(kui_tr_fold_circumflex(kui_tr_upper(cp)));
}

int bm_is_letter(int cp)
{
	static const int tr[] = { 0xC7, 0x11E, 'I', 0x130, 0xD6, 0x15E, 0xDC };
	int u = bm_fold(cp);
	if (u >= 'A' && u <= 'Z') return u != 'Q' && u != 'W' && u != 'X';
	for (int i = 0; i < (int) (sizeof tr / sizeof tr[0]); i++)
		if (u == tr[i]) return 1;
	return 0;
}

/* ---- grid numbering ---- */

static int is_open(const BmPuzzle *p, int r, int c) { return r >= 0 && c >= 0 && r < p->h && c < p->w && p->sol[r * p->w + c]; }

int bm_word_cell(const BmPuzzle *p, int word, int i)
{
	const BmWord *w = &p->words[word];
	return w->dir == BM_ACROSS ? w->r * p->w + w->c + i : (w->r + i) * p->w + w->c;
}

void bm_number(BmPuzzle *p)
{
	int n = 0, next = 1;
	BmWord across[BM_MAX_WORDS], down[BM_MAX_WORDS];
	int na = 0, nd = 0;
	memset(p->num, 0, sizeof p->num);
	for (int r = 0; r < p->h; r++)
		for (int c = 0; c < p->w; c++) {
			if (!is_open(p, r, c)) continue;
			int a = !is_open(p, r, c - 1) && is_open(p, r, c + 1);
			int d = !is_open(p, r - 1, c) && is_open(p, r + 1, c);
			if (!a && !d) continue;
			int num = next++;
			p->num[r * p->w + c] = num;
			if (a && na < BM_MAX_WORDS) {
				int len = 0;
				while (is_open(p, r, c + len)) len++;
				across[na++] = (BmWord){ BM_ACROSS, r, c, len, num, "" };
			}
			if (d && nd < BM_MAX_WORDS) {
				int len = 0;
				while (is_open(p, r + len, c)) len++;
				down[nd++] = (BmWord){ BM_DOWN, r, c, len, num, "" };
			}
		}
	for (int i = 0; i < na && n < BM_MAX_WORDS; i++) p->words[n++] = across[i];
	p->n_across = n;
	for (int i = 0; i < nd && n < BM_MAX_WORDS; i++) p->words[n++] = down[i];
	p->n_words = n;
	for (int k = 0; k < 2; k++)
		for (int i = 0; i < BM_CELLS; i++) p->word_at[k][i] = -1;
	for (int i = 0; i < n; i++)
		for (int j = 0; j < p->words[i].len; j++) p->word_at[p->words[i].dir][bm_word_cell(p, i, j)] = i;
}

void bm_word_answer(const BmPuzzle *p, int word, char *out, int cap)
{
	int o = 0;
	char enc[4];
	for (int i = 0; i < p->words[word].len; i++) {
		int k = kui_utf8_encode(p->sol[bm_word_cell(p, word, i)], enc);
		if (o + k >= cap) break;
		memcpy(out + o, enc, (size_t) k);
		o += k;
	}
	if (cap > 0) out[o] = '\0';
}

/* ---- puzzle file ---- */

static const char *line_end(const char *s)
{
	const char *e = strchr(s, '\n');
	return e ? e : s + strlen(s);
}

int bm_parse(const char *text, BmPuzzle *p)
{
	memset(p, 0, sizeof *p);
	if (sscanf(text, "P %d %d %d", &p->id, &p->w, &p->h) != 3 || p->w < 3 || p->h < 3 || p->w > BM_MAX || p->h > BM_MAX)
		return -1;
	const char *s = line_end(text);
	int row = 0;
	int have[BM_MAX_WORDS] = { 0 };
	int numbered = 0;
	while (*s) {
		s++; /* the '\n' */
		const char *e = line_end(s);
		if (s[0] == 'P' && s[1] == ' ') break; /* the next puzzle */
		if (s[0] == 'G' && s[1] == ' ') {
			if (row >= p->h) return -1;
			const char *q = s + 2;
			for (int c = 0; c < p->w; c++) {
				if (q >= e) return -1;
				int cp;
				q += kui_utf8_decode(q, &cp);
				if (cp == '#') p->sol[row * p->w + c] = 0;
				else if (bm_is_letter(cp)) p->sol[row * p->w + c] = bm_fold(cp);
				else return -1;
			}
			if (q != e && *q != '\r') return -1;
			row++;
		} else if ((s[0] == 'A' || s[0] == 'D') && s[1] == ' ') {
			if (row != p->h) return -1;
			if (!numbered) {
				bm_number(p);
				numbered = 1;
			}
			int r, c, off = 0;
			if (sscanf(s + 2, "%d %d %n", &r, &c, &off) != 2 || !off) return -1;
			const char *ans = s + 2 + off, *tab = memchr(ans, '\t', (size_t) (e - ans));
			if (!tab || r < 0 || c < 0 || r >= p->h || c >= p->w) return -1;
			int dir = s[0] == 'A' ? BM_ACROSS : BM_DOWN, wi = p->word_at[dir][r * p->w + c];
			if (wi < 0 || p->words[wi].r != r || p->words[wi].c != c) return -1;
			/* the answer must spell the grid's letters */
			const char *q = ans;
			for (int i = 0; i < p->words[wi].len; i++) {
				int cp;
				if (q >= tab) return -1;
				q += kui_utf8_decode(q, &cp);
				if (bm_fold(cp) != p->sol[bm_word_cell(p, wi, i)]) return -1;
			}
			if (q != tab) return -1;
			int n = (int) (e - tab - 1);
			if (n > 0 && tab[n] == '\r') n--;
			if (n <= 0) return -1;
			if (n >= BM_CLUE) n = BM_CLUE - 1;
			while (n > 0 && ((unsigned char) tab[1 + n] & 0xC0) == 0x80) n--; /* do not cut a letter */
			memcpy(p->words[wi].clue, tab + 1, (size_t) n);
			p->words[wi].clue[n] = '\0';
			have[wi] = 1;
		} else if (s < e && s[0] != '#') {
			return -1;
		}
		s = e;
	}
	if (row != p->h) return -1;
	if (!numbered) bm_number(p);
	if (!p->n_words) return -1;
	for (int i = 0; i < p->n_words; i++)
		if (!have[i]) return -1;
	return 0;
}

int bm_db_load(const char *path, BmDb *db)
{
	memset(db, 0, sizeof *db);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0 || !(db->buf = malloc((size_t) n + 1))) {
		fclose(f);
		return -1;
	}
	n = (long) fread(db->buf, 1, (size_t) n, f);
	fclose(f);
	db->buf[n] = '\0';
	int cap = 0;
	for (const char *s = db->buf; *s;) {
		if (s[0] == 'P' && s[1] == ' ') {
			if (db->n == cap) {
				cap = cap ? cap * 2 : 256;
				const char **np = realloc(db->p, sizeof *np * (size_t) cap);
				if (!np) break;
				db->p = np;
			}
			db->p[db->n++] = s;
		}
		const char *e = strchr(s, '\n');
		if (!e) break;
		s = e + 1;
	}
	return db->n;
}

void bm_db_free(BmDb *db)
{
	free(db->buf);
	free(db->p);
	memset(db, 0, sizeof *db);
}

int bm_db_get(const BmDb *db, int index, BmPuzzle *p)
{
	if (index < 0 || index >= db->n) return -1;
	return bm_parse(db->p[index], p);
}

/* ---- selection and cursor ---- */

int bm_select(const BmPuzzle *p, int cell, int dir, int same_tap, int *out_dir)
{
	if (cell < 0 || cell >= p->w * p->h || !p->sol[cell]) return -1;
	int a = p->word_at[BM_ACROSS][cell], d = p->word_at[BM_DOWN][cell];
	int want = same_tap ? !dir : dir;
	int w = want == BM_ACROSS ? a : d;
	if (w < 0) { /* only one direction here */
		want = !want;
		w = want == BM_ACROSS ? a : d;
	}
	if (w >= 0 && out_dir) *out_dir = want;
	return w;
}

int bm_word_pos(const BmPuzzle *p, int word, int cell)
{
	if (word < 0 || word >= p->n_words) return -1;
	for (int i = 0; i < p->words[word].len; i++)
		if (bm_word_cell(p, word, i) == cell) return i;
	return -1;
}

int bm_next_cell(const BmPuzzle *p, const BmState *s, int word, int cell)
{
	if (word < 0 || word >= p->n_words) return cell;
	int pos = bm_word_pos(p, word, cell), len = p->words[word].len;
	if (pos < 0) return cell;
	for (int i = pos + 1; i < len; i++) {
		int c = bm_word_cell(p, word, i);
		if (!s->fill[c]) return c;
	}
	return pos + 1 < len ? bm_word_cell(p, word, pos + 1) : cell;
}

int bm_prev_cell(const BmPuzzle *p, int word, int cell)
{
	if (word < 0 || word >= p->n_words) return cell;
	int pos = bm_word_pos(p, word, cell);
	return pos > 0 ? bm_word_cell(p, word, pos - 1) : cell;
}

int bm_first_empty(const BmPuzzle *p, const BmState *s, int word)
{
	for (int i = 0; i < p->words[word].len; i++) {
		int c = bm_word_cell(p, word, i);
		if (!s->fill[c]) return c;
	}
	return bm_word_cell(p, word, 0);
}

int bm_step_word(const BmPuzzle *p, int word, int delta)
{
	if (p->n_words <= 0) return -1;
	return ((word + delta) % p->n_words + p->n_words) % p->n_words;
}

/* ---- play ---- */

void bm_state_init(BmState *s, const BmPuzzle *p)
{
	memset(s, 0, sizeof *s);
	s->id = p->id;
}

int bm_type(const BmPuzzle *p, BmState *s, int cell, int cp)
{
	if (cell < 0 || !p->sol[cell] || (s->flags[cell] & BM_REVEALED) || !bm_is_letter(cp)) return 0;
	int u = bm_fold(cp);
	int changed = s->fill[cell] != u || (s->flags[cell] & BM_WRONG);
	s->fill[cell] = u;
	s->flags[cell] &= (unsigned char) ~BM_WRONG;
	return changed;
}

int bm_erase(const BmPuzzle *p, BmState *s, int cell)
{
	if (cell < 0 || !p->sol[cell] || (s->flags[cell] & BM_REVEALED) || !s->fill[cell]) return 0;
	s->fill[cell] = 0;
	s->flags[cell] &= (unsigned char) ~BM_WRONG;
	return 1;
}

int bm_reveal(const BmPuzzle *p, BmState *s, int cell)
{
	if (cell < 0 || !p->sol[cell] || (s->flags[cell] & BM_REVEALED)) return 0;
	if (s->fill[cell] == p->sol[cell]) return 0; /* already right: no help needed */
	s->fill[cell] = p->sol[cell];
	s->flags[cell] = BM_REVEALED;
	s->helped = 1;
	return 1;
}

static int check_cell(const BmPuzzle *p, BmState *s, int c, int *empty)
{
	if (!s->fill[c]) {
		(*empty)++;
		return 0;
	}
	if (s->fill[c] == p->sol[c]) return 0;
	s->flags[c] |= BM_WRONG;
	return 1;
}

int bm_check_word(const BmPuzzle *p, BmState *s, int word, int *empty)
{
	int wrong = 0, e = 0;
	for (int i = 0; i < p->words[word].len; i++) wrong += check_cell(p, s, bm_word_cell(p, word, i), &e);
	if (empty) *empty = e;
	return wrong;
}

int bm_check_all(const BmPuzzle *p, BmState *s, int *empty)
{
	int wrong = 0, e = 0;
	for (int c = 0; c < p->w * p->h; c++)
		if (p->sol[c]) wrong += check_cell(p, s, c, &e);
	if (empty) *empty = e;
	return wrong;
}

int bm_solved(const BmPuzzle *p, const BmState *s)
{
	for (int c = 0; c < p->w * p->h; c++)
		if (p->sol[c] && s->fill[c] != p->sol[c]) return 0;
	return 1;
}

int bm_filled_pct(const BmPuzzle *p, const BmState *s)
{
	int open = 0, full = 0;
	for (int c = 0; c < p->w * p->h; c++)
		if (p->sol[c]) {
			open++;
			full += s->fill[c] != 0;
		}
	return open ? full * 100 / open : 0;
}

int bm_word_full(const BmPuzzle *p, const BmState *s, int word)
{
	for (int i = 0; i < p->words[word].len; i++)
		if (!s->fill[bm_word_cell(p, word, i)]) return 0;
	return 1;
}

/* ---- saved progress ---- */

unsigned bm_hash(const BmPuzzle *p)
{
	unsigned h = 2166136261u; /* FNV-1a over the solution: a save never lands on a different grid */
	for (int c = 0; c < p->w * p->h; c++) {
		h = (h ^ (unsigned) p->sol[c]) * 16777619u;
		h = (h ^ (unsigned) (p->sol[c] >> 8)) * 16777619u;
	}
	return h;
}

int bm_save_format(const BmPuzzle *p, const BmState *s, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "bulmaca 1\nid %d\ngrid %08x\nfill ", p->id, bm_hash(p));
	char enc[4];
	for (int c = 0; c < p->w * p->h && o < cap; c++) {
		int k = 1;
		if (!p->sol[c]) enc[0] = '#';
		else if (!s->fill[c]) enc[0] = '-';
		else k = kui_utf8_encode(s->fill[c], enc);
		if (o + k >= cap) return -1;
		memcpy(buf + o, enc, (size_t) k);
		o += k;
	}
	if (o + p->w * p->h + 6 >= cap) return -1;
	o += snprintf(buf + o, (size_t) (cap - o), "\nrev ");
	for (int c = 0; c < p->w * p->h; c++) buf[o++] = (s->flags[c] & BM_REVEALED) ? '1' : '0';
	o += snprintf(buf + o, (size_t) (cap - o), "\nsec %d\ndone %d\nhelp %d\n", s->seconds, s->done, s->helped);
	return o < cap ? o : -1;
}

static const char *key_line(const char *buf, const char *key)
{
	size_t k = strlen(key);
	for (const char *s = buf; s && *s; s = strchr(s, '\n'), s = s ? s + 1 : NULL)
		if (!strncmp(s, key, k) && s[k] == ' ') return s + k + 1;
	return NULL;
}

int bm_save_parse(const char *buf, const BmPuzzle *p, BmState *s)
{
	BmState t;
	bm_state_init(&t, p);
	const char *v;
	if (strncmp(buf, "bulmaca 1\n", 10) || !(v = key_line(buf, "id")) || atoi(v) != p->id) return -1;
	if (!(v = key_line(buf, "grid")) || strtoul(v, NULL, 16) != bm_hash(p)) return -1;
	if (!(v = key_line(buf, "fill"))) return -1;
	for (int c = 0; c < p->w * p->h; c++) {
		int cp;
		if (!*v || *v == '\n') return -1;
		v += kui_utf8_decode(v, &cp);
		if (!p->sol[c]) {
			if (cp != '#') return -1;
		} else if (cp == '-') {
			t.fill[c] = 0;
		} else if (bm_is_letter(cp)) {
			t.fill[c] = bm_fold(cp);
		} else {
			return -1;
		}
	}
	if ((v = key_line(buf, "rev")))
		for (int c = 0; c < p->w * p->h && v[c] && v[c] != '\n'; c++)
			if (v[c] == '1' && p->sol[c]) {
				t.flags[c] = BM_REVEALED;
				t.fill[c] = p->sol[c];
			}
	if ((v = key_line(buf, "sec"))) t.seconds = atoi(v) > 0 ? atoi(v) : 0;
	if ((v = key_line(buf, "done"))) t.done = atoi(v) != 0;
	if ((v = key_line(buf, "help"))) t.helped = atoi(v) != 0;
	*s = t;
	return 0;
}

/* ---- days ---- */

long bm_days(int y, int m, int d)
{ /* H. Hinnant's days_from_civil */
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void bm_civil(long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	long doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1);
	*m = (int) (mp < 10 ? mp + 3 : mp - 9);
	*y = (int) (yoe + era * 400 + (*m <= 2));
}

int bm_daily(long day, int n)
{
	if (n <= 0) return -1;
	long k = (day - bm_days(BM_EPOCH_Y, 1, 1)) % n;
	return (int) (k < 0 ? k + n : k);
}

/* ---- statistics ---- */

void bm_stats_record(BmStats *st, long daily_day, int seconds, int clean)
{
	st->solved++;
	if (clean) st->clean++;
	if (seconds > 0) {
		st->total_sec += seconds;
		if (!st->best_sec || seconds < st->best_sec) st->best_sec = seconds;
	}
	if (daily_day >= 0 && daily_day != st->last_day) {
		st->streak = st->last_day && daily_day == st->last_day + 1 ? st->streak + 1 : 1;
		st->last_day = (int) daily_day;
		if (st->streak > st->best) st->best = st->streak;
	}
}

int bm_stats_format(const BmStats *st, char *buf, int cap)
{
	int n = snprintf(buf, (size_t) cap, "solved=%d\nclean=%d\nstreak=%d\nbest=%d\nlast_day=%d\ntotal_sec=%d\nbest_sec=%d\n", st->solved, st->clean,
			 st->streak, st->best, st->last_day, st->total_sec, st->best_sec);
	return n < cap ? n : -1;
}

int bm_stats_parse(const char *buf, BmStats *st)
{
	memset(st, 0, sizeof *st);
	struct { const char *k; int *v; } keys[] = {
		{ "solved", &st->solved }, { "clean", &st->clean }, { "streak", &st->streak }, { "best", &st->best },
		{ "last_day", &st->last_day }, { "total_sec", &st->total_sec }, { "best_sec", &st->best_sec },
	};
	for (const char *s = buf; s && *s;) {
		for (int i = 0; i < (int) (sizeof keys / sizeof keys[0]); i++) {
			size_t k = strlen(keys[i].k);
			if (!strncmp(s, keys[i].k, k) && s[k] == '=') {
				int v = atoi(s + k + 1);
				*keys[i].v = v > 0 ? v : 0;
			}
		}
		s = strchr(s, '\n');
		if (s) s++;
	}
	return 0;
}

void bm_fmt_time(int sec, char *out, int cap)
{
	if (sec < 0) sec = 0;
	if (sec >= 3600) snprintf(out, (size_t) cap, "%d:%02d:%02d", sec / 3600, sec / 60 % 60, sec % 60);
	else snprintf(out, (size_t) cap, "%d:%02d", sec / 60, sec % 60);
}

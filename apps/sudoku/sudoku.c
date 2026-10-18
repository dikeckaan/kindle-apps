/* Sudoku logic: bitmask solver with MRV, unique-solution generator, board state with notes and undo. */
#include "sudoku.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALL 0x1FF

static int box_of(int i) { return i / 27 * 3 + i % 9 / 3; }

static unsigned xs_next(unsigned *s)
{
	unsigned x = *s;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *s = x;
}

static unsigned xs_seed(unsigned seed)
{
	unsigned s = seed * 2654435761u ^ 0x9E3779B9u;
	if (!s) s = 0x12345678u;
	for (int i = 0; i < 4; i++) xs_next(&s);
	return s;
}

/* ---- solver ---- */

typedef struct {
	unsigned char g[81];
	unsigned short row[9], col[9], box[9]; /* digits used */
	int count, limit;
	unsigned *rng;      /* random candidate order when set */
	unsigned char *out; /* first solution */
} Search;

static int search_init(Search *s, const unsigned char grid[81])
{
	memset(s, 0, sizeof *s);
	for (int i = 0; i < 81; i++) {
		int d = grid[i];
		if (d > 9) return 0;
		s->g[i] = (unsigned char) d;
		if (!d) continue;
		unsigned short b = (unsigned short) (1u << (d - 1));
		int r = i / 9, c = i % 9, x = box_of(i);
		if ((s->row[r] | s->col[c] | s->box[x]) & b) return 0;
		s->row[r] |= b;
		s->col[c] |= b;
		s->box[x] |= b;
	}
	return 1;
}

/* returns 1 when the search should stop (limit reached) */
static int search(Search *s)
{
	int best = -1, bestn = 10;
	unsigned bestm = 0;
	for (int i = 0; i < 81; i++) {
		if (s->g[i]) continue;
		unsigned m = ALL & ~(unsigned) (s->row[i / 9] | s->col[i % 9] | s->box[box_of(i)]);
		int n = __builtin_popcount(m);
		if (n == 0) return 0; /* dead end */
		if (n < bestn) {
			best = i;
			bestn = n;
			bestm = m;
			if (n == 1) break;
		}
	}
	if (best < 0) {
		if (++s->count == 1 && s->out) memcpy(s->out, s->g, 81);
		return s->count >= s->limit;
	}
	int digits[9], n = 0;
	for (int d = 0; d < 9; d++)
		if (bestm & (1u << d)) digits[n++] = d;
	if (s->rng)
		for (int k = n - 1; k > 0; k--) {
			int j = (int) (xs_next(s->rng) % (unsigned) (k + 1)), t = digits[k];
			digits[k] = digits[j];
			digits[j] = t;
		}
	int r = best / 9, c = best % 9, x = box_of(best);
	for (int k = 0; k < n; k++) {
		unsigned short b = (unsigned short) (1u << digits[k]);
		s->g[best] = (unsigned char) (digits[k] + 1);
		s->row[r] |= b;
		s->col[c] |= b;
		s->box[x] |= b;
		int stop = search(s);
		s->row[r] &= (unsigned short) ~b;
		s->col[c] &= (unsigned short) ~b;
		s->box[x] &= (unsigned short) ~b;
		if (stop) {
			s->g[best] = 0;
			return 1;
		}
	}
	s->g[best] = 0;
	return 0;
}

int su_solve(const unsigned char grid[81], unsigned char out[81])
{
	Search s;
	if (!search_init(&s, grid)) return 0;
	s.limit = 1;
	s.out = out;
	search(&s);
	return s.count > 0;
}

int su_count_solutions(const unsigned char grid[81], int limit)
{
	Search s;
	if (limit < 1 || !search_init(&s, grid)) return 0;
	s.limit = limit;
	search(&s);
	return s.count;
}

/* ---- generator ---- */

int su_clue_target(int level)
{
	static const int t[SU_LEVELS] = { 40, 32, 26 };
	return level >= 0 && level < SU_LEVELS ? t[level] : t[SU_MEDIUM];
}

/* one pass of random-order removal from `full`; returns the clue count */
static int dig(const unsigned char full[81], int target, unsigned *rng, unsigned char puzzle[81])
{
	int order[81], clues = 81;
	for (int i = 0; i < 81; i++) order[i] = i;
	for (int k = 80; k > 0; k--) {
		int j = (int) (xs_next(rng) % (unsigned) (k + 1)), t = order[k];
		order[k] = order[j];
		order[j] = t;
	}
	memcpy(puzzle, full, 81);
	for (int k = 0; k < 81 && clues > target; k++) {
		int i = order[k];
		unsigned char d = puzzle[i];
		puzzle[i] = 0;
		if (su_count_solutions(puzzle, 2) == 1) clues--;
		else puzzle[i] = d;
	}
	return clues;
}

#define DIG_TRIES 6 /* removal orders tried when one gets stuck above the target */

int su_generate(unsigned seed, int target, unsigned char puzzle[81], unsigned char solution[81])
{
	unsigned rng = xs_seed(seed);
	unsigned char empty[81] = { 0 }, tmp[81];
	Search s;
	search_init(&s, empty);
	s.limit = 1;
	s.rng = &rng;
	s.out = solution;
	search(&s); /* random full grid */
	if (target < 17) target = 17;
	int best = 82;
	for (int t = 0; t < DIG_TRIES && best > target; t++) {
		int n = dig(solution, target, &rng, tmp);
		if (n < best) {
			best = n;
			memcpy(puzzle, tmp, 81);
		}
	}
	return best;
}

/* ---- board ---- */

static int editable(const Sudoku *s, int cell) { return cell >= 0 && cell < 81 && !s->given[cell]; }

static void push_undo(Sudoku *s, int cell)
{
	SuUndo u = { (unsigned char) cell, s->value[cell], s->notes[cell] };
	if (s->undo_n == SU_UNDO_MAX) {
		s->undo[s->undo_head] = u; /* overwrite the oldest */
		s->undo_head = (s->undo_head + 1) % SU_UNDO_MAX;
	} else {
		s->undo[(s->undo_head + s->undo_n) % SU_UNDO_MAX] = u;
		s->undo_n++;
	}
}

void su_new(Sudoku *s, int level, unsigned seed)
{
	if (level < 0 || level >= SU_LEVELS) level = SU_MEDIUM;
	memset(s, 0, sizeof *s);
	s->level = level;
	su_generate(seed, su_clue_target(level), s->given, s->solution);
	memcpy(s->value, s->given, 81);
}

int su_set(Sudoku *s, int cell, int digit)
{
	if (!editable(s, cell) || digit < 0 || digit > 9) return 0;
	if (s->value[cell] == digit && (digit || !s->notes[cell])) return 0; /* erasing an empty cell drops its notes */
	push_undo(s, cell);
	s->value[cell] = (unsigned char) digit;
	s->notes[cell] = 0;
	return 1;
}

int su_toggle_note(Sudoku *s, int cell, int digit)
{
	if (!editable(s, cell) || digit < 1 || digit > 9 || s->value[cell]) return 0;
	push_undo(s, cell);
	s->notes[cell] ^= (unsigned short) (1u << (digit - 1));
	return 1;
}

int su_hint(Sudoku *s, int cell)
{
	if (!editable(s, cell) || s->value[cell] == s->solution[cell]) return 0;
	return su_set(s, cell, s->solution[cell]);
}

int su_undo(Sudoku *s)
{
	if (s->undo_n == 0) return -1;
	s->undo_n--;
	SuUndo u = s->undo[(s->undo_head + s->undo_n) % SU_UNDO_MAX];
	s->value[u.cell] = u.value;
	s->notes[u.cell] = u.notes;
	return u.cell;
}

int su_is_wrong(const Sudoku *s, int cell)
{
	return editable(s, cell) && s->value[cell] && s->value[cell] != s->solution[cell];
}

int su_is_won(const Sudoku *s) { return memcmp(s->value, s->solution, 81) == 0; }

/* ---- save / load ----
 * sudoku v1
 * level L
 * solution <81 digits>
 * given <81 digits>
 * value <81 digits>
 * notes <81 numbers 0..511>
 * undo N
 * <cell> <value> <notes>     (N lines, oldest first)
 * end
 */

__attribute__((format(printf, 4, 5))) static int put(char *buf, int cap, int *n, const char *fmt, ...)
{
	if (*n >= cap) return -1;
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(buf + *n, (size_t) (cap - *n), fmt, ap);
	va_end(ap);
	if (k < 0 || k >= cap - *n) {
		*n = cap;
		return -1;
	}
	*n += k;
	return 0;
}

static void digits81(const unsigned char g[81], char out[82])
{
	for (int i = 0; i < 81; i++) out[i] = (char) ('0' + g[i]);
	out[81] = '\0';
}

int su_save(const Sudoku *s, char *buf, int cap)
{
	char d[82];
	int n = 0, err = 0;
	err |= put(buf, cap, &n, "sudoku v1\nlevel %d\n", s->level);
	digits81(s->solution, d);
	err |= put(buf, cap, &n, "solution %s\n", d);
	digits81(s->given, d);
	err |= put(buf, cap, &n, "given %s\n", d);
	digits81(s->value, d);
	err |= put(buf, cap, &n, "value %s\nnotes", d);
	for (int i = 0; i < 81; i++) err |= put(buf, cap, &n, " %d", s->notes[i]);
	err |= put(buf, cap, &n, "\nundo %d\n", s->undo_n);
	for (int k = 0; k < s->undo_n; k++) {
		const SuUndo *u = &s->undo[(s->undo_head + k) % SU_UNDO_MAX];
		err |= put(buf, cap, &n, "%d %d %d\n", u->cell, u->value, u->notes);
	}
	err |= put(buf, cap, &n, "end\n");
	return err ? -1 : n;
}

/* parses "<key> " + 81 digits + '\n' */
static int read_digits(const char **pp, const char *key, unsigned char out[81])
{
	const char *p = *pp;
	size_t kl = strlen(key);
	if (strncmp(p, key, kl) != 0 || p[kl] != ' ') return -1;
	p += kl + 1;
	for (int i = 0; i < 81; i++) {
		if (p[i] < '0' || p[i] > '9') return -1;
		out[i] = (unsigned char) (p[i] - '0');
	}
	if (p[81] != '\n') return -1;
	*pp = p + 82;
	return 0;
}

/* parses a decimal in [lo, hi] preceded by optional spaces */
static int read_int(const char **pp, long lo, long hi, long *v)
{
	const char *p = *pp;
	while (*p == ' ') p++;
	if (*p < '0' || *p > '9') return -1;
	char *end;
	*v = strtol(p, &end, 10);
	if (end - p > 6 || *v < lo || *v > hi) return -1;
	*pp = end;
	return 0;
}

static int expect(const char **pp, const char *lit)
{
	size_t n = strlen(lit);
	if (strncmp(*pp, lit, n) != 0) return -1;
	*pp += n;
	return 0;
}

static int valid_solution(const unsigned char g[81])
{
	unsigned short row[9] = { 0 }, col[9] = { 0 }, box[9] = { 0 };
	for (int i = 0; i < 81; i++) {
		if (g[i] < 1 || g[i] > 9) return 0;
		unsigned short b = (unsigned short) (1u << (g[i] - 1));
		if ((row[i / 9] | col[i % 9] | box[box_of(i)]) & b) return 0;
		row[i / 9] |= b;
		col[i % 9] |= b;
		box[box_of(i)] |= b;
	}
	return 1;
}

int su_load(Sudoku *s, const char *buf)
{
	static Sudoku t; /* large-ish; keeps the caller's stack small */
	const char *p = buf;
	long v;
	memset(&t, 0, sizeof t);
	if (expect(&p, "sudoku v1\nlevel ") || read_int(&p, 0, SU_LEVELS - 1, &v) || expect(&p, "\n")) return -1;
	t.level = (int) v;
	if (read_digits(&p, "solution", t.solution) || read_digits(&p, "given", t.given) ||
	    read_digits(&p, "value", t.value))
		return -1;
	if (!valid_solution(t.solution)) return -1;
	for (int i = 0; i < 81; i++) {
		if (t.given[i] && t.given[i] != t.solution[i]) return -1;
		if (t.given[i] && t.value[i] != t.given[i]) return -1;
	}
	if (expect(&p, "notes")) return -1;
	for (int i = 0; i < 81; i++) {
		if (*p != ' ' || read_int(&p, 0, ALL, &v)) return -1;
		if (v && (t.given[i] || t.value[i])) return -1;
		t.notes[i] = (unsigned short) v;
	}
	if (expect(&p, "\nundo ") || read_int(&p, 0, SU_UNDO_MAX, &v) || expect(&p, "\n")) return -1;
	t.undo_n = (int) v;
	for (int k = 0; k < t.undo_n; k++) {
		long c, val, nt;
		if (read_int(&p, 0, 80, &c) || read_int(&p, 0, 9, &val) || read_int(&p, 0, ALL, &nt) || expect(&p, "\n"))
			return -1;
		if (t.given[c]) return -1;
		t.undo[k] = (SuUndo){ (unsigned char) c, (unsigned char) val, (unsigned short) nt };
	}
	if (expect(&p, "end\n")) return -1;
	*s = t;
	return 0;
}

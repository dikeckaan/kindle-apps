#include <time.h>
#include "sudoku/sudoku.h"
#include "t.h"

static const char *KNOWN = "530070000600195000098000060800060003400803001700020006060000280000419005000080079";
static const char *KNOWN_SOL = "534678912672195348198342567859761423426853791713924856961537284287419635345286179";

static void parse(const char *s, unsigned char g[81])
{
	for (int i = 0; i < 81; i++) g[i] = (unsigned char) (s[i] - '0');
}

static void fmt(const unsigned char g[81], char out[82])
{
	for (int i = 0; i < 81; i++) out[i] = (char) ('0' + g[i]);
	out[81] = '\0';
}

/* every row, column and box holds 1..9 exactly once */
static int valid_full(const unsigned char g[81])
{
	for (int u = 0; u < 9; u++) {
		int rm = 0, cm = 0, bm = 0;
		for (int k = 0; k < 9; k++) {
			int r = g[u * 9 + k], c = g[k * 9 + u], b = g[(u / 3 * 3 + k / 3) * 9 + u % 3 * 3 + k % 3];
			if (r < 1 || r > 9 || c < 1 || c > 9 || b < 1 || b > 9) return 0;
			rm |= 1 << r; cm |= 1 << c; bm |= 1 << b;
		}
		if (rm != 0x3FE || cm != 0x3FE || bm != 0x3FE) return 0;
	}
	return 1;
}

static void test_solve_known(void)
{
	unsigned char g[81], out[81];
	char s[82];
	parse(KNOWN, g);
	T_EQ_INT(su_solve(g, out), 1);
	fmt(out, s);
	T_EQ_STR(s, KNOWN_SOL);
}

static void test_count_solutions(void)
{
	unsigned char g[81];
	parse(KNOWN, g);
	T_EQ_INT(su_count_solutions(g, 2), 1);

	unsigned char empty[81] = { 0 };
	T_EQ_INT(su_count_solutions(empty, 2), 2); /* stops at the limit */

	parse(KNOWN, g);
	g[1] = 5; /* two 5s in row 0 */
	T_EQ_INT(su_count_solutions(g, 2), 0);
	unsigned char out[81];
	T_EQ_INT(su_solve(g, out), 0);

	/* no conflict among the clues, but cell (0,8) has no candidate */
	unsigned char dead[81] = { 0 };
	for (int i = 0; i < 8; i++) dead[i] = (unsigned char) (i + 1);
	dead[9 * 4 + 8] = 9;
	T_EQ_INT(su_count_solutions(dead, 2), 0);

	/* the known solution with a swappable rectangle cleared has exactly two solutions */
	parse(KNOWN_SOL, g);
	/* find a rectangle a b / b a whose rows share a band: the two digits can swap */
	int found = 0;
	for (int r1 = 0; r1 < 9 && !found; r1++)
		for (int r2 = r1 + 1; r2 < 9 && !found; r2++)
			if (r1 / 3 == r2 / 3)
				for (int c1 = 0; c1 < 9 && !found; c1++)
					for (int c2 = c1 + 1; c2 < 9 && !found; c2++)
						if (g[r1 * 9 + c1] == g[r2 * 9 + c2] && g[r1 * 9 + c2] == g[r2 * 9 + c1]) {
							g[r1 * 9 + c1] = g[r2 * 9 + c2] = g[r1 * 9 + c2] = g[r2 * 9 + c1] = 0;
							found = 1;
						}
	T_ASSERT(found);
	T_EQ_INT(su_count_solutions(g, 5), 2);
}

static void test_generator(void)
{
	static const int targets[3] = { 40, 32, 26 };
	clock_t t0 = clock();
	for (int lv = 0; lv < 3; lv++) {
		T_EQ_INT(su_clue_target(lv), targets[lv]);
		clock_t l0 = clock();
		int lo = 81, hi = 0, sum = 0;
		for (unsigned seed = 1; seed <= 50; seed++) {
			unsigned char p[81], sol[81], check[81];
			int clues = su_generate(seed * 7919u + (unsigned) lv, targets[lv], p, sol);
			int n = 0;
			for (int i = 0; i < 81; i++) {
				if (p[i]) n++;
				if (p[i] && p[i] != sol[i]) T_FAIL_("seed %u: clue %d differs from the solution", seed, i);
			}
			T_EQ_INT(n, clues);
			T_ASSERT(valid_full(sol));
			T_EQ_INT(su_count_solutions(p, 2), 1);
			T_EQ_INT(su_solve(p, check), 1);
			T_ASSERT(memcmp(check, sol, 81) == 0);
			T_ASSERT(clues >= targets[lv]);
			T_ASSERT(clues <= targets[lv] + 4);
			if (clues < lo) lo = clues;
			if (clues > hi) hi = clues;
			sum += clues;
		}
		double ms = (double) (clock() - l0) * 1000.0 / CLOCKS_PER_SEC;
		fprintf(stderr, "sudoku level %d: target %d, clues min %d avg %.1f max %d, %.2f ms/puzzle\n", lv, targets[lv], lo,
			sum / 50.0, hi, ms / 50);
	}
	double total = (double) (clock() - t0) / CLOCKS_PER_SEC;
	fprintf(stderr, "sudoku generation: 150 puzzles in %.2f s\n", total);
	T_ASSERT(total < 15.0); /* generous: ASan build; the target is well under 0.3 s per puzzle */
}

static void test_same_seed_same_puzzle(void)
{
	unsigned char a[81], b[81], sa[81], sb[81];
	su_generate(42, 32, a, sa);
	su_generate(42, 32, b, sb);
	T_ASSERT(memcmp(a, b, 81) == 0);
	su_generate(43, 32, b, sb);
	T_ASSERT(memcmp(a, b, 81) != 0);
}

/* a board from the known puzzle, so cells are predictable */
static void known_board(Sudoku *s)
{
	memset(s, 0, sizeof *s);
	parse(KNOWN, s->given);
	parse(KNOWN, s->value);
	parse(KNOWN_SOL, s->solution);
	s->level = 1;
}

static void test_set_and_errors(void)
{
	Sudoku s;
	known_board(&s);
	T_EQ_INT(su_set(&s, 0, 9), 0); /* given */
	T_EQ_INT(s.value[0], 5);
	T_EQ_INT(su_set(&s, 2, 4), 1); /* solution digit */
	T_EQ_INT(su_is_wrong(&s, 2), 0);
	T_EQ_INT(su_set(&s, 2, 4), 0); /* unchanged */
	T_EQ_INT(su_set(&s, 3, 1), 1); /* solution is 6 */
	T_EQ_INT(su_is_wrong(&s, 3), 1);
	T_EQ_INT(su_is_wrong(&s, 0), 0); /* givens are never wrong */
	T_EQ_INT(su_is_wrong(&s, 5), 0); /* empty */
	T_EQ_INT(su_set(&s, 3, 0), 1);
	T_EQ_INT(su_is_wrong(&s, 3), 0);
	T_EQ_INT(su_set(&s, 3, 10), 0);
	T_EQ_INT(su_set(&s, 81, 1), 0);
	T_EQ_INT(su_set(&s, -1, 1), 0);
}

static void test_notes(void)
{
	Sudoku s;
	known_board(&s);
	T_EQ_INT(su_toggle_note(&s, 2, 1), 1);
	T_EQ_INT(su_toggle_note(&s, 2, 9), 1);
	T_EQ_INT(s.notes[2], 0x101);
	T_EQ_INT(su_toggle_note(&s, 2, 1), 1);
	T_EQ_INT(s.notes[2], 0x100);
	T_EQ_INT(su_toggle_note(&s, 0, 1), 0); /* given */
	T_EQ_INT(su_toggle_note(&s, 2, 0), 0);
	su_set(&s, 2, 4); /* a digit clears the notes */
	T_EQ_INT(s.notes[2], 0);
	T_EQ_INT(su_toggle_note(&s, 2, 3), 0); /* no notes on a filled cell */
}

static void test_undo_chain(void)
{
	Sudoku s;
	known_board(&s);
	T_EQ_INT(su_undo(&s), -1);
	su_toggle_note(&s, 2, 3);
	su_toggle_note(&s, 2, 7);
	su_set(&s, 2, 1);
	su_set(&s, 2, 4);
	su_set(&s, 3, 6);
	T_EQ_INT(su_undo(&s), 3);
	T_EQ_INT(s.value[3], 0);
	T_EQ_INT(su_undo(&s), 2);
	T_EQ_INT(s.value[2], 1);
	T_EQ_INT(su_undo(&s), 2);
	T_EQ_INT(s.value[2], 0);
	T_EQ_INT(s.notes[2], (1 << 2) | (1 << 6)); /* notes come back with the erased digit */
	T_EQ_INT(su_undo(&s), 2);
	T_EQ_INT(s.notes[2], 1 << 2);
	T_EQ_INT(su_undo(&s), 2);
	T_EQ_INT(s.notes[2], 0);
	T_EQ_INT(su_undo(&s), -1);

	/* only the last SU_UNDO_MAX steps are kept */
	for (int i = 0; i < SU_UNDO_MAX + 10; i++) su_toggle_note(&s, 2, 1);
	int n = 0;
	while (su_undo(&s) >= 0) n++;
	T_EQ_INT(n, SU_UNDO_MAX);
	T_EQ_INT(s.notes[2], 0); /* 74 toggles, 64 undone: back to the state after 10 toggles */

	/* hints are undoable, and do nothing on a correct cell */
	known_board(&s);
	T_EQ_INT(su_hint(&s, 2), 1);
	T_EQ_INT(s.value[2], 4);
	T_EQ_INT(su_hint(&s, 2), 0);
	T_EQ_INT(su_hint(&s, 0), 0);
	T_EQ_INT(su_undo(&s), 2);
	T_EQ_INT(s.value[2], 0);
}

static void test_win(void)
{
	Sudoku s;
	known_board(&s);
	T_EQ_INT(su_is_won(&s), 0);
	int last = -1;
	for (int i = 0; i < 81; i++)
		if (!s.given[i]) last = i;
	for (int i = 0; i < 81; i++)
		if (!s.given[i] && i != last) su_set(&s, i, s.solution[i]);
	T_EQ_INT(su_is_won(&s), 0);
	su_set(&s, last, s.solution[last] % 9 + 1);
	T_EQ_INT(su_is_won(&s), 0); /* full but wrong */
	su_set(&s, last, s.solution[last]);
	T_EQ_INT(su_is_won(&s), 1);
}

static void test_new_game(void)
{
	Sudoku s;
	su_new(&s, SU_HARD, 99);
	T_EQ_INT(s.level, SU_HARD);
	T_EQ_INT(s.undo_n, 0);
	int clues = 0;
	for (int i = 0; i < 81; i++) {
		clues += s.given[i] != 0;
		T_EQ_INT(s.value[i], s.given[i]);
		T_EQ_INT(s.notes[i], 0);
	}
	T_ASSERT(clues >= 26 && clues <= 30);
	T_EQ_INT(su_count_solutions(s.given, 2), 1);
}

static void test_save_roundtrip(void)
{
	Sudoku s, t;
	su_new(&s, SU_MEDIUM, 5);
	int e = -1;
	for (int i = 0; i < 81 && e < 0; i++)
		if (!s.given[i]) e = i;
	su_set(&s, e, s.solution[e] % 9 + 1);
	int e2 = e + 1;
	while (s.given[e2]) e2++;
	su_toggle_note(&s, e2, 2);
	su_toggle_note(&s, e2, 8);
	char buf[SU_SAVE_CAP];
	int n = su_save(&s, buf, sizeof buf);
	T_ASSERT(n > 0);
	T_EQ_INT((int) strlen(buf), n);
	T_EQ_INT(su_load(&t, buf), 0);
	T_ASSERT(memcmp(s.solution, t.solution, 81) == 0);
	T_ASSERT(memcmp(s.given, t.given, 81) == 0);
	T_ASSERT(memcmp(s.value, t.value, 81) == 0);
	T_ASSERT(memcmp(s.notes, t.notes, sizeof s.notes) == 0);
	T_EQ_INT(t.level, SU_MEDIUM);
	T_EQ_INT(su_is_wrong(&t, e), 1);
	T_EQ_INT(su_undo(&t), e2); /* the undo stack survives */
	T_EQ_INT(su_undo(&t), e2);
	T_EQ_INT(su_undo(&t), e);
	T_EQ_INT(su_undo(&t), -1);
	T_EQ_INT(su_save(&s, buf, 20), -1);

	/* a full undo ring round-trips too */
	for (int i = 0; i < SU_UNDO_MAX + 5; i++) su_toggle_note(&s, e2, 1 + i % 9);
	n = su_save(&s, buf, sizeof buf);
	T_ASSERT(n > 0);
	T_EQ_INT(su_load(&t, buf), 0);
	int k = 0;
	while (su_undo(&t) >= 0) k++;
	T_EQ_INT(k, SU_UNDO_MAX);
}

static void test_load_rejects_garbage(void)
{
	Sudoku s, t;
	su_new(&s, SU_EASY, 11);
	char good[SU_SAVE_CAP], bad[SU_SAVE_CAP];
	T_ASSERT(su_save(&s, good, sizeof good) > 0);
	memcpy(&t, &s, sizeof t);

	T_EQ_INT(su_load(&t, ""), -1);
	T_EQ_INT(su_load(&t, "hello"), -1);
	T_EQ_INT(su_load(&t, "sudoku v2\n"), -1);
	T_EQ_INT(su_load(&t, "sudoku v1\nlevel 0\n"), -1); /* truncated */

	/* truncate the good file anywhere: always rejected */
	for (int cut = 0; cut < (int) strlen(good) - 1; cut += 7) {
		memcpy(bad, good, (size_t) cut);
		bad[cut] = '\0';
		if (su_load(&t, bad) == 0) T_FAIL_("truncated at %d accepted", cut);
	}

	/* a clue that disagrees with the solution */
	int g = 0;
	while (!s.given[g]) g++;
	strcpy(bad, good);
	char *p = strstr(bad, "given ");
	T_ASSERT(p != NULL);
	if (p) {
		p[6 + g] = (char) ('0' + s.given[g] % 9 + 1);
		T_EQ_INT(su_load(&t, bad), -1);
	}
	/* a solution that is not a valid grid */
	strcpy(bad, good);
	p = strstr(bad, "solution ");
	if (p) {
		p[9] = p[10]; /* row 0 now repeats a digit */
		T_EQ_INT(su_load(&t, bad), -1);
	}
	/* a non-digit in the values */
	strcpy(bad, good);
	p = strstr(bad, "value ");
	if (p) {
		p[6 + 40] = 'x';
		T_EQ_INT(su_load(&t, bad), -1);
	}
	/* t was left untouched by every failure */
	T_ASSERT(memcmp(&t, &s, sizeof t) == 0);
	T_EQ_INT(su_load(&t, good), 0);
}

int main(void)
{
	T_RUN(test_solve_known);
	T_RUN(test_count_solutions);
	T_RUN(test_generator);
	T_RUN(test_same_seed_same_puzzle);
	T_RUN(test_set_and_errors);
	T_RUN(test_notes);
	T_RUN(test_undo_chain);
	T_RUN(test_win);
	T_RUN(test_new_game);
	T_RUN(test_save_roundtrip);
	T_RUN(test_load_rejects_garbage);
	return T_DONE();
}

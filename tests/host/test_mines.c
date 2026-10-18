#include "mines/mines.h"
#include "t.h"

/* Blank board of the given level with mines laid from a picture: '*' = mine, anything else = empty. */
static void layout(Mines *g, int level, const char **rows)
{
	ms_new(g, level, 1);
	for (int r = 0; r < g->h; r++)
		for (int c = 0; c < g->w; c++) g->mine[r][c] = rows && rows[r] && rows[r][c] == '*';
	int n = 0;
	for (int r = 0; r < g->h; r++)
		for (int c = 0; c < g->w; c++) n += g->mine[r][c];
	g->mines = n;
	g->placed = 1;
}

static int count_mines(const Mines *g)
{
	int n = 0;
	for (int r = 0; r < g->h; r++)
		for (int c = 0; c < g->w; c++) n += g->mine[r][c];
	return n;
}

static int count_open(const Mines *g)
{
	int n = 0;
	for (int r = 0; r < g->h; r++)
		for (int c = 0; c < g->w; c++) n += g->cell[r][c] == MS_OPEN;
	return n;
}

static void test_levels(void)
{
	int w, h, m;
	T_EQ_INT(ms_level_info(0, &w, &h, &m), 0); T_EQ_INT(w, 9); T_EQ_INT(h, 9); T_EQ_INT(m, 10);
	T_EQ_INT(ms_level_info(1, &w, &h, &m), 0); T_EQ_INT(w, 12); T_EQ_INT(h, 16); T_EQ_INT(m, 30);
	T_EQ_INT(ms_level_info(2, &w, &h, &m), 0); T_EQ_INT(w, 14); T_EQ_INT(h, 20); T_EQ_INT(m, 50);
	T_EQ_INT(ms_level_info(3, &w, &h, &m), -1);
	T_EQ_STR(ms_level_name(0), "Kolay");
	T_EQ_STR(ms_level_name(2), "Zor");
}

static void test_new_game_has_no_mines_until_first_open(void)
{
	Mines g;
	ms_new(&g, 1, 42);
	T_EQ_INT(g.placed, 0);
	T_EQ_INT(count_mines(&g), 0);
	T_EQ_INT(g.status, MS_PLAYING);
	T_EQ_INT(ms_mines_left(&g), 30);
	T_EQ_INT(ms_view(&g, 0, 0), MS_V_CLOSED);
}

static void test_first_tap_and_neighbours_safe_1000_seeds(void)
{
	for (unsigned seed = 1; seed <= 1000; seed++) {
		int level = (int) (seed % 3);
		Mines g;
		ms_new(&g, level, seed);
		/* corners, edges and middle */
		int r = (int) (seed * 7 % (unsigned) g.h), c = (int) (seed * 13 % (unsigned) g.w);
		ms_open(&g, r, c);
		if (!g.placed) { T_FAIL_("seed %u: mines not placed", seed); return; }
		if (count_mines(&g) != g.mines) { T_FAIL_("seed %u: %d mines, want %d", seed, count_mines(&g), g.mines); return; }
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = r + dr, cc = c + dc;
				if (rr < 0 || cc < 0 || rr >= g.h || cc >= g.w) continue;
				if (g.mine[rr][cc]) { T_FAIL_("seed %u: mine next to first tap", seed); return; }
			}
		if (g.status != MS_PLAYING && g.status != MS_WON) { T_FAIL_("seed %u: lost on first tap", seed); return; }
		if (ms_view(&g, r, c) != 0) { T_FAIL_("seed %u: first tap is not a 0", seed); return; }
	}
}

static void test_mine_counts_per_level(void)
{
	for (int level = 0; level < 3; level++) {
		Mines g;
		ms_new(&g, level, 99);
		ms_open(&g, 0, 0);
		int w, h, m;
		ms_level_info(level, &w, &h, &m);
		T_EQ_INT(count_mines(&g), m);
	}
}

static void test_seed_is_deterministic(void)
{
	Mines a, b;
	ms_new(&a, 2, 1234);
	ms_new(&b, 2, 1234);
	ms_open(&a, 5, 5);
	ms_open(&b, 5, 5);
	T_ASSERT(memcmp(a.mine, b.mine, sizeof a.mine) == 0);
}

static void test_neighbour_numbers(void)
{
	const char *rows[9] = { "*........", "**.......", ".........", "....*....", ".........", ".........", ".........", ".........", "........*" };
	Mines g;
	layout(&g, 0, rows);
	T_EQ_INT(ms_neighbours(&g, 0, 1), 3);
	T_EQ_INT(ms_neighbours(&g, 2, 0), 2);
	T_EQ_INT(ms_neighbours(&g, 4, 4), 1);
	T_EQ_INT(ms_neighbours(&g, 7, 7), 1);
	T_EQ_INT(ms_neighbours(&g, 5, 0), 0);
}

static void test_flood_fill_known_layout(void)
{
	/* a wall of mines in column 4 rows 0-8 except a gap is absent: the left side floods, the right does not */
	const char *rows[9] = { "....*....", "....*....", "....*....", "....*....", "....*....",
				"....*....", "....*....", "....*....", "....*...." };
	Mines g;
	layout(&g, 0, rows);
	T_EQ_INT(ms_open(&g, 0, 0), 1);
	/* columns 0-2 are zeros, column 3 is numbers: 4 x 9 cells open */
	T_EQ_INT(count_open(&g), 36);
	T_EQ_INT(ms_view(&g, 0, 0), 0);
	T_EQ_INT(ms_view(&g, 0, 3), 2);
	T_EQ_INT(ms_view(&g, 4, 3), 3);
	T_EQ_INT(ms_view(&g, 4, 5), MS_V_CLOSED);
	T_EQ_INT(g.status, MS_PLAYING);
	T_EQ_INT(g.opened, 36);
}

static void test_flood_stops_at_flags(void)
{
	Mines g;
	layout(&g, 0, NULL); /* no mines at all */
	ms_toggle_flag(&g, 4, 4);
	ms_open(&g, 0, 0);
	T_EQ_INT(ms_view(&g, 4, 4), MS_V_FLAG);
	T_EQ_INT(count_open(&g), 80);
	/* the flagged cell is not a mine, so the game is not won until it is unflagged and opened */
	T_EQ_INT(g.status, MS_PLAYING);
	T_EQ_INT(ms_open(&g, 4, 4), 0); /* flags ignore taps */
	ms_toggle_flag(&g, 4, 4);
	ms_open(&g, 4, 4);
	T_EQ_INT(g.status, MS_WON);
}

static void test_big_empty_flood_no_overflow(void)
{
	Mines g;
	layout(&g, 2, NULL); /* 14 x 20, no mines */
	ms_open(&g, 10, 7);
	T_EQ_INT(count_open(&g), 14 * 20);
	T_EQ_INT(g.status, MS_WON);
}

static void test_chord_opens_neighbours(void)
{
	const char *rows[9] = { "*........", ".........", ".........", ".........", ".........",
				".........", ".........", ".........", "........*" };
	Mines g;
	layout(&g, 0, rows);
	ms_open(&g, 1, 1); /* a 1 */
	T_EQ_INT(ms_view(&g, 1, 1), 1);
	T_EQ_INT(count_open(&g), 1);
	/* no flag yet: chord does nothing */
	T_EQ_INT(ms_open(&g, 1, 1), 0);
	ms_toggle_flag(&g, 0, 0);
	T_EQ_INT(ms_open(&g, 1, 1), 1);
	T_EQ_INT(ms_view(&g, 0, 1), 1);
	T_EQ_INT(ms_view(&g, 2, 2), 0); /* a zero → flood reaches every safe cell */
	T_EQ_INT(count_open(&g), 79);
	T_EQ_INT(g.status, MS_WON);
}

static void test_chord_wrong_flag_hits_mine(void)
{
	const char *rows[9] = { "*........", ".........", ".........", ".........", ".........",
				".........", ".........", ".........", "........*" };
	Mines g;
	layout(&g, 0, rows);
	ms_open(&g, 1, 1);
	ms_toggle_flag(&g, 0, 1); /* wrong flag */
	T_EQ_INT(ms_open(&g, 1, 1), 1);
	T_EQ_INT(g.status, MS_LOST);
	T_EQ_INT(g.boom_r, 0);
	T_EQ_INT(g.boom_c, 0);
	T_EQ_INT(ms_view(&g, 0, 0), MS_V_BOOM);
	T_EQ_INT(ms_view(&g, 0, 1), MS_V_WRONG_FLAG);
	T_EQ_INT(ms_view(&g, 8, 8), MS_V_MINE); /* loss reveals every mine */
}

static void test_loss_reveals_all_mines_and_freezes(void)
{
	const char *rows[9] = { "*.......*", ".........", ".........", ".........", "....*....",
				".........", ".........", ".........", "*.......*" };
	Mines g;
	layout(&g, 0, rows);
	ms_toggle_flag(&g, 8, 8);
	T_EQ_INT(ms_open(&g, 4, 4), 1);
	T_EQ_INT(g.status, MS_LOST);
	T_EQ_INT(ms_view(&g, 4, 4), MS_V_BOOM);
	T_EQ_INT(ms_view(&g, 0, 0), MS_V_MINE);
	T_EQ_INT(ms_view(&g, 0, 8), MS_V_MINE);
	T_EQ_INT(ms_view(&g, 8, 8), MS_V_FLAG); /* correct flag stays */
	T_EQ_INT(ms_view(&g, 2, 2), MS_V_CLOSED);
	T_EQ_INT(ms_open(&g, 2, 2), 0);
	T_EQ_INT(ms_toggle_flag(&g, 2, 2), 0);
}

static void test_win_when_all_safe_cells_open(void)
{
	const char *rows[9] = { "*........", ".........", ".........", ".........", ".........",
				".........", ".........", ".........", "........." };
	Mines g;
	layout(&g, 0, rows);
	ms_open(&g, 8, 8); /* floods everything except the mine */
	T_EQ_INT(g.status, MS_WON);
	T_EQ_INT(ms_view(&g, 0, 0), MS_V_FLAG); /* mines get flagged on a win */
	T_EQ_INT(ms_mines_left(&g), 0);
}

static void test_flag_counter(void)
{
	Mines g;
	ms_new(&g, 0, 5);
	T_EQ_INT(ms_mines_left(&g), 10);
	T_EQ_INT(ms_toggle_flag(&g, 0, 0), 1);
	T_EQ_INT(ms_toggle_flag(&g, 0, 1), 1);
	T_EQ_INT(ms_mines_left(&g), 8);
	T_EQ_INT(ms_toggle_flag(&g, 0, 0), 1);
	T_EQ_INT(ms_mines_left(&g), 9);
	T_EQ_INT(g.flags, 1);
	/* can go negative with too many flags */
	for (int c = 0; c < 9; c++) ms_toggle_flag(&g, 8, c);
	for (int c = 0; c < 3; c++) ms_toggle_flag(&g, 7, c);
	T_EQ_INT(ms_mines_left(&g), -3);
	/* an opened cell cannot be flagged */
	Mines h;
	layout(&h, 0, NULL);
	h.mine[8][8] = 1;
	h.mines = 1;
	ms_open(&h, 0, 0);
	T_EQ_INT(ms_toggle_flag(&h, 0, 0), 0);
	T_EQ_INT(ms_view(&h, 0, 0), 0);
	/* out of range */
	T_EQ_INT(ms_toggle_flag(&h, -1, 0), 0);
	T_EQ_INT(ms_open(&h, 0, 9), 0);
}

static void test_time(void)
{
	Mines g;
	ms_new(&g, 0, 3);
	ms_tick(&g, 100);
	ms_tick(&g, 150); /* not placed yet: no time */
	T_EQ_INT(ms_seconds(&g), 0);
	ms_open(&g, 4, 4);
	ms_tick(&g, 200);
	ms_tick(&g, 230);
	T_EQ_INT(ms_seconds(&g), 30);
	ms_tick(&g, 235);
	T_EQ_INT(ms_seconds(&g), 35);
	ms_tick(&g, 200); /* clock went backwards: ignored */
	T_EQ_INT(ms_seconds(&g), 35);
}

static void test_save_round_trip(void)
{
	Mines g, h;
	ms_new(&g, 1, 77);
	ms_open(&g, 8, 6);
	ms_tick(&g, 1000);
	ms_tick(&g, 1042);
	/* flag a closed cell */
	for (int r = 0, done = 0; r < g.h && !done; r++)
		for (int c = 0; c < g.w && !done; c++)
			if (g.cell[r][c] == MS_CLOSED) done = ms_toggle_flag(&g, r, c);
	char buf[1024];
	int n = ms_save(&g, buf, sizeof buf);
	T_ASSERT(n > 0);
	T_ASSERT(strncmp(buf, "mines v1\n", 9) == 0);
	memset(&h, 0xAB, sizeof h);
	T_EQ_INT(ms_load(&h, buf), 0);
	T_EQ_INT(h.level, 1); T_EQ_INT(h.w, 12); T_EQ_INT(h.h, 16); T_EQ_INT(h.mines, 30);
	T_ASSERT(memcmp(g.mine, h.mine, sizeof g.mine) == 0);
	T_ASSERT(memcmp(g.cell, h.cell, sizeof g.cell) == 0);
	T_EQ_INT(h.flags, g.flags);
	T_EQ_INT(h.opened, g.opened);
	T_EQ_INT(h.status, MS_PLAYING);
	T_EQ_INT(ms_seconds(&h), 42);
	T_EQ_INT(h.t_start, -1);
	T_EQ_INT(h.placed, 1);
	/* unplaced game round-trips too */
	ms_new(&g, 2, 5);
	n = ms_save(&g, buf, sizeof buf);
	T_ASSERT(n > 0);
	T_EQ_INT(ms_load(&h, buf), 0);
	T_EQ_INT(h.placed, 0);
	T_EQ_INT(h.w, 14);
	/* too small a buffer */
	T_EQ_INT(ms_save(&g, buf, 20), -1);
}

static void test_load_rejects_garbage(void)
{
	Mines g, keep;
	ms_new(&g, 0, 9);
	ms_open(&g, 4, 4);
	keep = g;
	char buf[1024];
	if (ms_save(&g, buf, sizeof buf) <= 0) { T_FAIL_("%s", "save failed"); return; }
	const char *bad[] = {
		"",
		"garbage",
		"mines v2\n0 9 9 10\n",
		"mines v1\n7 9 9 10\n",
		"mines v1\n0 9 9 10\n0 1 0 5\n",            /* rows missing */
		"mines v1\n0 8 9 10\n0 1 0 5\n",            /* size does not match level */
	};
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		if (ms_load(&g, bad[i]) != -1) T_FAIL_("accepted bad input %u", i);
	}
	T_ASSERT(memcmp(&g, &keep, sizeof g) == 0);
	/* corrupt a cell character */
	char b2[1024];
	memcpy(b2, buf, sizeof b2);
	char *rowp = strchr(strchr(strchr(b2, '\n') + 1, '\n') + 1, '\n') + 1;
	rowp[0] = '?';
	T_EQ_INT(ms_load(&g, b2), -1);
	/* wrong mine count: turn a mine into an empty closed cell */
	memcpy(b2, buf, sizeof b2);
	char *star = strchr(b2 + 9, '*');
	T_ASSERT(star != NULL);
	if (star) *star = '.';
	T_EQ_INT(ms_load(&g, b2), -1);
	/* truncated row */
	memcpy(b2, buf, sizeof b2);
	b2[strlen(b2) - 3] = '\0';
	T_EQ_INT(ms_load(&g, b2), -1);
	T_ASSERT(memcmp(&g, &keep, sizeof g) == 0);
	/* the original still loads */
	T_EQ_INT(ms_load(&g, buf), 0);
}

int main(void)
{
	T_RUN(test_levels);
	T_RUN(test_new_game_has_no_mines_until_first_open);
	T_RUN(test_first_tap_and_neighbours_safe_1000_seeds);
	T_RUN(test_mine_counts_per_level);
	T_RUN(test_seed_is_deterministic);
	T_RUN(test_neighbour_numbers);
	T_RUN(test_flood_fill_known_layout);
	T_RUN(test_flood_stops_at_flags);
	T_RUN(test_big_empty_flood_no_overflow);
	T_RUN(test_chord_opens_neighbours);
	T_RUN(test_chord_wrong_flag_hits_mine);
	T_RUN(test_loss_reveals_all_mines_and_freezes);
	T_RUN(test_win_when_all_safe_cells_open);
	T_RUN(test_flag_counter);
	T_RUN(test_time);
	T_RUN(test_save_round_trip);
	T_RUN(test_load_rejects_garbage);
	return T_DONE();
}

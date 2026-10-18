#include "game2048/g2048.h"
#include "t.h"

static void set_row(G2048 *g, int r, int a, int b, int c, int d)
{
	g->cell[r][0] = a; g->cell[r][1] = b; g->cell[r][2] = c; g->cell[r][3] = d;
}

static void blank(G2048 *g)
{
	memset(g, 0, sizeof *g);
	g->rng = 12345;
}

static int count_tiles(const G2048 *g)
{
	int n = 0;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) n += g->cell[r][c] != 0;
	return n;
}

static void test_new_game_has_two_tiles(void)
{
	G2048 g;
	g.best = 500;
	g2048_new(&g, 7);
	T_EQ_INT(count_tiles(&g), 2);
	T_EQ_INT(g.score, 0);
	T_EQ_INT(g.best, 500);
	T_EQ_INT(g.can_undo, 0);
}

static void test_merge_once_per_move(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 2, 2, 2);
	T_EQ_INT(g2048_move(&g, KUI_DIR_LEFT), 1);
	T_EQ_INT(g.cell[0][0], 4); T_EQ_INT(g.cell[0][1], 4);
	T_EQ_INT(g.cell[0][2], 0); T_EQ_INT(g.cell[0][3], 0);
	T_EQ_INT(g.score, 8);
	T_EQ_INT(count_tiles(&g), 3); /* two merged tiles + one spawned */
}

static void test_merge_order_left(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 2, 4, 0);
	g2048_move(&g, KUI_DIR_LEFT);
	T_EQ_INT(g.cell[0][0], 4); T_EQ_INT(g.cell[0][1], 4); T_EQ_INT(g.cell[0][2], 0);
	blank(&g);
	set_row(&g, 0, 4, 4, 8, 0); /* merged 8 must not merge again with 8 */
	g2048_move(&g, KUI_DIR_LEFT);
	T_EQ_INT(g.cell[0][0], 8); T_EQ_INT(g.cell[0][1], 8);
}

static void test_no_move_returns_0_and_no_spawn(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 4, 0, 0);
	T_EQ_INT(g2048_move(&g, KUI_DIR_LEFT), 0);
	T_EQ_INT(count_tiles(&g), 2);
	T_EQ_INT(g.score, 0);
}

static void test_move_right_up_down(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 2, 0, 4);
	g2048_move(&g, KUI_DIR_RIGHT);
	T_EQ_INT(g.cell[0][3], 4); T_EQ_INT(g.cell[0][2], 4); T_EQ_INT(g.cell[0][1], 0);

	blank(&g);
	g.cell[1][2] = 2; g.cell[3][2] = 2;
	g2048_move(&g, KUI_DIR_UP);
	T_EQ_INT(g.cell[0][2], 4); T_EQ_INT(g.cell[3][2] == 2 && g.cell[1][2] == 2, 0);

	blank(&g);
	g.cell[0][1] = 8; g.cell[1][1] = 8; g.cell[2][1] = 2;
	g2048_move(&g, KUI_DIR_DOWN);
	T_EQ_INT(g.cell[3][1], 2); T_EQ_INT(g.cell[2][1], 16);
}

static void test_undo_restores_board_and_score(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 2, 0, 0);
	G2048 before = g;
	g2048_move(&g, KUI_DIR_LEFT);
	T_EQ_INT(g2048_undo(&g), 1);
	T_ASSERT(memcmp(g.cell, before.cell, sizeof g.cell) == 0);
	T_EQ_INT(g.score, 0);
}

static void test_undo_only_once(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 2, 0, 0);
	g2048_move(&g, KUI_DIR_LEFT);
	T_EQ_INT(g2048_undo(&g), 1);
	T_EQ_INT(g2048_undo(&g), 0);
}

static void test_game_over_detection(void)
{
	G2048 g;
	blank(&g);
	set_row(&g, 0, 2, 4, 2, 4);
	set_row(&g, 1, 4, 2, 4, 2);
	set_row(&g, 2, 2, 4, 2, 4);
	set_row(&g, 3, 4, 2, 4, 2);
	T_EQ_INT(g2048_can_move(&g), 0);
	g.cell[3][3] = 4; /* now equal neighbours vertically */
	T_EQ_INT(g2048_can_move(&g), 1);
	g.cell[3][3] = 0;
	T_EQ_INT(g2048_can_move(&g), 1);
}

static void test_best_score_updates(void)
{
	G2048 g;
	blank(&g);
	g.best = 4;
	set_row(&g, 0, 4, 4, 0, 0);
	g2048_move(&g, KUI_DIR_LEFT);
	T_EQ_INT(g.score, 8);
	T_EQ_INT(g.best, 8);
	T_EQ_INT(g2048_max_tile(&g), 8);
}

static void test_save_load_roundtrip(void)
{
	G2048 g, h;
	g2048_new(&g, 99);
	g2048_move(&g, KUI_DIR_LEFT);
	g2048_move(&g, KUI_DIR_UP);
	g.best = 1234;
	char buf[512];
	T_ASSERT(g2048_save(&g, buf, sizeof buf) > 0);
	memset(&h, 0, sizeof h);
	T_EQ_INT(g2048_load(&h, buf), 0);
	T_ASSERT(memcmp(h.cell, g.cell, sizeof g.cell) == 0);
	T_EQ_INT(h.score, g.score);
	T_EQ_INT(h.best, 1234);
	T_EQ_INT(h.rng, g.rng);
}

static void test_2048_load_corrupt(void)
{
	G2048 g;
	g2048_new(&g, 5);
	G2048 before = g;
	T_EQ_INT(g2048_load(&g, "v1\n2 2 x"), -1);
	T_EQ_INT(g2048_load(&g, ""), -1);
	T_EQ_INT(g2048_load(&g, "v9\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n0 0 1\n"), -1);
	T_EQ_INT(g2048_load(&g, "v1\n3 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n0 0 1\n"), -1); /* 3 is not a power of two */
	T_ASSERT(memcmp(&g, &before, sizeof g) == 0);
}

static void test_spawn_distribution(void)
{
	G2048 g;
	int fours = 0, total = 0;
	g.rng = 1;
	for (int i = 0; i < 10000; i++) {
		g2048_new(&g, (unsigned) i + 1);
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				if (g.cell[r][c]) { total++; fours += g.cell[r][c] == 4; }
	}
	T_EQ_INT(total, 20000);
	T_ASSERT(fours * 100 >= total * 8 && fours * 100 <= total * 12);
}

int main(void)
{
	T_RUN(test_new_game_has_two_tiles);
	T_RUN(test_merge_once_per_move);
	T_RUN(test_merge_order_left);
	T_RUN(test_no_move_returns_0_and_no_spawn);
	T_RUN(test_move_right_up_down);
	T_RUN(test_undo_restores_board_and_score);
	T_RUN(test_undo_only_once);
	T_RUN(test_game_over_detection);
	T_RUN(test_best_score_updates);
	T_RUN(test_save_load_roundtrip);
	T_RUN(test_2048_load_corrupt);
	T_RUN(test_spawn_distribution);
	return T_DONE();
}

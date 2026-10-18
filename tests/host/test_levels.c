#include "chess/levels.h"
#include "t.h"

static void test_levels_monotonic(void)
{
	for (int i = 1; i < LEVEL_COUNT; i++) T_ASSERT(level_get(i)->movetime_ms <= level_get(i + 1)->movetime_ms);
	for (int i = 2; i < LEVEL_COUNT; i++) T_ASSERT(level_get(i)->elo < level_get(i + 1)->elo || !level_get(i + 1)->limit_strength);
	T_EQ_INT(level_get(6)->limit_strength, 0);
	T_EQ_INT(level_get(6)->skill, 20);
	T_ASSERT(level_get(6)->movetime_ms <= 5000);
	T_EQ_INT(level_get(1)->skill, 0);
	T_EQ_STR(level_get(1)->name, "Çok Kolay");
	T_EQ_STR(level_get(6)->name, "Usta");
	for (int i = 2; i <= 5; i++) T_ASSERT(level_get(i)->elo >= 1350); /* engine minimum */
}

static void test_level_out_of_range_is_3(void)
{
	T_ASSERT(level_get(0) == level_get(3));
	T_ASSERT(level_get(7) == level_get(3));
}

int main(void)
{
	T_RUN(test_levels_monotonic);
	T_RUN(test_level_out_of_range_is_3);
	return T_DONE();
}

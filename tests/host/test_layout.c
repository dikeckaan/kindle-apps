#include "kui.h"
#include "t.h"

static void test_grid_cell(void)
{
	KuiRect area = { 0, 0, 400, 400 };
	KuiRect a = kui_grid_cell(area, 4, 4, 0, 0, 10);
	T_EQ_INT(a.x, 10); T_EQ_INT(a.y, 10); T_EQ_INT(a.w, 87); T_EQ_INT(a.h, 87);
	KuiRect b = kui_grid_cell(area, 4, 4, 3, 3, 10);
	T_EQ_INT(b.x, 301); T_EQ_INT(b.y, 301); T_EQ_INT(b.w, 87); T_EQ_INT(b.h, 87);
}

static void test_grid_cell_offset_area(void)
{
	KuiRect area = { 40, 200, 400, 200 };
	KuiRect c = kui_grid_cell(area, 2, 1, 1, 0, 0);
	T_EQ_INT(c.x, 240); T_EQ_INT(c.y, 200); T_EQ_INT(c.w, 200); T_EQ_INT(c.h, 200);
}

static void test_union_ignores_empty(void)
{
	KuiRect e = { 0, 0, 0, 0 }, r = { 5, 6, 7, 8 };
	KuiRect u = kui_rect_union(e, r);
	T_EQ_INT(u.x, 5); T_EQ_INT(u.y, 6); T_EQ_INT(u.w, 7); T_EQ_INT(u.h, 8);
	u = kui_rect_union(r, e);
	T_EQ_INT(u.w, 7);
	KuiRect s = { 20, 0, 10, 10 };
	u = kui_rect_union(r, s);
	T_EQ_INT(u.x, 5); T_EQ_INT(u.y, 0); T_EQ_INT(u.w, 25); T_EQ_INT(u.h, 14);
}

static void test_contains_edges(void)
{
	KuiRect r = { 10, 10, 20, 20 };
	T_ASSERT(kui_rect_contains(r, 10, 10));
	T_ASSERT(kui_rect_contains(r, 29, 29));
	T_ASSERT(!kui_rect_contains(r, 30, 15));
	T_ASSERT(!kui_rect_contains(r, 15, 30));
	T_ASSERT(!kui_rect_contains(r, 9, 15));
}

int main(void)
{
	T_RUN(test_grid_cell);
	T_RUN(test_grid_cell_offset_area);
	T_RUN(test_union_ignores_empty);
	T_RUN(test_contains_edges);
	return T_DONE();
}

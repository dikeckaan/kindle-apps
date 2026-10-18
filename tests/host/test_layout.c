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

static void test_topbar_hit(void)
{
	T_EQ_INT(kui_topbar_hit(1072, 1, 10, 10), KUI_BTN_EXIT);
	T_EQ_INT(kui_topbar_hit(1072, 1, 119, 119), KUI_BTN_EXIT);
	T_EQ_INT(kui_topbar_hit(1072, 1, 1060, 60), KUI_BTN_MENU);
	T_EQ_INT(kui_topbar_hit(1072, 0, 1060, 60), 0);
	T_EQ_INT(kui_topbar_hit(1072, 1, 500, 60), 0);
	T_EQ_INT(kui_topbar_hit(1072, 1, 10, 130), 0);
}

static void test_buttons_hit(void)
{
	KuiButton bs[2] = { { { 0, 0, 100, 100 }, "a", 7 }, { { 100, 0, 100, 100 }, "b", 9 } };
	T_EQ_INT(kui_buttons_hit(bs, 2, 50, 50), 7);
	T_EQ_INT(kui_buttons_hit(bs, 2, 150, 50), 9);
	T_EQ_INT(kui_buttons_hit(bs, 2, 250, 50), 0);
}

static void test_dirty_keeps_separate_rects(void)
{
	KuiDirty d = { .n = 0 };
	kui_dirty_add(&d, (KuiRect){ 0, 0, 10, 10 });
	kui_dirty_add(&d, (KuiRect){ 100, 100, 10, 10 });
	T_EQ_INT(d.n, 2);
}

static void test_dirty_merges_overlapping(void)
{
	KuiDirty d = { .n = 0 };
	kui_dirty_add(&d, (KuiRect){ 0, 0, 10, 10 });
	kui_dirty_add(&d, (KuiRect){ 5, 5, 10, 10 });
	T_EQ_INT(d.n, 1);
	T_EQ_INT(d.r[0].w, 15);
	kui_dirty_add(&d, (KuiRect){ 20, 0, 5, 5 });
	kui_dirty_add(&d, (KuiRect){ 0, 0, 30, 30 }); /* swallows both */
	T_EQ_INT(d.n, 1);
	T_EQ_INT(d.r[0].w, 30);
}

static void test_dirty_ignores_empty(void)
{
	KuiDirty d = { .n = 0 };
	kui_dirty_add(&d, (KuiRect){ 0, 0, 0, 10 });
	T_EQ_INT(d.n, 0);
}

static void test_dirty_overflow_merges(void)
{
	KuiDirty d = { .n = 0 };
	for (int i = 0; i < 40; i++) kui_dirty_add(&d, (KuiRect){ i * 50, 0, 10, 10 });
	T_ASSERT(d.n <= KUI_MAX_DIRTY);
	int x0 = 1 << 30, x1 = 0; /* every added rect stays covered */
	for (int i = 0; i < d.n; i++) {
		if (d.r[i].x < x0) x0 = d.r[i].x;
		if (d.r[i].x + d.r[i].w > x1) x1 = d.r[i].x + d.r[i].w;
	}
	T_EQ_INT(x0, 0);
	T_EQ_INT(x1, 39 * 50 + 10);
	for (int i = 0; i < 40; i++) {
		int covered = 0;
		for (int k = 0; k < d.n; k++) covered |= kui_rect_contains(d.r[k], i * 50, 0);
		T_ASSERT(covered);
	}
}

int main(void)
{
	T_RUN(test_grid_cell);
	T_RUN(test_grid_cell_offset_area);
	T_RUN(test_union_ignores_empty);
	T_RUN(test_contains_edges);
	T_RUN(test_topbar_hit);
	T_RUN(test_buttons_hit);
	T_RUN(test_dirty_keeps_separate_rects);
	T_RUN(test_dirty_merges_overlapping);
	T_RUN(test_dirty_ignores_empty);
	T_RUN(test_dirty_overflow_merges);
	return T_DONE();
}

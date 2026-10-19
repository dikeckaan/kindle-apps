#include "kui.h"
#include "t.h"

static KuiEvent evs[64];
static int nev;

static void feed(KuiDrag *d, int contact, int x, int y, long t)
{
	KuiEvent out[KUI_DRAG_MAX_OUT];
	int n = kui_drag_feed(d, contact, x, y, t, out);
	T_ASSERT(n <= KUI_DRAG_MAX_OUT);
	for (int i = 0; i < n && nev < 64; i++) evs[nev++] = out[i];
}

static void test_tap_gives_down_up_tap(void)
{
	KuiDrag d = { 0 };
	nev = 0;
	feed(&d, 7, 100, 200, 1000);
	feed(&d, 7, 102, 201, 1030);
	feed(&d, -1, 0, 0, 1080);
	T_EQ_INT(nev, 4);
	T_EQ_INT(evs[0].type, KUI_EV_DOWN);
	T_EQ_INT(evs[0].x, 100);
	T_EQ_INT(evs[0].y, 200);
	T_EQ_INT(evs[1].type, KUI_EV_MOVE);
	T_EQ_INT(evs[2].type, KUI_EV_UP);
	T_EQ_INT(evs[2].x, 102); /* the last position, not the release report's */
	T_EQ_INT(evs[2].y, 201);
	T_EQ_INT(evs[3].type, KUI_EV_TAP);
	T_EQ_INT(evs[3].x, 100);
}

static void test_drag_moves_and_swipe(void)
{
	KuiDrag d = { 0 };
	nev = 0;
	feed(&d, 1, 500, 500, 0);
	for (int i = 1; i <= 10; i++) feed(&d, 1, 500 - 30 * i, 500, i * 10);
	feed(&d, 1, 200, 500, 120); /* same position: no MOVE */
	feed(&d, -1, 0, 0, 130);
	T_EQ_INT(evs[0].type, KUI_EV_DOWN);
	int moves = 0;
	for (int i = 0; i < nev; i++) moves += evs[i].type == KUI_EV_MOVE;
	T_EQ_INT(moves, 10);
	T_EQ_INT(evs[nev - 2].type, KUI_EV_UP);
	T_EQ_INT(evs[nev - 2].x, 200);
	T_EQ_INT(evs[nev - 1].type, KUI_EV_SWIPE);
	T_EQ_INT(evs[nev - 1].dir, KUI_DIR_LEFT);
}

static void test_release_without_touch_is_silent(void)
{
	KuiDrag d = { 0 };
	nev = 0;
	feed(&d, -1, 0, 0, 0);
	T_EQ_INT(nev, 0);
}

static void test_new_contact_ends_old_touch(void)
{
	KuiDrag d = { 0 };
	nev = 0;
	feed(&d, 3, 100, 100, 0);
	feed(&d, 4, 600, 900, 50); /* slot 0 now tracks another finger */
	T_EQ_INT(nev, 4);
	T_EQ_INT(evs[1].type, KUI_EV_UP);
	T_EQ_INT(evs[1].x, 100);
	T_EQ_INT(evs[2].type, KUI_EV_TAP);
	T_EQ_INT(evs[3].type, KUI_EV_DOWN);
	T_EQ_INT(evs[3].x, 600);
	feed(&d, -1, 0, 0, 900);
	T_EQ_INT(evs[nev - 1].type, KUI_EV_LONGPRESS);
	T_EQ_INT(evs[nev - 1].x, 600);
}

static void test_ambiguous_drag_has_no_gesture(void)
{
	KuiDrag d = { 0 };
	nev = 0;
	feed(&d, 0, 100, 100, 0);
	feed(&d, 0, 160, 100, 40); /* between slop and swipe distance */
	feed(&d, -1, 0, 0, 80);
	T_EQ_INT(nev, 3);
	T_EQ_INT(evs[2].type, KUI_EV_UP);
}

int main(void)
{
	T_RUN(test_tap_gives_down_up_tap);
	T_RUN(test_drag_moves_and_swipe);
	T_RUN(test_release_without_touch_is_silent);
	T_RUN(test_new_contact_ends_old_touch);
	T_RUN(test_ambiguous_drag_has_no_gesture);
	return T_DONE();
}

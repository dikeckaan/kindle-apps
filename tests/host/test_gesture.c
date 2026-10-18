#include "kui.h"
#include "t.h"

static KuiEvent stroke(int x0, int y0, int x1, int y1, long dt)
{
	KuiGesture g = { 0 };
	kui_gesture_feed(&g, 1, x0, y0, 1000);
	kui_gesture_feed(&g, 1, x1, y1, 1000 + dt / 2);
	return kui_gesture_feed(&g, 0, x1, y1, 1000 + dt);
}

static void test_tap(void)
{
	KuiEvent e = stroke(100, 100, 105, 103, 120);
	T_EQ_INT(e.type, KUI_EV_TAP);
	T_EQ_INT(e.x, 100);
	T_EQ_INT(e.y, 100);
}

static void test_swipe_left(void)
{
	KuiEvent e = stroke(500, 500, 300, 510, 200);
	T_EQ_INT(e.type, KUI_EV_SWIPE);
	T_EQ_INT(e.dir, KUI_DIR_LEFT);
}

static void test_swipe_right_up_down(void)
{
	T_EQ_INT(stroke(100, 500, 300, 500, 200).dir, KUI_DIR_RIGHT);
	T_EQ_INT(stroke(500, 500, 500, 300, 200).dir, KUI_DIR_UP);
	T_EQ_INT(stroke(500, 300, 500, 500, 200).dir, KUI_DIR_DOWN);
}

static void test_swipe_dominant_axis(void)
{
	KuiEvent e = stroke(500, 500, 590, 620, 200);
	T_EQ_INT(e.type, KUI_EV_SWIPE);
	T_EQ_INT(e.dir, KUI_DIR_DOWN);
}

static void test_longpress(void)
{
	KuiEvent e = stroke(200, 200, 210, 200, 700);
	T_EQ_INT(e.type, KUI_EV_LONGPRESS);
	T_EQ_INT(e.x, 200);
}

static void test_move_under_threshold_slow_is_longpress_not_swipe(void)
{
	KuiEvent e = stroke(200, 200, 250, 200, 900); /* 50 px: beyond slop, below swipe */
	T_EQ_INT(e.type, KUI_EV_NONE);
	e = stroke(200, 200, 230, 200, 900);         /* 30 px: within slop */
	T_EQ_INT(e.type, KUI_EV_LONGPRESS);
}

static void test_no_event_while_dragging(void)
{
	KuiGesture g = { 0 };
	T_EQ_INT(kui_gesture_feed(&g, 1, 10, 10, 0).type, KUI_EV_NONE);
	T_EQ_INT(kui_gesture_feed(&g, 1, 200, 10, 50).type, KUI_EV_NONE);
	T_EQ_INT(kui_gesture_feed(&g, 1, 400, 10, 90).type, KUI_EV_NONE);
}

static void test_release_without_press_is_none(void)
{
	KuiGesture g = { 0 };
	T_EQ_INT(kui_gesture_feed(&g, 0, 10, 10, 0).type, KUI_EV_NONE);
}

static void test_double_release_emits_once(void)
{
	KuiGesture g = { 0 };
	kui_gesture_feed(&g, 1, 10, 10, 0);
	T_EQ_INT(kui_gesture_feed(&g, 0, 10, 10, 50).type, KUI_EV_TAP);
	T_EQ_INT(kui_gesture_feed(&g, 0, 10, 10, 60).type, KUI_EV_NONE);
}

int main(void)
{
	T_RUN(test_tap);
	T_RUN(test_swipe_left);
	T_RUN(test_swipe_right_up_down);
	T_RUN(test_swipe_dominant_axis);
	T_RUN(test_longpress);
	T_RUN(test_move_under_threshold_slow_is_longpress_not_swipe);
	T_RUN(test_no_event_while_dragging);
	T_RUN(test_release_without_press_is_none);
	T_RUN(test_double_release_emits_once);
	return T_DONE();
}

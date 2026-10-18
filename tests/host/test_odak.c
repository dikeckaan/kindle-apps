#include <stdlib.h>
#include "odak/odak.h"
#include "t.h"

#define T0 1760000000L /* 2025-10-09 08:53:20 UTC, a Thursday */
#define OFF (3 * 3600)

static void test_presets(void)
{
	OdakPlan p;
	odak_plan(ODAK_PRESET_POMODORO, 0, 0, &p);
	T_EQ_INT(p.focus_min, 25); T_EQ_INT(p.break_min, 5); T_EQ_INT(p.long_min, 15); T_EQ_INT(p.long_every, 4);
	odak_plan(ODAK_PRESET_LONG, 0, 0, &p);
	T_EQ_INT(p.focus_min, 50); T_EQ_INT(p.break_min, 10); T_EQ_INT(p.long_every, 0);
	odak_plan(ODAK_PRESET_CUSTOM, 40, 7, &p);
	T_EQ_INT(p.focus_min, 40); T_EQ_INT(p.break_min, 7); T_EQ_INT(p.long_every, 0);
	odak_plan(ODAK_PRESET_CUSTOM, 1, 99, &p); /* clamped */
	T_EQ_INT(p.focus_min, ODAK_CUSTOM_MIN); T_EQ_INT(p.break_min, ODAK_BREAK_MAX);
	odak_plan(ODAK_PRESET_CUSTOM, 999, -3, &p);
	T_EQ_INT(p.focus_min, ODAK_CUSTOM_MAX); T_EQ_INT(p.break_min, 0);
	odak_plan(42, 0, 0, &p); /* unknown → Pomodoro */
	T_EQ_INT(p.focus_min, 25);
	T_EQ_INT(odak_step(25, 5, 5, 180), 30);
	T_EQ_INT(odak_step(5, -5, 5, 180), 5);
	T_EQ_INT(odak_step(178, 5, 5, 180), 180);
}

static void test_start_and_countdown(void)
{
	OdakPlan p;
	OdakSession s;
	odak_plan(ODAK_PRESET_POMODORO, 0, 0, &p);
	odak_start(&s, &p, T0);
	T_EQ_INT(s.state, ODAK_RUNNING);
	T_EQ_INT(s.phase, ODAK_FOCUS);
	T_EQ_INT(s.end, T0 + 25 * 60);
	T_EQ_INT(odak_left(&s, T0), 1500);
	T_EQ_INT(odak_left_min(&s, T0), 25);
	T_EQ_INT(odak_left_min(&s, T0 + 1), 25);  /* 24:59 left still shows 25 */
	T_EQ_INT(odak_left_min(&s, T0 + 60), 24);
	T_EQ_INT(odak_left_min(&s, T0 + 1499), 1);
	T_EQ_INT(odak_left_min(&s, T0 + 1500), 0);
	T_EQ_INT(odak_left(&s, T0 + 9999), 0);
	T_EQ_INT(odak_progress(&s, T0, 1000), 0);
	T_EQ_INT(odak_progress(&s, T0 + 750, 1000), 500);
	T_EQ_INT(odak_progress(&s, T0 + 5000, 1000), 1000);
	T_EQ_INT(odak_due(&s, T0 + 1499), 0);
	T_EQ_INT(odak_due(&s, T0 + 1500), 1);
}

static void test_next_wake(void)
{
	OdakPlan p;
	OdakSession s;
	odak_plan(ODAK_PRESET_POMODORO, 0, 0, &p);
	odak_start(&s, &p, T0);
	T_EQ_INT(odak_next_wake(&s, T0), T0 + 60);       /* 25 → 24 */
	T_EQ_INT(odak_next_wake(&s, T0 + 59), T0 + 60);
	T_EQ_INT(odak_next_wake(&s, T0 + 60), T0 + 120); /* just shown 24: the next change */
	T_EQ_INT(odak_next_wake(&s, T0 + 1430), T0 + 1440);
	T_EQ_INT(odak_next_wake(&s, T0 + 1440), T0 + 1500); /* the last minute: wake at the end second */
	T_EQ_INT(odak_next_wake(&s, T0 + 1499), T0 + 1500);
	T_EQ_INT(odak_next_wake(&s, T0 + 1600), T0 + 1600); /* overdue: now */
	/* every wake changes the shown minutes */
	for (long t = T0; t < T0 + 1500;) {
		long w = odak_next_wake(&s, t);
		T_ASSERT(w > t);
		T_ASSERT(odak_left_min(&s, w) == odak_left_min(&s, t) - 1);
		t = w;
	}
	odak_pause(&s, T0 + 100);
	T_EQ_INT(odak_next_wake(&s, T0 + 200), 0);
}

static void test_pause_resume_extend(void)
{
	OdakPlan p;
	OdakSession s;
	odak_plan(ODAK_PRESET_POMODORO, 0, 0, &p);
	odak_start(&s, &p, T0);
	odak_pause(&s, T0 + 600);
	T_EQ_INT(s.state, ODAK_PAUSED);
	T_EQ_INT(odak_left(&s, T0 + 5000), 900); /* time stands still */
	T_EQ_INT(odak_due(&s, T0 + 5000), 0);
	odak_pause(&s, T0 + 6000); /* twice: no change */
	T_EQ_INT(odak_left(&s, T0 + 6000), 900);
	odak_resume(&s, T0 + 5000);
	T_EQ_INT(s.state, ODAK_RUNNING);
	T_EQ_INT(s.end, T0 + 5900);
	T_EQ_INT(odak_progress(&s, T0 + 5000, 100), 40);
	odak_extend(&s, T0 + 5000, 5);
	T_EQ_INT(s.end, T0 + 6200);
	T_EQ_INT(s.length, 1800);
	T_EQ_INT(odak_left_min(&s, T0 + 5000), 20);
	odak_pause(&s, T0 + 5100);
	odak_extend(&s, T0 + 5100, 5);
	T_EQ_INT(odak_left(&s, T0 + 5100), 1100 + 300);
	odak_resume(&s, T0 + 7000);
	T_EQ_INT(odak_due(&s, T0 + 8399), 0);
	T_EQ_INT(odak_due(&s, T0 + 8400), 1);
	T_EQ_INT(odak_complete(&s, T0 + 8400), 35); /* 25 + 5 + 5 */
	odak_extend(&s, T0 + 8400, 5); /* the waiting break */
	T_EQ_INT(s.length, 600);
}

static void test_phase_sequence_pomodoro(void)
{
	OdakPlan p;
	OdakSession s;
	odak_plan(ODAK_PRESET_POMODORO, 0, 0, &p);
	odak_start(&s, &p, T0);
	long t = T0;
	OdakPhase want[] = { ODAK_BREAK, ODAK_FOCUS, ODAK_BREAK, ODAK_FOCUS, ODAK_BREAK, ODAK_FOCUS, ODAK_LONG_BREAK, ODAK_FOCUS, ODAK_BREAK };
	int want_min[] = { 5, 25, 5, 25, 5, 25, 15, 25, 5 };
	for (int i = 0; i < 9; i++) {
		t = s.end;
		int was_focus = s.phase == ODAK_FOCUS;
		int logged = odak_complete(&s, t);
		T_EQ_INT(logged, was_focus ? 25 : 0);
		T_EQ_INT(s.state, ODAK_WAITING);
		T_EQ_INT(s.phase, want[i]);
		T_EQ_INT(odak_left(&s, t + 999), want_min[i] * 60);
		odak_begin_next(&s, t + 30);
		T_EQ_INT(s.state, ODAK_RUNNING);
		T_EQ_INT(s.end, t + 30 + want_min[i] * 60);
		T_EQ_INT(s.phase_start, t + 30);
	}
	T_EQ_INT(s.done_focus, 5);
	T_EQ_INT(odak_is_break(ODAK_LONG_BREAK), 1);
	T_EQ_INT(odak_is_break(ODAK_FOCUS), 0);
}

static void test_phase_sequence_long_and_custom(void)
{
	OdakPlan p;
	OdakSession s;
	odak_plan(ODAK_PRESET_LONG, 0, 0, &p);
	odak_start(&s, &p, T0);
	for (int i = 0; i < 8; i++) {
		odak_complete(&s, s.end);
		T_ASSERT(s.phase != ODAK_LONG_BREAK);
		odak_begin_next(&s, s.end);
	}
	odak_plan(ODAK_PRESET_CUSTOM, 45, 0, &p); /* no break: done after one focus phase */
	odak_start(&s, &p, T0);
	T_EQ_INT(odak_complete(&s, T0 + 2700), 45);
	T_EQ_INT(s.state, ODAK_FINISHED);
	odak_begin_next(&s, T0 + 2800); /* nothing to start */
	T_EQ_INT(s.state, ODAK_FINISHED);
	T_EQ_INT(odak_due(&s, T0 + 9000), 0);
}

static void test_stop(void)
{
	OdakPlan p;
	OdakSession s;
	odak_plan(ODAK_PRESET_POMODORO, 0, 0, &p);
	odak_start(&s, &p, T0);
	T_EQ_INT(odak_stop(&s, T0 + 59), 0);
	T_EQ_INT(s.state, ODAK_STOPPED);
	odak_start(&s, &p, T0);
	odak_pause(&s, T0 + 610);
	T_EQ_INT(odak_stop(&s, T0 + 3000), 10); /* paused time does not count */
	odak_start(&s, &p, T0);
	odak_complete(&s, T0 + 1500);
	odak_begin_next(&s, T0 + 1500);
	T_EQ_INT(odak_stop(&s, T0 + 1700), 0); /* a break is not focus */
}

static void test_log_round_trip(void)
{
	OdakEntry e = { T0, 25, "İş planı" }, out[4];
	char line[128];
	int n = odak_log_format(&e, line, sizeof line);
	T_EQ_STR(line, "1760000000 25 İş planı\n");
	T_EQ_INT(n, (int) strlen(line));
	T_EQ_INT(odak_log_format(&e, line, 5), 0);
	const char *buf = "1760000000 25 Okuma\ngarbage\n1760000100 -3 X\n1760003600 50 Ders\n\n1760007200 10\n";
	n = odak_log_parse(buf, out, 4);
	T_EQ_INT(n, 3);
	T_EQ_STR(out[0].label, "Okuma");
	T_EQ_INT(out[1].start, 1760003600L);
	T_EQ_INT(out[1].min, 50);
	T_EQ_STR(out[2].label, "");
	n = odak_log_parse(buf, out, 2); /* the newest */
	T_EQ_INT(n, 2);
	T_EQ_STR(out[0].label, "Ders");
	T_EQ_INT(out[1].min, 10);
	T_EQ_INT(odak_log_parse("", out, 4), 0);
}

static void test_log_append_bounded(void)
{
	char buf[32] = "";
	int len = 0;
	len = odak_log_append(buf, len, sizeof buf, "1 1 a\n");
	len = odak_log_append(buf, len, sizeof buf, "2 2 bb\n");
	T_EQ_STR(buf, "1 1 a\n2 2 bb\n");
	T_EQ_INT(len, 13);
	len = odak_log_append(buf, len, sizeof buf, "3 3 cccccccccccccc\n"); /* 19 bytes: the oldest line goes */
	T_EQ_STR(buf, "2 2 bb\n3 3 cccccccccccccc\n");
	T_EQ_INT(len, (int) strlen(buf));
	len = odak_log_append(buf, len, sizeof buf, "4 4 dddddddddddddddddddd\n"); /* 25: only it fits */
	T_EQ_STR(buf, "4 4 dddddddddddddddddddd\n");
	len = odak_log_append(buf, len, sizeof buf, "this line is far too long for the buffer\n");
	T_EQ_STR(buf, "4 4 dddddddddddddddddddd\n"); /* unchanged */
}

static void test_stats(void)
{
	/* now: Thursday 2025-10-09 12:00 local (UTC+3) */
	long now = T0 - (T0 + OFF) % 86400 + 12 * 3600;
	long d = 86400;
	OdakEntry e[] = {
		{ now - 10 * d, 30, "Okuma" }, /* outside the 7 days, breaks nothing (gap) */
		{ now - 4 * d, 25, "Ders" },   /* Sunday */
		{ now - 3 * d, 25, "Ders" },   /* Monday: this week */
		{ now - 2 * d, 25, "Okuma" },
		{ now - 1 * d, 50, "İş" },
		{ now - 1 * d + 600, 25, "Ders" },
		{ now - 3600, 25, "Okuma" },
		{ now - 12 * 3600 + 60, 10, "Yazı" }, /* 00:01 today */
		{ now - 12 * 3600 - 60, 5, "Yazı" },  /* 23:59 yesterday */
	};
	OdakStats st;
	odak_stats(e, 9, now, OFF, &st);
	T_EQ_INT(st.today_min, 35);
	T_EQ_INT(st.today_n, 2);
	T_EQ_INT(st.day_min[6], 35);
	T_EQ_INT(st.day_min[5], 80);
	T_EQ_INT(st.day_min[4], 25);
	T_EQ_INT(st.day_min[2], 25);
	T_EQ_INT(st.day_min[0], 0);
	T_EQ_INT(st.week_min, 25 + 25 + 80 + 35);
	T_EQ_INT(st.total_min, 220);
	T_EQ_INT(st.streak, 5); /* Sunday..today */
	T_EQ_INT(st.n_labels, 4);
	T_EQ_STR(st.label[0], "Okuma");
	T_EQ_INT(st.label_min[0], 80);
	T_EQ_STR(st.label[1], "Ders");
	T_EQ_INT(st.label_min[1], 75);
	T_EQ_STR(st.label[3], "Yazı");
	T_EQ_INT(st.label_min[3], 15);
	/* nothing today yet: the streak up to yesterday still counts */
	odak_stats(e, 6, now, OFF, &st);
	T_EQ_INT(st.streak, 4);
	T_EQ_INT(st.today_min, 0);
	/* a day without a session ends it */
	odak_stats(e, 5, now + 2 * d, OFF, &st);
	T_EQ_INT(st.streak, 0);
	odak_stats(e, 0, now, OFF, &st);
	T_EQ_INT(st.streak, 0);
	T_EQ_INT(st.n_labels, 0);
	T_EQ_INT(odak_day(0, 0), 0);
	T_EQ_INT(odak_day(86399, 0), 0);
	T_EQ_INT(odak_day(86399, 1), 1);
	T_EQ_INT(odak_day(-1, 0), -1);
}

static void test_stats_many_labels(void)
{
	OdakEntry e[12];
	for (int i = 0; i < 12; i++) {
		e[i].start = T0;
		e[i].min = 10 + i;
		snprintf(e[i].label, sizeof e[i].label, "L%d", i);
	}
	OdakStats st;
	odak_stats(e, 12, T0, 0, &st);
	T_EQ_INT(st.n_labels, ODAK_MAX_LABELS);
	T_EQ_STR(st.label[0], "L11");
	T_EQ_INT(st.label_min[ODAK_MAX_LABELS - 1], 14);
}

static void test_labels(void)
{
	char l[ODAK_MAX_LABELS][ODAK_LABEL_MAX], buf[512];
	int n = odak_labels_parse("", l, ODAK_MAX_LABELS);
	T_EQ_INT(n, 4);
	T_EQ_STR(l[0], "Okuma");
	T_EQ_STR(l[2], "İş");
	n = odak_labels_parse("  Kod \n\nKod\nMüzik\r\n", l, ODAK_MAX_LABELS);
	T_EQ_INT(n, 2);
	T_EQ_STR(l[0], "Kod");
	T_EQ_STR(l[1], "Müzik");
	n = odak_labels_add(l, n, ODAK_MAX_LABELS, " Spor ");
	T_EQ_INT(n, 3);
	T_EQ_STR(l[2], "Spor");
	T_EQ_INT(odak_labels_add(l, n, ODAK_MAX_LABELS, "Kod"), 3);
	T_EQ_INT(odak_labels_add(l, n, ODAK_MAX_LABELS, "   "), 3);
	T_EQ_INT(odak_labels_add(l, n, 3, "Yeni"), 3); /* full */
	odak_labels_format(l, n, buf, sizeof buf);
	T_EQ_STR(buf, "Kod\nMüzik\nSpor\n");
	n = odak_labels_remove(l, n, 0);
	T_EQ_INT(n, 2);
	T_EQ_STR(l[0], "Müzik");
	T_EQ_INT(odak_labels_remove(l, n, 7), 2);
	n = odak_labels_remove(l, n, 1);
	T_EQ_INT(odak_labels_remove(l, n, 0), 1); /* the last one stays */
	char c[ODAK_LABEL_MAX];
	odak_label_clean("ğğğğğğğğğğğğğğğğğğğğ", c); /* 40 bytes: cut at a code point */
	T_EQ_INT((int) strlen(c), 30);
	odak_label_clean(" a\nb ", c);
	T_EQ_STR(c, "a b");
}

int main(void)
{
	T_RUN(test_presets);
	T_RUN(test_start_and_countdown);
	T_RUN(test_next_wake);
	T_RUN(test_pause_resume_extend);
	T_RUN(test_phase_sequence_pomodoro);
	T_RUN(test_phase_sequence_long_and_custom);
	T_RUN(test_stop);
	T_RUN(test_log_round_trip);
	T_RUN(test_log_append_bounded);
	T_RUN(test_stats);
	T_RUN(test_stats_many_labels);
	T_RUN(test_labels);
	return T_DONE();
}

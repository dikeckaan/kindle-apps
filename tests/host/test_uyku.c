#include <string.h>
#include "t.h"
#include "uyku/uyku.h"

#define TR (3 * 3600)
/* 2026-10-19 14:10:00 in Türkiye (UTC+3) */
#define T0 (1792408200L)

static void test_interval(void)
{
	T_EQ_INT(uyku_interval(30), 30);
	T_EQ_INT(uyku_interval(120), 120);
	T_EQ_INT(uyku_interval(0), 60);
	T_EQ_INT(uyku_interval(45), 60);
}

static void test_next_wake(void)
{
	/* 14:10 local, every 60 min → 15:00:30 local */
	T_EQ_INT(uyku_next_wake(T0, TR, 60), T0 + 50 * 60 + 30);
	/* every 30 → 14:30:30 */
	T_EQ_INT(uyku_next_wake(T0, TR, 30), T0 + 20 * 60 + 30);
	/* every 120 → 16:00:30 local (even local hours, not UTC ones) */
	T_EQ_INT(uyku_next_wake(T0, TR, 120), T0 + 110 * 60 + 30);
	/* just after a boundary (14:59:40): not 20 s later but the one after */
	T_EQ_INT(uyku_next_wake(T0 + 49 * 60 + 40, TR, 60), T0 + 110 * 60 + 30);
	/* 23:10 local, every 120 → midnight (00:00:30), the date changes on time */
	long late = T0 + 9 * 3600;
	T_EQ_INT(uyku_next_wake(late, TR, 120), late + 50 * 60 + 30);
	/* the result is always ahead */
	for (long t = T0; t < T0 + 86400; t += 397) T_ASSERT(uyku_next_wake(t, TR, 30) - t >= 60);
	/* UTC device time (no zone): multiples of UTC */
	T_EQ_INT(uyku_next_wake(3600 * 10 + 5, 0, 60), 3600 * 11 + 30);
}

static void test_update_kind(void)
{
	T_EQ_INT(uyku_update_kind(1, 0, 5, 0, 0), UYKU_UPD_FULL);
	T_EQ_INT(uyku_update_kind(0, 5, 5, 0, 0), UYKU_UPD_FOOTER);
	T_EQ_INT(uyku_update_kind(0, 5, 6, 0, 0), UYKU_UPD_PARTIAL);
	T_EQ_INT(uyku_update_kind(0, 5, 5, 1, 0), UYKU_UPD_FULL); /* a new day: full even if nothing changed */
	T_EQ_INT(uyku_update_kind(0, 5, 6, 0, UYKU_FULL_EVERY - 1), UYKU_UPD_FULL);
	T_ASSERT(uyku_hash(uyku_hash(0, "ab"), "c") != uyku_hash(uyku_hash(0, "a"), "bc"));
	T_ASSERT(uyku_hash(0, "x") == uyku_hash(0, "x"));
	T_ASSERT(uyku_hash(0, NULL) == uyku_hash(0, ""));
}

static void test_weather_due(void)
{
	T_ASSERT(uyku_weather_due(T0, 0));
	T_ASSERT(!uyku_weather_due(T0, T0 - 3600));
	T_ASSERT(uyku_weather_due(T0, T0 - 3 * 3600 + 5 * 60)); /* a wake that drifted a few minutes early */
	T_ASSERT(uyku_weather_due(T0, T0 - 5 * 3600));
}

static void test_proverb(void)
{
	T_ASSERT(uyku_proverb_count() >= 30);
	T_EQ_STR(uyku_proverb(0), uyku_proverb(uyku_proverb_count()));
	T_ASSERT(strcmp(uyku_proverb(1), uyku_proverb(2)) != 0);
	T_ASSERT(uyku_proverb(-1)[0] != '\0');
}

static void test_excerpt(void)
{
	char out[512];
	int more;
	const char *poem = "a1\na2\na3\n\nb1\nb2\nb3\nb4\n\nc1";
	/* everything fits */
	T_EQ_INT(uyku_poem_excerpt(poem, 20, out, sizeof out, &more), 10);
	T_EQ_STR(out, poem);
	T_EQ_INT(more, 0);
	/* 6 lines: the second stanza (4) does not fit after 3 + gap, and half is used → the first stanza alone */
	T_EQ_INT(uyku_poem_excerpt(poem, 6, out, sizeof out, &more), 3);
	T_EQ_STR(out, "a1\na2\na3");
	T_EQ_INT(more, 1);
	/* 8 lines: two stanzas exactly */
	T_EQ_INT(uyku_poem_excerpt(poem, 8, out, sizeof out, &more), 8);
	T_EQ_STR(out, "a1\na2\na3\n\nb1\nb2\nb3\nb4");
	T_EQ_INT(more, 1);
	/* 2 lines of a 3-line stanza: cut inside it */
	T_EQ_INT(uyku_poem_excerpt(poem, 2, out, sizeof out, &more), 2);
	T_EQ_STR(out, "a1\na2");
	T_EQ_INT(more, 1);
	/* a short first stanza: going on into the next rather than showing one line */
	T_EQ_INT(uyku_poem_excerpt("x\n\ny1\ny2\ny3\ny4\ny5", 5, out, sizeof out, &more), 5);
	T_EQ_STR(out, "x\n\ny1\ny2\ny3");
	T_EQ_INT(more, 1);
	/* trailing blank lines are not "more" */
	T_EQ_INT(uyku_poem_excerpt("x\ny\n\n", 5, out, sizeof out, &more), 2);
	T_EQ_INT(more, 0);
	/* a tiny buffer never overflows */
	T_EQ_INT(uyku_poem_excerpt(poem, 20, out, 6, &more), 2);
	T_EQ_STR(out, "a1\na2");
	T_EQ_INT(more, 1);
	T_EQ_INT(uyku_poem_excerpt(NULL, 5, out, sizeof out, &more), 0);
}

int main(void)
{
	T_RUN(test_interval);
	T_RUN(test_next_wake);
	T_RUN(test_update_kind);
	T_RUN(test_weather_due);
	T_RUN(test_proverb);
	T_RUN(test_excerpt);
	return T_DONE();
}

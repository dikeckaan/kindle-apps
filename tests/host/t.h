/* Minimal host test macros. */
#ifndef T_H
#define T_H
#include <stdio.h>
#include <string.h>

static int t_failures;
static const char *t_current;

#define T_FAIL_(fmt, ...)                                                                    \
	do {                                                                                 \
		t_failures++;                                                                \
		fprintf(stderr, "FAIL %s (%s:%d): " fmt "\n", t_current, __FILE__, __LINE__, \
			__VA_ARGS__);                                                        \
	} while (0)
#define T_ASSERT(c) \
	do { if (!(c)) T_FAIL_("%s", #c); } while (0)
#define T_EQ_INT(a, b) \
	do { long t_a = (long) (a), t_b = (long) (b); if (t_a != t_b) T_FAIL_("%s == %ld, expected %ld", #a, t_a, t_b); } while (0)
#define T_EQ_STR(a, b) \
	do { const char *t_a = (a), *t_b = (b); if (strcmp(t_a, t_b) != 0) T_FAIL_("%s == \"%s\", expected \"%s\"", #a, t_a, t_b); } while (0)
#define T_RUN(fn) \
	do { t_current = #fn; fn(); } while (0)
#define T_DONE() (t_failures ? (fprintf(stderr, "%d failure(s)\n", t_failures), 1) : 0)
#endif

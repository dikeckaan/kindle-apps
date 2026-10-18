#include "kui.h"
#include "t.h"

/* every code point is 10 px wide */
static int mono(const char *s, int n, void *ctx)
{
	(void) ctx;
	int w = 0;
	for (int i = 0; i < n;) {
		int cp;
		i += kui_utf8_decode(s + i, &cp);
		w += 10;
	}
	return w;
}

static void line_is(const char *text, KuiLine l, const char *want)
{
	char buf[128];
	snprintf(buf, sizeof buf, "%.*s", l.len, text + l.off);
	T_EQ_STR(buf, want);
}

static void test_wrap_at_spaces(void)
{
	const char *t = "bir iki üç dört beş";
	KuiLine l[8];
	int n = kui_wrap(t, 80, mono, NULL, l, 8); /* 8 code points per line */
	T_EQ_INT(n, 3);
	line_is(t, l[0], "bir iki");
	line_is(t, l[1], "üç dört");
	line_is(t, l[2], "beş");
}

static void test_wrap_hard_newlines_and_blank_lines(void)
{
	const char *t = "a\n\nb c";
	KuiLine l[8];
	int n = kui_wrap(t, 200, mono, NULL, l, 8);
	T_EQ_INT(n, 3);
	line_is(t, l[0], "a");
	line_is(t, l[1], "");
	line_is(t, l[2], "b c");
}

static void test_wrap_long_word_split_on_code_points(void)
{
	const char *t = "ğğğğğğğğğğ x"; /* 10 two-byte letters, 4 per line */
	KuiLine l[8];
	int n = kui_wrap(t, 40, mono, NULL, l, 8);
	T_EQ_INT(n, 3);
	line_is(t, l[0], "ğğğğ");
	line_is(t, l[1], "ğğğğ");
	line_is(t, l[2], "ğğ x");
}

static void test_wrap_respects_max_and_empty(void)
{
	KuiLine l[2];
	T_EQ_INT(kui_wrap("a b c d e f", 10, mono, NULL, l, 2), 2);
	T_EQ_INT(kui_wrap("", 100, mono, NULL, l, 2), 0);
	T_EQ_INT(kui_wrap("   ", 100, mono, NULL, l, 2), 1);
}

static void test_wrap_covers_all_text(void)
{
	const char *t = "Kindle için uzun bir haber metni, satırlara bölünür ve hiçbir kelime kaybolmaz.";
	KuiLine l[32];
	int n = kui_wrap(t, 120, mono, NULL, l, 32);
	char joined[256] = "";
	for (int i = 0; i < n; i++) {
		if (i) strcat(joined, " ");
		strncat(joined, t + l[i].off, (size_t) l[i].len);
	}
	T_EQ_STR(joined, t);
	for (int i = 0; i < n; i++) T_ASSERT(mono(t + l[i].off, l[i].len, NULL) <= 120);
}

int main(void)
{
	T_RUN(test_wrap_at_spaces);
	T_RUN(test_wrap_hard_newlines_and_blank_lines);
	T_RUN(test_wrap_long_word_split_on_code_points);
	T_RUN(test_wrap_respects_max_and_empty);
	T_RUN(test_wrap_covers_all_text);
	return T_DONE();
}

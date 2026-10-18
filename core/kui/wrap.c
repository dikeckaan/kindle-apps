/* Greedy word wrap with a caller-supplied width measure (pure; the device measure is FBInk's). */
#include "kui.h"

/* bytes of the longest prefix of [s, s+n) that fits, at least one code point */
static int fit_prefix(const char *s, int n, int width, int (*measure)(const char *, int, void *), void *ctx)
{
	int best = 0;
	for (int i = 0; i < n;) {
		int cp, k = kui_utf8_decode(s + i, &cp);
		if (i + k > n) k = n - i;
		if (best && measure(s, i + k, ctx) > width) break;
		i += k;
		best = i;
	}
	return best;
}

int kui_wrap(const char *text, int width, int (*measure)(const char *s, int n, void *ctx), void *ctx, KuiLine *out, int max)
{
	int n = 0, p = 0;
	if (!text[0]) return 0;
	for (;;) {
		/* one paragraph: [p, pe) */
		int pe = p;
		while (text[pe] && text[pe] != '\n') pe++;
		int ls = p, le = p, i = p;
		while (i < pe && text[i] == ' ') i++;
		if (i == pe) { /* empty or blank paragraph: one empty line */
			if (n == max) return n;
			out[n++] = (KuiLine){ p, 0 };
		}
		ls = le = i;
		while (i < pe) {
			int ws = i, we = i;
			while (we < pe && text[we] != ' ') we++;
			if (measure(text + ls, we - ls, ctx) <= width) {
				le = we;
			} else if (le > ls) { /* line full: break before this word */
				if (n == max) return n;
				out[n++] = (KuiLine){ ls, le - ls };
				ls = le = ws;
				continue; /* retry the word on the new line */
			} else { /* the word alone is too wide: split it on code points */
				int k = fit_prefix(text + ws, we - ws, width, measure, ctx);
				if (n == max) return n;
				out[n++] = (KuiLine){ ws, k };
				ls = le = i = ws + k;
				continue;
			}
			i = we;
			while (i < pe && text[i] == ' ') i++;
			if (i < pe && le > ls) {
				/* keep scanning: the next word may still fit on this line */
			}
			if (i >= pe) break;
		}
		if (le > ls) {
			if (n == max) return n;
			out[n++] = (KuiLine){ ls, le - ls };
		}
		if (!text[pe]) break;
		p = pe + 1;
	}
	return n;
}

#include <stdio.h>
#include "sleep_opts.h"

#ifndef N_
#define N_(s) (s) /* translatable; shown with T() */
#endif

static const SleepOpt opts[] = {
	{ N_("5 dakika"), 300 }, { N_("10 dakika"), 600 }, { N_("30 dakika"), 1800 },
	{ N_("1 saat"), 3600 },  { N_("2 saat"), 7200 },   { N_("Hiç uyuma"), 0 },
};

static const char *(*text_hook)(const char *);

void sleep_set_text_hook(const char *(*translate)(const char *)) { text_hook = translate; }

static const char *tx(const char *s) { return text_hook ? text_hook(s) : s; }

int sleep_opts(const SleepOpt **out)
{
	*out = opts;
	return (int) (sizeof opts / sizeof opts[0]);
}

int sleep_current_index(int timeout_s, int prevent)
{
	int n = (int) (sizeof opts / sizeof opts[0]);
	if (prevent) return n - 1;
	for (int i = 0; i < n - 1; i++)
		if (opts[i].seconds == timeout_s) return i;
	return -1;
}

void sleep_describe(int timeout_s, int prevent, char *out, int cap)
{
	if (prevent) snprintf(out, (size_t) cap, "%s", tx(N_("Şu an: hiç uyumuyor")));
	else if (timeout_s % 3600 == 0 && timeout_s > 0) snprintf(out, (size_t) cap, tx(N_("Şu an: %d saat")), timeout_s / 3600);
	else snprintf(out, (size_t) cap, tx(N_("Şu an: %d dakika")), timeout_s / 60);
}

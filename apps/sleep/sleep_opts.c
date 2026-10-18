#include <stdio.h>
#include "sleep_opts.h"

static const SleepOpt opts[] = {
	{ "5 dakika", 300 }, { "10 dakika", 600 }, { "30 dakika", 1800 },
	{ "1 saat", 3600 },  { "2 saat", 7200 },   { "Hiç uyuma", 0 },
};

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
	if (prevent) snprintf(out, (size_t) cap, "Şu an: hiç uyumuyor");
	else if (timeout_s % 3600 == 0 && timeout_s > 0) snprintf(out, (size_t) cap, "Şu an: %d saat", timeout_s / 3600);
	else snprintf(out, (size_t) cap, "Şu an: %d dakika", timeout_s / 60);
}

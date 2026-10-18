/* Turkish day/month names, dates and number formatting. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "kui.h"

static const char *const days[] = { /* N_ */ "Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi" };
static const char *const days_short[] = { "Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt" };
static const char *const months[] = { "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
				      "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık" };

static const char *(*text_hook)(const char *);
static const char *num_dec = ",", *num_thou = ".";

void kui_set_text_hook(const char *(*translate)(const char *)) { text_hook = translate; }

void kui_set_num_format(const char *decimal_mark, const char *thousands_sep)
{
	num_dec = decimal_mark;
	num_thou = thousands_sep;
}

static const char *tx(const char *s) { return text_hook ? text_hook(s) : s; }

const char *kui_tr_weekday(int wday) { return wday >= 0 && wday < 7 ? tx(days[wday]) : ""; }
const char *kui_tr_weekday_short(int wday) { return wday >= 0 && wday < 7 ? tx(days_short[wday]) : ""; }
const char *kui_tr_month(int mon0) { return mon0 >= 0 && mon0 < 12 ? tx(months[mon0]) : ""; }

void kui_tr_date(const struct tm *tm, char *out, int cap)
{
	/* "%1$d %2$s %3$d, %4$s" order is the same in every supported language's catalog entry */
	snprintf(out, (size_t) cap, tx("%d %s %d, %s"), tm->tm_mday, kui_tr_month(tm->tm_mon), tm->tm_year + 1900,
		 kui_tr_weekday(tm->tm_wday));
}

void kui_fmt_num(double v, int decimals, char *out, int cap)
{
	char raw[64];
	snprintf(raw, sizeof raw, "%.*f", decimals, fabs(v));
	int neg = v < 0 && strspn(raw, "0.") != strlen(raw); /* no "-0,00" */
	char *dot = strchr(raw, '.');
	int ilen = dot ? (int) (dot - raw) : (int) strlen(raw), o = 0;
	char tmp[160];
	size_t tl = strlen(num_thou), dl = strlen(num_dec);
	if (neg) tmp[o++] = '-';
	for (int i = 0; i < ilen && o < 120; i++) {
		if (i && (ilen - i) % 3 == 0) {
			memcpy(tmp + o, num_thou, tl);
			o += (int) tl;
		}
		tmp[o++] = raw[i];
	}
	if (dot) {
		memcpy(tmp + o, num_dec, dl);
		o += (int) dl;
		for (const char *d = dot + 1; *d && o < 150; d++) tmp[o++] = *d;
	}
	tmp[o] = '\0';
	snprintf(out, (size_t) cap, "%s", tmp);
}

/* Turkish day/month names, dates and number formatting. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "kui.h"

static const char *const days[] = { "Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi" };
static const char *const days_short[] = { "Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt" };
static const char *const months[] = { "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
				      "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık" };

const char *kui_tr_weekday(int wday) { return wday >= 0 && wday < 7 ? days[wday] : ""; }
const char *kui_tr_weekday_short(int wday) { return wday >= 0 && wday < 7 ? days_short[wday] : ""; }
const char *kui_tr_month(int mon0) { return mon0 >= 0 && mon0 < 12 ? months[mon0] : ""; }

void kui_tr_date(const struct tm *tm, char *out, int cap)
{
	snprintf(out, (size_t) cap, "%d %s %d, %s", tm->tm_mday, kui_tr_month(tm->tm_mon), tm->tm_year + 1900,
		 kui_tr_weekday(tm->tm_wday));
}

void kui_fmt_num(double v, int decimals, char *out, int cap)
{
	char raw[64];
	snprintf(raw, sizeof raw, "%.*f", decimals, fabs(v));
	int neg = v < 0 && strspn(raw, "0.") != strlen(raw); /* no "-0,00" */
	char *dot = strchr(raw, '.');
	int ilen = dot ? (int) (dot - raw) : (int) strlen(raw), o = 0;
	char tmp[96];
	if (neg) tmp[o++] = '-';
	for (int i = 0; i < ilen; i++) {
		if (i && (ilen - i) % 3 == 0) tmp[o++] = '.';
		tmp[o++] = raw[i];
	}
	if (dot) {
		tmp[o++] = ',';
		for (const char *d = dot + 1; *d; d++) tmp[o++] = *d;
	}
	tmp[o] = '\0';
	snprintf(out, (size_t) cap, "%s", tmp);
}

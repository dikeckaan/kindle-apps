#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "net/net.h"
#include "news.h"

static void child_text(const XmlEl *el, const char *tag, char *out, int cap)
{
	XmlEl c;
	out[0] = '\0';
	if (xml_next(el->inner, el->inner_end, tag, &c)) xml_text(c.inner, c.inner_end, out, cap);
}

/* element content (CDATA/entities decoded) turned from HTML into plain text */
static char *child_plain(const XmlEl *el, const char *tag)
{
	XmlEl c;
	if (!xml_next(el->inner, el->inner_end, tag, &c)) return NULL;
	int cap = (int) (c.inner_end - c.inner) + 16;
	char *raw = malloc((size_t) cap), *txt = malloc((size_t) cap);
	if (!raw || !txt) {
		free(raw);
		free(txt);
		return NULL;
	}
	int n = xml_text(c.inner, c.inner_end, raw, cap);
	html_to_text(raw, raw + n, txt, cap);
	free(raw);
	return txt;
}

/* the longer of two plain texts (the other is freed); never NULL unless out of memory */
static char *longer(char *a, char *b)
{
	size_t la = a ? strlen(a) : 0, lb = b ? strlen(b) : 0;
	if (la >= lb) {
		free(b);
		return a ? a : calloc(1, 1);
	}
	free(a);
	return b;
}

/* Atom <link href="…"> (rel="alternate" or no rel) */
static void atom_link(const XmlEl *e, char *out, int cap)
{
	XmlEl l;
	out[0] = '\0';
	for (const char *p = e->inner; (p = xml_next(p, e->inner_end, "link", &l));) {
		char rel[32];
		if (xml_attr(&l, "rel", rel, sizeof rel) && strcmp(rel, "alternate") != 0) continue;
		if (xml_attr(&l, "href", out, cap)) return;
	}
}

int news_parse_rss(const char *xml, int len, NewsItem *out, int max)
{
	const char *end = xml + len;
	XmlEl el;
	int n = 0;
	for (const char *p = xml; n < max && (p = xml_next(p, end, "item", &el));) {
		NewsItem *it = &out[n];
		child_text(&el, "title", it->title, sizeof it->title);
		if (!it->title[0]) continue;
		child_text(&el, "link", it->link, sizeof it->link);
		if (!it->link[0]) child_text(&el, "guid", it->link, sizeof it->link);
		child_text(&el, "pubDate", it->date, sizeof it->date);
		it->text = longer(child_plain(&el, "content:encoded"), child_plain(&el, "description"));
		if (!it->text) continue;
		n++;
	}
	if (n) return n;
	/* Atom (NTV): <entry> with <summary>/<content>, <published>/<updated> */
	for (const char *p = xml; n < max && (p = xml_next(p, end, "entry", &el));) {
		NewsItem *it = &out[n];
		child_text(&el, "title", it->title, sizeof it->title);
		if (!it->title[0]) continue;
		atom_link(&el, it->link, sizeof it->link);
		if (!it->link[0]) child_text(&el, "id", it->link, sizeof it->link);
		child_text(&el, "published", it->date, sizeof it->date);
		if (!it->date[0]) child_text(&el, "updated", it->date, sizeof it->date);
		it->text = longer(child_plain(&el, "content"), child_plain(&el, "summary"));
		if (!it->text) continue;
		n++;
	}
	return n;
}

void news_free(NewsItem *it, int n)
{
	for (int i = 0; i < n; i++) {
		free(it[i].text);
		it[i].text = NULL;
	}
}

#define SEC(label, url) { label, url }
static const NewsSource sources[] = {
	{ "aa", "Anadolu Ajansı", 1, 8, { SEC(N_("Güncel"), "https://www.aa.com.tr/tr/rss/default?cat=guncel"),
		SEC(N_("Gündem"), "https://www.aa.com.tr/tr/rss/default?cat=gundem"), SEC(N_("Dünya"), "https://www.aa.com.tr/tr/rss/default?cat=dunya"),
		SEC(N_("Ekonomi"), "https://www.aa.com.tr/tr/rss/default?cat=ekonomi"), SEC(N_("Spor"), "https://www.aa.com.tr/tr/rss/default?cat=spor"),
		SEC(N_("Bilim"), "https://www.aa.com.tr/tr/rss/default?cat=bilim-teknoloji"),
		SEC(N_("Sağlık"), "https://www.aa.com.tr/tr/rss/default?cat=saglik"), SEC(N_("Kültür"), "https://www.aa.com.tr/tr/rss/default?cat=kultur") } },
	{ "trt", "TRT Haber", 1, 8, { SEC(N_("Manşet"), "https://www.trthaber.com/manset_articles.rss"),
		SEC(N_("Gündem"), "https://www.trthaber.com/gundem_articles.rss"), SEC(N_("Dünya"), "https://www.trthaber.com/dunya_articles.rss"),
		SEC(N_("Ekonomi"), "https://www.trthaber.com/ekonomi_articles.rss"), SEC(N_("Spor"), "https://www.trthaber.com/spor_articles.rss"),
		SEC(N_("Bilim"), "https://www.trthaber.com/bilim_teknoloji_articles.rss"),
		SEC(N_("Sağlık"), "https://www.trthaber.com/saglik_articles.rss"), SEC(N_("Kültür"), "https://www.trthaber.com/kultur_sanat_articles.rss") } },
	{ "hurriyet", "Hürriyet", 1, 4, { SEC(N_("Gündem"), "https://www.hurriyet.com.tr/rss/gundem"),
		SEC(N_("Ekonomi"), "https://www.hurriyet.com.tr/rss/ekonomi"), SEC(N_("Dünya"), "https://www.hurriyet.com.tr/rss/dunya"),
		SEC(N_("Teknoloji"), "https://www.hurriyet.com.tr/rss/teknoloji") } },
	{ "sozcu", "Sözcü", 1, 4, { SEC(N_("Gündem"), "https://www.sozcu.com.tr/feeds-rss-category-gundem"),
		SEC(N_("Ekonomi"), "https://www.sozcu.com.tr/feeds-rss-category-ekonomi"), SEC(N_("Dünya"), "https://www.sozcu.com.tr/feeds-rss-category-dunya"),
		SEC(N_("Spor"), "https://www.sozcu.com.tr/feeds-rss-category-spor") } },
	{ "ntv", "NTV", 1, 4, { SEC(N_("Gündem"), "https://www.ntv.com.tr/gundem.rss"), SEC(N_("Ekonomi"), "https://www.ntv.com.tr/ekonomi.rss"),
		SEC(N_("Dünya"), "https://www.ntv.com.tr/dunya.rss"), SEC(N_("Teknoloji"), "https://www.ntv.com.tr/teknoloji.rss") } },
	{ "cnnturk", "CNN Türk", 1, 4, { SEC(N_("Türkiye"), "https://www.cnnturk.com/feed/rss/turkiye/news"),
		SEC(N_("Ekonomi"), "https://www.cnnturk.com/feed/rss/ekonomi/news"), SEC(N_("Dünya"), "https://www.cnnturk.com/feed/rss/dunya/news"),
		SEC(N_("Spor"), "https://www.cnnturk.com/feed/rss/spor/news") } },
	{ "bbc", "BBC Türkçe", 1, 1, { SEC(N_("Tümü"), "https://feeds.bbci.co.uk/turkce/rss.xml") } },
	{ "cumhuriyet", "Cumhuriyet", 1, 1, { SEC(N_("Son dakika"), "https://www.cumhuriyet.com.tr/rss/son_dakika.xml") } },
	{ "milliyet", "Milliyet", 0, 2, { SEC(N_("Gündem"), "https://www.milliyet.com.tr/rss/rssnew/gundemrss.xml"),
		SEC(N_("Dünya"), "https://www.milliyet.com.tr/rss/rssnew/dunyarss.xml") } },
	{ "sabah", "Sabah", 0, 4, { SEC(N_("Gündem"), "https://www.sabah.com.tr/rss/gundem.xml"),
		SEC(N_("Ekonomi"), "https://www.sabah.com.tr/rss/ekonomi.xml"), SEC(N_("Dünya"), "https://www.sabah.com.tr/rss/dunya.xml"),
		SEC(N_("Spor"), "https://www.sabah.com.tr/rss/spor.xml") } },
	{ "haberturk", "Habertürk", 0, 2, { SEC(N_("Tümü"), "https://www.haberturk.com/rss"),
		SEC(N_("Gündem"), "https://www.haberturk.com/rss/kategori/gundem.xml") } },
	{ "dw", "DW Türkçe", 0, 1, { SEC(N_("Tümü"), "https://rss.dw.com/xml/rss-tur-all") } },
	{ "euronews", "Euronews", 0, 1, { SEC(N_("Tümü"), "https://tr.euronews.com/rss") } },
	{ "yenisafak", "Yeni Şafak", 0, 1, { SEC(N_("Tümü"), "https://www.yenisafak.com/rss") } },
	{ "birgun", "BirGün", 0, 1, { SEC(N_("Tümü"), "https://www.birgun.net/rss/home") } },
	{ "karar", "Karar", 0, 1, { SEC(N_("Tümü"), "https://www.karar.com/rss") } },
	{ "star", "Star", 0, 1, { SEC(N_("Tümü"), "https://www.star.com.tr/rss/rss.asp") } },
	{ "aksam", "Akşam", 0, 1, { SEC(N_("Tümü"), "https://www.aksam.com.tr/rss/rss.asp") } },
	{ "dunya", "Dünya", 0, 1, { SEC(N_("Ekonomi"), "https://www.dunya.com/rss") } },
	{ "bloomberght", "Bloomberg HT", 0, 1, { SEC(N_("Ekonomi"), "https://www.bloomberght.com/rss") } },
};

const NewsSource *news_sources(int *n)
{
	*n = (int) (sizeof sources / sizeof sources[0]);
	return sources;
}

void news_feed_id(const NewsSource *s, int section, char *out, int cap) { snprintf(out, (size_t) cap, "%s_%d", s->id, section); }

static int month_of(const char *m)
{
	static const char *const en[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	for (int i = 0; i < 12; i++)
		if (!strncmp(m, en[i], 3)) return i;
	return -1;
}

/* the date as written (local time of the publisher) and its UTC offset in minutes; 0 = ok */
static int parse_date(const char *s, int *y, int *mo, int *d, int *hh, int *mm, int *ss, int *off_min)
{
	char mon[8] = "", zone[16] = "";
	*ss = 0;
	*off_min = 0;
	if (sscanf(s, "%d-%d-%dT%d:%d:%d%15s", y, mo, d, hh, mm, ss, zone) >= 5) { /* ISO 8601 (Atom) */
		(*mo)--;
	} else {
		const char *comma = strchr(s, ',');
		int k = sscanf(comma ? comma + 1 : s, "%d %7s %d %d:%d:%d %15s", d, mon, y, hh, mm, ss, zone);
		if (k < 5 || (*mo = month_of(mon)) < 0) return -1;
	}
	if (zone[0] == '+' || zone[0] == '-') { /* "+0300" or "+03:00" */
		int zh = 0, zm = 0;
		if (sscanf(zone + 1, "%2d:%2d", &zh, &zm) != 2 && sscanf(zone + 1, "%2d%2d", &zh, &zm) != 2) zh = zm = 0;
		*off_min = (zone[0] == '-' ? -1 : 1) * (zh * 60 + zm);
	}
	/* plausible dates only: keeps the arithmetic far from overflow on a 32-bit long */
	return *y >= 1970 && *y <= 2100 && *mo >= 0 && *mo < 12 && *d >= 1 && *d <= 31 && *hh >= 0 && *hh < 24 && *mm >= 0 && *mm < 60 &&
			       *ss >= 0 && *ss <= 60
		       ? 0
		       : -1;
}

/* days since 1970-01-01 of a civil date (proleptic Gregorian) */
static long days_from_civil(int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

long news_date_epoch(const char *s)
{
	int y, mo, d, hh, mm, ss, off;
	if (parse_date(s, &y, &mo, &d, &hh, &mm, &ss, &off) != 0) return 0;
	return days_from_civil(y, mo + 1, d) * 86400L + hh * 3600L + mm * 60L + ss - off * 60L;
}

void news_date_short(const char *s, char *out, int cap)
{
	int y, mo, d, hh, mm, ss, off;
	out[0] = '\0';
	if (parse_date(s, &y, &mo, &d, &hh, &mm, &ss, &off) != 0) return;
	/* first three letters (code points) of the month name */
	const char *tm = kui_tr_month(mo);
	int b = 0;
	for (int k = 0; k < 3 && tm[b]; k++) {
		int cp;
		b += kui_utf8_decode(tm + b, &cp);
	}
	snprintf(out, (size_t) cap, "%d %.*s %02d:%02d", d, b, tm, hh, mm);
}

long news_when(const char *date, long now, int local_off_s)
{
	int y, mo, d, hh, mm, ss, off;
	if (parse_date(date, &y, &mo, &d, &hh, &mm, &ss, &off) != 0) return 0;
	long base = days_from_civil(y, mo + 1, d) * 86400L + hh * 3600L + mm * 60L + ss, e = base - off * 60L;
	/* a story "from the future" is local time with a wrong zone label (CNN Türk writes GMT): read it as local time */
	if (e > now + 600) e = base - local_off_s;
	return e;
}

void news_date_local(const char *date, long now, int local_off_s, char *out, int cap)
{
	long e = news_when(date, now, local_off_s);
	out[0] = '\0';
	if (!e) return;
	long t = e + local_off_s, days = t / 86400, rem = t % 86400;
	if (rem < 0) {
		rem += 86400;
		days--;
	}
	/* civil date from days since 1970 (inverse of days_from_civil) */
	long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	int dd = (int) (doy - (153 * mp + 2) / 5 + 1), m = (int) (mp < 10 ? mp + 3 : mp - 9);
	const char *tm = kui_tr_month(m - 1);
	int b = 0;
	for (int k = 0; k < 3 && tm[b]; k++) {
		int cp;
		b += kui_utf8_decode(tm + b, &cp);
	}
	snprintf(out, (size_t) cap, "%d %.*s %02ld:%02ld", dd, b, tm, rem / 3600, rem % 3600 / 60);
}

/* case-insensitive (Turkish rules) title comparison, so the same story from two sources is shown once */
static int same_title(const char *a, const char *b)
{
	char ua[512], ub[512];
	kui_tr_upper_str(a, ua, sizeof ua);
	kui_tr_upper_str(b, ub, sizeof ub);
	return !strcmp(ua, ub);
}

int news_front_page(const NewsItem *const *lists, const int *counts, int n_lists, int per_source, long now, int local_off_s, NewsRef *out,
		    int max)
{
	enum { MAXL = 32, MAXP = 16 };
	static NewsRef pick[MAXL][MAXP];
	int np[MAXL] = { 0 }, n = 0, order[MAXL];
	if (n_lists > MAXL) n_lists = MAXL;
	if (per_source > MAXP) per_source = MAXP;
	/* each source's share: the same title only once across sources, newest first */
	for (int l = 0; l < n_lists; l++) {
		for (int i = 0; i < counts[l] && np[l] < per_source; i++) {
			int dup = 0;
			for (int m = 0; m <= l && !dup; m++)
				for (int k = 0; k < np[m] && !dup; k++) dup = same_title(lists[m][pick[m][k].idx].title, lists[l][i].title);
			if (dup) continue;
			pick[l][np[l]++] = (NewsRef){ l, i, news_when(lists[l][i].date, now, local_off_s) };
		}
		for (int a = 1; a < np[l]; a++) { /* stable insertion sort, newest first; undated last */
			NewsRef r = pick[l][a];
			int b = a;
			for (; b > 0 && pick[l][b - 1].when < r.when; b--) pick[l][b] = pick[l][b - 1];
			pick[l][b] = r;
		}
		order[l] = l;
	}
	/* sources take turns, the one with the newest story first, so one busy source cannot fill the page */
	for (int a = 1; a < n_lists; a++) {
		int o = order[a], b = a;
		long wo = np[o] ? pick[o][0].when : -1;
		for (; b > 0 && (np[order[b - 1]] ? pick[order[b - 1]][0].when : -1) < wo; b--) order[b] = order[b - 1];
		order[b] = o;
	}
	for (int round = 0; round < per_source; round++)
		for (int k = 0; k < n_lists && n < max; k++) {
			int l = order[k];
			if (round < np[l]) out[n++] = pick[l][round];
		}
	return n;
}

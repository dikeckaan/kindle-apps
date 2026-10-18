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
		char *a = child_plain(&el, "content:encoded"), *b = child_plain(&el, "description");
		size_t la = a ? strlen(a) : 0, lb = b ? strlen(b) : 0;
		if (la >= lb) {
			it->text = a;
			free(b);
		} else {
			it->text = b;
			free(a);
		}
		if (!it->text) it->text = calloc(1, 1);
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

const NewsFeed *news_feeds(int *n)
{
	static const NewsFeed f[] = {
		{ "AA", N_("Güncel"), "https://www.aa.com.tr/tr/rss/default?cat=guncel", "aa_guncel" },
		{ "AA", N_("Gündem"), "https://www.aa.com.tr/tr/rss/default?cat=gundem", "aa_gundem" },
		{ "AA", N_("Dünya"), "https://www.aa.com.tr/tr/rss/default?cat=dunya", "aa_dunya" },
		{ "AA", N_("Ekonomi"), "https://www.aa.com.tr/tr/rss/default?cat=ekonomi", "aa_ekonomi" },
		{ "AA", N_("Spor"), "https://www.aa.com.tr/tr/rss/default?cat=spor", "aa_spor" },
		{ "AA", N_("Bilim"), "https://www.aa.com.tr/tr/rss/default?cat=bilim-teknoloji", "aa_bilim" },
		{ "AA", N_("Sağlık"), "https://www.aa.com.tr/tr/rss/default?cat=saglik", "aa_saglik" },
		{ "AA", N_("Kültür"), "https://www.aa.com.tr/tr/rss/default?cat=kultur", "aa_kultur" },
		{ "TRT", N_("Manşet"), "https://www.trthaber.com/manset_articles.rss", "trt_manset" },
		{ "TRT", N_("Gündem"), "https://www.trthaber.com/gundem_articles.rss", "trt_gundem" },
		{ "TRT", N_("Dünya"), "https://www.trthaber.com/dunya_articles.rss", "trt_dunya" },
		{ "TRT", N_("Ekonomi"), "https://www.trthaber.com/ekonomi_articles.rss", "trt_ekonomi" },
		{ "TRT", N_("Spor"), "https://www.trthaber.com/spor_articles.rss", "trt_spor" },
		{ "TRT", N_("Bilim"), "https://www.trthaber.com/bilim_teknoloji_articles.rss", "trt_bilim" },
		{ "TRT", N_("Sağlık"), "https://www.trthaber.com/saglik_articles.rss", "trt_saglik" },
		{ "TRT", N_("Kültür"), "https://www.trthaber.com/kultur_sanat_articles.rss", "trt_kultur" },
	};
	*n = (int) (sizeof f / sizeof f[0]);
	return f;
}

void news_date_short(const char *s, char *out, int cap)
{
	static const char *const en[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char mon[8];
	int d, y, hh, mm;
	out[0] = '\0';
	const char *comma = strchr(s, ',');
	if (sscanf(comma ? comma + 1 : s, "%d %7s %d %d:%d", &d, mon, &y, &hh, &mm) != 5) return;
	for (int i = 0; i < 12; i++) {
		if (strcmp(mon, en[i]) != 0) continue;
		/* first three letters (code points) of the Turkish month name */
		const char *tm = kui_tr_month(i);
		int b = 0;
		for (int k = 0; k < 3 && tm[b]; k++) {
			int cp;
			b += kui_utf8_decode(tm + b, &cp);
		}
		snprintf(out, (size_t) cap, "%d %.*s %02d:%02d", d, b, tm, hh, mm);
		return;
	}
}

#include <stdlib.h>
#include "news/news.h"
#include "t.h"

static char *slurp(const char *path, int *len)
{
	static char buf[262144];
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	*len = (int) n;
	return buf;
}

static void test_aa(void)
{
	NewsItem it[NEWS_MAX];
	int len;
	const char *xml = slurp("tests/host/fixtures/aa.xml", &len); /* len is set by slurp: read it afterwards */
	int n = news_parse_rss(xml, len, it, NEWS_MAX);
	T_EQ_INT(n, 30);
	T_EQ_STR(it[0].title, "Türk Yıldızları, Kayseri'de gösteri uçuşu yaptı");
	T_EQ_STR(it[0].link, "https://www.aa.com.tr/tr/gundem/turk-yildizlari-kayseride-gosteri-ucusu-yapti/4092689");
	T_EQ_STR(it[0].date, "Sun, 18 Oct 2026 16:04:02 +0300");
	T_EQ_STR(it[0].text, "Türk Hava Kuvvetlerinin akrobasi timi Türk Yıldızları, Kayseri'de gösteri uçuşu gerçekleştirdi.");
	news_free(it, n);
}

static void test_trt_content_without_images(void)
{
	NewsItem it[NEWS_MAX];
	int len;
	const char *xml = slurp("tests/host/fixtures/trt.xml", &len);
	int n = news_parse_rss(xml, len, it, NEWS_MAX);
	T_EQ_INT(n, 4);
	T_EQ_STR(it[0].title, "Cumhurbaşkanı Erdoğan'dan Nuri Pakdil için anma mesajı");
	T_ASSERT(strstr(it[0].text, "<") == NULL);
	T_ASSERT(strstr(it[0].text, "jpg") == NULL);
	T_ASSERT(strstr(it[0].text, "18/10/2026") == NULL); /* the <time> stamp is shown from pubDate instead */
	T_ASSERT(strncmp(it[0].text, "Cumhurbaşkanı Recep Tayyip Erdoğan, şair", 40) == 0);
	T_ASSERT(strstr(it[0].text, "© TRT HABER") != NULL);
	news_free(it, n);
}

static void test_truncated_feed_keeps_complete_items(void)
{
	int len;
	char *s = slurp("tests/host/fixtures/aa.xml", &len);
	char *third = strstr(strstr(strstr(s, "<item>") + 1, "<item>") + 1, "<item>");
	NewsItem it[NEWS_MAX];
	int n = news_parse_rss(s, (int) (third - s) + 40, it, NEWS_MAX);
	T_EQ_INT(n, 2);
	news_free(it, n);
	T_EQ_INT(news_parse_rss("<html>503</html>", 16, it, NEWS_MAX), 0);
}

static void test_date_short(void)
{
	char s[32];
	news_date_short("Sun, 18 Oct 2026 16:04:02 +0300", s, sizeof s);
	T_EQ_STR(s, "18 Eki 16:04");
	news_date_short("Mon, 2 Feb 2026 09:05:00 GMT", s, sizeof s);
	T_EQ_STR(s, "2 Şub 09:05");
	news_date_short("garbage", s, sizeof s);
	T_EQ_STR(s, "");
}

static void test_atom_feed(void)
{
	NewsItem it[NEWS_MAX];
	int len;
	const char *xml = slurp("tests/host/fixtures/ntv.atom", &len);
	int n = news_parse_rss(xml, len, it, NEWS_MAX);
	T_EQ_INT(n, 3);
	T_EQ_STR(it[0].title, "Kırklareli'nde ilaç fabrikasında yangın");
	T_EQ_STR(it[0].link, "https://www.ntv.com.tr/turkiye/kirklarelinde-ilac-fabrikasinda-yangin-1746896");
	T_EQ_STR(it[0].date, "2026-10-18T17:54:24+03:00");
	T_ASSERT(strstr(it[0].text, "itfaiye, AFAD ve sağlık ekipleri") != NULL); /* full content, not just the summary */
	T_ASSERT(strstr(it[0].text, "<") == NULL);
	news_free(it, n);
}

static void test_dates(void)
{
	char s[32];
	news_date_short("2026-10-18T17:54:24+03:00", s, sizeof s);
	T_EQ_STR(s, "18 Eki 17:54");
	/* the same moment written three ways */
	long a = news_date_epoch("Sun, 18 Oct 2026 14:54:24 GMT"), b = news_date_epoch("Sun, 18 Oct 2026 17:54:24 +0300"),
	     c = news_date_epoch("2026-10-18T17:54:24+03:00"), d = news_date_epoch("2026-10-18T14:54:24Z");
	T_ASSERT(a > 0);
	T_EQ_INT(b, a);
	T_EQ_INT(c, a);
	T_EQ_INT(d, a);
	T_EQ_INT(news_date_epoch("Sun, 18 Oct 2026 14:54:24 Z"), a);
	T_EQ_INT(news_date_epoch("Sun, 18 Oct 2026 14:54:24  Z"), a); /* Milliyet's double space */
	T_EQ_INT(news_date_epoch("garbage"), 0);
}

static void test_local_times(void)
{
	long now = news_date_epoch("2026-10-18T18:40:00+03:00");
	int tr = 3 * 3600;
	char s[32];
	/* Hürriyet writes UTC correctly: shown in local time */
	news_date_local("Sun, 18 Oct 2026 14:26:00 Z", now, tr, s, sizeof s);
	T_EQ_STR(s, "18 Eki 17:26");
	/* CNN Türk labels local time as GMT: in the future, so it is read as local time */
	news_date_local("Sun, 18 Oct 2026 17:39:10 GMT", now, tr, s, sizeof s);
	T_EQ_STR(s, "18 Eki 17:39");
	T_EQ_INT(news_when("Sun, 18 Oct 2026 17:39:10 GMT", now, tr), news_date_epoch("2026-10-18T17:39:10+03:00"));
	T_EQ_INT(news_when("Sun, 18 Oct 2026 18:34:00 +0300", now, tr), news_date_epoch("2026-10-18T15:34:00Z"));
	news_date_local("Sun, 18 Oct 2026 12:30:00 +0300", now, 0, s, sizeof s); /* a device on UTC */
	T_EQ_STR(s, "18 Eki 09:30");
	news_date_local("junk", now, tr, s, sizeof s);
	T_EQ_STR(s, "");
	T_EQ_INT(news_when("junk", now, tr), 0);
}

static void test_sources(void)
{
	int n;
	const NewsSource *src = news_sources(&n);
	T_ASSERT(n >= 20);
	int on = 0;
	for (int i = 0; i < n; i++) {
		T_ASSERT(src[i].n_sec >= 1 && src[i].n_sec <= NEWS_MAX_SECTIONS);
		on += src[i].default_on;
		for (int k = 0; k < src[i].n_sec; k++) T_ASSERT(strncmp(src[i].sec[k].url, "https://", 8) == 0);
		for (int j = 0; j < i; j++) T_ASSERT(strcmp(src[i].id, src[j].id) != 0);
	}
	T_ASSERT(on >= 4);
	T_EQ_STR(src[0].id, "aa");
	char id[48];
	news_feed_id(&src[1], 2, id, sizeof id);
	T_EQ_STR(id, "trt_2");
}

static void test_front_page_merge(void)
{
	static NewsItem a[3], b[3];
	strcpy(a[0].title, "Eski haber");
	strcpy(a[0].date, "Sun, 18 Oct 2026 10:00:00 +0300");
	strcpy(a[1].title, "Yeni haber");
	strcpy(a[1].date, "Sun, 18 Oct 2026 12:00:00 +0300");
	strcpy(a[2].title, "Üçüncü");
	strcpy(a[2].date, "Sun, 18 Oct 2026 09:00:00 +0300");
	strcpy(b[0].title, "En yeni");
	strcpy(b[0].date, "2026-10-18T13:00:00+03:00");
	strcpy(b[1].title, "yeni HABER"); /* same story from another source: shown once */
	strcpy(b[1].date, "2026-10-18T11:30:00+03:00");
	strcpy(b[2].title, "Tarihsiz");
	const NewsItem *lists[2] = { a, b };
	int counts[2] = { 3, 3 };
	NewsRef out[8];
	int n = news_front_page(lists, counts, 2, 2, news_date_epoch("2026-10-18T18:40:00+03:00"), 3 * 3600, out, 8); /* 2 per source */
	T_EQ_INT(n, 4);
	/* sources take turns (the one with the newest story first), each source's stories newest first,
	 * so one busy source cannot fill the page; "yeni HABER" is a duplicate and "Üçüncü" exceeds a's share */
	T_EQ_INT(out[0].src, 1);
	T_EQ_INT(out[0].idx, 0); /* b: "En yeni" 13:00 */
	T_EQ_INT(out[1].src, 0);
	T_EQ_INT(out[1].idx, 1); /* a: "Yeni haber" 12:00 */
	T_EQ_INT(out[2].src, 1);
	T_EQ_INT(out[2].idx, 2); /* b: "Tarihsiz" */
	T_EQ_INT(out[3].src, 0);
	T_EQ_INT(out[3].idx, 0); /* a: "Eski haber" 10:00 */
}

int main(void)
{
	T_RUN(test_aa);
	T_RUN(test_trt_content_without_images);
	T_RUN(test_truncated_feed_keeps_complete_items);
	T_RUN(test_date_short);
	T_RUN(test_atom_feed);
	T_RUN(test_dates);
	T_RUN(test_local_times);
	T_RUN(test_sources);
	T_RUN(test_front_page_merge);
	return T_DONE();
}

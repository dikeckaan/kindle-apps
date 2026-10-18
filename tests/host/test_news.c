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
	int len, n = news_parse_rss(slurp("tests/host/fixtures/aa.xml", &len), len, it, NEWS_MAX);
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
	int len, n = news_parse_rss(slurp("tests/host/fixtures/trt.xml", &len), len, it, NEWS_MAX);
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

static void test_feeds(void)
{
	int n;
	const NewsFeed *f = news_feeds(&n);
	T_ASSERT(n >= 12);
	T_EQ_STR(f[0].source, "AA");
	int trt = 0;
	for (int i = 0; i < n; i++) {
		T_ASSERT(strncmp(f[i].url, "https://", 8) == 0);
		if (!strcmp(f[i].source, "TRT")) trt++;
	}
	T_ASSERT(trt >= 6);
}

int main(void)
{
	T_RUN(test_aa);
	T_RUN(test_trt_content_without_images);
	T_RUN(test_truncated_feed_keeps_complete_items);
	T_RUN(test_date_short);
	T_RUN(test_feeds);
	return T_DONE();
}

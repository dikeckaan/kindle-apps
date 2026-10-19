#include <stdlib.h>
#include <string.h>
#include "akis/akis.h"
#include "kui.h"
#include "t.h"

static AkisFeed feeds[AKIS_MAX_FEEDS];

static void test_feeds_roundtrip(void)
{
	const char *buf = "# Akış: klasör<TAB>başlık<TAB>adres\n"
			  "Teknoloji\tWebtekno\thttps://www.webtekno.com/rss.xml\n"
			  "\tBlog\thttps://jvns.ca/atom.xml\r\n"
			  "bad line without tabs\n"
			  "Teknoloji\tAynı\thttps://www.webtekno.com/rss.xml\n" /* duplicate URL */
			  "Bilim\t\t\n"                                          /* no URL */
			  "Bilim\tQuanta\thttps://www.quantamagazine.org/feed/";
	int n = akis_feeds_parse(buf, feeds, AKIS_MAX_FEEDS);
	T_EQ_INT(n, 3);
	T_EQ_STR(feeds[0].folder, "Teknoloji");
	T_EQ_STR(feeds[0].title, "Webtekno");
	T_EQ_STR(feeds[1].folder, "");
	T_EQ_STR(feeds[1].url, "https://jvns.ca/atom.xml");
	T_EQ_STR(feeds[2].title, "Quanta");
	char out[4096];
	int len = akis_feeds_format(feeds, n, out, sizeof out);
	T_ASSERT(len > 0);
	static AkisFeed again[AKIS_MAX_FEEDS];
	T_EQ_INT(akis_feeds_parse(out, again, AKIS_MAX_FEEDS), 3);
	T_EQ_STR(again[2].url, feeds[2].url);
	T_EQ_INT(akis_feeds_format(feeds, n, out, 20), -1);
}

static void test_feed_add_remove(void)
{
	int n = 0;
	T_EQ_INT(akis_feed_add(feeds, &n, 2, "A\tB", "x\ny", "https://a.org/f"), 0);
	T_EQ_STR(feeds[0].folder, "A B");
	T_EQ_STR(feeds[0].title, "x y");
	T_EQ_INT(akis_feed_add(feeds, &n, 2, "", "", "https://a.org/f"), -2);
	T_EQ_INT(akis_feed_add(feeds, &n, 2, "", "", "https://b.org/f"), 1);
	T_EQ_INT(akis_feed_add(feeds, &n, 2, "", "", "https://c.org/f"), -1); /* full */
	T_EQ_INT(akis_feed_add(feeds, &n, 5, "", "", "https://c.org/ f"), -1); /* a space */
	T_EQ_INT(akis_feed_find(feeds, n, "https://b.org/f"), 1);
	akis_feed_remove(feeds, &n, 0);
	T_EQ_INT(n, 1);
	T_EQ_STR(feeds[0].url, "https://b.org/f");
	akis_feed_remove(feeds, &n, 5); /* out of range: nothing */
	T_EQ_INT(n, 1);
	/* a long title is cut at a character boundary */
	char lng[400] = "";
	for (int i = 0; i < 100; i++) strcat(lng, "ş");
	T_EQ_INT(akis_feed_add(feeds, &n, 5, "", lng, "https://d.org/"), 1);
	T_EQ_INT((int) strlen(feeds[1].title), 126);
	T_EQ_INT(kui_utf8_len(feeds[1].title), 63);
}

static void test_folders(void)
{
	int n = 0;
	akis_feed_add(feeds, &n, 10, "Bilim", "a", "https://a.org/");
	akis_feed_add(feeds, &n, 10, "", "b", "https://b.org/");
	akis_feed_add(feeds, &n, 10, "Bilim", "c", "https://c.org/");
	akis_feed_add(feeds, &n, 10, "Blog", "d", "https://d.org/");
	char f[AKIS_MAX_FOLDERS][AKIS_FOLDER_MAX];
	T_EQ_INT(akis_folders(feeds, n, f, AKIS_MAX_FOLDERS), 3);
	T_EQ_STR(f[0], "Bilim");
	T_EQ_STR(f[1], "");
	T_EQ_STR(f[2], "Blog");
	int idx[8];
	T_EQ_INT(akis_folder_feeds(feeds, n, "Bilim", idx, 8), 2);
	T_EQ_INT(idx[1], 2);
	T_EQ_INT(akis_folder_feeds(feeds, n, "Yok", idx, 8), 0);
}

static void test_ids(void)
{
	char id[17];
	akis_feed_id("https://xkcd.com/atom.xml", id);
	T_EQ_INT((int) strlen(id), 16);
	char id2[17];
	akis_feed_id("https://xkcd.com/atom.xml", id2);
	T_EQ_STR(id, id2);
	akis_feed_id("https://xkcd.com/rss.xml", id2);
	T_ASSERT(strcmp(id, id2) != 0);
	T_EQ_INT(akis_hash("") == 1469598103934665603ULL, 1);
	/* the link is the identity; without one the title */
	unsigned long long a = akis_item_key("https://f/", "https://x/1", "Başlık"), b = akis_item_key("https://f/", "https://x/1", "Düzeltilmiş");
	T_ASSERT(a == b);
	T_ASSERT(akis_item_key("https://f/", "", "A") != akis_item_key("https://f/", "", "B"));
	T_ASSERT(akis_item_key("https://f/", "https://x/1", "") != akis_item_key("https://g/", "https://x/1", ""));
}

static void test_urls(void)
{
	char u[AKIS_URL_MAX];
	T_EQ_INT(akis_normalize_url("  webtekno.com/rss.xml ", u, sizeof u), 0);
	T_EQ_STR(u, "https://webtekno.com/rss.xml");
	T_EQ_INT(akis_normalize_url("HTTP://Example.ORG/Feed?A=B", u, sizeof u), 0);
	T_EQ_STR(u, "http://example.org/Feed?A=B");
	T_EQ_INT(akis_normalize_url("feed://xkcd.com/atom.xml", u, sizeof u), 0);
	T_EQ_STR(u, "https://xkcd.com/atom.xml");
	T_EQ_INT(akis_normalize_url("feed:https://xkcd.com/atom.xml", u, sizeof u), 0);
	T_EQ_STR(u, "https://xkcd.com/atom.xml");
	T_EQ_INT(akis_normalize_url("xkcd.com", u, sizeof u), 0);
	T_EQ_STR(u, "https://xkcd.com");
	T_EQ_INT(akis_normalize_url("", u, sizeof u), -1);
	T_EQ_INT(akis_normalize_url("merhaba", u, sizeof u), -1);
	T_EQ_INT(akis_normalize_url("a b.com", u, sizeof u), -1);
	T_EQ_INT(akis_normalize_url("ftp://x.org/f", u, sizeof u), -1);
	T_EQ_INT(akis_normalize_url("file:///etc/passwd", u, sizeof u), -1);
	T_EQ_INT(akis_normalize_url("https:///x", u, sizeof u), -1);
	T_EQ_INT(akis_normalize_url("https://user@x.org/", u, sizeof u), -1);
	char typed[64];
	akis_url_from_typed("Lwn.net/headlınes/rss?İd=1", typed, sizeof typed);
	T_EQ_STR(typed, "Lwn.net/headlines/rss?id=1");
	akis_url_from_typed("ışık", typed, 4); /* bounded */
	T_EQ_STR(typed, "iş");
	char h[64];
	akis_url_host("https://www.Quantamagazine.org:443/feed/", h, sizeof h);
	T_EQ_STR(h, "Quantamagazine.org");
	akis_url_host("jvns.ca", h, sizeof h);
	T_EQ_STR(h, "jvns.ca");
}

static void test_feed_docs(void)
{
	const char *rss = "<?xml version=\"1.0\"?><rss version=\"2.0\"><channel><title>Webtekno &amp; Co</title>"
			  "<item><title>Haber</title></item></channel></rss>";
	const char *atom = "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title type=\"text\">Julia Evans</title>"
			   "<entry><title>Post</title></entry></feed>";
	const char *notitle = "<rss><channel><item><title>Only an item</title></item></channel></rss>";
	char t[128];
	T_ASSERT(akis_is_feed(rss, (int) strlen(rss)));
	T_ASSERT(akis_is_feed(atom, (int) strlen(atom)));
	T_ASSERT(!akis_is_feed("<html><body>Hi</body></html>", 28));
	akis_feed_title(rss, (int) strlen(rss), t, sizeof t);
	T_EQ_STR(t, "Webtekno & Co");
	akis_feed_title(atom, (int) strlen(atom), t, sizeof t);
	T_EQ_STR(t, "Julia Evans");
	akis_feed_title(notitle, (int) strlen(notitle), t, sizeof t);
	T_EQ_STR(t, "");
}

static void test_read_state(void)
{
	static AkisRead r;
	akis_read_parse("00000000000000ab\nzz\n\n00000000000000cd\n00000000000000ab\n", &r);
	T_EQ_INT(r.n, 2);
	T_ASSERT(akis_read_has(&r, 0xab));
	T_ASSERT(!akis_read_has(&r, 0xef));
	akis_read_add(&r, 0xef);
	akis_read_add(&r, 0xef);
	T_EQ_INT(r.n, 3);
	char buf[200];
	T_EQ_INT(akis_read_format(&r, buf, sizeof buf), 51);
	T_EQ_STR(buf, "00000000000000ab\n00000000000000cd\n00000000000000ef\n");
	T_EQ_INT(akis_read_format(&r, buf, 10), -1);
	akis_read_remove(&r, 0xcd);
	T_EQ_INT(r.n, 2);
	unsigned long long keep[] = { 0xef, 0x99 };
	T_EQ_INT(akis_read_prune(&r, keep, 2), 1);
	T_EQ_INT(r.n, 1);
	T_ASSERT(akis_read_has(&r, 0xef));
	/* full: the oldest goes */
	r.n = 0;
	for (int i = 1; i <= AKIS_READ_MAX + 2; i++) akis_read_add(&r, (unsigned long long) i);
	T_EQ_INT(r.n, AKIS_READ_MAX);
	T_ASSERT(!akis_read_has(&r, 1) && !akis_read_has(&r, 2) && akis_read_has(&r, 3));
}

static void test_opml(void)
{
	const char *opml = "<?xml version=\"1.0\"?><opml version=\"1.0\"><head><title>Benim</title></head><body>"
			   "<outline text=\"Tek\" type=\"rss\" xmlUrl=\"https://a.org/feed\"/>"
			   "<outline text=\"Bilim &amp; Doğa\" title=\"Bilim &amp; Doğa\">"
			   "  <outline type=\"rss\" text=\"Quanta\" title=\"Quanta Magazine\" xmlUrl=\"https://www.quantamagazine.org/feed/\" htmlUrl=\"x\"/>"
			   "  <outline text=\"Alt\"><outline xmlurl=\"nature.com/nature.rss\"/></outline>"
			   "</outline>"
			   "<outline text=\"Bozuk\" xmlUrl=\"not a url\"/>"
			   "<outline text=\"Aynı\" xmlUrl=\"https://a.org/feed\"/>"
			   "</body></opml>";
	int n = akis_opml_parse(opml, (int) strlen(opml), feeds, AKIS_MAX_FEEDS);
	T_EQ_INT(n, 3);
	T_EQ_STR(feeds[0].folder, "");
	T_EQ_STR(feeds[0].title, "Tek");
	T_EQ_STR(feeds[1].folder, "Bilim & Doğa");
	T_EQ_STR(feeds[1].title, "Quanta Magazine");
	T_EQ_STR(feeds[2].folder, "Alt");
	T_EQ_STR(feeds[2].url, "https://nature.com/nature.rss");
	T_EQ_STR(feeds[2].title, "nature.com");
	/* export and import again */
	static char out[8192];
	int len = akis_opml_format(feeds, n, out, sizeof out);
	T_ASSERT(len > 0);
	T_ASSERT(strstr(out, "text=\"Bilim &amp; Doğa\"") != NULL);
	static AkisFeed again[AKIS_MAX_FEEDS];
	T_EQ_INT(akis_opml_parse(out, len, again, AKIS_MAX_FEEDS), 3);
	T_EQ_STR(again[1].folder, "Bilim & Doğa");
	T_EQ_STR(again[1].url, "https://www.quantamagazine.org/feed/");
	T_EQ_STR(again[0].folder, "");
	T_EQ_INT(akis_opml_format(feeds, n, out, 50), -1);
	T_ASSERT(akis_is_opml_name("abonelikler.OPML"));
	T_ASSERT(akis_is_opml_name("a b.opml"));
	T_ASSERT(!akis_is_opml_name(".gizli.opml"));
	T_ASSERT(!akis_is_opml_name(".opml"));
	T_ASSERT(!akis_is_opml_name("kitap.epub"));
}

static void test_catalog(void)
{
	int n;
	const AkisCatFeed *c = akis_catalog(&n);
	T_ASSERT(n >= 30);
	const char *cats[16];
	int nc = akis_catalog_cats(cats, 16);
	T_ASSERT(nc >= 6);
	T_EQ_STR(cats[0], "Teknoloji (Türkçe)");
	for (int i = 0; i < n; i++) { /* every entry is a normalised https URL, listed once */
		char u[AKIS_URL_MAX];
		T_EQ_INT(akis_normalize_url(c[i].url, u, sizeof u), 0);
		T_EQ_STR(u, c[i].url);
		T_ASSERT(!strncmp(c[i].url, "https://", 8));
		for (int j = 0; j < i; j++) T_ASSERT(strcmp(c[i].url, c[j].url) != 0);
	}
	T_EQ_INT(akis_pages(0, 10), 1);
	T_EQ_INT(akis_pages(10, 10), 1);
	T_EQ_INT(akis_pages(11, 10), 2);
}

int main(void)
{
	T_RUN(test_feeds_roundtrip);
	T_RUN(test_feed_add_remove);
	T_RUN(test_folders);
	T_RUN(test_ids);
	T_RUN(test_urls);
	T_RUN(test_feed_docs);
	T_RUN(test_read_state);
	T_RUN(test_opml);
	T_RUN(test_catalog);
	return T_DONE();
}

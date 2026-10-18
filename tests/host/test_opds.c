#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ebooks/opds.h"
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

static void test_parse_fixture(void)
{
	static Book b[OPDS_PAGE];
	int len, more = 0;
	const char *xml = slurp("tests/host/fixtures/gutenberg.opds", &len); /* len is set by slurp: read it afterwards */
	int n = opds_parse(xml, len, b, OPDS_PAGE, &more);
	T_EQ_INT(n, 25); /* 26 entries, the "Subjects" navigation entry is skipped */
	T_EQ_INT(more, 26); /* start_index of the next page, from <link rel="next"> */
	T_EQ_INT(b[0].id, 46);
	T_EQ_STR(b[0].title, "A Christmas Carol in Prose; Being a Ghost Story of Christmas");
	T_EQ_STR(b[0].author, "Charles Dickens");
	T_EQ_INT(opds_has_author(&b[0]), 1);
	T_EQ_INT(b[1].id, 25948);
	T_EQ_STR(b[1].author, "6339 downloads"); /* shown as info, but not an author */
	T_EQ_INT(opds_has_author(&b[1]), 0);
	T_EQ_INT(b[24].id, 40723);
	for (int i = 0; i < n; i++) T_ASSERT(b[i].id > 0 && b[i].title[0]);
	/* max is respected */
	T_EQ_INT(opds_parse(xml, len, b, 3, &more), 3);
}

static void test_parse_navigation_and_no_next(void)
{
	const char *xml = "<feed><entry><id>https://www.gutenberg.org/ebooks/authors/search.opds/?query=x</id><title>Authors</title>"
			  "<content>2 authors</content></entry>"
			  "<entry><id>https://www.gutenberg.org/ebooks/12x.opds</id><title>Bad id</title></entry>"
			  "<entry><id>https://www.gutenberg.org/ebooks/.opds</id><title>Empty id</title></entry>"
			  "<entry><id>https://www.gutenberg.org/ebooks/1342.opds</id><title>Pride &amp; Prejudice</title></entry>"
			  "</feed>";
	Book b[4];
	int more = 7;
	int n = opds_parse(xml, (int) strlen(xml), b, 4, &more);
	T_EQ_INT(n, 1);
	T_EQ_INT(more, 0);
	T_EQ_INT(b[0].id, 1342);
	/* a next link without a usable start_index still says "there is more" */
	const char *odd = "<feed><link rel=\"next\" href=\"/ebooks/search.opds/?page=2\"/></feed>";
	opds_parse(odd, (int) strlen(odd), b, 4, &more);
	T_EQ_INT(more, 1);
	T_EQ_STR(b[0].title, "Pride & Prejudice");
	T_EQ_STR(b[0].author, "");
	T_EQ_INT(opds_has_author(&b[0]), 0);
}

static void test_parse_truncated(void)
{
	static Book b[OPDS_PAGE];
	int len, more = 0;
	const char *xml = slurp("tests/host/fixtures/gutenberg.opds", &len);
	const char *cut = strstr(xml, "/ebooks/24022.opds</id>"); /* inside the 3rd book */
	T_ASSERT(cut != NULL);
	int n = opds_parse(xml, (int) (cut - xml), b, OPDS_PAGE, &more);
	T_EQ_INT(n, 2);
	T_EQ_INT(b[1].id, 25948);
	T_EQ_INT(opds_parse("", 0, b, OPDS_PAGE, &more), 0);
	T_EQ_INT(more, 0);
	const char *open_entry = "<feed><entry><id>x/ebooks/5.opds</id><title>A";
	T_EQ_INT(opds_parse(open_entry, (int) strlen(open_entry), b, OPDS_PAGE, &more), 0);
}

static void test_urls(void)
{
	char u[512];
	opds_search_url("dickens christmas", 1, u, sizeof u);
	T_EQ_STR(u, "https://www.gutenberg.org/ebooks/search.opds/?query=dickens%20christmas&start_index=1");
	opds_search_url("Ömer & Seyfettin", 26, u, sizeof u);
	T_EQ_STR(u, "https://www.gutenberg.org/ebooks/search.opds/?query=%C3%96mer%20%26%20Seyfettin&start_index=26");
	/* the Turkish keyboard gives İ/ı for English names: folded to I/i */
	opds_search_url("DİCKENS dıckens", 1, u, sizeof u);
	T_EQ_STR(u, "https://www.gutenberg.org/ebooks/search.opds/?query=DICKENS%20dickens&start_index=1");
	opds_download_url(46, OPDS_AZW3, u, sizeof u);
	T_EQ_STR(u, "https://www.gutenberg.org/ebooks/46.kf8.images");
	opds_download_url(46, OPDS_EPUB, u, sizeof u);
	T_EQ_STR(u, "https://www.gutenberg.org/ebooks/46.epub3.images");
	T_EQ_STR(opds_ext(OPDS_AZW3), "azw3");
	T_EQ_STR(opds_ext(OPDS_EPUB), "epub");
}

static int valid_utf8(const char *s)
{
	const unsigned char *p = (const unsigned char *) s;
	while (*p) {
		int n = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
		if (!n) return 0;
		for (int k = 1; k < n; k++)
			if ((p[k] & 0xC0) != 0x80) return 0;
		p += n;
	}
	return 1;
}

static void test_file_name(void)
{
	char o[256];
	opds_file_name("A Christmas Carol", "Charles Dickens", "azw3", o, sizeof o);
	T_EQ_STR(o, "A Christmas Carol - Charles Dickens.azw3");
	opds_file_name("Dickens: A Life / Vol. 1", "", "epub", o, sizeof o);
	T_EQ_STR(o, "Dickens - A Life Vol. 1.epub");
	opds_file_name("../../etc/passwd", "a\\b", "azw3", o, sizeof o);
	T_EQ_STR(o, "etc passwd - a b.azw3");
	opds_file_name("  ...hidden?*<>|\"\x01\x7f\ttab\n  ", "  Yazar  ", "azw3", o, sizeof o);
	T_EQ_STR(o, "hidden tab - Yazar.azw3");
	opds_file_name("", "", "epub", o, sizeof o);
	T_EQ_STR(o, "Kitap.epub");
	opds_file_name("\xff\xfe ok", "", "epub", o, sizeof o); /* invalid UTF-8 never reaches the file system */
	T_ASSERT(valid_utf8(o));
	/* 300-byte Turkish title: at most 150 bytes, UTF-8 intact, extension and author kept */
	char t[400] = "";
	while (strlen(t) < 300) strcat(t, "Çalıkuşu ");
	opds_file_name(t, "Reşat Nuri Güntekin", "azw3", o, sizeof o);
	T_ASSERT(strlen(o) <= 150);
	T_ASSERT(valid_utf8(o));
	T_ASSERT(strncmp(o, "Çalıkuşu Çalıkuşu", 20) == 0);
	const char *tail = " - Reşat Nuri Güntekin.azw3";
	size_t L = strlen(o), TL = strlen(tail);
	T_ASSERT(L > TL && strcmp(o + L - TL, tail) == 0);
	T_ASSERT(o[L - TL - 1] != ' ');
	/* small buffers stay terminated and valid */
	opds_file_name(t, "Reşat Nuri Güntekin", "azw3", o, 20);
	T_ASSERT(strlen(o) < 20);
	T_ASSERT(valid_utf8(o));
}

static void test_unique_path(void)
{
	char dir[] = "build/host/opds_dir";
	char o[512];
	mkdir("build", 0755);
	mkdir("build/host", 0755);
	mkdir(dir, 0755);
	unlink("build/host/opds_dir/Book.azw3");
	unlink("build/host/opds_dir/Book (2).azw3");
	unlink("build/host/opds_dir/Book (3).azw3");
	T_EQ_INT(opds_unique_path(dir, "Book.azw3", o, sizeof o), 0);
	T_EQ_STR(o, "build/host/opds_dir/Book.azw3");
	FILE *f = fopen(o, "w");
	if (f) fclose(f);
	T_EQ_INT(opds_unique_path(dir, "Book.azw3", o, sizeof o), 0);
	T_EQ_STR(o, "build/host/opds_dir/Book (2).azw3");
	f = fopen(o, "w");
	if (f) fclose(f);
	T_EQ_INT(opds_unique_path(dir, "Book.azw3", o, sizeof o), 0);
	T_EQ_STR(o, "build/host/opds_dir/Book (3).azw3");
	T_EQ_INT(opds_unique_path(dir, "Book.azw3", o, 10), -1); /* does not fit */
	unlink("build/host/opds_dir/Book.azw3");
	unlink("build/host/opds_dir/Book (2).azw3");
}

int main(void)
{
	T_RUN(test_parse_fixture);
	T_RUN(test_parse_navigation_and_no_next);
	T_RUN(test_parse_truncated);
	T_RUN(test_urls);
	T_RUN(test_file_name);
	T_RUN(test_unique_path);
	return T_DONE();
}

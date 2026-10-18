/* E-Kitap: Project Gutenberg OPDS search results, download URLs and safe file names (pure, host-tested). */
#ifndef OPDS_H
#define OPDS_H

#define OPDS_PAGE 32 /* Gutenberg sends 25 books per page */
typedef struct {
	int id;
	char title[256];
	char author[160]; /* <content>: usually the author, sometimes "6339 downloads" */
} Book;
typedef enum { OPDS_AZW3, OPDS_EPUB } OpdsFmt;

/* https://www.gutenberg.org/ebooks/search.opds/?query=<urlencoded>&start_index=N */
void opds_search_url(const char *query, int start_index, char *out, int cap);
/* book entries (id ending in /ebooks/<number>.opds) of an OPDS page; navigation entries ("Subjects", "Authors")
 * and a truncated last entry are skipped. *has_next = 0 when there is no next page, else the next page's
 * start_index from <link rel="next"> (1 when the link has none). */
int  opds_parse(const char *xml, int len, Book *out, int max, int *has_next);
int  opds_has_author(const Book *b); /* 0 when <content> is empty or a download count */
void opds_download_url(int id, OpdsFmt fmt, char *out, int cap); /* .kf8.images / .epub3.images */
const char *opds_ext(OpdsFmt fmt);                                /* "azw3" / "epub" */
/* "Başlık - Yazar.ext", safe on FAT/fuse: no / \ : * ? " < > | or control bytes, no leading dots, trimmed, valid UTF-8,
 * at most 150 bytes (and < cap) without splitting a character; author may be "" */
void opds_file_name(const char *title, const char *author, const char *ext, char *out, int cap);
/* dir/name, or dir/"stem (2).ext", (3)… when taken; 0 = ok, -1 = no free name fits in cap */
int  opds_unique_path(const char *dir, const char *name, char *out, int cap);
#endif

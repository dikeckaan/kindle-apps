/* Akış: an RSS/Atom reader for the user's own feeds. Pure logic (no drawing): the feed list file, folders, read
 * state, feed URLs, OPML import/export and the built-in catalogue of popular feeds. Articles themselves are parsed
 * with news_parse_rss (apps/news/news.c, shared with Gazete and Masa Panosu), which is linked unchanged. */
#ifndef AKIS_H
#define AKIS_H

#define AKIS_MAX_FEEDS 200
#define AKIS_FOLDER_MAX 64
#define AKIS_TITLE_MAX 128
#define AKIS_URL_MAX 512
typedef struct { char folder[AKIS_FOLDER_MAX], title[AKIS_TITLE_MAX], url[AKIS_URL_MAX]; } AkisFeed; /* folder "" = Genel */

/* ---- feeds file: one "folder<TAB>title<TAB>url" per line; '#' lines and lines without a URL are skipped, a URL
 * already listed is dropped ---- */
int  akis_feeds_parse(const char *buf, AkisFeed *out, int max);
int  akis_feeds_format(const AkisFeed *f, int n, char *buf, int cap); /* bytes written (NUL-terminated), -1 if too small */
int  akis_feed_find(const AkisFeed *f, int n, const char *url);      /* index, -1 */
/* appends a feed (tabs/newlines in folder and title become spaces); index, -1 when full or the URL is empty,
 * -2 when the URL is already listed */
int  akis_feed_add(AkisFeed *f, int *n, int max, const char *folder, const char *title, const char *url);
void akis_feed_remove(AkisFeed *f, int *n, int idx);

/* ---- folders: distinct folder names in order of first appearance ("" — Genel — when a feed has none) ---- */
#define AKIS_MAX_FOLDERS 32
int akis_folders(const AkisFeed *f, int n, char out[][AKIS_FOLDER_MAX], int max);
int akis_folder_feeds(const AkisFeed *f, int n, const char *folder, int *idx, int max); /* indices of its feeds */

/* ---- ids and keys ---- */
unsigned long long akis_hash(const char *s);              /* FNV-1a 64 */
void akis_feed_id(const char *url, char out[17]);          /* cache file name: 16 hex digits of the URL's hash */
unsigned long long akis_item_key(const char *feed_url, const char *link, const char *title); /* an article's identity */

/* "webtekno.com/rss.xml" → "https://webtekno.com/rss.xml": trims, adds https:// when there is no scheme, lowercases
 * the scheme and host; feed:// becomes https://. 0 = ok, -1 when it cannot be a web address. */
int akis_normalize_url(const char *in, char *out, int cap);
void akis_url_host(const char *url, char *out, int cap); /* "https://www.x.org/a" → "x.org" (a fallback title) */
/* text typed on the Turkish keyboard as an address: its I key gives dotless ı (and İ), which in an address are i */
void akis_url_from_typed(const char *in, char *out, int cap);

/* ---- feed documents ---- */
int  akis_is_feed(const char *xml, int len);                       /* an RSS, RDF or Atom document */
void akis_feed_title(const char *xml, int len, char *out, int cap); /* the channel's / feed's own title, "" if none */

/* ---- read state: keys of read articles, oldest first ---- */
#define AKIS_READ_MAX 8192
typedef struct { unsigned long long k[AKIS_READ_MAX]; int n; } AkisRead;
void akis_read_parse(const char *buf, AkisRead *r);              /* hex keys, one per line */
int  akis_read_format(const AkisRead *r, char *buf, int cap);    /* bytes, -1 if too small */
int  akis_read_has(const AkisRead *r, unsigned long long k);
void akis_read_add(AkisRead *r, unsigned long long k);           /* when full the oldest key goes */
void akis_read_remove(AkisRead *r, unsigned long long k);
/* keeps only the keys that are in keep[] (articles still in some feed); returns how many were dropped */
int  akis_read_prune(AkisRead *r, const unsigned long long *keep, int nk);

/* ---- OPML: <outline xmlUrl=…> feeds; the text/title of an enclosing outline without xmlUrl is the folder ---- */
int akis_opml_parse(const char *xml, int len, AkisFeed *out, int max);
int akis_opml_format(const AkisFeed *f, int n, char *buf, int cap); /* bytes, -1 if too small */
int akis_is_opml_name(const char *name);                            /* "*.opml" (any case), not hidden */

/* ---- built-in catalogue: feed URLs only, the content is fetched live from the publisher ---- */
typedef struct { const char *cat, *name, *url; } AkisCatFeed; /* cat: Turkish source text (show with T()) */
const AkisCatFeed *akis_catalog(int *n);
int akis_catalog_cats(const char **out, int max); /* distinct categories in order */

/* ---- list paging ---- */
int akis_pages(int n, int per_page); /* at least 1 */
#endif

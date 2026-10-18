/* Gazete: RSS/Atom feeds of many Turkish news sources, parsed to plain text (no images). */
#ifndef NEWS_H
#define NEWS_H

#define NEWS_MAX 64
#define NEWS_MAX_SECTIONS 8
typedef struct { char title[256], link[256], date[40]; char *text; } NewsItem; /* text is malloc'd */
typedef struct { const char *label, *url; } NewsSection;      /* label: Turkish text, show with T() */
typedef struct {
	const char *id, *name; /* name: the publication's own name (not translated) */
	int default_on;        /* on the front page unless the user turns it off */
	int n_sec;
	NewsSection sec[NEWS_MAX_SECTIONS];
} NewsSource;
typedef struct { int src, idx; long when; } NewsRef; /* a story on the front page: lists[src][idx] */

/* complete <item>s (RSS) or <entry>s (Atom) of a document of len bytes; text = the longest of the full content
 * and the summary */
int  news_parse_rss(const char *xml, int len, NewsItem *out, int max);
void news_free(NewsItem *it, int n);
const NewsSource *news_sources(int *n);
void news_feed_id(const NewsSource *s, int section, char *out, int cap); /* cache name: "<id>_<section>" */
void news_date_short(const char *date, char *out, int cap); /* RFC 822 or ISO 8601 → "18 Eki 16:04"; "" if unparsable */
long news_date_epoch(const char *date);                     /* seconds since 1970 (UTC); 0 if unparsable */
/* publication time in UTC; a time more than 10 minutes in the future is taken as local time with a wrong zone label */
long news_when(const char *date, long now, int local_off_s);
void news_date_local(const char *date, long now, int local_off_s, char *out, int cap); /* "18 Eki 17:26" local time */
/* front page: up to per_source stories from each list (the same title once), sources taking turns, newest first */
int news_front_page(const NewsItem *const *lists, const int *counts, int n_lists, int per_source, long now, int local_off_s, NewsRef *out,
		    int max);
#endif

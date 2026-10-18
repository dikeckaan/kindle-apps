/* Gazete: RSS feeds of Anadolu Ajansı and TRT Haber, parsed to plain text (no images). */
#ifndef NEWS_H
#define NEWS_H

#define NEWS_MAX 64
typedef struct { char title[256], link[256], date[40]; char *text; } NewsItem; /* text is malloc'd */
typedef struct { const char *source, *label, *url, *id; } NewsFeed;

/* complete <item>s of an RSS document of len bytes; text = content:encoded or description, whichever says more */
int  news_parse_rss(const char *xml, int len, NewsItem *out, int max);
void news_free(NewsItem *it, int n);
const NewsFeed *news_feeds(int *n);
void news_date_short(const char *rfc822, char *out, int cap); /* "18 Eki 16:04"; "" if unparsable */
#endif

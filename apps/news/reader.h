/* News article reader (device UI), shared by Gazete and Masa Panosu. */
#ifndef NEWS_READER_H
#define NEWS_READER_H
#include "news.h"
int  news_local_off(void);                                  /* device UTC offset; Türkiye's when no zone is set */
void news_show_date(const char *date, char *out, int cap); /* local "18 Eki 17:26" */
void news_read(const NewsItem *it, const char *source);    /* full-screen reader until ✕ */
#endif

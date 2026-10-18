/* Şiir Defteri: the anthology file (poets and poems), the poem of the day, paging a poem by line heights, favourites. */
#ifndef SIIR_H
#define SIIR_H

typedef struct { const char *id, *name, *years, *note; int first, count; } SiirAuthor; /* poems[first .. first+count) */
typedef struct { int author; const char *title, *url, *text; } SiirPoem; /* text: lines joined by '\n', "" = stanza break */
typedef struct { char *buf; SiirAuthor *authors; int n_authors; SiirPoem *poems; int n_poems; } SiirDb;

/* Parses the anthology in place (buf is kept and freed by siir_free). Format, one record per line starting with '@':
 *   @A<TAB>id<TAB>name<TAB>years<TAB>note       a poet; the poems that follow belong to the last @A
 *   @P<TAB>author id<TAB>title<TAB>source url   a poem; its lines follow until the next '@' line
 * '#' lines before the first record are comments. Returns the poem count, -1 on a malformed file. */
int  siir_parse(char *buf, SiirDb *db);
int  siir_load(const char *path, SiirDb *db); /* reads and parses; -1 if missing or malformed */
void siir_free(SiirDb *db);

/* ---- choosing a poem ---- */
long siir_days(int year, int mon, int mday); /* days since 1970-01-01 (proleptic Gregorian); mon 1..12 */
/* Poem of the day: a fixed permutation of 0..n-1 walked one step a day, so every poem comes once per n days and
 * neighbouring days rarely share a poet (the file is grouped by poet). -1 when n <= 0. */
int  siir_daily(long day, int n);
int  siir_random(unsigned seed, int n, int avoid); /* any index but avoid (when n > 1) */

/* ---- paging: items are laid out top to bottom; the first page has first_h pixels, later pages page_h ---- */
typedef enum { SIIR_LINE, SIIR_CONT, SIIR_GAP } SiirKind; /* a verse line, its wrapped continuation, a stanza break */
typedef struct { SiirKind kind; int h; } SiirItem;
/* Breaks never fall inside a verse line (between it and its SIIR_CONT parts) unless the line alone overflows a page,
 * a stanza break that would open a page is dropped there, and when a stanza does not fit, the page ends at the last
 * stanza break instead if that leaves the page at least two thirds full (otherwise a stanza's last line
 * does not go over alone: the line before it goes too). starts[k] = first item of page k (a stanza
 * break opening a page is skipped by the caller). Returns the page count (>= 1 for n > 0, at most max). */
int siir_paginate(const SiirItem *it, int n, int first_h, int page_h, int *starts, int max);

/* ---- favourites: poem keys (source urls), one per line, in the order they were added ---- */
#define SIIR_MAX_FAVS 1000
typedef struct { char *key[SIIR_MAX_FAVS]; int n; } SiirFavs;
void siir_favs_parse(const char *buf, SiirFavs *f); /* f (zeroed or used before) is reset; blanks and duplicates dropped */
int  siir_favs_has(const SiirFavs *f, const char *key);
int  siir_favs_toggle(SiirFavs *f, const char *key); /* new state: 1 added, 0 removed, -1 full */
int  siir_favs_format(const SiirFavs *f, char *buf, int cap); /* bytes written (NUL-terminated), -1 if cap is too small */
void siir_favs_free(SiirFavs *f);
int  siir_find_url(const SiirDb *db, const char *url); /* poem index, -1 */
#endif

/* Şiir Defteri: pure logic (no drawing). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "siir.h"

/* ---- anthology file ---- */

/* splits s at tabs into up to max fields (in place); returns the field count */
static int split_tabs(char *s, char **f, int max)
{
	int n = 0;
	f[n++] = s;
	for (char *p = s; *p && n < max; p++)
		if (*p == '\t') {
			*p = '\0';
			f[n++] = p + 1;
		}
	return n;
}

static void chomp_cr(char *s)
{
	size_t n = strlen(s);
	if (n && s[n - 1] == '\r') s[n - 1] = '\0';
}

int siir_parse(char *buf, SiirDb *db)
{
	memset(db, 0, sizeof *db);
	int na = 0, np = 0;
	for (char *p = buf; *p; p++)
		if (*p == '@' && (p == buf || p[-1] == '\n')) {
			if (p[1] == 'A') na++;
			else if (p[1] == 'P') np++;
		}
	if (!na || !np) return -1;
	db->authors = calloc((size_t) na, sizeof *db->authors);
	db->poems = calloc((size_t) np, sizeof *db->poems);
	if (!db->authors || !db->poems) goto bad;
	char *p = buf, *text_end = NULL;
	SiirPoem *cur = NULL;
	while (*p) {
		char *nl = strchr(p, '\n');
		char *next = nl ? nl + 1 : p + strlen(p);
		if (*p == '@') {
			if (cur) { /* the previous poem ends before this record; drop its trailing newlines and CRs */
				char *e = text_end;
				while (e > cur->text && (e[-1] == '\n' || e[-1] == '\r')) e--;
				*e = '\0';
				cur = NULL;
			}
			if (nl) *nl = '\0';
			chomp_cr(p);
			char *f[6];
			int k = p[2] == '\t' ? split_tabs(p + 2, f, 6) : 0; /* f[0] is the empty text before the first tab */
			if (p[1] == 'A' && k == 5 && !strchr(f[4], '\t')) {
				SiirAuthor *a = &db->authors[db->n_authors++];
				*a = (SiirAuthor){ f[1], f[2], f[3], f[4], db->n_poems, 0 };
			} else if (p[1] == 'P' && k == 4 &&db->n_authors && !strcmp(f[1], db->authors[db->n_authors - 1].id)) {
				cur = &db->poems[db->n_poems++];
				*cur = (SiirPoem){ db->n_authors - 1, f[2], f[3], next };
				db->authors[db->n_authors - 1].count++;
				text_end = next;
			} else {
				goto bad;
			}
		} else if (cur) {
			/* a poem line: CRLF → LF in place by moving the text down */
			size_t len = (size_t) (next - p);
			if (len >= 2 && nl && nl[-1] == '\r') {
				memmove(text_end, p, len - 2);
				text_end += len - 2;
				*text_end++ = '\n';
			} else {
				memmove(text_end, p, len);
				text_end += len;
			}
		} else if (*p != '#' && *p != '\n' && *p != '\r') {
			goto bad; /* text outside a poem */
		}
		p = next;
	}
	if (cur) {
		char *e = text_end;
		while (e > cur->text && (e[-1] == '\n' || e[-1] == '\r')) e--;
		*e = '\0';
	}
	if (!db->n_poems) goto bad;
	db->buf = buf;
	return db->n_poems;
bad:
	free(db->authors);
	free(db->poems);
	memset(db, 0, sizeof *db);
	return -1;
}

int siir_load(const char *path, SiirDb *db)
{
	memset(db, 0, sizeof *db);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = n >= 0 ? malloc((size_t) n + 1) : NULL;
	if (!buf || fread(buf, 1, (size_t) n, f) != (size_t) n) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[n] = '\0';
	int r = siir_parse(buf, db);
	if (r < 0) free(buf);
	return r;
}

void siir_free(SiirDb *db)
{
	free(db->buf);
	free(db->authors);
	free(db->poems);
	memset(db, 0, sizeof *db);
}

int siir_find_url(const SiirDb *db, const char *url)
{
	for (int i = 0; i < db->n_poems; i++)
		if (!strcmp(db->poems[i].url, url)) return i;
	return -1;
}

/* ---- choosing a poem ---- */

long siir_days(int y, int m, int d)
{
	/* days from civil (H. Hinnant) */
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static long gcd(long a, long b)
{
	while (b) {
		long t = a % b;
		a = b;
		b = t;
	}
	return a;
}

int siir_daily(long day, int n)
{
	if (n <= 0) return -1;
	if (n == 1) return 0;
	/* step ≈ n / golden ratio, made coprime with n: consecutive days land far apart in the file */
	long step = (long) (n * 0.6180339887) | 1;
	while (gcd(step, n) != 1) step++;
	long k = ((day % n) + n) % n;
	return (int) ((k * step + n / 3) % n);
}

int siir_random(unsigned seed, int n, int avoid)
{
	if (n <= 0) return -1;
	if (n == 1) return 0;
	unsigned x = seed * 2654435761u + 0x9E3779B9u; /* one xorshift round over a multiplicative hash */
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	if (avoid < 0 || avoid >= n) return (int) (x % (unsigned) n);
	int r = (int) (x % (unsigned) (n - 1));
	return r >= avoid ? r + 1 : r;
}

/* ---- paging ---- */

int siir_paginate(const SiirItem *it, int n, int first_h, int page_h, int *starts, int max)
{
	if (n <= 0 || max <= 0) return 0;
	int pages = 0, y = 0, cap = first_h, last_gap = -1, gap_y = 0, first_unit = -1, last_unit = -1;
	starts[pages++] = 0;
	for (int i = 0; i < n;) {
		int unit = 1, uh = it[i].h;
		if (it[i].kind != SIIR_GAP)
			while (i + unit < n && it[i + unit].kind == SIIR_CONT) uh += it[i + unit++].h;
		if (it[i].kind == SIIR_GAP && y == 0) { /* a stanza break opening a page takes no room */
			i++;
			continue;
		}
		if (y + uh <= cap) {
			if (it[i].kind == SIIR_GAP) {
				last_gap = i;
				gap_y = y;
			} else {
				if (first_unit < 0) first_unit = i;
				last_unit = i;
			}
			y += uh;
			i += unit;
			continue;
		}
		/* does not fit */
		int at = i;
		int stanza_end = i + unit >= n || it[i + unit].kind == SIIR_GAP;
		if (it[i].kind != SIIR_GAP && last_gap >= 0 && 3 * gap_y >= 2 * cap) at = last_gap + 1; /* move the stanza */
		else if (it[i].kind != SIIR_GAP && stanza_end && last_unit > first_unit && last_unit > last_gap)
			at = last_unit; /* no widow: a stanza's last line does not go over alone */
		else if (y == 0) { /* taller than a whole page: place its parts while they fit (at least one) */
			for (int k = 0; k < unit && (k == 0 || y + it[i].h <= cap); k++) y += it[i++].h;
			continue;
		}
		if (pages == max) return pages;
		starts[pages++] = at;
		cap = page_h;
		y = 0;
		last_gap = first_unit = last_unit = -1;
		i = at;
	}
	return pages;
}

/* ---- favourites ---- */

void siir_favs_free(SiirFavs *f)
{
	for (int i = 0; i < f->n; i++) free(f->key[i]);
	f->n = 0;
}

int siir_favs_has(const SiirFavs *f, const char *key)
{
	for (int i = 0; i < f->n; i++)
		if (!strcmp(f->key[i], key)) return 1;
	return 0;
}

static int favs_add(SiirFavs *f, const char *key, size_t len)
{
	if (f->n == SIIR_MAX_FAVS) return -1;
	char *k = malloc(len + 1);
	if (!k) return -1;
	memcpy(k, key, len);
	k[len] = '\0';
	if (siir_favs_has(f, k)) {
		free(k);
		return 0;
	}
	f->key[f->n++] = k;
	return 1;
}

void siir_favs_parse(const char *buf, SiirFavs *f)
{
	siir_favs_free(f);
	for (const char *p = buf; p && *p;) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t) (e - p) : strlen(p);
		size_t a = 0, b = len;
		while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
		while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t' || p[b - 1] == '\r')) b--;
		if (b > a) favs_add(f, p + a, b - a);
		p = e ? e + 1 : p + len;
	}
}

int siir_favs_toggle(SiirFavs *f, const char *key)
{
	for (int i = 0; i < f->n; i++)
		if (!strcmp(f->key[i], key)) {
			free(f->key[i]);
			memmove(&f->key[i], &f->key[i + 1], (size_t) (f->n - i - 1) * sizeof f->key[0]);
			f->n--;
			return 0;
		}
	return favs_add(f, key, strlen(key));
}

int siir_favs_format(const SiirFavs *f, char *buf, int cap)
{
	int n = 0;
	for (int i = 0; i < f->n; i++) {
		int len = (int) strlen(f->key[i]);
		if (n + len + 2 > cap) return -1;
		memcpy(buf + n, f->key[i], (size_t) len);
		n += len;
		buf[n++] = '\n';
	}
	if (n + 1 > cap) return -1;
	buf[n] = '\0';
	return n;
}

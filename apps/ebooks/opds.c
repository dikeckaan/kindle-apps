/* Project Gutenberg OPDS: search URL, result parsing, download URL and a file name that is safe on the Kindle's
 * FAT/fuse /mnt/us. Titles and authors come from the network: nothing of them reaches a path unsanitised. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "kui.h"
#include "net/net.h"
#include "opds.h"

#define BASE "https://www.gutenberg.org/ebooks/"
#define NAME_MAX_BYTES 150

void opds_search_url(const char *query, int start_index, char *out, int cap)
{
	/* Gutenberg's catalogue is mostly English: the Turkish keyboard's İ (U+0130) and ı (U+0131) become I and i */
	char f[200], q[600];
	int o = 0;
	for (const char *s = query; *s && o < (int) sizeof f - 5;) {
		int cp, k = kui_utf8_decode(s, &cp);
		if (cp == 0x130 || cp == 0x131) f[o++] = cp == 0x130 ? 'I' : 'i';
		else {
			memcpy(f + o, s, (size_t) k);
			o += k;
		}
		s += k;
	}
	f[o] = '\0';
	net_urlencode(f, q, sizeof q);
	snprintf(out, (size_t) cap, BASE "search.opds/?query=%s&start_index=%d", q, start_index < 1 ? 1 : start_index);
}

/* book number from an entry id ".../ebooks/<digits>.opds", or 0 */
static int book_id(const char *id)
{
	size_t n = strlen(id);
	if (n < 6 || strcmp(id + n - 5, ".opds") != 0) return 0;
	const char *e = id + n - 5, *s = e;
	while (s > id && s[-1] >= '0' && s[-1] <= '9') s--;
	if (s == e || e - s > 9 || s - id < 8 || memcmp(s - 8, "/ebooks/", 8) != 0) return 0;
	return atoi(s);
}

int opds_parse(const char *xml, int len, Book *out, int max, int *has_next)
{
	const char *p = xml, *end = xml + (len > 0 ? len : 0);
	int n = 0;
	*has_next = 0;
	XmlEl el;
	/* feed-level <link rel="next">; links inside entries carry other rels */
	for (const char *q = p; (q = xml_next(q, end, "link", &el));) {
		char rel[32], href[600];
		if (xml_attr(&el, "rel", rel, sizeof rel) && !strcmp(rel, "next")) {
			const char *si = xml_attr(&el, "href", href, sizeof href) ? strstr(href, "start_index=") : NULL;
			int v = si ? atoi(si + 12) : 0;
			*has_next = v > 1 ? v : 1;
			break;
		}
	}
	while (n < max && (p = xml_next(p, end, "entry", &el))) {
		XmlEl f;
		char id[300];
		if (!xml_next(el.inner, el.inner_end, "id", &f)) continue;
		xml_text(f.inner, f.inner_end, id, sizeof id);
		Book *b = &out[n];
		if (!(b->id = book_id(id))) continue; /* Subjects / Authors / Bookshelves navigation */
		b->title[0] = b->author[0] = '\0';
		if (xml_next(el.inner, el.inner_end, "title", &f)) xml_text(f.inner, f.inner_end, b->title, sizeof b->title);
		if (xml_next(el.inner, el.inner_end, "content", &f)) xml_text(f.inner, f.inner_end, b->author, sizeof b->author);
		if (!b->title[0]) continue;
		n++;
	}
	return n;
}

int opds_has_author(const Book *b)
{
	const char *s = b->author;
	if (!*s) return 0;
	while (*s >= '0' && *s <= '9') s++;
	return !(s != b->author && (!strcmp(s, " downloads") || !strcmp(s, " download")));
}

void opds_download_url(int id, OpdsFmt fmt, char *out, int cap)
{
	snprintf(out, (size_t) cap, BASE "%d.%s", id, fmt == OPDS_EPUB ? "epub3.images" : "kf8.images");
}

const char *opds_ext(OpdsFmt fmt) { return fmt == OPDS_EPUB ? "epub" : "azw3"; }

/* sanitised copy of s in out (< cap bytes and <= limit bytes): forbidden and control characters become spaces
 * (':' becomes " - "), runs of spaces collapse, leading dots/spaces and trailing dots/spaces/dashes are dropped,
 * invalid UTF-8 becomes '_', and characters are never split. Returns the length. */
static int clean(const char *s, char *out, int cap, int limit)
{
	int o = 0;
	if (limit > cap - 1) limit = cap - 1;
	if (limit < 0) limit = 0;
	while (*s && o < limit) {
		int cp, k = kui_utf8_decode(s, &cp);
		const char *rep = NULL;
		if (k == 1 && cp == 0xFFFD) rep = "_"; /* invalid byte */
		else if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || (cp < 0x80 && strchr("/\\*?\"<>|", cp))) rep = " ";
		else if (cp == ':') rep = " - ";
		if (!rep) { /* a whole character or nothing */
			if (o + k > limit) break;
			memcpy(out + o, s, (size_t) k);
			o += k;
		}
		for (; rep && *rep && o < limit; rep++) {
			if (*rep == ' ' && (o == 0 || out[o - 1] == ' ')) continue; /* collapse, no leading space */
			out[o++] = *rep;
		}
		if (!rep && o == 1 && out[0] == '.') o = 0; /* no leading dots */
		if (!rep && o >= 2 && out[o - 1] == ' ' && out[o - 2] == ' ') o--;
		s += k;
	}
	/* trailing dots/spaces/dashes are trimmed; leading spaces, dots and dashes (from ":x" or "..") too */
	while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.' || out[o - 1] == '-')) o--;
	int lead = 0;
	while (lead < o && (out[lead] == '-' || out[lead] == ' ' || out[lead] == '.')) lead++;
	memmove(out, out + lead, (size_t) (o - lead));
	o -= lead;
	if (cap > 0) out[o] = '\0';
	return o;
}

void opds_file_name(const char *title, const char *author, const char *ext, char *out, int cap)
{
	char e[16], t[NAME_MAX_BYTES + 1], a[64];
	if (cap <= 0) return;
	int budget = cap - 1 < NAME_MAX_BYTES ? cap - 1 : NAME_MAX_BYTES;
	int el = clean(ext, e, sizeof e, 10);
	int room = budget - (el ? el + 1 : 0);
	if (room < 1) {
		out[0] = '\0';
		return;
	}
	int al = clean(author, a, sizeof a, 60);
	if (al && al + 3 + 8 > room) al = 0; /* keep at least a little of the title before giving room to the author */
	int tl = clean(title, t, sizeof t, room - (al ? al + 3 : 0));
	if (!tl) tl = clean("Kitap", t, sizeof t, room - (al ? al + 3 : 0));
	snprintf(out, (size_t) cap, "%s%s%s%s%s", t, al ? " - " : "", al ? a : "", el ? "." : "", e);
}

int opds_unique_path(const char *dir, const char *name, char *out, int cap)
{
	struct stat st;
	const char *dot = strrchr(name, '.');
	int stem = dot && dot != name ? (int) (dot - name) : (int) strlen(name);
	for (int i = 1; i < 1000; i++) {
		int n = i == 1 ? snprintf(out, (size_t) cap, "%s/%s", dir, name)
			       : snprintf(out, (size_t) cap, "%s/%.*s (%d)%s", dir, stem, name, i, name + stem);
		if (n < 0 || n >= cap) break;
		if (lstat(out, &st) != 0) return 0;
	}
	if (cap > 0) out[0] = '\0';
	return -1;
}

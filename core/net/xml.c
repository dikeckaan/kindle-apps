/* Tolerant XML/HTML text extraction for feeds (RSS, TCMB). Not a validating parser: it finds named
 * elements, reads attributes and turns element content into plain text. All reads stay inside [s, e). */
#include <stdlib.h>
#include <string.h>
#include "kui.h"
#include "net.h"

static int is_name_end(char c) { return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* finds "<tag" followed by a name terminator in [p, end) */
static const char *find_open(const char *p, const char *end, const char *tag, size_t tl)
{
	for (; p + tl + 1 < end; p++) {
		p = memchr(p, '<', (size_t) (end - p));
		if (!p || p + tl + 1 >= end) return NULL;
		if (memcmp(p + 1, tag, tl) == 0 && is_name_end(p[1 + tl])) return p;
	}
	return NULL;
}

const char *xml_next(const char *p, const char *end, const char *tag, XmlEl *el)
{
	size_t tl = strlen(tag);
	const char *o = find_open(p, end, tag, tl);
	if (!o) return NULL;
	const char *gt = memchr(o, '>', (size_t) (end - o));
	if (!gt) return NULL;
	el->attrs = o + 1 + tl;
	if (gt[-1] == '/') { /* <tag/> */
		el->attrs_end = gt - 1;
		el->inner = el->inner_end = gt + 1;
		return gt + 1;
	}
	el->attrs_end = gt;
	el->inner = gt + 1;
	/* matching close tag, allowing nested elements of the same name */
	int depth = 1;
	for (const char *q = gt + 1; q < end;) {
		const char *lt = memchr(q, '<', (size_t) (end - q));
		if (!lt) return NULL;
		if (lt + 9 <= end && memcmp(lt, "<![CDATA[", 9) == 0) { /* skip CDATA: it may contain '<' */
			const char *ce = lt + 9;
			for (; ce + 3 <= end && memcmp(ce, "]]>", 3) != 0; ce++) {}
			if (ce + 3 > end) return NULL;
			q = ce + 3;
			continue;
		}
		if (lt + 2 + tl <= end && lt[1] == '/' && memcmp(lt + 2, tag, tl) == 0 && lt + 2 + tl < end && is_name_end(lt[2 + tl])) {
			if (--depth == 0) {
				el->inner_end = lt;
				const char *cg = memchr(lt, '>', (size_t) (end - lt));
				return cg ? cg + 1 : NULL;
			}
		} else if (lt + 1 + tl < end && memcmp(lt + 1, tag, tl) == 0 && is_name_end(lt[1 + tl])) {
			const char *g = memchr(lt, '>', (size_t) (end - lt));
			if (!g) return NULL;
			if (g[-1] != '/') depth++;
		}
		q = lt + 1;
	}
	return NULL;
}

/* appends the UTF-8 bytes of one character if they fit (out keeps room for the NUL) */
static int put(char *out, int cap, int *o, const char *b, int n)
{
	if (*o + n >= cap) return 0;
	memcpy(out + *o, b, (size_t) n);
	*o += n;
	return 1;
}

/* decodes an entity at s ("&…;"); returns bytes consumed and the UTF-8 in u (*un bytes), or 0 if not one */
static int entity(const char *s, const char *e, char u[8], int *un)
{
	static const struct { const char *name; int cp; } named[] = {
		{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
		{ "#39", '\'' }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 }, { "rdquo", 0x201D }, { "ldquo", 0x201C },
		{ "hellip", 0x2026 }, { "ndash", 0x2013 }, { "mdash", 0x2014 },
	};
	const char *semi = NULL;
	for (const char *q = s + 1; q < e && q < s + 12; q++)
		if (*q == ';') { semi = q; break; }
	if (!semi) return 0;
	int len = (int) (semi - s - 1), cp = -1;
	if (len >= 2 && s[1] == '#') {
		char num[12];
		memcpy(num, s + 2, (size_t) (len - 1));
		num[len - 1] = '\0';
		char *endp;
		long v = (num[0] == 'x' || num[0] == 'X') ? strtol(num + 1, &endp, 16) : strtol(num, &endp, 10);
		if (*endp == '\0' && endp != num && v > 0 && v <= 0x10FFFF) cp = (int) v;
	} else {
		for (size_t i = 0; i < sizeof named / sizeof named[0]; i++)
			if ((int) strlen(named[i].name) == len && memcmp(named[i].name, s + 1, (size_t) len) == 0) cp = named[i].cp;
	}
	if (cp < 0) return 0;
	*un = kui_utf8_encode(cp, u);
	return len + 2;
}

/* length of the UTF-8 character starting at s (bounded by e) */
static int char_len(const char *s, const char *e)
{
	unsigned char c = (unsigned char) *s;
	int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	return s + n <= e ? n : (int) (e - s);
}

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int xml_text(const char *s, const char *e, char *out, int cap)
{
	int o = 0, full = 0;
	if (cap <= 0) return 0;
	while (s < e && is_ws(*s)) s++;
	while (s < e && !full) {
		if (e - s >= 9 && memcmp(s, "<![CDATA[", 9) == 0) {
			const char *c = s + 9;
			while (c < e && !(e - c >= 3 && memcmp(c, "]]>", 3) == 0)) {
				int n = char_len(c, e);
				if (!put(out, cap, &o, c, n)) { full = 1; break; }
				c += n;
			}
			s = c + 3 <= e ? c + 3 : e;
			continue;
		}
		char u[8];
		int un, k = *s == '&' ? entity(s, e, u, &un) : 0;
		if (k) {
			if (!put(out, cap, &o, u, un)) break;
			s += k;
			continue;
		}
		int n = char_len(s, e);
		if (!put(out, cap, &o, s, n)) break;
		s += n;
	}
	while (o > 0 && is_ws(out[o - 1])) o--;
	out[o] = '\0';
	return o;
}

static int tag_is(const char *name, int nl, const char *const *set)
{
	for (; *set; set++)
		if ((int) strlen(*set) == nl && strncasecmp(*set, name, (size_t) nl) == 0) return 1;
	return 0;
}

int html_to_text(const char *s, const char *e, char *out, int cap)
{
	static const char *const blocks[] = { "p", "br", "h1", "h2", "h3", "h4", "h5", "h6", "li", "div", "header", "footer",
					      "article", "section", "tr", "ul", "ol", "blockquote", "hr", NULL };
	static const char *const skips[] = { "script", "style", "figure", "noscript", NULL };
	int o = 0, space = 0, nl = 0;
	if (cap <= 0) return 0;
	while (s < e) {
		if (*s == '<') {
			if (e - s >= 4 && memcmp(s, "<!--", 4) == 0) {
				const char *c = s + 4;
				while (c < e && !(e - c >= 3 && memcmp(c, "-->", 3) == 0)) c++;
				s = c + 3 <= e ? c + 3 : e;
				continue;
			}
			const char *gt = memchr(s, '>', (size_t) (e - s));
			if (!gt) break;
			const char *nm = s + 1;
			int closing = *nm == '/';
			if (closing) nm++;
			int nlen = 0;
			while (nm + nlen < gt && !is_name_end(nm[nlen])) nlen++;
			if (!closing && tag_is(nm, nlen, skips) && gt[-1] != '/') {
				/* drop everything up to the matching close tag */
				const char *c = gt + 1;
				for (; c + nlen + 2 < e; c++)
					if (c[0] == '<' && c[1] == '/' && strncasecmp(c + 2, nm, (size_t) nlen) == 0) break;
				const char *cg = c < e ? memchr(c, '>', (size_t) (e - c)) : NULL;
				s = cg ? cg + 1 : e;
				continue;
			}
			if (tag_is(nm, nlen, blocks) && o > 0) nl = 1;
			s = gt + 1;
			continue;
		}
		if (is_ws(*s)) {
			if (o > 0) space = 1;
			s++;
			continue;
		}
		char u[8];
		int un = 0, k = *s == '&' ? entity(s, e, u, &un) : 0;
		const char *b = s;
		int n;
		if (k) {
			if (un == 2 && (unsigned char) u[0] == 0xC2 && (unsigned char) u[1] == 0xA0) { /* &nbsp; */
				if (o > 0) space = 1;
				s += k;
				continue;
			}
			b = u;
			n = un;
		} else {
			n = char_len(s, e);
			k = n;
		}
		if (nl) {
			if (!put(out, cap, &o, "\n", 1)) break;
		} else if (space) {
			if (!put(out, cap, &o, " ", 1)) break;
		}
		nl = space = 0;
		if (!put(out, cap, &o, b, n)) break;
		s += k;
	}
	out[o] = '\0';
	return o;
}

int xml_attr(const XmlEl *el, const char *name, char *out, int cap)
{
	size_t nl = strlen(name);
	for (const char *p = el->attrs; p + nl < el->attrs_end; p++) {
		if ((p == el->attrs || is_ws(p[-1])) && memcmp(p, name, nl) == 0 && p[nl] == '=') {
			const char *v = p + nl + 1;
			if (v >= el->attrs_end || (*v != '"' && *v != '\'')) return 0;
			char q = *v++;
			const char *ve = memchr(v, q, (size_t) (el->attrs_end - v));
			if (!ve) return 0;
			xml_text(v, ve, out, cap);
			return 1;
		}
	}
	return 0;
}

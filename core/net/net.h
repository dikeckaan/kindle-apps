/* net — HTTP via the system curl (no shell), and a small tolerant XML/HTML text extractor. */
#ifndef NET_H
#define NET_H

#define NET_MAX_BODY (2 * 1024 * 1024)

/* GET url with curl (path from $NET_CURL, default /usr/bin/curl). On success returns 0 and a malloc'd,
 * NUL-terminated *body (caller frees); on any failure (curl error, HTTP error, timeout, > NET_MAX_BODY)
 * returns -1 with *body = NULL. Never blocks longer than timeout_s + 2 seconds. */
int  net_get(const char *url, char **body, int *len, int timeout_s);
void net_urlencode(const char *in, char *out, int cap); /* RFC 3986: unreserved bytes kept, others %XX */

/* ---- XML: elements are found by name; attrs/inner point into the source ---- */
typedef struct { const char *attrs, *attrs_end, *inner, *inner_end; } XmlEl;
/* next <tag ...>…</tag> or <tag/> in [p, end); returns the position after it, or NULL (none / truncated) */
const char *xml_next(const char *p, const char *end, const char *tag, XmlEl *el);
int xml_attr(const XmlEl *el, const char *name, char *out, int cap); /* 1 if found (value entity-decoded) */
/* element text: CDATA unwrapped, entities decoded, surrounding whitespace trimmed; bounded, never splits UTF-8 */
int xml_text(const char *s, const char *e, char *out, int cap);
/* readable text from HTML: tags dropped, block tags become line breaks, whitespace collapsed,
 * script/style/figure content dropped */
int html_to_text(const char *s, const char *e, char *out, int cap);
#endif

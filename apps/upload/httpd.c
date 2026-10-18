/* Pure HTTP/multipart helpers for the Wi-Fi upload server (no sockets, no files). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "httpd.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ieq_n(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (lower((unsigned char) a[i]) != lower((unsigned char) b[i])) return 0;
	return 1;
}

/* finds "\r\n\r\n" in buf[0..len); returns the index just past it, or 0 */
static size_t find_end(const char *buf, size_t len)
{
	for (size_t i = 3; i < len; i++)
		if (buf[i] == '\n' && buf[i - 1] == '\r' && buf[i - 2] == '\n' && buf[i - 3] == '\r') return i + 1;
	return 0;
}

/* first "\r\n" in [p, end), or end */
static const char *find_crlf(const char *p, const char *end)
{
	for (; p + 1 < end; p++)
		if (p[0] == '\r' && p[1] == '\n') return p;
	return end;
}

static const char *trim_ws(const char *s, const char *end, const char **out_end)
{
	while (s < end && (*s == ' ' || *s == '\t')) s++;
	while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;
	*out_end = end;
	return s;
}

/* RFC 2046 bchars (plus space, not last) */
static int bchar(int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || strchr("'()+_,-./:=? ", c) != NULL;
}

/* Parses "param=value" pairs after the first ';' in [s, end). Calls back for each; quoted values are unquoted
 * (backslash escapes kept literal except \" and \\). Returns -1 on malformed input. */
typedef int (*param_fn)(void *ctx, const char *key, size_t klen, const char *val, size_t vlen);
static int each_param(const char *s, const char *end, param_fn fn, void *ctx)
{
	const char *p = memchr(s, ';', (size_t) (end - s));
	char val[1024];
	while (p && p < end) {
		p++;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		const char *k = p;
		while (p < end && *p != '=' && *p != ';') p++;
		const char *kend;
		k = trim_ws(k, p, &kend);
		size_t vl = 0;
		if (p < end && *p == '=') {
			p++;
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			if (p < end && *p == '"') {
				p++;
				while (p < end && *p != '"') {
					if (*p == '\\' && p + 1 < end && (p[1] == '"' || p[1] == '\\')) p++;
					if (vl + 1 >= sizeof val) return -1;
					val[vl++] = *p++;
				}
				if (p >= end) return -1;
				p++; /* closing quote */
				while (p < end && *p != ';') p++;
			} else {
				const char *v = p;
				while (p < end && *p != ';') p++;
				const char *vend;
				v = trim_ws(v, p, &vend);
				vl = (size_t) (vend - v);
				if (vl >= sizeof val) return -1;
				memcpy(val, v, vl);
			}
		}
		val[vl] = '\0';
		if (kend > k && fn(ctx, k, (size_t) (kend - k), val, vl) < 0) return -1;
		if (p >= end) break;
	}
	return 0;
}

static int ct_param(void *ctx, const char *k, size_t kl, const char *v, size_t vl)
{
	HttpHead *h = ctx;
	if (kl == 8 && ieq_n(k, "boundary", 8)) {
		if (vl < 1 || vl > 70 || v[vl - 1] == ' ') return -1;
		for (size_t i = 0; i < vl; i++)
			if (!bchar((unsigned char) v[i])) return -1;
		memcpy(h->boundary, v, vl);
		h->boundary[vl] = '\0';
	}
	return 0;
}

static int parse_header(HttpHead *h, const char *line, const char *end)
{
	if (line < end && (*line == ' ' || *line == '\t')) return -400; /* obsolete folding */
	const char *colon = memchr(line, ':', (size_t) (end - line));
	if (!colon || colon == line) return -400;
	for (const char *p = line; p < colon; p++)
		if (*p == ' ' || *p == '\t') return -400;
	size_t nl = (size_t) (colon - line);
	const char *vend, *v = trim_ws(colon + 1, end, &vend);
	size_t vl = (size_t) (vend - v);
	if (nl == 14 && ieq_n(line, "content-length", 14)) {
		if (vl == 0 || vl > 15) return -400;
		long long n = 0;
		for (size_t i = 0; i < vl; i++) {
			if (v[i] < '0' || v[i] > '9') return -400;
			n = n * 10 + (v[i] - '0');
		}
		if (h->content_length >= 0 && h->content_length != n) return -400;
		h->content_length = n;
	} else if (nl == 17 && ieq_n(line, "transfer-encoding", 17)) {
		h->chunked = 1;
	} else if (nl == 6 && ieq_n(line, "expect", 6)) {
		if (vl == 12 && ieq_n(v, "100-continue", 12)) h->expect_continue = 1;
	} else if (nl == 12 && ieq_n(line, "content-type", 12)) {
		const char *semi = memchr(v, ';', vl);
		const char *tend, *t = trim_ws(v, semi ? semi : vend, &tend);
		if ((size_t) (tend - t) == 19 && ieq_n(t, "multipart/form-data", 19)) {
			h->boundary[0] = '\0';
			if (each_param(v, vend, ct_param, h) < 0 || !h->boundary[0]) return -400;
			h->multipart = 1;
		}
	}
	return 0;
}

int httpd_parse_head(const char *buf, size_t len, HttpHead *h)
{
	size_t scan = len < HTTPD_MAX_HEAD ? len : HTTPD_MAX_HEAD;
	size_t end = find_end(buf, scan);
	if (!end) return len >= HTTPD_MAX_HEAD ? -431 : 0;
	memset(h, 0, sizeof *h);
	h->content_length = -1;
	if (memchr(buf, '\0', end)) return -400;
	/* request line: METHOD SP TARGET SP HTTP/1.x */
	const char *p = buf, *eol = find_crlf(buf, buf + end);
	const char *sp1 = memchr(p, ' ', (size_t) (eol - p));
	if (!sp1 || sp1 == p || (size_t) (sp1 - p) >= sizeof h->method) return -400;
	for (const char *q = p; q < sp1; q++)
		if (*q < 'A' || *q > 'Z') return -400;
	memcpy(h->method, p, (size_t) (sp1 - p));
	const char *t = sp1 + 1;
	const char *sp2 = memchr(t, ' ', (size_t) (eol - t));
	if (!sp2 || sp2 == t) return -400;
	if (sp2 - t > HTTPD_MAX_TARGET) return -414;
	if (*t != '/') return -400;
	const char *ver = sp2 + 1;
	if (eol - ver != 8 || memcmp(ver, "HTTP/1.", 7) != 0 || (ver[7] != '0' && ver[7] != '1')) {
		if (eol - ver >= 5 && memcmp(ver, "HTTP/", 5) == 0) return -505;
		return -400;
	}
	for (const char *q = t; q < sp2; q++)
		if ((unsigned char) *q <= ' ' || (unsigned char) *q >= 0x7f) return -400;
	const char *qm = memchr(t, '?', (size_t) (sp2 - t));
	const char *pend = qm ? qm : sp2;
	memcpy(h->path, t, (size_t) (pend - t));
	if (qm) memcpy(h->query, qm + 1, (size_t) (sp2 - qm - 1));
	/* headers */
	const char *line = eol + 2, *stop = buf + end - 2;
	while (line < stop) {
		const char *le = find_crlf(line, buf + end);
		for (const char *q = line; q < le; q++)
			if (*q == '\r' || *q == '\n') return -400;
		int rc = parse_header(h, line, le);
		if (rc < 0) return rc;
		line = le + 2;
	}
	return (int) end;
}

HttpRoute httpd_route(const HttpHead *h)
{
	int get = !strcmp(h->method, "GET") || !strcmp(h->method, "HEAD"), post = !strcmp(h->method, "POST");
	if (!strcmp(h->path, "/")) return get ? HTTPD_ROUTE_PAGE : HTTPD_ROUTE_BAD_METHOD;
	if (!strcmp(h->path, "/upload")) return post ? HTTPD_ROUTE_UPLOAD : HTTPD_ROUTE_BAD_METHOD;
	return HTTPD_ROUTE_NOT_FOUND;
}

int httpd_query_param(const char *q, const char *key, char *out, size_t cap)
{
	size_t kl = strlen(key);
	while (q && *q) {
		const char *amp = strchr(q, '&');
		size_t n = amp ? (size_t) (amp - q) : strlen(q);
		if (n > kl && q[kl] == '=' && !memcmp(q, key, kl)) {
			size_t vl = n - kl - 1;
			if (vl >= cap) return 0;
			memcpy(out, q + kl + 1, vl);
			out[vl] = '\0';
			return 1;
		}
		q = amp ? amp + 1 : NULL;
	}
	return 0;
}

int httpd_pin_equal(const char *pin, const char *given)
{
	if (!given) return 0;
	size_t gl = strnlen(given, 64);
	unsigned diff = gl != HTTPD_PIN_LEN;
	for (size_t i = 0; i < HTTPD_PIN_LEN; i++) {
		unsigned char g = i < gl ? (unsigned char) given[i] : 0;
		diff |= (unsigned) ((unsigned char) pin[i] ^ g);
	}
	return diff == 0;
}

/* length of a valid UTF-8 sequence at s (max n bytes), or 0 */
static int utf8_seq(const unsigned char *s, size_t n)
{
	int len;
	unsigned cp;
	if (s[0] < 0x80) return 1;
	if (s[0] >= 0xC2 && s[0] <= 0xDF) len = 2, cp = s[0] & 0x1F;
	else if (s[0] >= 0xE0 && s[0] <= 0xEF) len = 3, cp = s[0] & 0x0F;
	else if (s[0] >= 0xF0 && s[0] <= 0xF4) len = 4, cp = s[0] & 0x07;
	else return 0;
	if ((size_t) len > n) return 0;
	for (int i = 1; i < len; i++) {
		if ((s[i] & 0xC0) != 0x80) return 0;
		cp = (cp << 6) | (s[i] & 0x3F);
	}
	if ((len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
	return len;
}

int httpd_sanitize_name(const char *in, size_t len, char *out, size_t cap)
{
	if (cap == 0) return -1;
	out[0] = '\0';
	/* basename: after the last '/' or '\\' */
	size_t start = 0;
	for (size_t i = 0; i < len; i++)
		if (in[i] == '/' || in[i] == '\\') start = i + 1;
	char tmp[512];
	size_t n = 0;
	for (size_t i = start; i < len && n + 4 < sizeof tmp;) {
		unsigned char c = (unsigned char) in[i];
		if (c < 0x20 || c == 0x7f) { /* control characters and NUL are dropped */
			i++;
			continue;
		}
		if (c < 0x80) {
			tmp[n++] = strchr(":*?\"<>|", c) ? '_' : (char) c;
			i++;
			continue;
		}
		int k = utf8_seq((const unsigned char *) in + i, len - i);
		if (k == 0) {
			tmp[n++] = '_';
			i++;
			continue;
		}
		memcpy(tmp + n, in + i, (size_t) k);
		n += (size_t) k;
		i += (size_t) k;
	}
	/* trim leading/trailing dots and spaces (hidden files, Windows/FAT quirks, "." and "..") */
	size_t a = 0, b = n;
	while (a < b && (tmp[a] == '.' || tmp[a] == ' ')) a++;
	while (b > a && (tmp[b - 1] == '.' || tmp[b - 1] == ' ')) b--;
	if (a == b) return -1;
	size_t limit = HTTPD_MAX_NAME < cap - 1 ? HTTPD_MAX_NAME : cap - 1;
	size_t total = b - a;
	if (total <= limit) {
		memcpy(out, tmp + a, total);
		out[total] = '\0';
		return 0;
	}
	/* too long: keep the extension (if short), cut the stem at a code point boundary */
	const char *dot = NULL;
	for (size_t i = b; i > a; i--)
		if (tmp[i - 1] == '.') {
			dot = tmp + i - 1;
			break;
		}
	size_t ext = dot && (size_t) (tmp + b - dot) <= 10 ? (size_t) (tmp + b - dot) : 0;
	if (ext >= limit) ext = 0;
	size_t stem = limit - ext;
	while (stem > 0 && ((unsigned char) tmp[a + stem] & 0xC0) == 0x80) stem--; /* do not split a sequence */
	while (stem > 0 && tmp[a + stem - 1] == ' ') stem--;
	if (stem == 0) return -1;
	memcpy(out, tmp + a, stem);
	memcpy(out + stem, tmp + b - ext, ext);
	out[stem + ext] = '\0';
	return 0;
}

int httpd_ext_allowed(const char *name)
{
	static const char *const exts[] = { "epub", "pdf", "mobi", "azw", "azw3", "kfx", "txt", "cbz", "cbr",
					    "djvu", "fb2", "rtf", "doc", "docx", "html", "htm" };
	const char *dot = strrchr(name, '.');
	if (!dot || dot == name) return 0;
	size_t el = strlen(dot + 1);
	for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++)
		if (strlen(exts[i]) == el && ieq_n(dot + 1, exts[i], el)) return 1;
	return 0;
}

size_t httpd_html_escape(const char *in, char *out, size_t cap)
{
	size_t n = 0;
	if (cap == 0) return 0;
	for (; *in; in++) {
		const char *rep = NULL;
		char one[2] = { *in, 0 };
		switch (*in) {
		case '&': rep = "&amp;"; break;
		case '<': rep = "&lt;"; break;
		case '>': rep = "&gt;"; break;
		case '"': rep = "&quot;"; break;
		case '\'': rep = "&#39;"; break;
		default: rep = one;
		}
		size_t l = strlen(rep);
		if (n + l >= cap) break;
		memcpy(out + n, rep, l);
		n += l;
	}
	out[n] = '\0';
	return n;
}

void httpd_fmt_size(long long b, char *out, size_t cap)
{
	if (b < 1024) {
		snprintf(out, cap, "%lld B", b);
		return;
	}
	const char *unit = "KB";
	long long div = 1024;
	if (b >= 1024LL * 1024 * 1024) unit = "GB", div = 1024LL * 1024 * 1024;
	else if (b >= 1024LL * 1024) unit = "MB", div = 1024LL * 1024;
	long long tenths = (b * 10 + div / 2) / div;
	snprintf(out, cap, "%lld,%lld %s", tenths / 10, tenths % 10, unit);
}

static const char PAGE_HEAD[] =
	"<!doctype html><html lang=\"tr\"><head><meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>Kindle'a yükle</title><style>"
	"body{font-family:system-ui,-apple-system,sans-serif;max-width:32rem;margin:0 auto;padding:1rem;"
	"background:#f4f1ea;color:#1a1a1a;line-height:1.45}"
	"h1{font-size:1.5rem;margin:.5rem 0 1rem}"
	"form{background:#fff;border:1px solid #ccc;border-radius:.5rem;padding:1rem}"
	"label{display:block;font-weight:600;margin:.75rem 0 .3rem}"
	"input[type=text]{font-size:1.6rem;letter-spacing:.3rem;width:9rem;padding:.3rem .5rem}"
	"input[type=file]{width:100%;font-size:1rem}"
	"button{margin-top:1rem;width:100%;font-size:1.2rem;padding:.7rem;border:0;border-radius:.4rem;"
	"background:#1a1a1a;color:#fff}"
	".msg{background:#fff;border-left:4px solid #1a1a1a;padding:.6rem .8rem;margin-bottom:1rem}"
	"small{color:#555}</style></head><body><h1>Kindle'a kitap gönder</h1>";
static const char PAGE_FORM[] =
	"<form method=\"post\" action=\"/upload\" enctype=\"multipart/form-data\">"
	"<label for=\"pin\">Kindle ekranındaki PIN</label>"
	"<input type=\"text\" id=\"pin\" name=\"pin\" inputmode=\"numeric\" pattern=\"[0-9]{6}\" maxlength=\"6\" "
	"autocomplete=\"off\" required>"
	"<label for=\"file\">Dosyalar</label>"
	"<input type=\"file\" id=\"file\" name=\"file\" multiple required "
	"accept=\".epub,.pdf,.mobi,.azw,.azw3,.kfx,.txt,.cbz,.cbr,.djvu,.fb2,.rtf,.doc,.docx,.html,.htm\">"
	"<button type=\"submit\">Gönder</button></form>"
	"<p><small>EPUB, PDF, MOBI, AZW3, KFX, TXT, CBZ, CBR, DJVU, FB2, RTF, DOC, DOCX, HTML. "
	"Dosya başına en fazla 200 MB. Gönderim bitene kadar bu sayfayı kapatmayın.</small></p></body></html>";

int httpd_page(char *out, size_t cap, const char *msg_html)
{
	int n = snprintf(out, cap, "%s%s%s%s%s", PAGE_HEAD, msg_html ? "<div class=\"msg\">" : "", msg_html ? msg_html : "",
			 msg_html ? "</div>" : "", PAGE_FORM);
	return n < 0 || (size_t) n >= cap ? -1 : n;
}

const char *httpd_status_text(int st)
{
	switch (st) {
	case 100: return "Continue";
	case 200: return "OK";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 408: return "Request Timeout";
	case 411: return "Length Required";
	case 413: return "Content Too Large";
	case 414: return "URI Too Long";
	case 415: return "Unsupported Media Type";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	case 505: return "HTTP Version Not Supported";
	case 507: return "Insufficient Storage";
	default: return "Error";
	}
}

int httpd_response(char *out, size_t cap, int status, const char *body, size_t blen)
{
	int n = snprintf(out, cap,
			 "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\n"
			 "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
			 "Referrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n"
			 "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'\r\n\r\n",
			 status, httpd_status_text(status), blen);
	if (n < 0 || (size_t) n + blen >= cap) return -1;
	memcpy(out + n, body, blen);
	out[n + blen] = '\0';
	return n + (int) blen;
}

/* ---------------- multipart ---------------- */

enum { MP_PREAMBLE, MP_AFTER, MP_HEADERS, MP_DATA, MP_DONE_ST, MP_ERR_ST, MP_ABORT_ST };

void mp_init(Multipart *m, const char *boundary)
{
	memset(m, 0, sizeof *m);
	m->dlen = snprintf(m->delim, sizeof m->delim, "\r\n--%s", boundary);
	m->state = MP_PREAMBLE;
	m->match = 2; /* the body may start with the delimiter itself: pretend a CRLF came before it */
	m->after = MP_PREAMBLE;
}

int mp_done(const Multipart *m) { return m->state == MP_DONE_ST; }

typedef struct {
	char name[256];
	char filename[512];
	size_t flen;
	int has_name, has_file;
} PartHdr;

static int disp_param(void *ctx, const char *k, size_t kl, const char *v, size_t vl)
{
	PartHdr *ph = ctx;
	if (kl == 4 && ieq_n(k, "name", 4)) {
		if (vl >= sizeof ph->name) return -1;
		memcpy(ph->name, v, vl);
		ph->name[vl] = '\0';
		ph->has_name = 1;
	} else if (kl == 8 && ieq_n(k, "filename", 8)) {
		if (vl >= sizeof ph->filename) return -1;
		memcpy(ph->filename, v, vl);
		ph->filename[vl] = '\0';
		ph->flen = vl;
		ph->has_file = 1;
	}
	return 0;
}

/* parses part headers in m->hbuf[0..hlen) (ends with CRLFCRLF) and calls part_begin */
static int part_headers(Multipart *m, const MpCallbacks *cb, void *ctx)
{
	PartHdr ph;
	memset(&ph, 0, sizeof ph);
	const char *p = m->hbuf, *stop = m->hbuf + m->hlen - 2;
	if (memchr(m->hbuf, '\0', (size_t) m->hlen)) return MP_ERR;
	int seen = 0;
	while (p < stop) {
		const char *le = p;
		while (le + 1 < stop + 2 && !(le[0] == '\r' && le[1] == '\n')) le++;
		const char *colon = memchr(p, ':', (size_t) (le - p));
		if (!colon) return MP_ERR;
		if (colon - p == 19 && ieq_n(p, "content-disposition", 19)) {
			const char *vend, *v = trim_ws(colon + 1, le, &vend);
			if (vend - v < 9 || !ieq_n(v, "form-data", 9)) return MP_ERR;
			if (each_param(v, vend, disp_param, &ph) < 0) return MP_ERR;
			seen = 1;
		}
		p = le + 2;
	}
	if (!seen || !ph.has_name) return MP_ERR;
	if (cb->part_begin(ctx, ph.name, ph.has_file ? ph.filename : NULL, ph.flen) < 0) return MP_ABORT;
	return 0;
}

static int emit(Multipart *m, const char *d, size_t n, const MpCallbacks *cb, void *ctx)
{
	if (n == 0 || m->state != MP_DATA) return 0;
	return cb->part_data(ctx, d, n) < 0 ? MP_ABORT : 0;
}

static int fail(Multipart *m, int rc)
{
	m->state = rc == MP_ABORT ? MP_ABORT_ST : MP_ERR_ST;
	return rc;
}

/* scanning state (PREAMBLE or DATA): look for the delimiter; m->match bytes of it are pending */
static int scan(Multipart *m, const char *d, size_t n, size_t *used, const MpCallbacks *cb, void *ctx)
{
	size_t i = 0;
	while (i < n) {
		if (m->match == 0) {
			const char *cr = memchr(d + i, '\r', n - i);
			size_t end = cr ? (size_t) (cr - d) : n;
			int rc = emit(m, d + i, end - i, cb, ctx);
			if (rc) return rc;
			i = end;
			if (!cr) break;
			m->match = 1;
			i++;
			continue;
		}
		char c = d[i];
		if (m->delim[m->match] == c) {
			m->match++;
			i++;
			if (m->match == m->dlen) {
				*used = i;
				return 1; /* full delimiter */
			}
			continue;
		}
		/* mismatch: keep the longest pending suffix that is still a delimiter prefix */
		int j, mm = m->match;
		for (j = 1; j <= mm; j++)
			if (memcmp(m->delim + j, m->delim, (size_t) (mm - j)) == 0 && m->delim[mm - j] == c) break;
		if (j <= mm) {
			int rc = emit(m, m->delim, (size_t) j, cb, ctx);
			if (rc) return rc;
			m->match = mm - j + 1;
		} else {
			int rc = emit(m, m->delim, (size_t) mm, cb, ctx);
			if (!rc) rc = emit(m, &c, 1, cb, ctx);
			if (rc) return rc;
			m->match = 0;
		}
		i++;
	}
	*used = n;
	return 0;
}

int mp_feed(Multipart *m, const char *d, size_t n, const MpCallbacks *cb, void *ctx)
{
	size_t i = 0;
	while (i < n) {
		switch (m->state) {
		case MP_ERR_ST: return MP_ERR;
		case MP_ABORT_ST: return MP_ABORT;
		case MP_DONE_ST: return 0; /* epilogue is ignored */
		case MP_PREAMBLE:
		case MP_DATA: {
			size_t used = 0;
			int rc = scan(m, d + i, n - i, &used, cb, ctx);
			if (rc < 0) return fail(m, rc);
			i += used;
			if (rc == 1) {
				m->after = m->state;
				m->state = MP_AFTER;
				m->hlen = 0;
				m->match = 0;
			}
			break;
		}
		case MP_AFTER: {
			/* after a delimiter: "--" (close), or optional spaces/tabs then CRLF (next part) */
			char c = d[i++];
			m->hbuf[m->hlen++] = c;
			const char *h = m->hbuf;
			int hl = m->hlen, verdict = 0; /* 0 need more, 1 close, 2 next part, -1 not a delimiter */
			if (h[0] == '-') verdict = hl < 2 ? 0 : h[1] == '-' ? 1 : -1;
			else {
				int k = 0;
				while (k < hl && (h[k] == ' ' || h[k] == '\t')) k++;
				if (k == hl) verdict = hl > 32 ? -1 : 0;
				else if (h[k] != '\r') verdict = -1;
				else if (k + 1 == hl) verdict = 0;
				else verdict = h[k + 1] == '\n' ? 2 : -1;
			}
			if (verdict == 0) break;
			int prev = m->after;
			if (verdict < 0) {
				/* boundary-like data: the delimiter and the held bytes were content after all */
				char held[40];
				int hn = m->hlen;
				memcpy(held, m->hbuf, (size_t) hn);
				m->state = prev;
				int rc = emit(m, m->delim, (size_t) m->dlen, cb, ctx);
				if (rc) return fail(m, rc);
				m->match = 0;
				size_t used;
				rc = scan(m, held, (size_t) hn, &used, cb, ctx);
				if (rc < 0) return fail(m, rc);
				/* held bytes never hold a whole delimiter (spaces/dashes + one other byte) */
				break;
			}
			if (prev == MP_DATA && cb->part_end(ctx) < 0) return fail(m, MP_ABORT);
			if (verdict == 1) {
				m->state = MP_DONE_ST;
				return 0;
			}
			m->state = MP_HEADERS;
			m->hlen = 0;
			break;
		}
		case MP_HEADERS: {
			if (m->hlen >= (int) sizeof m->hbuf) return fail(m, MP_ERR);
			m->hbuf[m->hlen++] = d[i++];
			int hl = m->hlen;
			if (hl == 2 && m->hbuf[0] == '\r' && m->hbuf[1] == '\n') return fail(m, MP_ERR); /* no headers */
			if (hl >= 4 && !memcmp(m->hbuf + hl - 4, "\r\n\r\n", 4)) {
				int rc = part_headers(m, cb, ctx);
				if (rc) return fail(m, rc);
				m->state = MP_DATA;
				m->match = 0;
			}
			break;
		}
		}
	}
	return m->state == MP_ERR_ST ? MP_ERR : m->state == MP_ABORT_ST ? MP_ABORT : 0;
}

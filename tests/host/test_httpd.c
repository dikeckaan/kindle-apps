#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "t.h"
#include "upload/httpd.h"
#include "upload/server.h"

/* ---------------- request head ---------------- */

static void test_head_basic(void)
{
	HttpHead h;
	const char *r = "GET /?a=1 HTTP/1.1\r\nHost: k\r\n\r\nextra";
	int n = httpd_parse_head(r, strlen(r), &h);
	T_EQ_INT(n, (int) strlen(r) - 5);
	T_EQ_STR(h.method, "GET");
	T_EQ_STR(h.path, "/");
	T_EQ_STR(h.query, "a=1");
	T_EQ_INT(h.content_length, -1);
	T_EQ_INT(httpd_route(&h), HTTPD_ROUTE_PAGE);
	/* incomplete: every prefix needs more */
	for (size_t k = 0; k < strlen(r) - 5; k++) T_EQ_INT(httpd_parse_head(r, k, &h), 0);
}

static void test_head_upload(void)
{
	HttpHead h;
	const char *r = "POST /upload?pin=123456 HTTP/1.1\r\ncontent-type: multipart/form-data; boundary=\"--xyz 1\"\r\n"
			"Content-Length: 1234\r\nExpect: 100-continue\r\n\r\n";
	T_EQ_INT(httpd_parse_head(r, strlen(r), &h), (int) strlen(r));
	T_EQ_INT(httpd_route(&h), HTTPD_ROUTE_UPLOAD);
	T_EQ_INT(h.multipart, 1);
	T_EQ_STR(h.boundary, "--xyz 1");
	T_EQ_INT(h.content_length, 1234);
	T_EQ_INT(h.expect_continue, 1);
	char pin[16];
	T_EQ_INT(httpd_query_param(h.query, "pin", pin, sizeof pin), 1);
	T_EQ_STR(pin, "123456");
	T_EQ_INT(httpd_query_param("xpin=1&pin=22", "pin", pin, sizeof pin), 1);
	T_EQ_STR(pin, "22");
	T_EQ_INT(httpd_query_param("pinx=1", "pin", pin, sizeof pin), 0);
	T_EQ_INT(httpd_query_param("pin=12345678901234567890", "pin", pin, sizeof pin), 0);
}

static void test_head_limits(void)
{
	HttpHead h;
	static char big[HTTPD_MAX_HEAD + 100];
	/* a header block that never ends: need more until the limit, then 431 */
	strcpy(big, "GET / HTTP/1.1\r\n");
	size_t n = strlen(big);
	while (n < sizeof big - 3) {
		const char *ln = "X-A: bbbbbbbbbbbbbbbbbbbbbbbbbbbb\r\n";
		memcpy(big + n, ln, strlen(ln));
		n += strlen(ln);
		if (n > sizeof big - 40) break;
	}
	T_EQ_INT(httpd_parse_head(big, HTTPD_MAX_HEAD - 1, &h), 0);
	T_EQ_INT(httpd_parse_head(big, HTTPD_MAX_HEAD, &h), -431);
	T_EQ_INT(httpd_parse_head(big, n, &h), -431);
	/* a head that ends exactly at the limit is fine */
	memcpy(big + HTTPD_MAX_HEAD - 4, "\r\n\r\n", 4);
	int r = httpd_parse_head(big, HTTPD_MAX_HEAD, &h);
	T_ASSERT(r == HTTPD_MAX_HEAD || r == -400);
	/* a long target → 414 */
	static char lt[3000];
	strcpy(lt, "GET /");
	memset(lt + 5, 'a', 1500);
	strcpy(lt + 1505, " HTTP/1.1\r\n\r\n");
	T_EQ_INT(httpd_parse_head(lt, strlen(lt), &h), -414);

	const char *bad[] = {
		"GET / HTTP/2.0\r\n\r\n",
		"GARBAGE\r\n\r\n",
		"GET  / HTTP/1.1\r\n\r\n",
		"GET / HTTP/1.1\r\nNoColon\r\n\r\n",
		"GET / HTTP/1.1\r\nA: b\r\n folded\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Length: 12a\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Length: 99999999999999999999999\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Type: multipart/form-data\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=\r\n\r\n",
		"POST /upload HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n",
		"GET relative HTTP/1.1\r\n\r\n",
	};
	const int want[] = { -505, -400, -400, -400, -400, -400, -400, -400, -400, -400, -400, -400, -400 };
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		int got = httpd_parse_head(bad[i], strlen(bad[i]), &h);
		if (got != want[i]) T_FAIL_("bad[%u] -> %d, expected %d", i, got, want[i]);
	}
	/* NUL inside the head */
	const char nul[] = "GET / HTTP/1.1\r\nA: b\0c\r\n\r\n";
	T_EQ_INT(httpd_parse_head(nul, sizeof nul - 1, &h), -400);
	/* chunked bodies are flagged (the server refuses them) */
	const char *ch = "POST /upload HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
	T_EQ_INT(httpd_parse_head(ch, strlen(ch), &h), (int) strlen(ch));
	T_EQ_INT(h.chunked, 1);
}

static void test_route(void)
{
	HttpHead h;
	struct { const char *req; HttpRoute r; } c[] = {
		{ "GET / HTTP/1.1\r\n\r\n", HTTPD_ROUTE_PAGE },
		{ "HEAD / HTTP/1.0\r\n\r\n", HTTPD_ROUTE_PAGE },
		{ "POST /upload HTTP/1.1\r\n\r\n", HTTPD_ROUTE_UPLOAD },
		{ "GET /upload HTTP/1.1\r\n\r\n", HTTPD_ROUTE_BAD_METHOD },
		{ "POST / HTTP/1.1\r\n\r\n", HTTPD_ROUTE_BAD_METHOD },
		{ "GET /etc/passwd HTTP/1.1\r\n\r\n", HTTPD_ROUTE_NOT_FOUND },
		{ "GET /../ HTTP/1.1\r\n\r\n", HTTPD_ROUTE_NOT_FOUND },
		{ "GET /upload/x HTTP/1.1\r\n\r\n", HTTPD_ROUTE_NOT_FOUND },
	};
	for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
		T_ASSERT(httpd_parse_head(c[i].req, strlen(c[i].req), &h) > 0);
		if (httpd_route(&h) != c[i].r) T_FAIL_("route %u: %d", i, (int) httpd_route(&h));
	}
}

/* ---------------- names, extensions, PIN, HTML ---------------- */

static void san(const char *in, size_t len, int want_rc, const char *want)
{
	char out[256] = "?";
	int rc = httpd_sanitize_name(in, len, out, sizeof out);
	if (rc != want_rc) T_FAIL_("sanitize(%s) rc %d", in, rc);
	else if (rc == 0 && strcmp(out, want) != 0) T_FAIL_("sanitize(%s) = \"%s\", expected \"%s\"", in, out, want);
}
#define SAN(s, rc, w) san(s, sizeof(s) - 1, rc, w)

static void test_sanitize(void)
{
	SAN("kitap.epub", 0, "kitap.epub");
	SAN("../../etc/passwd", 0, "passwd");
	SAN("..\\x.epub", 0, "x.epub");
	SAN("C:\\Users\\a\\b.pdf", 0, "b.pdf");
	SAN("/abs.epub", 0, "abs.epub");
	SAN("%2e%2e", 0, "%2e%2e");
	SAN("%2e%2e%2fx.epub", 0, "%2e%2e%2fx.epub");
	SAN("a\0b.epub", 0, "ab.epub");
	SAN("../\0../a.epub", 0, "a.epub");
	SAN("kitap şiir.epub", 0, "kitap şiir.epub");
	SAN("Çağrı Öğüt İı.pdf", 0, "Çağrı Öğüt İı.pdf");
	SAN("a\tb\r\nc\x7f.txt", 0, "abc.txt");
	SAN("a:b*c?d\"e<f>g|h.txt", 0, "a_b_c_d_e_f_g_h.txt");
	SAN("bad\xff\xfe.epub", 0, "bad__.epub");
	SAN("  .hidden.epub. ", 0, "hidden.epub");
	SAN(".", -1, "");
	SAN("..", -1, "");
	SAN("", -1, "");
	SAN("a/", -1, "");
	SAN("dir/..", -1, "");
	SAN("...", -1, "");
	SAN("\x01\x02", -1, "");
	/* long names are cut to 120 bytes at a code point boundary, keeping the extension */
	char lng[400], out[256];
	strcpy(lng, "");
	for (int i = 0; i < 100; i++) strcat(lng, "ş");
	strcat(lng, ".epub");
	T_EQ_INT(httpd_sanitize_name(lng, strlen(lng), out, sizeof out), 0);
	T_ASSERT(strlen(out) <= HTTPD_MAX_NAME);
	T_ASSERT(strlen(out) >= HTTPD_MAX_NAME - 1);
	size_t ol = strlen(out);
	T_ASSERT(ol > 5 && strcmp(out + ol - 5, ".epub") == 0);
	T_ASSERT((unsigned char) out[ol - 6] == 0x9F); /* last ş complete (C5 9F) */
	/* small output buffer */
	char tiny[8];
	T_EQ_INT(httpd_sanitize_name("abcdefghij.epub", 15, tiny, sizeof tiny), 0);
	T_ASSERT(strlen(tiny) < sizeof tiny);
}

static void test_ext(void)
{
	const char *ok[] = { "a.epub", "a.PDF", "a.mobi", "a.azw", "a.AZW3", "a.kfx", "a.txt", "a.cbz", "a.cbr",
			     "a.djvu", "a.fb2", "a.rtf", "a.doc", "a.docx", "a.html", "a.htm", "kitap şiir.Epub", "abonelikler.opml" };
	for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++)
		if (!httpd_ext_allowed(ok[i])) T_FAIL_("%s refused", ok[i]);
	const char *no[] = { "a.sh", "a.so", "a", "epub", ".epub", "a.epub.sh", "a.exe", "a.", "a.zip", "a.epubx",
			     "%2e%2e", "a.bin", "a.azw4" };
	for (unsigned i = 0; i < sizeof no / sizeof no[0]; i++)
		if (httpd_ext_allowed(no[i])) T_FAIL_("%s allowed", no[i]);
}

static void test_native_ext(void)
{
	/* what the Kindle's own library lists; everything else opens in KOReader */
	T_EQ_INT(httpd_kindle_native("kitap.azw3"), 1);
	T_EQ_INT(httpd_kindle_native("KITAP.PDF"), 1);
	T_EQ_INT(httpd_kindle_native("a.mobi"), 1);
	T_EQ_INT(httpd_kindle_native("a.txt"), 1);
	T_EQ_INT(httpd_kindle_native("a.kfx"), 1);
	T_EQ_INT(httpd_kindle_native("quran.epub"), 0);
	T_EQ_INT(httpd_kindle_native("a.cbz"), 0);
	T_EQ_INT(httpd_kindle_native("noext"), 0);
}

static void test_pin(void)
{
	T_EQ_INT(httpd_pin_equal("012345", "012345"), 1);
	T_EQ_INT(httpd_pin_equal("012345", "012346"), 0);
	T_EQ_INT(httpd_pin_equal("012345", "01234"), 0);
	T_EQ_INT(httpd_pin_equal("012345", "0123456"), 0);
	T_EQ_INT(httpd_pin_equal("012345", ""), 0);
	T_EQ_INT(httpd_pin_equal("012345", NULL), 0);
}

static void test_html(void)
{
	char out[128];
	httpd_html_escape("<a href=\"x\">&'</a>", out, sizeof out);
	T_EQ_STR(out, "&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;");
	char small[8];
	httpd_html_escape("ab<<<", small, sizeof small); /* "ab&lt;" fits, the next entity does not */
	T_EQ_STR(small, "ab&lt;");
	char sz[32];
	httpd_fmt_size(512, sz, sizeof sz);
	T_EQ_STR(sz, "512 B");
	httpd_fmt_size(12595, sz, sizeof sz);
	T_EQ_STR(sz, "12,3 KB");
	httpd_fmt_size(1258291, sz, sizeof sz);
	T_EQ_STR(sz, "1,2 MB");
	httpd_fmt_size(200LL * 1024 * 1024, sz, sizeof sz);
	T_EQ_STR(sz, "200,0 MB");
	static char page[16384];
	int n = httpd_page(page, sizeof page, "<b>Alındı</b>");
	T_ASSERT(n > 0);
	T_ASSERT(strstr(page, "name=\"pin\"") != NULL);
	T_ASSERT(strstr(page, "type=\"file\"") != NULL);
	T_ASSERT(strstr(page, "multipart/form-data") != NULL);
	T_ASSERT(strstr(page, "<b>Alındı</b>") != NULL);
	T_ASSERT(strstr(page, "<script") == NULL);
	static char resp[20000];
	int rn = httpd_response(resp, sizeof resp, 404, "x", 1);
	T_ASSERT(rn > 0 && strncmp(resp, "HTTP/1.1 404 Not Found\r\n", 24) == 0);
	T_ASSERT(strstr(resp, "Content-Length: 1\r\n") != NULL);
	T_ASSERT(strstr(resp, "Connection: close\r\n") != NULL);
	T_EQ_INT(httpd_response(resp, 50, 200, page, (size_t) n), -1);
}

/* ---------------- multipart ---------------- */

typedef struct {
	char log[8192];
	size_t len;
	int abort_on_file;
} Rec;

static void rec_add(Rec *r, const char *s, size_t n)
{
	if (r->len + n < sizeof r->log) {
		memcpy(r->log + r->len, s, n);
		r->len += n;
	}
}
static int rb(void *ctx, const char *name, const char *fn, size_t fnlen)
{
	Rec *r = ctx;
	rec_add(r, "[B:", 3);
	rec_add(r, name, strlen(name));
	if (fn) {
		rec_add(r, "|", 1);
		rec_add(r, fn, fnlen);
		if (r->abort_on_file) return -1;
	}
	rec_add(r, "]", 1);
	return 0;
}
static int rd(void *ctx, const char *d, size_t n)
{
	if (n == 0) T_FAIL_("%s", "empty data callback");
	rec_add(ctx, d, n);
	return 0;
}
static int re(void *ctx)
{
	rec_add(ctx, "[E]", 3);
	return 0;
}
static const MpCallbacks CB = { rb, rd, re };

#define BND "----kBnd42"
static const char BODY[] = "preamble junk\r\n"
			   "--" BND "\r\n"
			   "Content-Disposition: form-data; name=\"pin\"\r\n\r\n"
			   "123456\r\n"
			   "--" BND "  \r\n" /* transport padding */
			   "Content-Disposition: form-data; name=\"file\"; filename=\"a b.epub\"\r\n"
			   "Content-Type: application/epub+zip\r\n\r\n"
			   /* boundary-like data: prefixes, a delimiter followed by junk, the boundary without CRLF */
			   "\r\n\r\r\n-\r\n--\r\n--" BND "x\r\nzz--" BND "-- not\r\r\n--" "----kBnd4\r\n--" BND "-Q\r\n--" BND " \tX"
			   "\r\n--" BND "\r\n" /* a real delimiter: the file data ends before it */
			   "Content-Disposition: form-data; name=\"empty\"\r\n\r\n"
			   "\r\n--" BND "--\r\nepilogue --" BND "\r\n";
static const char WANT[] = "[B:pin]123456[E]"
			   "[B:file|a b.epub]\r\n\r\r\n-\r\n--\r\n--" BND "x\r\nzz--" BND "-- not\r\r\n--" "----kBnd4\r\n--" BND "-Q\r\n--" BND " \tX[E]"
			   "[B:empty][E]";

static int feed_split(const char *body, size_t len, const size_t *cuts, int ncuts, Rec *r)
{
	Multipart m;
	mp_init(&m, BND);
	memset(r, 0, sizeof *r);
	size_t prev = 0;
	for (int i = 0; i <= ncuts; i++) {
		size_t end = i < ncuts ? cuts[i] : len;
		int rc = mp_feed(&m, body + prev, end - prev, &CB, r);
		if (rc != 0) return rc;
		prev = end;
	}
	return mp_done(&m) ? 0 : 1;
}

static void test_multipart_splits(void)
{
	size_t len = sizeof BODY - 1;
	Rec r;
	const char *want = WANT;

	T_EQ_INT(feed_split(BODY, len, NULL, 0, &r), 0);
	r.log[r.len] = '\0';
	T_EQ_STR(r.log, want);
	/* every single cut point */
	for (size_t c = 0; c <= len; c++) {
		size_t cuts[1] = { c };
		int rc = feed_split(BODY, len, cuts, 1, &r);
		r.log[r.len] = '\0';
		if (rc != 0 || strcmp(r.log, want) != 0) {
			T_FAIL_("cut at %zu: rc %d log %s", c, rc, r.log);
			break;
		}
	}
	/* every pair of cut points */
	int bad = 0;
	for (size_t a = 0; a <= len && !bad; a++)
		for (size_t b = a; b <= len && !bad; b++) {
			size_t cuts[2] = { a, b };
			int rc = feed_split(BODY, len, cuts, 2, &r);
			r.log[r.len] = '\0';
			if (rc != 0 || strcmp(r.log, want) != 0) {
				T_FAIL_("cuts %zu,%zu: rc %d", a, b, rc);
				bad = 1;
			}
		}
	/* one byte at a time */
	static size_t ones[sizeof BODY];
	for (size_t i = 0; i < len; i++) ones[i] = i + 1;
	T_EQ_INT(feed_split(BODY, len, ones, (int) len - 1, &r), 0);
	r.log[r.len] = '\0';
	T_EQ_STR(r.log, want);
}

static void test_multipart_errors(void)
{
	Multipart m;
	Rec r;
	/* body without the closing delimiter is not done */
	const char *trunc = "--" BND "\r\nContent-Disposition: form-data; name=\"a\"\r\n\r\nxyz";
	mp_init(&m, BND);
	memset(&r, 0, sizeof r);
	T_EQ_INT(mp_feed(&m, trunc, strlen(trunc), &CB, &r), 0);
	T_EQ_INT(mp_done(&m), 0);
	/* junk right after a delimiter in the preamble is still preamble */
	const char *junk = "--" BND "XX\r\n";
	mp_init(&m, BND);
	T_EQ_INT(mp_feed(&m, junk, strlen(junk), &CB, &r), 0);
	T_EQ_INT(mp_done(&m), 0);
	/* part without Content-Disposition name */
	const char *nodisp = "--" BND "\r\nContent-Type: text/plain\r\n\r\nx\r\n--" BND "--";
	mp_init(&m, BND);
	T_EQ_INT(mp_feed(&m, nodisp, strlen(nodisp), &CB, &r), MP_ERR);
	T_EQ_INT(mp_feed(&m, "a", 1, &CB, &r), MP_ERR); /* sticky */
	/* oversized part headers */
	static char big[4096];
	snprintf(big, sizeof big, "--" BND "\r\nX: ");
	memset(big + strlen(big), 'a', 3000);
	mp_init(&m, BND);
	T_EQ_INT(mp_feed(&m, big, strlen(big), &CB, &r), MP_ERR);
	/* callback abort */
	const char *f = "--" BND "\r\nContent-Disposition: form-data; name=\"f\"; filename=\"x.epub\"\r\n\r\ndata\r\n--" BND "--";
	mp_init(&m, BND);
	memset(&r, 0, sizeof r);
	r.abort_on_file = 1;
	T_EQ_INT(mp_feed(&m, f, strlen(f), &CB, &r), MP_ABORT);
	/* filename with NUL inside reaches the callback with its full length; unquoted values work */
	const char fn[] = "--" BND "\r\nContent-Disposition: form-data; name=f; filename=\"a\0b.txt\"\r\n\r\n\r\n--" BND "--";
	mp_init(&m, BND);
	memset(&r, 0, sizeof r);
	T_EQ_INT(mp_feed(&m, fn, sizeof fn - 1, &CB, &r), MP_ERR); /* NUL in part headers is refused */
	const char fn2[] = "--" BND "\r\ncontent-disposition: form-data; filename=\"x;y=z.txt\"; name=f\r\n\r\n\r\n--" BND "--";
	mp_init(&m, BND);
	memset(&r, 0, sizeof r);
	T_EQ_INT(mp_feed(&m, fn2, sizeof fn2 - 1, &CB, &r), 0);
	r.log[r.len] = '\0';
	T_EQ_STR(r.log, "[B:f|x;y=z.txt][E]");
}

/* ---------------- end to end against the real server ---------------- */

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int connect_to(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { 0 };
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short) port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr *) &a, sizeof a) != 0) {
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

static char resp[65536];

/* sends req, polling the server in between, until the server closes; returns the HTTP status (0 = none) */
static int exchange(UploadServer *s, int fd, const char *req, size_t len)
{
	size_t off = 0, got = 0;
	long t0 = now_ms();
	while (now_ms() - t0 < 10000) {
		if (off < len) {
			ssize_t w = send(fd, req + off, len - off > 4096 ? 4096 : len - off, 0);
			if (w > 0) off += (size_t) w;
			else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) off = len;
		}
		upload_poll(s, 2);
		ssize_t r = recv(fd, resp + got, sizeof resp - 1 - got, 0);
		if (r > 0) got += (size_t) r;
		else if (r == 0) break;
		else if (errno != EAGAIN && errno != EWOULDBLOCK) break;
	}
	resp[got] = '\0';
	close(fd);
	int st = 0;
	if (sscanf(resp, "HTTP/1.1 %d", &st) != 1) st = 0;
	return st;
}

static int roundtrip(UploadServer *s, const char *req, size_t len)
{
	int fd = connect_to(upload_port(s));
	if (fd < 0) return -1;
	return exchange(s, fd, req, len);
}

/* builds a multipart upload request; field_pin and query_pin may be NULL */
static char *build(const char *query_pin, const char *field_pin, const char *filename, const char *data, size_t dlen, size_t *out_len)
{
	size_t cap = dlen + 4096;
	char *body = malloc(cap), *req = malloc(cap + 1024);
	size_t n = 0;
	if (field_pin)
		n += (size_t) snprintf(body + n, cap - n, "--" BND "\r\nContent-Disposition: form-data; name=\"pin\"\r\n\r\n%s\r\n", field_pin);
	n += (size_t) snprintf(body + n, cap - n,
			       "--" BND "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
			       "Content-Type: application/octet-stream\r\n\r\n", filename);
	memcpy(body + n, data, dlen);
	n += dlen;
	n += (size_t) snprintf(body + n, cap - n, "\r\n--" BND "--\r\n");
	int h = snprintf(req, 1024, "POST /upload%s%s HTTP/1.1\r\nHost: k\r\nContent-Type: multipart/form-data; boundary=" BND
			 "\r\nContent-Length: %zu\r\n\r\n", query_pin ? "?pin=" : "", query_pin ? query_pin : "", n);
	memcpy(req + h, body, n);
	free(body);
	*out_len = (size_t) h + n;
	return req;
}

static int count_files(const char *dir)
{
	DIR *d = opendir(dir);
	int n = 0;
	struct dirent *e;
	while (d && (e = readdir(d)))
		if (e->d_name[0] != '.') n++;
	if (d) closedir(d);
	return n;
}

static int file_equals(const char *path, const char *data, size_t len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	char *buf = malloc(len + 1);
	size_t n = fread(buf, 1, len + 1, f);
	fclose(f);
	int ok = n == len && memcmp(buf, data, len) == 0;
	free(buf);
	return ok;
}

static char tmpdir[256];

static void rm_tmp(void)
{
	DIR *d = opendir(tmpdir);
	struct dirent *e;
	char p[512];
	while (d && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(p, sizeof p, "%s/%s", tmpdir, e->d_name);
		unlink(p);
	}
	if (d) closedir(d);
}

static void test_e2e(void)
{
	snprintf(tmpdir, sizeof tmpdir, "/tmp/test_httpd.XXXXXX");
	T_ASSERT(mkdtemp(tmpdir) != NULL);
	UploadServer *s = upload_open(tmpdir, 0, 0);
	T_ASSERT(s != NULL);
	if (!s) return;
	upload_set_limits(s, 5000, 0); /* generous: the Mac may be busy; the slowloris test shortens it */
	T_ASSERT(upload_port(s) > 0);
	char pin[8];
	snprintf(pin, sizeof pin, "%s", upload_pin(s));
	T_EQ_INT(strlen(pin), 6);
	char path[512];

	/* GET / serves the form */
	const char *get = "GET / HTTP/1.1\r\nHost: k\r\n\r\n";
	T_EQ_INT(roundtrip(s, get, strlen(get)), 200);
	T_ASSERT(strstr(resp, "type=\"file\"") != NULL);
	T_ASSERT(strstr(resp, pin) == NULL); /* the page never shows the PIN */
	const char *nf = "GET /etc/passwd HTTP/1.1\r\n\r\n";
	T_EQ_INT(roundtrip(s, nf, strlen(nf)), 404);

	/* a small epub with the PIN as a form field */
	const char epub[] = "PK\x03\x04mimetypeapplication/epub+zip\r\n--" BND "x\0\xff\r\n--" BND "-";
	size_t len;
	char *req = build(NULL, pin, "kitap şiir.epub", epub, sizeof epub - 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);
	snprintf(path, sizeof path, "%s/kitap şiir.epub", tmpdir);
	T_ASSERT(file_equals(path, epub, sizeof epub - 1));
	T_ASSERT(strstr(resp, "kitap şiir.epub") != NULL);
	const UploadFile *list;
	T_EQ_INT(upload_files(s, &list), 1);
	T_EQ_STR(list[0].name, "kitap şiir.epub");
	T_EQ_INT(list[0].size, sizeof epub - 1);

	/* same name again (PIN in the query this time): " (2)" */
	req = build(pin, NULL, "../../kitap şiir.epub", "v2", 2, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);
	snprintf(path, sizeof path, "%s/kitap şiir (2).epub", tmpdir);
	T_ASSERT(file_equals(path, "v2", 2));
	T_EQ_INT(count_files(tmpdir), 2);

	/* a name that would be HTML: escaped in the response */
	req = build(NULL, pin, "Tom & Jerry's.txt", "t", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);
	T_ASSERT(strstr(resp, "Tom & Jerry") == NULL);
	T_ASSERT(strstr(resp, "Tom &amp; Jerry&#39;s.txt") != NULL);
	T_EQ_INT(count_files(tmpdir), 3);
	rm_tmp();

	/* refused extension: no file */
	req = build(NULL, pin, "evil.sh", "#!/bin/sh", 9, &len);
	roundtrip(s, req, len);
	free(req);
	T_EQ_INT(count_files(tmpdir), 0);
	T_ASSERT(strstr(resp, "evil.sh") != NULL);

	/* wrong PIN (field and query), missing PIN: 403 and no file */
	int gen = upload_pin_generation(s);
	req = build(NULL, "000000", "a.epub", "x", 1, &len);
	if (!strcmp(pin, "000000")) req[strstr(req, "000000") - req] = '1';
	T_EQ_INT(roundtrip(s, req, len), 403);
	free(req);
	req = build("99999x", NULL, "a.epub", "x", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 403);
	free(req);
	req = build(NULL, NULL, "a.epub", "x", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 403);
	free(req);
	T_EQ_INT(count_files(tmpdir), 0);
	T_EQ_INT(upload_pin_generation(s), gen);
	/* three more wrong ones (a file sent without any PIN is refused but is not a guess): the PIN is replaced */
	for (int i = 0; i < 3; i++) {
		req = build("abcdef", NULL, "a.epub", "x", 1, &len);
		T_EQ_INT(roundtrip(s, req, len), 403);
		free(req);
	}
	T_EQ_INT(upload_pin_generation(s), gen + 1);
	if (strcmp(upload_pin(s), pin) != 0) {
		req = build(NULL, pin, "a.epub", "x", 1, &len);
		T_EQ_INT(roundtrip(s, req, len), 403);
		free(req);
	}
	T_EQ_INT(count_files(tmpdir), 0);
	snprintf(pin, sizeof pin, "%s", upload_pin(s));

	/* pause after a wrong PIN: the next upload is told to wait */
	req = build("111111", NULL, "a.epub", "x", 1, &len);
	if (!strcmp(pin, "111111")) memcpy(strstr(req, "111111"), "222222", 6);
	T_EQ_INT(roundtrip(s, req, len), 403);
	free(req);
	upload_set_limits(s, 5000, 60000);
	req = build(NULL, pin, "a.epub", "x", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 429);
	free(req);
	upload_set_limits(s, 5000, 0);
	T_EQ_INT(count_files(tmpdir), 0);
	snprintf(pin, sizeof pin, "%s", upload_pin(s));

	/* a bigger file (written over many polls) arrives intact */
	size_t big_n = 700 * 1024;
	char *big = malloc(big_n);
	for (size_t i = 0; i < big_n; i++) big[i] = (char) (i * 131 + (i >> 9));
	memcpy(big + 1000, "\r\n--" BND "-Q", strlen("\r\n--" BND "-Q")); /* boundary-like data */
	req = build(NULL, pin, "büyük.pdf", big, big_n, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);
	snprintf(path, sizeof path, "%s/büyük.pdf", tmpdir);
	T_ASSERT(file_equals(path, big, big_n));
	free(big);
	rm_tmp();

	/* oversized Content-Length: 413 before any byte is stored */
	char hdr[512];
	int hn = snprintf(hdr, sizeof hdr, "POST /upload HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=" BND
			  "\r\nContent-Length: %lld\r\n\r\n", HTTPD_MAX_FILE + 2 * 1024 * 1024);
	T_EQ_INT(roundtrip(s, hdr, (size_t) hn), 413);
	/* no Content-Length / chunked */
	const char *nocl = "POST /upload HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=" BND "\r\n\r\n";
	T_EQ_INT(roundtrip(s, nocl, strlen(nocl)), 411);
	const char *notmp = "POST /upload HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: 1\r\n\r\nx";
	T_EQ_INT(roundtrip(s, notmp, strlen(notmp)), 400);
	/* huge header block: 431 */
	static char huge[HTTPD_MAX_HEAD + 2000];
	strcpy(huge, "GET / HTTP/1.1\r\nX: ");
	memset(huge + strlen(huge), 'a', sizeof huge - 40);
	huge[sizeof huge - 1] = '\0';
	T_EQ_INT(roundtrip(s, huge, strlen(huge)), 431);

	/* one upload at a time: a second one gets 503 while the first is half-sent */
	req = build(NULL, pin, "ilk.epub", "first", 5, &len);
	int a = connect_to(upload_port(s));
	T_ASSERT(send(a, req, len - 10, 0) == (ssize_t) (len - 10));
	long long rec = 0, tot = 0, want_rec = (long long) len - 10 - (long long) (strstr(req, "\r\n\r\n") - req) - 4;
	char cur[64];
	for (long t1 = now_ms(); now_ms() - t1 < 3000;) {
		upload_poll(s, 5);
		if (upload_progress(s, &rec, &tot, cur, sizeof cur) && rec == want_rec) break;
	}
	T_EQ_INT(upload_progress(s, &rec, &tot, cur, sizeof cur), 1);
	T_EQ_INT(rec, want_rec);
	T_EQ_INT(tot, (long long) len - (long long) (strstr(req, "\r\n\r\n") - req) - 4);
	T_EQ_STR(cur, "ilk.epub");
	size_t len2;
	char *req2 = build(NULL, pin, "ikinci.epub", "second", 6, &len2);
	T_EQ_INT(roundtrip(s, req2, len2), 503);
	free(req2);
	T_EQ_INT(exchange(s, a, req + len - 10, 10), 200);
	free(req);
	snprintf(path, sizeof path, "%s/ilk.epub", tmpdir);
	T_ASSERT(file_equals(path, "first", 5));
	T_EQ_INT(count_files(tmpdir), 1);
	T_EQ_INT(upload_progress(s, &rec, &tot, cur, sizeof cur), 0);
	rm_tmp();

	/* slowloris: an idle connection is dropped and its .part removed; the server keeps serving */
	upload_set_limits(s, 300, 0);
	req = build(NULL, pin, "yavaş.epub", "slowslowslow", 12, &len);
	a = connect_to(upload_port(s));
	T_ASSERT(send(a, req, len - 20, 0) == (ssize_t) (len - 20));
	free(req);
	long t0 = now_ms();
	char c;
	ssize_t r = -1;
	while (now_ms() - t0 < 3000) {
		upload_poll(s, 20);
		r = recv(a, &c, 1, 0);
		if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) break;
	}
	T_ASSERT(r <= 0 && now_ms() - t0 < 3000);
	T_ASSERT(now_ms() - t0 >= 250);
	close(a);
	T_EQ_INT(count_files(tmpdir), 0);
	T_EQ_INT(upload_busy(s), 0);
	upload_set_limits(s, 5000, 0);
	/* a client that hangs up mid-upload leaves nothing behind either */
	req = build(NULL, pin, "kesik.epub", "cutcutcut", 9, &len);
	a = connect_to(upload_port(s));
	T_ASSERT(send(a, req, len - 15, 0) == (ssize_t) (len - 15));
	free(req);
	for (long t1 = now_ms(); now_ms() - t1 < 3000 && count_files(tmpdir) == 0;) upload_poll(s, 5);
	T_EQ_INT(count_files(tmpdir), 1); /* the .part */
	close(a);
	for (long t1 = now_ms(); now_ms() - t1 < 3000 && upload_busy(s);) upload_poll(s, 5);
	T_EQ_INT(count_files(tmpdir), 0);
	T_EQ_INT(roundtrip(s, get, strlen(get)), 200);

	/* closing mid-upload removes the .part */
	req = build(NULL, pin, "son.epub", "lastlast", 8, &len);
	a = connect_to(upload_port(s));
	T_ASSERT(send(a, req, len - 12, 0) == (ssize_t) (len - 12));
	free(req);
	for (long t1 = now_ms(); now_ms() - t1 < 3000 && count_files(tmpdir) == 0;) upload_poll(s, 5);
	T_EQ_INT(count_files(tmpdir), 1);
	upload_close(s);
	close(a);
	T_EQ_INT(count_files(tmpdir), 0);
	rmdir(tmpdir);
}

/* a browser or curl may send the head alone and then the PIN field and a big file in one burst:
 * the PIN arrives within the first bytes, so the upload must succeed */
static void test_e2e_pin_field_then_big_burst(void)
{
	char dir[256];
	snprintf(dir, sizeof dir, "/tmp/test_httpd_burst.XXXXXX");
	T_ASSERT(mkdtemp(dir) != NULL);
	UploadServer *s = upload_open(dir, 0, 0);
	T_ASSERT(s != NULL);
	if (!s) return;
	upload_set_limits(s, 5000, 0);
	static char data[200000];
	memset(data, 'e', sizeof data);
	size_t len;
	char *req = build(NULL, upload_pin(s), "big.epub", data, sizeof data, &len);
	char *body = strstr(req, "\r\n\r\n") + 4;
	int fd = connect_to(upload_port(s));
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
	T_ASSERT(send(fd, req, (size_t) (body - req), 0) > 0); /* the head alone */
	for (int i = 0; i < 10; i++) upload_poll(s, 2);         /* … is read before the body arrives */
	size_t blen = len - (size_t) (body - req), off = 0;
	while (off < blen) { /* the whole body at once, while the server is not reading */
		ssize_t w = send(fd, body + off, blen - off, MSG_DONTWAIT);
		if (w > 0) off += (size_t) w;
		else upload_poll(s, 2);
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	T_EQ_INT(exchange(s, fd, "", 0), 200);
	char path[400];
	snprintf(path, sizeof path, "%s/big.epub", dir);
	T_ASSERT(file_equals(path, data, sizeof data));
	free(req);
	upload_close(s);
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	T_EQ_INT(system(cmd), 0);
}

/* abuse from someone on the LAN without the PIN must not lock the owner out */
static void test_e2e_abuse(void)
{
	char dir[256];
	snprintf(dir, sizeof dir, "/tmp/test_httpd_abuse.XXXXXX");
	T_ASSERT(mkdtemp(dir) != NULL);
	UploadServer *s = upload_open(dir, 0, 0);
	T_ASSERT(s != NULL);
	if (!s) return;
	upload_set_limits(s, 5000, 0);
	char pin[8];
	snprintf(pin, sizeof pin, "%s", upload_pin(s));
	size_t len;

	/* 1. a PIN-less request trickling its body does not take the upload slot */
	int holder = connect_to(upload_port(s));
	const char *slow = "POST /upload HTTP/1.1\r\nHost: k\r\nContent-Type: multipart/form-data; boundary=" BND
			   "\r\nContent-Length: 200000000\r\n\r\npre";
	T_ASSERT(send(holder, slow, strlen(slow), 0) > 0);
	for (int i = 0; i < 20; i++) upload_poll(s, 2);
	char *req = build(NULL, pin, "ok.epub", "data", 4, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);

	/* 2. at most 16 KB may arrive before the PIN is verified */
	static char big[40000];
	memset(big, 'x', sizeof big);
	char head[512];
	int hl = snprintf(head, sizeof head, "POST /upload HTTP/1.1\r\nHost: k\r\nContent-Type: multipart/form-data; boundary=" BND
			  "\r\nContent-Length: %zu\r\n\r\n--" BND "\r\nContent-Disposition: form-data; name=\"note\"\r\n\r\n", sizeof big + 200);
	char *r2 = malloc((size_t) hl + sizeof big);
	memcpy(r2, head, (size_t) hl);
	memcpy(r2 + hl, big, sizeof big);
	T_EQ_INT(roundtrip(s, r2, (size_t) hl + sizeof big), 403);
	free(r2);

	/* 3. one address cannot take every connection: a fourth one from the same address is refused */
	int a = connect_to(upload_port(s)), b = connect_to(upload_port(s));
	for (int i = 0; i < 10; i++) upload_poll(s, 2);
	int extra = connect_to(upload_port(s));
	for (int i = 0; i < 10; i++) upload_poll(s, 2);
	const char *get = "GET / HTTP/1.1\r\nHost: k\r\n\r\n";
	T_EQ_INT(roundtrip(s, get, strlen(get)), 503); /* holder + a + b are open; extra was refused at accept */
	close(extra);
	close(holder);
	close(a);
	close(b);
	for (int i = 0; i < 10; i++) upload_poll(s, 2);
	T_EQ_INT(roundtrip(s, get, strlen(get)), 200);

	/* 4. after three PIN replacements (15 wrong guesses) the server locks for a while: even the right PIN is refused,
	 * also when it arrives as a form field; the lock expires (no permanent lock-out of the owner) */
	upload_set_lock_ms(s, 300);
	for (int i = 0; i < 15; i++) {
		req = build("000000", NULL, "x.epub", "d", 1, &len);
		if (!strcmp(upload_pin(s), "000000")) req[13] = '1'; /* never guess right by accident */
		roundtrip(s, req, len);
		free(req);
		for (int k = 0; k < 5; k++) upload_poll(s, 2);
	}
	T_EQ_INT(upload_locked(s), 1);
	snprintf(pin, sizeof pin, "%s", upload_pin(s));
	req = build(pin, NULL, "late.epub", "d", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 403);
	free(req);
	req = build(NULL, pin, "late.epub", "d", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 403);
	free(req);
	long t0 = now_ms();
	while (now_ms() - t0 < 400) upload_poll(s, 5);
	T_EQ_INT(upload_locked(s), 0);
	snprintf(pin, sizeof pin, "%s", upload_pin(s));
	req = build(NULL, pin, "after.epub", "d", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);
	/* 5. locks get longer, and after four of them only the owner's tap on the device unlocks (bounded guesses) */
	for (int cycle = 2; cycle <= 4; cycle++) {
		for (int i = 0; i < 15; i++) {
			req = build("000000", NULL, "x.epub", "d", 1, &len);
			if (!strcmp(upload_pin(s), "000000")) req[13] = '1';
			roundtrip(s, req, len);
			free(req);
			for (int k = 0; k < 5; k++) upload_poll(s, 2); /* let the answered connection close (per-address limit) */
		}
		T_EQ_INT(upload_locked(s), 1);
		t0 = now_ms();
		while (now_ms() - t0 < 300L * (1L << (cycle - 1)) + 100) upload_poll(s, 5);
		T_EQ_INT(upload_locked(s), cycle == 4); /* locks 2 and 3 expire after their doubled time; the fourth does not */
	}
	upload_unlock(s);
	T_EQ_INT(upload_locked(s), 0);
	snprintf(pin, sizeof pin, "%s", upload_pin(s));
	req = build(NULL, pin, "owner.epub", "d", 1, &len);
	T_EQ_INT(roundtrip(s, req, len), 200);
	free(req);
	upload_close(s);
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	T_EQ_INT(system(cmd), 0);
}

static void test_port_pick(void)
{
	/* two servers on the same range get different ports */
	snprintf(tmpdir, sizeof tmpdir, "/tmp/test_httpd.XXXXXX");
	T_ASSERT(mkdtemp(tmpdir) != NULL);
	UploadServer *a = upload_open(tmpdir, 18080, 18089);
	UploadServer *b = upload_open(tmpdir, 18080, 18089);
	T_ASSERT(a && b);
	if (a && b) {
		T_ASSERT(upload_port(a) >= 18080 && upload_port(a) <= 18089);
		T_ASSERT(upload_port(b) >= 18080 && upload_port(b) <= 18089);
		T_ASSERT(upload_port(a) != upload_port(b));
	}
	if (a) upload_close(a);
	if (b) upload_close(b);
	T_ASSERT(upload_open("/nonexistent/dir", 0, 0) == NULL);
	rmdir(tmpdir);
}

int main(void)
{
	T_RUN(test_head_basic);
	T_RUN(test_head_upload);
	T_RUN(test_head_limits);
	T_RUN(test_route);
	T_RUN(test_sanitize);
	T_RUN(test_ext);
	T_RUN(test_native_ext);
	T_RUN(test_pin);
	T_RUN(test_html);
	T_RUN(test_multipart_splits);
	T_RUN(test_multipart_errors);
	T_RUN(test_e2e);
	T_RUN(test_e2e_pin_field_then_big_burst);
	T_RUN(test_e2e_abuse);
	T_RUN(test_port_pick);
	return T_DONE();
}

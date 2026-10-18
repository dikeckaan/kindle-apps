/* Wi-Fi upload: pure HTTP pieces (no sockets) — request head parsing with hard limits, a streaming
 * multipart/form-data parser, file name sanitizing, the extension allow-list, PIN checks and the HTML page. */
#ifndef HTTPD_H
#define HTTPD_H
#include <stddef.h>

#define HTTPD_MAX_HEAD 8192   /* request line + headers, including the blank line */
#define HTTPD_MAX_TARGET 1024 /* request target (path + query) */
#define HTTPD_MAX_NAME 120    /* sanitized file name, bytes */
#define HTTPD_MAX_FILE (200LL * 1024 * 1024)
#define HTTPD_PIN_LEN 6

typedef struct {
	char method[16];
	char path[HTTPD_MAX_TARGET + 1];
	char query[HTTPD_MAX_TARGET + 1];
	long long content_length; /* -1 when absent */
	int chunked;              /* Transfer-Encoding present (we refuse it) */
	int expect_continue;
	int multipart;            /* Content-Type: multipart/form-data with a valid boundary */
	char boundary[71];
} HttpHead;

/* Parses a request head from the start of buf. Returns the head length (through the blank line) when complete,
 * 0 when more bytes are needed, or -status on error: 400 malformed, 414 target too long,
 * 431 no blank line within HTTPD_MAX_HEAD bytes, 505 not HTTP/1.x. */
int httpd_parse_head(const char *buf, size_t len, HttpHead *h);

typedef enum { HTTPD_ROUTE_PAGE, HTTPD_ROUTE_UPLOAD, HTTPD_ROUTE_NOT_FOUND, HTTPD_ROUTE_BAD_METHOD } HttpRoute;
/* GET / (also HEAD) → PAGE; POST /upload → UPLOAD; another method on a known path → BAD_METHOD; else NOT_FOUND */
HttpRoute httpd_route(const HttpHead *h);

/* Copies the raw value of key from an a=b&c=d query (no percent-decoding). 1 found, 0 absent or too long. */
int httpd_query_param(const char *query, const char *key, char *out, size_t cap);

/* 1 if given equals the 6-digit pin; the time taken does not depend on where they differ */
int httpd_pin_equal(const char *pin, const char *given);

/* Turns a client-supplied file name (len bytes, may hold NUL) into a safe base name: the part after the last
 * '/' or '\\', without control characters, NUL or characters FAT refuses (replaced by '_'), leading/trailing
 * dots and spaces trimmed, invalid UTF-8 replaced, at most HTTPD_MAX_NAME bytes (the extension is kept).
 * Nothing is percent-decoded. Returns 0, or -1 when nothing usable is left ("", ".", ".."). */
int httpd_sanitize_name(const char *in, size_t len, char *out, size_t cap);

/* 1 if the name ends in an allowed e-book/document extension (case-insensitive) */
int httpd_ext_allowed(const char *name);

/* HTML-escapes & < > " ' ; returns the output length (always NUL-terminated, truncated at a whole entity) */
size_t httpd_html_escape(const char *in, char *out, size_t cap);

/* "512 B", "12,3 KB", "1,2 MB" (Turkish decimal comma) */
void httpd_fmt_size(long long bytes, char *out, size_t cap);

/* The upload page; msg_html (already escaped HTML, may be NULL) is shown above the form. Returns length or -1. */
int httpd_page(char *out, size_t cap, const char *msg_html);

/* A complete "Connection: close" response with security headers. Returns length or -1 if it does not fit. */
int httpd_response(char *out, size_t cap, int status, const char *body, size_t body_len);
const char *httpd_status_text(int status);

/* ---- streaming multipart/form-data ---- */
typedef struct {
	/* name/filename are NUL-terminated raw values; filename is NULL for a plain field. Return <0 to abort. */
	int (*part_begin)(void *ctx, const char *name, const char *filename, size_t filename_len);
	int (*part_data)(void *ctx, const char *data, size_t n);
	int (*part_end)(void *ctx);
} MpCallbacks;

typedef struct {
	char delim[76]; /* "\r\n--" boundary */
	int dlen, match, state;
	char hbuf[2048];
	int hlen, after;
} Multipart;

#define MP_ERR (-1)   /* malformed body */
#define MP_ABORT (-2) /* a callback returned < 0 */

void mp_init(Multipart *m, const char *boundary);
/* Feeds any number of bytes. Returns 0, MP_ERR or MP_ABORT (the parser stays in that error afterwards). */
int mp_feed(Multipart *m, const char *data, size_t n, const MpCallbacks *cb, void *ctx);
/* 1 once the closing delimiter was seen */
int mp_done(const Multipart *m);
/* 1 if the Kindle's own library shows this file type (AZW3, MOBI, KFX, PDF, TXT …); others open in KOReader */
int httpd_kindle_native(const char *name);
#endif

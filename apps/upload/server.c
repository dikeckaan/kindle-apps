/* Wi-Fi upload server: non-blocking sockets driven by upload_poll() from the UI loop (single thread).
 * One upload at a time; files are written as <name>.part, fsynced, then renamed (never overwriting). */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include "httpd.h"
#include "server.h"
#include "ssh/sshsrv.h"

#define RECV_CHUNK 65536
#define DRAIN_MS 2000
#define DRAIN_MAX (4LL * 1024 * 1024)
#define PRE_PIN_MAX 16384 /* body bytes allowed before the PIN is verified */
#define PER_IP_CONNS 2
#define MAX_REGENS 3      /* PIN replacements before the server locks */
#define BODY_SLACK (1024LL * 1024) /* multipart framing on top of a 200 MB file */
#define MSG_CAP 4096
#define PATH_CAP 640

enum { C_FREE, C_HEAD, C_BODY, C_WRITE, C_DRAIN };

typedef struct {
	UploadServer *srv;
	int fd, state;
	long t_accept, t_last;
	char head[HTTPD_MAX_HEAD];
	size_t hlen;
	HttpHead hh;
	long long body_left, drained;
	Multipart mp;
	/* multipart progress */
	int in_pin;
	char pin_buf[16];
	size_t pin_len;
	int pin_ok, pin_counted, fail_status, stored, refused;
	unsigned ip;          /* peer IPv4 address */
	long long pre_pin;    /* body bytes received before the PIN was verified */
	int out_fd;
	char part_path[PATH_CAP + 8], final_name[200];
	long long file_bytes;
	char msg[MSG_CAP];
	size_t msg_len;
	/* response */
	char *resp;
	size_t resp_len, resp_off;
} Conn;

struct UploadServer {
	int lfd, port;
	char dir[400];
	char pin[HTTPD_PIN_LEN + 1];
	int bad_pins, gen, idle_ms, pause_ms, locked;
	long last_wrong;
	struct { unsigned ip; int wrong; long last; } peer[16]; /* wrong-PIN backoff per address */
	int active; /* index of the connection uploading, or -1 */
	long long body_recv, body_total;
	Conn c[UPLOAD_MAX_CONNS];
	UploadFile files[UPLOAD_MAX_LIST];
	int nfiles;
	char buf[RECV_CHUNK];
};

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void set_flags(int fd)
{
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	fcntl(fd, F_SETFD, FD_CLOEXEC); /* never leak sockets into system() children (iptables, lipc) */
}

static int new_pin(char out[HTTPD_PIN_LEN + 1], const char *avoid)
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	for (int tries = 0; tries < 100; tries++) {
		unsigned char b[4];
		if (read(fd, b, 4) != 4) break;
		unsigned v = ((unsigned) b[0] << 24) | ((unsigned) b[1] << 16) | ((unsigned) b[2] << 8) | b[3];
		if (v >= 4294000000u) continue; /* rejection sampling: no modulo bias */
		snprintf(out, HTTPD_PIN_LEN + 1, "%06u", v % 1000000u);
		if (avoid && !strcmp(out, avoid)) continue;
		close(fd);
		return 0;
	}
	close(fd);
	return -1;
}

static int port_in_use(int port)
{
	static char tcp[32768];
	FILE *f = fopen("/proc/net/tcp", "r");
	if (!f) return 0;
	size_t n = fread(tcp, 1, sizeof tcp - 1, f);
	fclose(f);
	tcp[n] = '\0';
	return tcp_port_listening(tcp, port);
}

static int listen_on(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short) port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *) &a, sizeof a) != 0 || listen(fd, 16) != 0) {
		close(fd);
		return -1;
	}
	set_flags(fd);
	return fd;
}

UploadServer *upload_open(const char *dir, int port_lo, int port_hi)
{
	struct stat st;
	if (!dir || strlen(dir) >= sizeof ((UploadServer *) 0)->dir || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
	UploadServer *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	signal(SIGPIPE, SIG_IGN);
	snprintf(s->dir, sizeof s->dir, "%s", dir);
	s->lfd = -1;
	s->active = -1;
	s->idle_ms = 30000;
	s->pause_ms = 1000;
	for (int i = 0; i < UPLOAD_MAX_CONNS; i++) s->c[i].fd = s->c[i].out_fd = -1;
	if (new_pin(s->pin, NULL) != 0) {
		free(s);
		return NULL;
	}
	if (port_lo == 0) {
		s->lfd = listen_on(0);
		struct sockaddr_in a;
		socklen_t al = sizeof a;
		if (s->lfd >= 0 && getsockname(s->lfd, (struct sockaddr *) &a, &al) == 0) s->port = ntohs(a.sin_port);
	} else {
		for (int p = port_lo; p <= port_hi && s->lfd < 0; p++) {
			if (port_in_use(p)) continue;
			s->lfd = listen_on(p);
			if (s->lfd >= 0) s->port = p;
		}
	}
	if (s->lfd < 0 || s->port <= 0) {
		if (s->lfd >= 0) close(s->lfd);
		free(s);
		return NULL;
	}
	return s;
}

int upload_port(const UploadServer *s) { return s->port; }
const char *upload_pin(const UploadServer *s) { return s->pin; }
int upload_pin_generation(const UploadServer *s) { return s->gen; }

void upload_set_limits(UploadServer *s, int idle_ms, int pause_ms)
{
	s->idle_ms = idle_ms;
	s->pause_ms = pause_ms;
}

int upload_files(const UploadServer *s, const UploadFile **list)
{
	*list = s->files;
	return s->nfiles;
}

int upload_busy(const UploadServer *s)
{
	for (int i = 0; i < UPLOAD_MAX_CONNS; i++)
		if (s->c[i].state != C_FREE) return 1;
	return 0;
}

int upload_progress(const UploadServer *s, long long *received, long long *total, char *name, int cap)
{
	if (s->active < 0) return 0;
	const Conn *c = &s->c[s->active];
	if (received) *received = s->body_recv;
	if (total) *total = s->body_total;
	if (name && cap > 0) snprintf(name, (size_t) cap, "%s", c->out_fd >= 0 ? c->final_name : "");
	return 1;
}

/* ---- messages shown on the result page ---- */

static void note(Conn *c, const char *pre, const char *raw, const char *post)
{
	char esc[700];
	httpd_html_escape(raw ? raw : "", esc, sizeof esc);
	int n = snprintf(c->msg + c->msg_len, MSG_CAP - c->msg_len, "%s%s%s<br>", pre, esc, post);
	if (n > 0 && c->msg_len + (size_t) n < MSG_CAP) c->msg_len += (size_t) n;
	else c->msg[c->msg_len] = '\0';
}

static const char *status_msg(int st)
{
	switch (st) {
	case 403: return "PIN yanlış. Kindle ekranındaki 6 haneli PIN'i girin.";
	case 404: return "Sayfa bulunamadı.";
	case 405: return "Bu işlem desteklenmiyor.";
	case 411: return "İstek boyutu bildirilmedi.";
	case 413: return "Dosya çok büyük (en fazla 200 MB).";
	case 414: return "Adres çok uzun.";
	case 429: return "Yanlış PIN denendi; birkaç saniye bekleyip tekrar deneyin.";
	case 431: return "İstek başlıkları çok büyük.";
	case 503: return "Şu anda başka bir yükleme sürüyor; bitince tekrar deneyin.";
	case 505: return "Desteklenmeyen HTTP sürümü.";
	case 507: return "Kindle'da yeterli boş yer yok.";
	case 500: return "Kindle dosyayı kaydedemedi.";
	default: return "Bozuk istek.";
	}
}

/* ---- connection lifecycle ---- */

static void abort_file(Conn *c)
{
	if (c->out_fd >= 0) {
		close(c->out_fd);
		c->out_fd = -1;
		unlink(c->part_path);
	}
}

static void conn_close(Conn *c)
{
	UploadServer *s = c->srv;
	abort_file(c);
	if (c->fd >= 0) close(c->fd);
	free(c->resp);
	if (s->active >= 0 && &s->c[s->active] == c) s->active = -1;
	memset(c, 0, sizeof *c);
	c->fd = c->out_fd = -1;
	c->state = C_FREE;
}

/* queues a full response (page with msg_html, or status text) and switches to writing */
static void respond(Conn *c, int status, const char *msg_html)
{
	UploadServer *s = c->srv;
	if (s->active >= 0 && &s->c[s->active] == c) s->active = -1;
	abort_file(c);
	static char page[MSG_CAP + 4096];
	char m[600];
	if (!msg_html) {
		httpd_html_escape(status_msg(status), m, sizeof m);
		msg_html = status == 200 ? NULL : m;
	}
	int pn = httpd_page(page, sizeof page, msg_html);
	if (pn < 0) pn = httpd_page(page, sizeof page, NULL);
	size_t cap = (size_t) pn + 1024;
	c->resp = malloc(cap);
	int n = c->resp ? httpd_response(c->resp, cap, status, page, (size_t) pn) : -1;
	if (n < 0) {
		conn_close(c);
		return;
	}
	if (!strcmp(c->hh.method, "HEAD")) {
		char *e = strstr(c->resp, "\r\n\r\n");
		if (e) n = (int) (e + 4 - c->resp);
	}
	c->resp_len = (size_t) n;
	c->resp_off = 0;
	c->state = C_WRITE;
	c->t_last = now_ms();
}

/* backoff entry for an address (the oldest entry is reused) */
static int peer_slot(UploadServer *s, unsigned ip)
{
	int old = 0;
	for (int i = 0; i < 16; i++) {
		if (s->peer[i].ip == ip && (s->peer[i].wrong || s->peer[i].last)) return i;
		if (s->peer[i].last < s->peer[old].last) old = i;
	}
	memset(&s->peer[old], 0, sizeof s->peer[old]);
	s->peer[old].ip = ip;
	return old;
}

/* 1 while this address must wait after wrong PINs: pause, 2×, 4× … up to 64× (the owner's address is unaffected) */
static int peer_waiting(UploadServer *s, unsigned ip)
{
	if (s->pause_ms <= 0) return 0;
	for (int i = 0; i < 16; i++)
		if (s->peer[i].ip == ip && s->peer[i].wrong) {
			int sh = s->peer[i].wrong - 1 < 6 ? s->peer[i].wrong - 1 : 6;
			return now_ms() - s->peer[i].last < (long) s->pause_ms << sh;
		}
	return 0;
}

/* takes the single upload slot once the PIN is known to be right; 0 if another upload holds it */
static int claim_slot(Conn *c)
{
	UploadServer *s = c->srv;
	int me = (int) (c - s->c);
	if (s->active >= 0 && s->active != me) return 0;
	if (s->active != me) {
		s->active = me;
		s->body_recv = c->hh.content_length - c->body_left;
		s->body_total = c->hh.content_length;
	}
	return 1;
}

static void wrong_pin(Conn *c)
{
	UploadServer *s = c->srv;
	if (c->pin_counted) return;
	c->pin_counted = 1;
	s->last_wrong = now_ms();
	int slot = peer_slot(s, c->ip);
	s->peer[slot].wrong++;
	s->peer[slot].last = now_ms();
	if (++s->bad_pins >= UPLOAD_MAX_BAD_PINS) {
		char fresh[HTTPD_PIN_LEN + 1];
		if (new_pin(fresh, s->pin) == 0) memcpy(s->pin, fresh, sizeof fresh);
		s->bad_pins = 0;
		s->gen++;
		if (s->gen >= MAX_REGENS) s->locked = 1; /* someone keeps guessing: stop until the app is reopened */
	}
}

/* picks "<stem> (k)<ext>" that exists neither as a file nor as a .part; 0 on success */
static int unique_name(UploadServer *s, const char *name, char *out, size_t cap)
{
	const char *dot = strrchr(name, '.');
	size_t stem = dot && dot != name ? (size_t) (dot - name) : strlen(name);
	char path[PATH_CAP], part[PATH_CAP + 8];
	struct stat st;
	for (int k = 1; k < 1000; k++) {
		if (k == 1) snprintf(out, cap, "%s", name);
		else snprintf(out, cap, "%.*s (%d)%s", (int) stem, name, k, name + stem);
		snprintf(path, sizeof path, "%s/%s", s->dir, out);
		snprintf(part, sizeof part, "%s.part", path);
		if (lstat(path, &st) != 0 && errno == ENOENT && lstat(part, &st) != 0 && errno == ENOENT) return 0;
	}
	return -1;
}

static int cb_begin(void *ctx, const char *name, const char *filename, size_t flen)
{
	Conn *c = ctx;
	UploadServer *s = c->srv;
	c->in_pin = 0;
	if (!filename) {
		if (!strcmp(name, "pin")) {
			c->in_pin = 1;
			c->pin_len = 0;
		}
		return 0;
	}
	if (!c->pin_ok) { /* no file data is ever stored without the PIN (not a guess: no strike) */
		c->fail_status = 403;
		return -1;
	}
	if (flen == 0) return 0; /* empty file input */
	char clean[HTTPD_MAX_NAME + 1];
	if (httpd_sanitize_name(filename, flen, clean, sizeof clean) != 0) {
		c->refused++;
		note(c, "Geçersiz dosya adı, alınmadı.", "", "");
		return 0;
	}
	if (!httpd_ext_allowed(clean)) {
		c->refused++;
		note(c, "Bu dosya türü kabul edilmiyor: ", clean, "");
		return 0;
	}
	char path[PATH_CAP];
	if (unique_name(s, clean, c->final_name, sizeof c->final_name) != 0) {
		c->refused++;
		note(c, "Kaydedilemedi: ", clean, "");
		return 0;
	}
	snprintf(path, sizeof path, "%s/%s", s->dir, c->final_name);
	snprintf(c->part_path, sizeof c->part_path, "%s.part", path);
	c->out_fd = open(c->part_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (c->out_fd < 0) {
		c->refused++;
		note(c, "Kaydedilemedi: ", clean, "");
		return 0;
	}
	c->file_bytes = 0;
	return 0;
}

static int cb_data(void *ctx, const char *d, size_t n)
{
	Conn *c = ctx;
	if (c->in_pin) {
		if (c->pin_len + n >= sizeof c->pin_buf) c->pin_len = sizeof c->pin_buf; /* too long: never matches */
		else {
			memcpy(c->pin_buf + c->pin_len, d, n);
			c->pin_len += n;
		}
		return 0;
	}
	if (c->out_fd < 0) return 0; /* skipped part */
	if (c->file_bytes + (long long) n > HTTPD_MAX_FILE) {
		abort_file(c);
		c->refused++;
		note(c, "200 MB sınırı aşıldı, alınmadı: ", c->final_name, "");
		return 0;
	}
	for (size_t off = 0; off < n;) {
		ssize_t w = write(c->out_fd, d + off, n - off);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) {
			abort_file(c);
			c->refused++;
			note(c, "Yazılamadı (yer kalmamış olabilir): ", c->final_name, "");
			return 0;
		}
		off += (size_t) w;
	}
	c->file_bytes += (long long) n;
	return 0;
}

static int cb_end(void *ctx)
{
	Conn *c = ctx;
	UploadServer *s = c->srv;
	if (c->in_pin) {
		c->in_pin = 0;
		if (c->pin_ok) return 0;
		char given[sizeof c->pin_buf + 1];
		size_t gl = c->pin_len < sizeof c->pin_buf ? c->pin_len : 0;
		memcpy(given, c->pin_buf, gl);
		given[gl] = '\0';
		if (c->pin_len < sizeof c->pin_buf && httpd_pin_equal(s->pin, given)) {
			c->pin_ok = 1;
			if (!claim_slot(c)) {
				c->fail_status = 503;
				return -1;
			}
			return 0;
		}
		wrong_pin(c);
		c->fail_status = 403;
		return -1;
	}
	if (c->out_fd < 0) return 0;
	int ok = fsync(c->out_fd) == 0;
	ok = close(c->out_fd) == 0 && ok;
	c->out_fd = -1;
	char path[PATH_CAP];
	snprintf(path, sizeof path, "%s/%s", s->dir, c->final_name);
	struct stat st;
	if (ok && lstat(path, &st) == 0) { /* appeared meanwhile: never overwrite */
		char again[200];
		ok = unique_name(s, c->final_name, again, sizeof again) == 0;
		if (ok) {
			snprintf(c->final_name, sizeof c->final_name, "%s", again);
			snprintf(path, sizeof path, "%s/%s", s->dir, c->final_name);
		}
	}
	if (!ok || rename(c->part_path, path) != 0) {
		unlink(c->part_path);
		c->refused++;
		note(c, "Kaydedilemedi: ", c->final_name, "");
		return 0;
	}
	int dfd = open(s->dir, O_RDONLY | O_CLOEXEC);
	if (dfd >= 0) {
		fsync(dfd); /* best effort: make the rename durable */
		close(dfd);
	}
	c->stored++;
	if (s->nfiles == UPLOAD_MAX_LIST) {
		memmove(s->files, s->files + 1, sizeof s->files[0] * (UPLOAD_MAX_LIST - 1));
		s->nfiles--;
	}
	snprintf(s->files[s->nfiles].name, sizeof s->files[0].name, "%s", c->final_name);
	s->files[s->nfiles].size = c->file_bytes;
	s->nfiles++;
	char sz[32], post[48];
	httpd_fmt_size(c->file_bytes, sz, sizeof sz);
	snprintf(post, sizeof post, " (%s)", sz);
	note(c, "Alındı: ", c->final_name, post);
	return 0;
}

static const MpCallbacks CALLBACKS = { cb_begin, cb_data, cb_end };

static void finish_upload(Conn *c)
{
	if (!mp_done(&c->mp)) {
		respond(c, 400, NULL);
		return;
	}
	if (c->stored == 0 && c->refused == 0) note(c, "Dosya seçilmedi.", "", "");
	respond(c, c->stored > 0 ? 200 : 400, c->msg);
}

static void body_bytes(Conn *c, const char *d, size_t n)
{
	UploadServer *s = c->srv;
	if ((long long) n > c->body_left) n = (size_t) c->body_left;
	c->body_left -= (long long) n;
	if (!c->pin_ok && (c->pre_pin += (long long) n) > PRE_PIN_MAX) { /* the PIN must come first */
		respond(c, 403, NULL);
		return;
	}
	if (s->active == (int) (c - s->c)) s->body_recv += (long long) n;
	int rc = mp_feed(&c->mp, d, n, &CALLBACKS, c);
	if (rc == MP_ABORT) {
		respond(c, c->fail_status ? c->fail_status : 500, NULL);
		return;
	}
	if (rc == MP_ERR) {
		respond(c, 400, NULL);
		return;
	}
	if (c->body_left == 0) finish_upload(c);
}

static void head_done(Conn *c, size_t head_len)
{
	UploadServer *s = c->srv;
	HttpHead *h = &c->hh;
	HttpRoute r = httpd_route(h);
	if (r == HTTPD_ROUTE_PAGE) { respond(c, 200, NULL); return; }
	if (r == HTTPD_ROUTE_NOT_FOUND) { respond(c, 404, NULL); return; }
	if (r == HTTPD_ROUTE_BAD_METHOD) { respond(c, 405, NULL); return; }
	if (h->chunked || h->content_length < 0) { respond(c, 411, NULL); return; }
	if (h->content_length > HTTPD_MAX_FILE + BODY_SLACK) { respond(c, 413, NULL); return; }
	if (!h->multipart) { respond(c, 400, NULL); return; }
	if (s->locked) { respond(c, 403, NULL); return; }
	if (s->active >= 0) { respond(c, 503, NULL); return; }
	if (peer_waiting(s, c->ip)) { respond(c, 429, NULL); return; }
	char qpin[16];
	if (httpd_query_param(h->query, "pin", qpin, sizeof qpin)) {
		if (!httpd_pin_equal(s->pin, qpin)) {
			wrong_pin(c);
			{ respond(c, 403, NULL); return; }
		}
		c->pin_ok = 1;
	}
	struct statvfs vf;
	if (statvfs(s->dir, &vf) == 0 && (long long) vf.f_bavail * (long long) vf.f_frsize < h->content_length)
		{ respond(c, 507, NULL); return; }
	c->body_left = h->content_length;
	if (c->pin_ok) claim_slot(c); /* PIN in the query: the slot is ours now; otherwise once the PIN part checks out */
	mp_init(&c->mp, h->boundary);
	c->state = C_BODY;
	if (h->expect_continue) {
		static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
		if (send(c->fd, cont, sizeof cont - 1, 0) < 0) { /* best effort: curl sends the body after 1 s anyway */
		}
	}
	if (c->hlen > head_len) body_bytes(c, c->head + head_len, c->hlen - head_len);
	else if (c->body_left == 0) finish_upload(c);
}

static void on_readable(Conn *c)
{
	UploadServer *s = c->srv;
	if (c->state == C_HEAD) {
		ssize_t n = recv(c->fd, c->head + c->hlen, sizeof c->head - c->hlen, 0);
		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { conn_close(c); return; }
		if (n < 0) return;
		c->hlen += (size_t) n;
		c->t_last = now_ms();
		int rc = httpd_parse_head(c->head, c->hlen, &c->hh);
		if (rc < 0) { respond(c, -rc, NULL); return; }
		if (rc > 0) head_done(c, (size_t) rc);
		return;
	}
	size_t want = RECV_CHUNK;
	if (c->state == C_BODY && c->body_left < (long long) want) want = (size_t) c->body_left;
	ssize_t n = recv(c->fd, s->buf, want, 0);
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
	if (n <= 0) { conn_close(c); return; } /* client gone: an unfinished .part is removed */
	c->t_last = now_ms();
	if (c->state == C_BODY) body_bytes(c, s->buf, (size_t) n);
	else if (c->state == C_DRAIN && (c->drained += n) > DRAIN_MAX) conn_close(c);
}

static void on_writable(Conn *c)
{
	ssize_t n = send(c->fd, c->resp + c->resp_off, c->resp_len - c->resp_off, 0);
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
	if (n <= 0) { conn_close(c); return; }
	c->resp_off += (size_t) n;
	c->t_last = now_ms();
	if (c->resp_off == c->resp_len) {
		/* half-close and read what the client still sends, so it sees the response instead of a reset */
		shutdown(c->fd, SHUT_WR);
		c->state = C_DRAIN;
	}
}

static void accept_all(UploadServer *s)
{
	for (;;) {
		struct sockaddr_in peer;
		socklen_t pl = sizeof peer;
		int fd = accept(s->lfd, (struct sockaddr *) &peer, &pl);
		if (fd < 0) return;
		set_flags(fd);
		unsigned ip = peer.sin_family == AF_INET ? peer.sin_addr.s_addr : 0;
		Conn *c = NULL;
		int same = 0;
		for (int i = 0; i < UPLOAD_MAX_CONNS; i++) {
			if (s->c[i].state == C_FREE) {
				if (!c) c = &s->c[i];
			} else if (s->c[i].ip == ip && (s->c[i].state == C_HEAD || s->c[i].state == C_BODY)) {
				same++; /* connections that hold a slot for long; answered ones are on their way out */
			}
		}
		if (!c || same >= PER_IP_CONNS) { /* one address cannot hold every connection */
			static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
			if (send(fd, busy, sizeof busy - 1, 0) < 0) { /* best effort */
			}
			close(fd);
			continue;
		}
		memset(c, 0, sizeof *c);
		c->srv = s;
		c->fd = fd;
		c->out_fd = -1;
		c->ip = ip;
		c->state = C_HEAD;
		c->t_accept = c->t_last = now_ms();
	}
}

static void check_timeouts(UploadServer *s)
{
	long t = now_ms();
	for (int i = 0; i < UPLOAD_MAX_CONNS; i++) {
		Conn *c = &s->c[i];
		if (c->state == C_FREE) continue;
		if (c->state == C_HEAD && t - c->t_accept > s->idle_ms) conn_close(c); /* the whole head within the limit */
		else if (c->state == C_DRAIN && t - c->t_last > (s->idle_ms < DRAIN_MS ? s->idle_ms : DRAIN_MS)) conn_close(c);
		else if (t - c->t_last > s->idle_ms) conn_close(c);
	}
}

void upload_poll(UploadServer *s, int budget_ms)
{
	long deadline = now_ms() + (budget_ms > 0 ? budget_ms : 0);
	for (;;) {
		struct pollfd pf[UPLOAD_MAX_CONNS + 1];
		int idx[UPLOAD_MAX_CONNS + 1], k = 0;
		pf[k] = (struct pollfd){ s->lfd, POLLIN, 0 };
		idx[k++] = -1;
		for (int i = 0; i < UPLOAD_MAX_CONNS; i++) {
			Conn *c = &s->c[i];
			if (c->state == C_FREE) continue;
			pf[k] = (struct pollfd){ c->fd, (short) (c->state == C_WRITE ? POLLOUT : POLLIN), 0 };
			idx[k++] = i;
		}
		long left = deadline - now_ms();
		int n = poll(pf, (nfds_t) k, left > 0 ? (int) left : 0);
		if (n > 0) {
			for (int j = 0; j < k; j++) {
				if (!pf[j].revents) continue;
				if (idx[j] < 0) {
					accept_all(s);
					continue;
				}
				Conn *c = &s->c[idx[j]];
				if (c->state == C_FREE || c->fd != pf[j].fd) continue;
				if (c->state == C_WRITE) {
					if (pf[j].revents & (POLLOUT | POLLERR | POLLHUP)) on_writable(c);
				} else on_readable(c);
			}
		}
		check_timeouts(s);
		if (n <= 0 || now_ms() >= deadline) break;
	}
}

void upload_close(UploadServer *s)
{
	if (!s) return;
	for (int i = 0; i < UPLOAD_MAX_CONNS; i++)
		if (s->c[i].state != C_FREE) conn_close(&s->c[i]);
	if (s->lfd >= 0) close(s->lfd);
	free(s);
}

int upload_locked(const UploadServer *s) { return s->locked; }

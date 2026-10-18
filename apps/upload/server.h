/* Wi-Fi upload server: single-threaded, non-blocking; the UI calls upload_poll() between input events. */
#ifndef UPLOAD_SERVER_H
#define UPLOAD_SERVER_H

#define UPLOAD_PORT_FIRST 8080
#define UPLOAD_PORT_LAST 8089
#define UPLOAD_MAX_CONNS 8
#define UPLOAD_MAX_BAD_PINS 5
#define UPLOAD_MAX_LIST 64

typedef struct UploadServer UploadServer;

typedef struct {
	char name[200];
	long long size;
} UploadFile;

/* Listens on 0.0.0.0, first free port in [port_lo, port_hi] (port_lo 0: an ephemeral port), and stores
 * uploads in dir. A fresh random PIN is generated. NULL on failure. */
UploadServer *upload_open(const char *dir, int port_lo, int port_hi);
/* Closes every connection; an unfinished upload's .part file is removed. */
void upload_close(UploadServer *s);
int upload_port(const UploadServer *s);
const char *upload_pin(const UploadServer *s);
/* test hooks: connection idle timeout (default 30 s) and the pause after a wrong PIN (default 1 s) */
void upload_set_limits(UploadServer *s, int idle_ms, int wrong_pin_pause_ms);
/* test hook: how long the lock after repeated wrong PINs lasts (default 5 min) */
void upload_set_lock_ms(UploadServer *s, long ms);

/* Does at most budget_ms of work (waits on the sockets for the rest of it); 0 = one non-blocking pass. */
void upload_poll(UploadServer *s, int budget_ms);

/* 1 while an upload is in progress: request bytes received so far and the request's Content-Length,
 * and the file currently being written ("" between files) */
int upload_progress(const UploadServer *s, long long *received, long long *total, char *name, int cap);
/* files stored in this session, oldest first */
int upload_files(const UploadServer *s, const UploadFile **list);
/* bumps every time the PIN is replaced after UPLOAD_MAX_BAD_PINS wrong attempts */
int upload_pin_generation(const UploadServer *s);
/* 1 while any connection is open (the UI polls faster then) */
int upload_busy(const UploadServer *s);
/* 1 after too many wrong PINs: nothing more is accepted until the app is reopened */
int upload_locked(const UploadServer *s);
#endif

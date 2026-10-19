/* Input layer: touchscreen via evdev (multi-touch protocol B, slot 0 only) and power events via lipc.
 * Opt-in drag stream (kui_set_drag_events): DOWN/MOVE/UP + the gesture, buffered in a small queue. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "kui.h"

#define BITS_LONG (sizeof(long) * 8)
#define TESTBIT(b, a) ((a[(b) / BITS_LONG] >> ((b) % BITS_LONG)) & 1)

int  kui_input_open(void);
void kui_input_close(void);
int  kui_input_asleep(void);
void kui_screen_woke(void); /* screen.c: the next refresh flashes (the screensaver covered the panel) */


static int touch_fd = -1, lipc_fd = -1, power_fd = -1;
static pid_t lipc_pid = -1;
static volatile sig_atomic_t quit_requested;
static KuiGesture gesture;
static int slot, down, tx, ty, changed;
static int track_id = -1;   /* slot 0's tracking id while down */
static int dropped;         /* SYN_DROPPED seen: ignore events up to the next SYN_REPORT, then resync */
static int drag_mode;       /* kui_set_drag_events */
static KuiDrag drag;
#define QN 256
static KuiEvent queue[QN];  /* drag-mode events not yet returned */
static int q_head, q_len;
static int asleep;          /* between goingToScreenSaver and outOfScreenSaver */
static long lipc_respawn_ms; /* last helper (re)start, to rate-limit respawns */

int kui_input_asleep(void) { return asleep; }
int kui_quit_pending(void) { return quit_requested != 0; }

/* the input device that reports KEY_POWER (on the Kindle Basic: bd71828-pwrkey), not the touchscreen */
static int open_power(void)
{
	for (int i = 0; i < 16; i++) {
		char p[32];
		snprintf(p, sizeof p, "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) continue;
		unsigned long keyb[(KEY_MAX + BITS_LONG) / BITS_LONG] = { 0 };
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keyb), keyb);
		if (fd != touch_fd && TESTBIT(KEY_POWER, keyb)) return fd;
		close(fd);
	}
	return -1;
}

int kui_grab_power_key(int on)
{
	if (!on) {
		if (power_fd >= 0) {
			ioctl(power_fd, EVIOCGRAB, 0);
			close(power_fd);
			power_fd = -1;
		}
		return 0;
	}
	if (power_fd >= 0) return 0;
	power_fd = open_power();
	if (power_fd < 0) return -1;
	if (ioctl(power_fd, EVIOCGRAB, 1) < 0) {
		close(power_fd);
		power_fd = -1;
		return -1;
	}
	return 0;
}

/* 1 if a power-button press was read */
static int read_power(void)
{
	struct input_event ev;
	int pressed = 0;
	while (power_fd >= 0 && read(power_fd, &ev, sizeof ev) == (ssize_t) sizeof ev)
		if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) pressed = 1;
	return pressed;
}

void kui_drain_power_key(void) { read_power(); }

static void on_signal(int sig)
{
	(void) sig;
	quit_requested = 1;
}

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int open_touch(void)
{
	for (int i = 0; i < 16; i++) {
		char p[32];
		snprintf(p, sizeof p, "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) continue;
		unsigned long absb[(ABS_MAX + BITS_LONG) / BITS_LONG] = { 0 };
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absb), absb);
		if (TESTBIT(ABS_MT_POSITION_X, absb)) {
			if (ioctl(fd, EVIOCGRAB, 1) < 0) {
				close(fd);
				return KUI_ERR_TOUCH_BUSY;
			}
			return fd;
		}
		close(fd);
	}
	return -1;
}

static void open_lipc(void)
{
	int pfd[2];
	if (pipe(pfd) < 0) return;
	lipc_pid = fork();
	if (lipc_pid == 0) {
		dup2(pfd[1], 1);
		close(pfd[0]);
		close(pfd[1]);
		execlp("lipc-wait-event", "lipc-wait-event", "-m", "com.lab126.powerd",
		       "goingToScreenSaver,outOfScreenSaver", (char *) NULL);
		_exit(127);
	}
	close(pfd[1]);
	if (lipc_pid < 0) {
		close(pfd[0]);
		return;
	}
	lipc_fd = pfd[0];
	fcntl(lipc_fd, F_SETFL, O_NONBLOCK);
	lipc_respawn_ms = now_ms();
}

/* the helper exited (e.g. `killall lipc-wait-event`): reap it, and respawn at most every 5 s */
static void lipc_lost(void)
{
	close(lipc_fd);
	lipc_fd = -1;
	if (lipc_pid > 0) waitpid(lipc_pid, NULL, WNOHANG);
	lipc_pid = -1;
	if (now_ms() - lipc_respawn_ms >= 5000) open_lipc();
}

/* queue: when full, a MOVE replaces the last queued MOVE (the path gets coarser, never wrong) */
static void q_push(KuiEvent e)
{
	if (q_len == QN) {
		KuiEvent *last = &queue[(q_head + q_len - 1) % QN];
		if (e.type == KUI_EV_MOVE && last->type == KUI_EV_MOVE) *last = e;
		else if (e.type != KUI_EV_MOVE) *last = e; /* never happens in practice: one read holds far fewer */
		return;
	}
	queue[(q_head + q_len++) % QN] = e;
}

static int q_pop(KuiEvent *e)
{
	if (!q_len) return 0;
	*e = queue[q_head];
	q_head = (q_head + 1) % QN;
	q_len--;
	return 1;
}

static void drag_reset(void)
{
	memset(&drag, 0, sizeof drag);
	q_head = q_len = 0;
}

void kui_set_drag_events(int on)
{
	drag_mode = on != 0;
	drag_reset();
	memset(&gesture, 0, sizeof gesture);
}

/* the kernel's current slot: events before the first ABS_MT_SLOT belong to it */
static void sync_slot(void)
{
	struct input_absinfo ai;
	if (touch_fd >= 0 && ioctl(touch_fd, EVIOCGABS(ABS_MT_SLOT), &ai) == 0 && ai.value >= 0) slot = ai.value;
}

/* after SYN_DROPPED: read slot 0's contact and position back from the kernel */
static void resync_touch(void)
{
	struct { unsigned int code; int v[16]; } req;
	sync_slot();
	memset(&req, 0, sizeof req);
	req.code = ABS_MT_TRACKING_ID;
	if (ioctl(touch_fd, EVIOCGMTSLOTS(sizeof req), &req) == 0) {
		track_id = req.v[0];
		down = req.v[0] >= 0;
	}
	req.code = ABS_MT_POSITION_X;
	if (ioctl(touch_fd, EVIOCGMTSLOTS(sizeof req), &req) == 0) tx = req.v[0];
	req.code = ABS_MT_POSITION_Y;
	if (ioctl(touch_fd, EVIOCGMTSLOTS(sizeof req), &req) == 0) ty = req.v[0];
	changed = 1;
}

int kui_input_open(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal; /* no SA_RESTART: poll() returns EINTR */
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	touch_fd = open_touch();
	if (touch_fd < 0) return touch_fd;
	open_lipc();
	memset(&gesture, 0, sizeof gesture);
	slot = down = changed = asleep = dropped = 0;
	track_id = -1;
	drag_reset();
	sync_slot(); /* the touch that launched us may still be down: its contact stays ignored as before */
	return 0;
}

void kui_input_close(void)
{
	kui_grab_power_key(0);
	if (touch_fd >= 0) {
		ioctl(touch_fd, EVIOCGRAB, 0);
		close(touch_fd);
		touch_fd = -1;
	}
	if (lipc_pid > 0) {
		kill(lipc_pid, SIGTERM);
		waitpid(lipc_pid, NULL, 0);
		lipc_pid = -1;
	}
	if (lipc_fd >= 0) {
		close(lipc_fd);
		lipc_fd = -1;
	}
}

/* returns an event built from pending touch input, or KUI_EV_NONE */
static KuiEvent drain_touch(void)
{
	struct input_event ev;
	KuiEvent out = { KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
	while (read(touch_fd, &ev, sizeof ev) == (ssize_t) sizeof ev) {
		if (ev.type == EV_SYN && ev.code == SYN_DROPPED) { /* the kernel buffer overflowed */
			dropped = 1;
			continue;
		}
		if (dropped) {
			if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
				dropped = 0;
				resync_touch();
			} else {
				continue;
			}
		}
		if (ev.type == EV_ABS) {
			if (ev.code == ABS_MT_SLOT) slot = ev.value;
			else if (slot != 0) continue;
			else if (ev.code == ABS_MT_TRACKING_ID) { down = ev.value >= 0; track_id = ev.value; changed = 1; }
			else if (ev.code == ABS_MT_POSITION_X) { tx = ev.value; changed = 1; }
			else if (ev.code == ABS_MT_POSITION_Y) { ty = ev.value; changed = 1; }
		} else if (ev.type == EV_SYN && ev.code == SYN_REPORT && changed) {
			changed = 0;
			if (asleep) continue; /* screensaver is up: input is not for us */
			if (drag_mode) { /* queue everything; the caller gets it event by event */
				KuiEvent evs[KUI_DRAG_MAX_OUT];
				int n = kui_drag_feed(&drag, down ? (track_id >= 0 ? track_id : 0) : -1, tx, ty, now_ms(), evs);
				for (int i = 0; i < n; i++) q_push(evs[i]);
				continue;
			}
			out = kui_gesture_feed(&gesture, down, tx, ty, now_ms());
			if (out.type != KUI_EV_NONE) return out;
		}
	}
	if (drag_mode) q_pop(&out);
	return out;
}

KuiEvent kui_next_event(int timeout_ms)
{
	KuiEvent none = { KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
	long deadline = now_ms() + timeout_ms;
	for (;;) {
		if (quit_requested) return (KuiEvent){ KUI_EV_QUIT, 0, 0, KUI_DIR_NONE };
		KuiEvent q;
		if (q_pop(&q)) {
			if (q.type != KUI_EV_MOVE) kui_crash_note_event((int) q.type, q.x, q.y);
			return q;
		}
		struct pollfd pf[3] = { { touch_fd, POLLIN, 0 }, { lipc_fd, POLLIN, 0 }, { power_fd, POLLIN, 0 } };
		long left = timeout_ms < 0 ? -1 : deadline - now_ms();
		if (timeout_ms >= 0 && left <= 0) return none;
		if (lipc_fd < 0 && (left < 0 || left > 5000)) left = 5000; /* wake up to respawn the helper */
		int n = poll(pf, 3, (int) left); /* negative fds are ignored by poll */
		if (n == 0 && timeout_ms < 0) continue;
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return none;
		if (lipc_fd < 0 && lipc_pid < 0 && now_ms() - lipc_respawn_ms >= 5000) open_lipc();
		if (lipc_fd >= 0 && pf[1].revents) {
			char buf[256];
			ssize_t r = (pf[1].revents & POLLIN) ? read(lipc_fd, buf, sizeof buf - 1) : 0;
			if (r > 0) {
				buf[r] = '\0';
				if (strstr(buf, "goingToScreenSaver")) {
					asleep = 1;
					memset(&gesture, 0, sizeof gesture);
					drag_reset();
				}
				if (strstr(buf, "outOfScreenSaver")) {
					asleep = 0;
					memset(&gesture, 0, sizeof gesture);
					kui_screen_woke(); /* the app's redraw after this event flashes once, with no change in the app */
					drag_reset();
					return (KuiEvent){ KUI_EV_WAKE, 0, 0, KUI_DIR_NONE };
				}
			} else if (r == 0 || (r < 0 && errno != EAGAIN)) {
				lipc_lost(); /* POLLHUP/EOF: without this poll() would spin at 100% CPU */
			}
		}
		if (power_fd >= 0 && (pf[2].revents & POLLIN) && read_power())
			return (KuiEvent){ KUI_EV_POWER, 0, 0, KUI_DIR_NONE };
		if (pf[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			return (KuiEvent){ KUI_EV_QUIT, 0, 0, KUI_DIR_NONE };
		if (pf[0].revents & POLLIN) {
			KuiEvent e = drain_touch();
			if (e.type != KUI_EV_NONE) {
				if (e.type != KUI_EV_MOVE) kui_crash_note_event((int) e.type, e.x, e.y); /* for a crash report */
				return e;
			}
		}
	}
}

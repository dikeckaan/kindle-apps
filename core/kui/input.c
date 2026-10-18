/* Input layer: touchscreen via evdev (multi-touch protocol B, slot 0 only) and power events via lipc. */
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

static int touch_fd = -1, lipc_fd = -1;
static pid_t lipc_pid = -1;
static volatile sig_atomic_t quit_requested;
static KuiGesture gesture;
static int slot, down, tx, ty, changed;

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
	slot = down = changed = 0;
	return 0;
}

void kui_input_close(void)
{
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
		if (ev.type == EV_ABS) {
			if (ev.code == ABS_MT_SLOT) slot = ev.value;
			else if (slot != 0) continue;
			else if (ev.code == ABS_MT_TRACKING_ID) { down = ev.value >= 0; changed = 1; }
			else if (ev.code == ABS_MT_POSITION_X) { tx = ev.value; changed = 1; }
			else if (ev.code == ABS_MT_POSITION_Y) { ty = ev.value; changed = 1; }
		} else if (ev.type == EV_SYN && ev.code == SYN_REPORT && changed) {
			changed = 0;
			out = kui_gesture_feed(&gesture, down, tx, ty, now_ms());
			if (out.type != KUI_EV_NONE) return out;
		}
	}
	return out;
}

KuiEvent kui_next_event(int timeout_ms)
{
	KuiEvent none = { KUI_EV_NONE, 0, 0, KUI_DIR_NONE };
	long deadline = now_ms() + timeout_ms;
	for (;;) {
		if (quit_requested) return (KuiEvent){ KUI_EV_QUIT, 0, 0, KUI_DIR_NONE };
		struct pollfd pf[2] = { { touch_fd, POLLIN, 0 }, { lipc_fd, POLLIN, 0 } };
		long left = timeout_ms < 0 ? -1 : deadline - now_ms();
		if (timeout_ms >= 0 && left <= 0) return none;
		int n = poll(pf, lipc_fd >= 0 ? 2 : 1, (int) left);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return none;
		if (lipc_fd >= 0 && (pf[1].revents & POLLIN)) {
			char buf[256];
			ssize_t r = read(lipc_fd, buf, sizeof buf - 1);
			if (r > 0) {
				buf[r] = '\0';
				if (strstr(buf, "outOfScreenSaver")) {
					memset(&gesture, 0, sizeof gesture);
					return (KuiEvent){ KUI_EV_WAKE, 0, 0, KUI_DIR_NONE };
				}
			}
		}
		if (pf[0].revents & POLLIN) {
			KuiEvent e = drain_touch();
			if (e.type != KUI_EV_NONE) return e;
		}
	}
}

/* On a crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) write the signal, the faulting address and the last
 * touches to stderr — the app's log — then die as before. Only async-signal-safe calls in the handler. */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "kui.h"

#define RING 16
static struct { int type, x, y; } ring[RING];
static unsigned ring_n;
static char app_name[32];

void kui_crash_note_event(int type, int x, int y)
{
	unsigned i = ring_n++ % RING;
	ring[i].type = type;
	ring[i].x = x;
	ring[i].y = y;
}

static void put(const char *s) { (void) !write(2, s, strlen(s)); }

static void put_num(long v)
{
	char b[24];
	int n = 0;
	unsigned long u = v < 0 ? (unsigned long) -v : (unsigned long) v;
	do b[n++] = (char) ('0' + u % 10);
	while ((u /= 10) && n < 22);
	if (v < 0) put("-");
	while (n) {
		char c = b[--n];
		(void) !write(2, &c, 1);
	}
}

static void put_hex(unsigned long v)
{
	char b[20] = "0x";
	int n = 2;
	for (int sh = (int) sizeof v * 8 - 4; sh >= 0; sh -= 4) b[n++] = "0123456789abcdef"[(v >> sh) & 15];
	b[n] = '\0';
	put(b);
}

static void on_crash(int sig, siginfo_t *si, void *uc)
{
	(void) uc;
	put("\n*** ");
	put(app_name);
	put(" crashed: signal ");
	put_num(sig);
	put(" at ");
	put_hex((unsigned long) (si ? si->si_addr : 0));
	put("\n*** last events (oldest first, type x y):");
	unsigned n = ring_n < RING ? ring_n : RING;
	for (unsigned k = 0; k < n; k++) {
		unsigned i = (ring_n - n + k) % RING;
		put(" [");
		put_num(ring[i].type);
		put(" ");
		put_num(ring[i].x);
		put(" ");
		put_num(ring[i].y);
		put("]");
	}
	put("\n");
	signal(sig, SIG_DFL); /* the default action: the hub sees the signal and says so */
	raise(sig);
}

void kui_crash_handler_install(const char *app)
{
	snprintf(app_name, sizeof app_name, "%s", app ? app : "app");
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_crash;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
	for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, NULL);
}

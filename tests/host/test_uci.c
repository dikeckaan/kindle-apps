#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "chess/uci.h"
#include "t.h"

#define FAKE "tests/host/fake_engine.sh"

static void test_uci_handshake_and_bestmove(void)
{
	Uci u;
	unsetenv("FAKE_MODE");
	T_EQ_INT(uci_start(&u, FAKE), 0);
	T_EQ_INT(uci_configure(&u, level_get(2)), 0);
	T_EQ_INT(uci_go(&u, "position startpos", level_get(2)), 0);
	char m[6] = "";
	T_EQ_INT(uci_wait_bestmove(&u, m, 2000), 1);
	T_EQ_STR(m, "e2e4");
	uci_close(&u);
}

static void test_uci_engine_dies_returns_error(void)
{
	Uci u;
	setenv("FAKE_MODE", "die", 1);
	T_EQ_INT(uci_start(&u, FAKE), 0);
	uci_go(&u, "position startpos", level_get(3));
	char m[6];
	T_EQ_INT(uci_wait_bestmove(&u, m, 2000), -1);
	uci_close(&u);
}

static void test_uci_timeout(void)
{
	Uci u;
	setenv("FAKE_MODE", "hang", 1);
	T_EQ_INT(uci_start(&u, FAKE), 0);
	uci_go(&u, "position startpos", level_get(3));
	char m[6];
	T_EQ_INT(uci_wait_bestmove(&u, m, 300), 0);
	T_EQ_INT(uci_poll_bestmove(&u, m), 0);
	uci_close(&u);
}

static void test_uci_close_kills_engine(void)
{
	Uci u;
	setenv("FAKE_MODE", "hang", 1);
	T_EQ_INT(uci_start(&u, FAKE), 0);
	pid_t pid = u.pid;
	uci_go(&u, "position startpos", level_get(3));
	uci_close(&u);
	T_ASSERT(kill(pid, 0) != 0);
}

static void test_uci_missing_binary(void)
{
	Uci u;
	T_EQ_INT(uci_start(&u, "/nonexistent/engine"), -1);
}

int main(void)
{
	signal(SIGPIPE, SIG_IGN);
	T_RUN(test_uci_handshake_and_bestmove);
	T_RUN(test_uci_engine_dies_returns_error);
	T_RUN(test_uci_timeout);
	T_RUN(test_uci_close_kills_engine);
	T_RUN(test_uci_missing_binary);
	return T_DONE();
}

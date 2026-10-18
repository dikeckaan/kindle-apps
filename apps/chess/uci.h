/* UCI chess engine client over pipes (POSIX). */
#ifndef UCI_H
#define UCI_H
#include <sys/types.h>
#include "levels.h"

typedef struct {
	pid_t pid;
	int to_fd, from_fd;
	char buf[4096];
	int len;
} Uci;

int  uci_start(Uci *u, const char *path); /* spawns the engine and completes "uci"/"uciok" (5 s); 0 ok, -1 error */
int  uci_configure(Uci *u, const Level *lv); /* setoption …, isready/readyok */
int  uci_go(Uci *u, const char *position_cmd, const Level *lv);
int  uci_poll_bestmove(Uci *u, char move[6]);                 /* non-blocking: 1 ready, 0 not yet, -1 engine died */
int  uci_wait_bestmove(Uci *u, char move[6], int timeout_ms); /* 1 ready, 0 timeout, -1 engine died */
void uci_stop(Uci *u);
void uci_close(Uci *u); /* quit, wait 500 ms, then SIGKILL; reaps the process */
int  uci_read_fd(const Uci *u);
#endif

/* Sleep timeout choices and labels (pure). */
#ifndef SLEEP_OPTS_H
#define SLEEP_OPTS_H
typedef struct { const char *label; int seconds; } SleepOpt; /* seconds 0 = never sleep */
int  sleep_opts(const SleepOpt **out);
int  sleep_current_index(int timeout_s, int prevent); /* -1 if the current timeout is not one of the options */
void sleep_describe(int timeout_s, int prevent, char *out, int cap);
#endif

/* Odak: focus timer logic (pure, no I/O). Times are epoch seconds passed in by the caller. */
#ifndef ODAK_H
#define ODAK_H

/* ---- plan and session state machine ---- */
enum { ODAK_PRESET_POMODORO, ODAK_PRESET_LONG, ODAK_PRESET_CUSTOM, ODAK_N_PRESETS };
#define ODAK_CUSTOM_MIN 5   /* custom focus: 5..180 min in steps of 5 */
#define ODAK_CUSTOM_MAX 180
#define ODAK_BREAK_MAX 30   /* custom break: 0..30 min (0 = one focus phase, then done) */

typedef struct {
	int focus_min, break_min, long_min, long_every; /* every long_every-th break is long_min (0 = never) */
} OdakPlan;

typedef enum { ODAK_FOCUS, ODAK_BREAK, ODAK_LONG_BREAK } OdakPhase;
typedef enum { ODAK_STOPPED, ODAK_RUNNING, ODAK_PAUSED, ODAK_WAITING, ODAK_FINISHED } OdakState;
/* WAITING: a phase ended and the next one is prepared but not started; FINISHED: no break follows */

typedef struct {
	OdakPlan plan;
	OdakState state;
	OdakPhase phase;
	int done_focus;   /* completed focus phases */
	long phase_start; /* when the current phase was first started */
	long end;         /* RUNNING: when the phase ends */
	long left;        /* PAUSED / WAITING: seconds left */
	long length;      /* phase length in seconds, with extensions */
} OdakSession;

/* Pomodoro 25/5 (every 4th break 15), 50/10, or custom focus/break (clamped to the ranges above) */
void odak_plan(int preset, int custom_focus, int custom_break, OdakPlan *p);
int  odak_step(int v, int delta, int lo, int hi); /* v + delta clamped to lo..hi */

void odak_start(OdakSession *s, const OdakPlan *p, long now); /* first focus phase, running */
void odak_pause(OdakSession *s, long now);
void odak_resume(OdakSession *s, long now);
void odak_extend(OdakSession *s, long now, int minutes); /* the current phase gets longer (running, paused or waiting) */
long odak_left(const OdakSession *s, long now);         /* seconds left in the phase, >= 0 */
int  odak_left_min(const OdakSession *s, long now);     /* rounded up: what the screen shows */
int  odak_progress(const OdakSession *s, long now, int scale); /* elapsed share of the phase, 0..scale */
int  odak_due(const OdakSession *s, long now);          /* running and the phase is over */
/* Ends the running phase: returns the focus minutes to log (0 after a break) and prepares the next phase
 * (WAITING), or FINISHED when a focus phase has no break after it. */
int  odak_complete(OdakSession *s, long now);
void odak_begin_next(OdakSession *s, long now); /* WAITING → RUNNING */
int  odak_stop(OdakSession *s, long now);        /* STOPPED; returns whole minutes focused in this focus phase */
/* Running: the next moment the shown minutes change — a whole minute before the end, or the end itself. 0 otherwise. */
long odak_next_wake(const OdakSession *s, long now);
int  odak_is_break(OdakPhase p);

/* ---- log: one "start minutes label" line per focus phase ---- */
#define ODAK_LABEL_MAX 32
#define ODAK_MAX_LABELS 8
#define ODAK_LOG_MAX 6000
typedef struct { long start; int min; char label[ODAK_LABEL_MAX]; } OdakEntry;

int  odak_log_format(const OdakEntry *e, char *out, int cap); /* bytes written ("…\n"), 0 if it does not fit */
int  odak_log_parse(const char *buf, OdakEntry *out, int max); /* keeps the newest max entries; skips bad lines */
/* Appends line to buf (len bytes used, cap total incl. NUL), dropping the oldest lines when full; returns the new length. */
int  odak_log_append(char *buf, int len, int cap, const char *line);

/* ---- stats ---- */
typedef struct {
	int today_min, today_n, week_min, total_min, streak;
	int day_min[7]; /* the last 7 days, [6] = today */
	int n_labels;   /* per-label totals, most first */
	char label[ODAK_MAX_LABELS][ODAK_LABEL_MAX];
	int label_min[ODAK_MAX_LABELS];
} OdakStats;
long odak_day(long t, int utc_off); /* local day number */
/* week = since Monday; streak = consecutive days with a session up to today (or up to yesterday if none yet today) */
void odak_stats(const OdakEntry *e, int n, long now, int utc_off, OdakStats *st);

/* ---- session labels: one per line ---- */
int  odak_labels_parse(const char *buf, char out[][ODAK_LABEL_MAX], int max); /* empty → the default list */
int  odak_labels_format(char l[][ODAK_LABEL_MAX], int n, char *out, int cap);
int  odak_labels_add(char l[][ODAK_LABEL_MAX], int n, int max, const char *label); /* new count; trims, no duplicates */
int  odak_labels_remove(char l[][ODAK_LABEL_MAX], int n, int i);                  /* new count; the last one stays */
void odak_label_clean(const char *in, char out[ODAK_LABEL_MAX]); /* trimmed, one line, cut at a code point */

#endif

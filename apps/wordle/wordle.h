/* Turkish Wordle rules: word lists, scoring with repeated letters, keyboard state, statistics, saved game. */
#ifndef WORDLE_H
#define WORDLE_H

#define WL_LEN 5
#define WL_TRIES 6
#define WL_BYTES 16 /* 5 Turkish letters in UTF-8 (≤ 2 bytes each) + NUL, with room */

typedef enum { WL_UNKNOWN = 0, WL_ABSENT, WL_PRESENT, WL_CORRECT } WlMark;
typedef struct { char *buf; const char **w; int n; } WlList; /* sorted by strcmp */
typedef struct { int cp[40]; WlMark m[40]; int n; } WlKb;
typedef struct { int played, won, streak, best, dist[WL_TRIES]; } WlStats;
typedef struct { char answer[WL_BYTES]; char guess[WL_TRIES][WL_BYTES]; int n; } WlGame;

int  wl_load(const char *path, WlList *l); /* words read, -1 if the file is missing */
void wl_free(WlList *l);
int  wl_contains(const WlList *l, const char *word);
int  wl_pick(const WlList *l, unsigned seed); /* index of an answer for this seed */
/* Turkish lower case with circumflexes folded; returns the letter count, -1 if longer than WL_LEN */
int  wl_normalize(const char *in, char out[WL_BYTES]);
void wl_score(const char *answer, const char *guess, WlMark out[WL_LEN]); /* both normalized, WL_LEN letters */
void wl_kb_update(WlKb *kb, const char *guess, const WlMark m[WL_LEN]); /* keeps the best mark per letter */
WlMark wl_kb_get(const WlKb *kb, int cp);
void wl_stats_record(WlStats *s, int won, int tries);
int  wl_stats_format(const WlStats *s, char *buf, int cap);
int  wl_stats_parse(const char *buf, WlStats *s); /* 0 ok; -1 (and s zeroed) on garbage */
int  wl_game_format(const WlGame *g, char *buf, int cap);
int  wl_game_parse(const char *buf, WlGame *g); /* 0 ok, -1 invalid */
#endif

/* Uyku Ekranı: while it runs the Kindle shows an info screen (date, weather, today's agenda, a poem or a proverb)
 * and suspends to RAM between updates every 30 / 60 / 120 minutes. Pure logic (no drawing): when to wake, which kind
 * of refresh a wake needs, what is due for fetching, and the texts chosen for the day. */
#ifndef UYKU_H
#define UYKU_H

int uyku_interval(int minutes); /* 30, 60 or 120; anything else gives 60 */

/* The next wake (Unix time): the next multiple of every_min minutes of local time after now (at least a minute away),
 * but no later than just after the next local midnight, so the date changes on time. local_off_s: seconds east of UTC. */
long uyku_next_wake(long now, int local_off_s, int every_min);

/* What one wake has to redraw: the body (date, weather, agenda, poem) is redrawn only when its content changed —
 * the first time, on a new day and every UYKU_FULL_EVERY body updates with a full (flashing, ghost-clearing)
 * refresh, otherwise in place; the footer (battery, update time) is always refreshed in place. */
typedef enum { UYKU_UPD_FOOTER, UYKU_UPD_PARTIAL, UYKU_UPD_FULL } UykuUpd;
#define UYKU_FULL_EVERY 6
UykuUpd uyku_update_kind(int first, unsigned long long prev_body, unsigned long long body, int new_day, int partials_since_full);
unsigned long long uyku_hash(unsigned long long h, const char *s); /* FNV-1a, chained (start with 0) */

/* weather fetched at `fetched` (0 = never) is due again after three hours (a few minutes early is fine: wakes
 * drift); with Wi-Fi off between wakes this keeps the radio off on most wakes */
int uyku_weather_due(long now, long fetched);

/* a Turkish proverb for the day (public domain), when Şiir Defteri is not installed */
const char *uyku_proverb(long day);
int uyku_proverb_count(void);

/* The opening of a poem for the sleep screen: whole lines (stanza breaks kept as empty lines) up to max_lines,
 * stopping at a stanza end when the next stanza would not fit whole and at least half the lines are used.
 * Writes the lines joined by '\n' and returns how many were taken; *more = 1 when the poem goes on. */
int uyku_poem_excerpt(const char *text, int max_lines, char *out, int cap, int *more);
#endif

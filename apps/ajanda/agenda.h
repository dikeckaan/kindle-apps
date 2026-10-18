/* Ajanda: calendar math, Turkish holidays (official, religious, kandils, notable days), the Hijri calendar as
 * Diyanet publishes it, and the notes file. Pure logic (no drawing); other apps may link agenda.c (+ core/kui/store.c)
 * to show today's items, e.g. agenda_upcoming(kui_data_dir("ajanda"), y, m, d, 1, items, 8).
 *
 * Hijri dates: the first days of the Hijri months from Receb 1444 (23 Jan 2023) to Zilkade 1457 (31 Dec 2035) are
 * a table copied from Diyanet's published "Dini Günler" lists (vakithesaplama.diyanet.gov.tr). Outside that range
 * the month starts are computed: the astronomical new moon (Meeus) and Diyanet's rule approximated as "the month
 * begins the next day when the conjunction is before 12:00 UTC, otherwise a day later" — this matches 155 of the
 * 161 tabled months and is never off by more than a day. */
#ifndef AGENDA_H
#define AGENDA_H

/* ---- calendar ---- */
long agenda_jdn(int y, int m, int d);                     /* Julian day number of a Gregorian date */
void agenda_from_jdn(long jdn, int *y, int *m, int *d);
int  agenda_days_in_month(int y, int m);
int  agenda_weekday(int y, int m, int d);                 /* 0 = Sunday … 6 = Saturday */
int  agenda_iso_week(int y, int m, int d);                /* 1 … 53 */
void agenda_add_days(int *y, int *m, int *d, int days);
void agenda_add_months(int *y, int *m, int months);       /* wraps across years */

/* ---- Hijri calendar (1 = Muharrem … 9 = Ramazan … 12 = Zilhicce) ---- */
/* returns 1 when the date comes from Diyanet's table, 0 when computed */
int  agenda_hijri(int y, int m, int d, int *hy, int *hm, int *hd);
long agenda_hijri_month_start(int hy, int hm);            /* JDN of the 1st of a Hijri month */
long agenda_hijri_month_start_astro(int hy, int hm);      /* the computed rule alone (tests compare it to the table) */
const char *agenda_hijri_month_name(int hm);              /* Turkish source text: show with T() */

/* ---- items of a day ---- */
typedef enum {
	AGENDA_HOLIDAY, /* resmi tatil: official holidays and the bayram days */
	AGENDA_HALF,    /* yarım gün: the eves (arife) of the bayrams */
	AGENDA_KANDIL,  /* kandil nights and other religious days (no day off) */
	AGENDA_NOTABLE, /* Anneler Günü, Öğretmenler Günü … */
	AGENDA_NOTE     /* the user's note: text is the user's own, not a translation key */
} AgendaKind;

#define AGENDA_TEXT_MAX 200
typedef struct {
	int y, m, d;
	AgendaKind kind;
	int note;    /* AGENDA_NOTE: index into the notes array searched (agenda_upcoming: its own copy); otherwise -1 */
	int yearly;  /* AGENDA_NOTE: repeats every year */
	char text[AGENDA_TEXT_MAX]; /* holidays: Turkish source text, show with T(); notes: as typed */
} AgendaItem;

/* holidays and special days of one date, most important first; returns the count (<= max) */
int agenda_holidays(int y, int m, int d, AgendaItem *out, int max);

/* ---- notes: one per line, "YYYY-MM-DD<TAB>text"; a yearly note has "*" after the date ("1990-05-12*<TAB>…");
 * the text has \\, \t and \n escaped ---- */
typedef struct { int y, m, d, yearly; char text[AGENDA_TEXT_MAX]; } AgendaNote;
#define AGENDA_MAX_NOTES 2000
int  agenda_notes_parse(const char *buf, AgendaNote *out, int max);           /* bad lines are skipped */
int  agenda_notes_format(const AgendaNote *n, int count, char *buf, int cap); /* bytes written, -1 if too small */
int  agenda_notes_load(const char *path, AgendaNote *out, int max);           /* 0 notes when missing */
int  agenda_notes_save(const char *path, const AgendaNote *n, int count);     /* 0 = ok (atomic) */
int  agenda_note_on(const AgendaNote *n, int y, int m, int d); /* yearly: same day each year since its year (29 Feb → 28 Feb) */
int  agenda_note_add(AgendaNote *n, int *count, int max, int y, int m, int d, const char *text, int yearly); /* index or -1 */
void agenda_note_delete(AgendaNote *n, int *count, int idx);

/* holidays then notes of one date */
int agenda_day_items(const AgendaNote *notes, int n_notes, int y, int m, int d, AgendaItem *out, int max);
/* holidays and notes of `days` days from y-m-d (days = 1: that day only), in date order */
int agenda_upcoming_notes(const AgendaNote *notes, int n_notes, int y, int m, int d, int days, AgendaItem *out, int max);
/* the same with the notes read from <data_dir>/notes.txt */
int agenda_upcoming(const char *data_dir, int y, int m, int d, int days, AgendaItem *out, int max);
#endif

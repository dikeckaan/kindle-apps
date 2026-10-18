/* Ajanda: a paper-diary calendar for Türkiye. A month grid (Monday first, ISO weeks) with official and religious
 * holidays, kandils and notable days computed offline, the user's notes (one-off or every year) and the coming
 * week underneath; a day page lists the day's holidays and notes and edits them with the on-screen keyboard.
 * Fully offline: no Wi-Fi, no polling; it waits for touches (and wakes once at midnight to move "today"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ajanda/agenda.h"
#include "kui.h"

#define UPCOMING_DAYS 8 /* today and the next 7 days */
#define GRID_ROWS 6
#define GRAY_HOL 0xCC   /* resmi tatil background */
#define GRAY_DIM 0xAA   /* days of the neighbouring months */
#define GRAY_TXT 0x66   /* weekends, labels */

static AgendaNote notes[AGENDA_MAX_NOTES];
static int n_notes;
static int ty, tm_, td;   /* today */
static int vy, vm;        /* the month on screen */
static int dy, dm, dd;    /* the day page's date */
static int quit;

/* ---- helpers ---- */

static const char *notes_path(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/notes.txt", kui_data_dir("ajanda"));
	return p;
}

/* today's date; a Kindle without a time zone runs on UTC: use Türkiye's (UTC+3, no DST) */
static void today(int *y, int *m, int *d)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	if (tm.tm_gmtoff == 0 && !getenv("TZ")) {
		t += 3 * 3600;
		gmtime_r(&t, &tm);
	}
	*y = tm.tm_year + 1900, *m = tm.tm_mon + 1, *d = tm.tm_mday;
}

/* milliseconds until just after the next local midnight (the only timer: "today" moves on) */
static int ms_to_midnight(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	if (tm.tm_gmtoff == 0 && !getenv("TZ")) {
		time_t u = t + 3 * 3600;
		gmtime_r(&u, &tm);
	}
	long left = 86400L - (tm.tm_hour * 3600L + tm.tm_min * 60L + tm.tm_sec);
	return (int) (left * 1000 + 1000);
}

static int is_today(int y, int m, int d) { return y == ty && m == tm_ && d == td; }

static int save_notes(void)
{
	if (agenda_notes_save(notes_path(), notes, n_notes) == 0) return 0;
	const char *ok[] = { T("Tamam") };
	kui_dialog(T("Ajanda"), T("Not kaydedilemedi."), ok, 1, NULL);
	return -1;
}

/* one line of text in box, cut with "…" when too wide */
static void text_cut(KuiRect box, const char *s, KuiFont f, int px, KuiAlign a, unsigned char fg)
{
	char buf[AGENDA_TEXT_MAX + 8];
	snprintf(buf, sizeof buf, "%s", s);
	for (char *p = buf; *p; p++)
		if (*p == '\n' || *p == '\t') *p = ' ';
	int len = (int) strlen(buf);
	if (kui_text_width(buf, f, px) > box.w) {
		while (len > 0) {
			do len--;
			while (len > 0 && ((unsigned char) buf[len] & 0xC0) == 0x80);
			while (len > 0 && buf[len - 1] == ' ') len--;
			strcpy(buf + len, "…");
			if (kui_text_width(buf, f, px) <= box.w) break;
		}
	}
	kui_text(box, buf, f, px, a, fg);
}

static const char *kind_label(AgendaKind k)
{
	switch (k) {
	case AGENDA_HOLIDAY: return T("Resmi tatil");
	case AGENDA_HALF: return T("Yarım gün");
	case AGENDA_KANDIL: return T("Kandil");
	case AGENDA_NOTABLE: return T("Özel gün");
	default: return T("Not");
	}
}

/* the small sign of an item kind: a gray box for a day off, half a box for a half day, ☾, ◇, ● */
static void kind_mark(KuiRect r, AgendaKind k)
{
	int s = r.h < r.w ? r.h : r.w, x = r.x + (r.w - s) / 2, y = r.y + (r.h - s) / 2;
	KuiRect b = { x + s / 8, y + s / 8, s * 3 / 4, s * 3 / 4 };
	switch (k) {
	case AGENDA_HOLIDAY:
		kui_fill(b, GRAY_HOL);
		kui_frame(b, 2, 0);
		break;
	case AGENDA_HALF:
		kui_fill((KuiRect){ b.x, b.y + b.h / 2, b.w, b.h - b.h / 2 }, GRAY_HOL);
		kui_frame(b, 2, 0);
		break;
	case AGENDA_KANDIL: kui_text(r, "☾", KUI_FONT_BOLD, s * 4 / 5, KUI_ALIGN_CENTER, 0); break;
	case AGENDA_NOTABLE: kui_text(r, "◇", KUI_FONT_BOLD, s * 4 / 5, KUI_ALIGN_CENTER, GRAY_TXT); break;
	default: kui_text(r, "●", KUI_FONT_BOLD, s * 3 / 5, KUI_ALIGN_CENTER, 0); break;
	}
}

/* ---- month view ---- */

enum { F_HOL = 1, F_HALF = 2, F_KANDIL = 4, F_NOTABLE = 8 };
typedef struct { int y, m, d, in_month, flags, notes; KuiRect r; char label[AGENDA_TEXT_MAX]; } Cell;
static Cell cells[GRID_ROWS * 7];
static KuiRect r_prev, r_next, r_up[16];
static AgendaItem up_items[16];
static int n_up_rows;

#define GRID_X 90
#define GRID_Y 290
#define ROW_H 118

static void build_month(void)
{
	int w = kui_width(), cw = (w - 20 - GRID_X) / 7;
	int first = (agenda_weekday(vy, vm, 1) + 6) % 7; /* Monday = 0 */
	long j = agenda_jdn(vy, vm, 1) - first;
	for (int i = 0; i < GRID_ROWS * 7; i++, j++) {
		Cell *c = &cells[i];
		agenda_from_jdn(j, &c->y, &c->m, &c->d);
		c->in_month = c->m == vm;
		c->r = (KuiRect){ GRID_X + (i % 7) * cw, GRID_Y + (i / 7) * ROW_H, cw, ROW_H };
		AgendaItem it[8];
		int n = agenda_holidays(c->y, c->m, c->d, it, 8);
		c->flags = 0;
		snprintf(c->label, sizeof c->label, "%s", n ? T(it[0].text) : ""); /* the most important one, printed small */
		for (int k = 0; k < n; k++)
			c->flags |= it[k].kind == AGENDA_HOLIDAY ? F_HOL : it[k].kind == AGENDA_HALF ? F_HALF : it[k].kind == AGENDA_KANDIL ? F_KANDIL : F_NOTABLE;
		c->notes = 0;
		for (int k = 0; k < n_notes; k++) c->notes += agenda_note_on(&notes[k], c->y, c->m, c->d);
	}
}

static void draw_cell(int i, int selected)
{
	const Cell *c = &cells[i];
	KuiRect r = c->r, in = { r.x + 1, r.y + 1, r.w - 1, r.h - 1 };
	kui_fill(in, 255);
	if (c->in_month && (c->flags & F_HOL)) kui_fill(in, GRAY_HOL);
	else if (c->in_month && (c->flags & F_HALF)) kui_fill((KuiRect){ in.x, in.y + in.h / 2, in.w, in.h - in.h / 2 }, GRAY_HOL);
	char num[16];
	snprintf(num, sizeof num, "%d", c->d);
	int wd = agenda_weekday(c->y, c->m, c->d);
	KuiRect nb = { r.x + 8, r.y + 6, 64, 52 };
	if (is_today(c->y, c->m, c->d)) { /* today: white number on black */
		kui_fill(nb, 0);
		kui_text_on(nb, num, KUI_FONT_BOLD, 40, KUI_ALIGN_CENTER, 255, 0);
	} else {
		unsigned char fg = !c->in_month ? GRAY_DIM : (wd == 0 || wd == 6) && !(c->flags & F_HOL) ? GRAY_TXT : 0;
		kui_text((KuiRect){ nb.x + 4, nb.y, nb.w + 20, nb.h }, num, KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, fg);
	}
	if (c->in_month && c->label[0]) text_cut((KuiRect){ r.x + 8, r.y + 58, r.w - 14, 30 }, c->label, KUI_FONT_REG, 20, KUI_ALIGN_LEFT, 0);
	if (c->in_month) { /* signs along the bottom: ☾ ◇ ● (with the note count) */
		int x = r.x + 8, y = r.y + r.h - 46;
		if (c->flags & F_KANDIL) {
			kind_mark((KuiRect){ x, y, 38, 40 }, AGENDA_KANDIL);
			x += 40;
		}
		if (c->flags & F_NOTABLE) {
			kind_mark((KuiRect){ x, y, 38, 40 }, AGENDA_NOTABLE);
			x += 40;
		}
		if (c->notes) {
			kind_mark((KuiRect){ x, y, 34, 40 }, AGENDA_NOTE);
			if (c->notes > 1) {
				snprintf(num, sizeof num, "%d", c->notes);
				kui_text((KuiRect){ x + 34, y, 30, 40 }, num, KUI_FONT_BOLD, 24, KUI_ALIGN_LEFT, 0);
			}
		}
	}
	if (selected) kui_frame((KuiRect){ r.x + 3, r.y + 3, r.w - 5, r.h - 5 }, 6, 0);
	kui_mark_dirty(r);
}

static void draw_legend(int y)
{
	int w = kui_width();
	static const AgendaKind kinds[] = { AGENDA_HOLIDAY, AGENDA_HALF, AGENDA_KANDIL, AGENDA_NOTABLE, AGENDA_NOTE };
	int px = 26, total;
	for (;; px -= 2) { /* everything on one line */
		total = 0;
		for (int i = 0; i < 5; i++) total += 40 + kui_text_width(kind_label(kinds[i]), KUI_FONT_REG, px) + 30;
		if (total <= w - 40 || px <= 18) break;
	}
	int x = (w - total) / 2;
	for (int i = 0; i < 5; i++) {
		kind_mark((KuiRect){ x, y, 36, 40 }, kinds[i]);
		int tw = kui_text_width(kind_label(kinds[i]), KUI_FONT_REG, px);
		kui_text((KuiRect){ x + 40, y, tw + 4, 40 }, kind_label(kinds[i]), KUI_FONT_REG, px, KUI_ALIGN_LEFT, GRAY_TXT);
		x += 40 + tw + 30;
	}
}

static void draw_upcoming(void)
{
	int w = kui_width(), h = kui_height(), y0 = 1060;
	KuiRect area = { 0, y0, w, h - y0 };
	kui_fill(area, 255);
	kui_fill((KuiRect){ 20, y0, w - 40, 3 }, 0);
	kui_text((KuiRect){ 20, y0 + 8, w - 40, 50 }, T("Yaklaşanlar"), KUI_FONT_BOLD, 34, KUI_ALIGN_LEFT, 0);
	int n = agenda_upcoming_notes(notes, n_notes, ty, tm_, td, UPCOMING_DAYS, up_items, 16);
	int lh = 50, y = y0 + 64, rows = (h - 10 - y) / lh;
	n_up_rows = 0;
	if (!n) {
		const char *s = T("Önümüzdeki 7 günde not ya da özel gün yok.");
		kui_text((KuiRect){ 20, y, w - 40, lh }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, w - 40), KUI_ALIGN_LEFT, GRAY_TXT);
	}
	for (int i = 0; i < n && n_up_rows < rows; i++) {
		const AgendaItem *it = &up_items[i];
		char s[96];
		if (n_up_rows == rows - 1 && i < n - 1) { /* no room for the rest */
			snprintf(s, sizeof s, T("… ve %d daha"), n - i);
			kui_text((KuiRect){ 200, y, w - 220, lh }, s, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, GRAY_TXT);
			break;
		}
		if (is_today(it->y, it->m, it->d)) snprintf(s, sizeof s, "%s", T("Bugün"));
		else snprintf(s, sizeof s, "%s %d", kui_tr_weekday_short(agenda_weekday(it->y, it->m, it->d)), it->d);
		kui_text((KuiRect){ 20, y, 170, lh }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 30, 170), KUI_ALIGN_LEFT, 0);
		kind_mark((KuiRect){ 196, y + 6, 38, lh - 12 }, it->kind);
		const char *text = it->kind == AGENDA_NOTE ? it->text : T(it->text);
		char line[AGENDA_TEXT_MAX + 64];
		if (it->kind == AGENDA_HALF) snprintf(line, sizeof line, "%s (%s)", text, T("yarım gün"));
		else snprintf(line, sizeof line, "%s", text);
		text_cut((KuiRect){ 246, y, w - 266, lh }, line, it->kind == AGENDA_HOLIDAY ? KUI_FONT_BOLD : KUI_FONT_REG, 30, KUI_ALIGN_LEFT,
			 it->kind == AGENDA_NOTABLE ? GRAY_TXT : 0);
		r_up[n_up_rows] = (KuiRect){ 0, y, w, lh };
		up_items[n_up_rows] = *it; /* row k ↔ item */
		n_up_rows++;
		y += lh;
	}
	kui_mark_dirty(area);
}

static void draw_month(int full)
{
	int w = kui_width(), h = kui_height(), cw = (w - 20 - GRID_X) / 7;
	build_month();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Ajanda"), T("Bugün"));
	r_prev = (KuiRect){ 20, 132, 150, 96 };
	r_next = (KuiRect){ w - 170, 132, 150, 96 };
	KuiButton bp = { r_prev, "◀", 1 }, bn = { r_next, "▶", 2 };
	kui_button(&bp, 0);
	kui_button(&bn, 0);
	char s[64];
	snprintf(s, sizeof s, "%s %d", kui_tr_month(vm - 1), vy);
	kui_text((KuiRect){ 180, 130, w - 360, 100 }, s, KUI_FONT_SERIF_BOLD, kui_fit_px(s, KUI_FONT_SERIF_BOLD, 64, w - 380), KUI_ALIGN_CENTER, 0);
	/* weekday names, Monday first, and the week-number column */
	kui_text((KuiRect){ 20, 240, GRID_X - 20, 50 }, T("Hf"), KUI_FONT_REG, kui_fit_px(T("Hf"), KUI_FONT_REG, 24, GRID_X - 24), KUI_ALIGN_CENTER, GRAY_TXT);
	for (int i = 0; i < 7; i++) {
		const char *wd = kui_tr_weekday_short((i + 1) % 7);
		kui_text((KuiRect){ GRID_X + i * cw, 240, cw, 50 }, wd, KUI_FONT_BOLD, kui_fit_px(wd, KUI_FONT_BOLD, 30, cw - 8), KUI_ALIGN_CENTER,
			 i >= 5 ? GRAY_TXT : 0);
	}
	for (int i = 0; i < GRID_ROWS * 7; i++) draw_cell(i, 0);
	for (int r = 0; r < GRID_ROWS; r++) {
		char wk[16];
		const Cell *c = &cells[r * 7];
		snprintf(wk, sizeof wk, "%d", agenda_iso_week(c->y, c->m, c->d));
		kui_text((KuiRect){ 20, c->r.y, GRID_X - 20, ROW_H }, wk, KUI_FONT_REG, 26, KUI_ALIGN_CENTER, GRAY_TXT);
	}
	/* the grid's rules */
	for (int r = 0; r <= GRID_ROWS; r++) kui_fill((KuiRect){ 20, GRID_Y + r * ROW_H, GRID_X - 20 + 7 * cw + 1, r == 0 || r == GRID_ROWS ? 3 : 1 }, 0);
	for (int i = 0; i <= 7; i++) kui_fill((KuiRect){ GRID_X + i * cw, GRID_Y, 1, GRID_ROWS * ROW_H }, i == 0 || i == 7 ? 0 : 0x99);
	draw_legend(GRID_Y + GRID_ROWS * ROW_H + 10);
	draw_upcoming();
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, w, h });
		kui_flush(0);
	}
}

/* ---- day view ---- */

static AgendaItem day_items[48];
static int n_day_items, n_day_notes, note_first;
static KuiRect r_add, r_note[16], r_yearly[16], r_dprev, r_dnext;
static int note_idx[16], n_note_rows;

static int note_rows_fit(int y) { return (kui_height() - 20 - y) / 118; }

static void draw_yearly(int row, int pressed)
{
	KuiRect r = r_yearly[row];
	kui_fill(r, pressed ? 0 : 255);
	kui_frame(r, 2, 0);
	const char *l = T("Her yıl");
	int on = notes[note_idx[row]].yearly;
	int px = kui_fit_px(l, KUI_FONT_REG, 28, r.w - 80);
	kui_text((KuiRect){ r.x + 10, r.y, r.w - 80, r.h }, l, KUI_FONT_REG, px, KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ r.x + r.w - 72, r.y, 64, r.h }, on ? "☑" : "☐", KUI_FONT_REG, 52, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
}

static void draw_day(int full)
{
	int w = kui_width(), h = kui_height();
	char s[200];
	n_day_items = agenda_day_items(notes, n_notes, dy, dm, dd, day_items, 48);
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	struct tm tm = { 0 };
	tm.tm_year = dy - 1900, tm.tm_mon = dm - 1, tm.tm_mday = dd, tm.tm_wday = agenda_weekday(dy, dm, dd);
	kui_tr_date(&tm, s, sizeof s);
	kui_topbar(s, NULL);
	r_dprev = (KuiRect){ 20, 132, 150, 110 };
	r_dnext = (KuiRect){ w - 170, 132, 150, 110 };
	KuiButton bp = { r_dprev, "◀", 1 }, bn = { r_dnext, "▶", 2 };
	kui_button(&bp, 0);
	kui_button(&bn, 0);
	int hy, hm, hd, exact = agenda_hijri(dy, dm, dd, &hy, &hm, &hd);
	char wk[96];
	long doy = agenda_jdn(dy, dm, dd) - agenda_jdn(dy, 1, 1) + 1;
	snprintf(wk, sizeof wk, T("Hafta %d · Gün %d"), agenda_iso_week(dy, dm, dd), (int) doy);
	if (is_today(dy, dm, dd)) snprintf(s, sizeof s, "%s · %s", T("Bugün"), wk);
	else snprintf(s, sizeof s, "%s", wk);
	kui_text((KuiRect){ 180, 132, w - 360, 55 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 34, w - 380), KUI_ALIGN_CENTER, 0);
	snprintf(s, sizeof s, T("Hicri: %d %s %d"), hd, T(agenda_hijri_month_name(hm)), hy);
	if (!exact) snprintf(s + strlen(s), sizeof s - strlen(s), " (≈)");
	kui_text((KuiRect){ 180, 187, w - 360, 55 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, w - 380), KUI_ALIGN_CENTER, GRAY_TXT);
	/* holidays and special days */
	int y = 262, n_hol = 0;
	for (int i = 0; i < n_day_items; i++) n_hol += day_items[i].kind != AGENDA_NOTE;
	if (n_hol) {
		kui_fill((KuiRect){ 20, y, w - 40, 3 }, 0);
		y += 14;
	}
	for (int i = 0; i < n_day_items; i++) {
		const AgendaItem *it = &day_items[i];
		if (it->kind == AGENDA_NOTE) continue;
		kind_mark((KuiRect){ 30, y + 10, 50, 50 }, it->kind);
		const char *t = T(it->text);
		KuiFont f = it->kind == AGENDA_HOLIDAY ? KUI_FONT_BOLD : KUI_FONT_REG;
		int px = kui_fit_px(t, f, 36, w - 130);
		text_cut((KuiRect){ 100, y, w - 130, 50 }, t, f, px < 28 ? 28 : px, KUI_ALIGN_LEFT,
			 it->kind == AGENDA_NOTABLE ? GRAY_TXT : 0);
		kui_text((KuiRect){ 100, y + 46, w - 130, 30 }, kind_label(it->kind), KUI_FONT_REG, 24, KUI_ALIGN_LEFT, GRAY_TXT);
		y += 84;
	}
	/* notes */
	kui_fill((KuiRect){ 20, y, w - 40, 3 }, 0);
	y += 14;
	kui_text((KuiRect){ 30, y, w - 400, 90 }, T("Notlar"), KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
	r_add = (KuiRect){ w - 360, y, 340, 90 };
	KuiButton ba = { r_add, T("+ Not ekle"), 3 };
	kui_button(&ba, 0);
	y += 110;
	int fit = note_rows_fit(y), total = 0;
	if (fit > 16) fit = 16;
	for (int i = 0; i < n_day_items; i++) total += day_items[i].kind == AGENDA_NOTE;
	if (note_first >= total) note_first = 0;
	n_day_notes = total;
	n_note_rows = 0;
	if (!total) {
		const char *t = T("Bu gün için not yok.");
		kui_text((KuiRect){ 30, y, w - 60, 80 }, t, KUI_FONT_REG, kui_fit_px(t, KUI_FONT_REG, 32, w - 60), KUI_ALIGN_LEFT, GRAY_TXT);
	}
	for (int i = 0, k = 0; i < n_day_items && n_note_rows < fit; i++) {
		if (day_items[i].kind != AGENDA_NOTE || k++ < note_first) continue;
		int row = n_note_rows++;
		KuiRect r = { 20, y + row * 118, w - 40, 106 };
		note_idx[row] = day_items[i].note;
		r_note[row] = (KuiRect){ r.x, r.y, r.w - 250, r.h };
		r_yearly[row] = (KuiRect){ r.x + r.w - 236, r.y + 13, 236, 80 };
		kui_frame(r_note[row], 2, 0);
		/* up to two lines of the note */
		static KuiLine ln[32];
		const char *t = day_items[i].text;
		int nl = kui_wrap_text(t, r_note[row].w - 40, KUI_FONT_REG, 32, ln, 32), lh = kui_line_height(KUI_FONT_REG, 32);
		if (nl > 2) nl = 2;
		int ty0 = r.y + (r.h - nl * lh) / 2;
		for (int l = 0; l < nl; l++) {
			char part[AGENDA_TEXT_MAX + 4];
			int len = ln[l].len < AGENDA_TEXT_MAX ? ln[l].len : AGENDA_TEXT_MAX - 1;
			memcpy(part, t + ln[l].off, (size_t) len);
			part[len] = '\0';
			if (l == 1 && ln[l].off + ln[l].len < (int) strlen(t)) snprintf(part, sizeof part, "%.*s…", len, t + ln[l].off);
			text_cut((KuiRect){ r.x + 20, ty0 + l * lh, r_note[row].w - 40, lh }, part, KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0);
		}
		draw_yearly(row, 0);
	}
	if (total > note_first + n_note_rows || note_first > 0) {
		snprintf(s, sizeof s, T("Notlar %d–%d / %d · kaydırın"), note_first + 1, note_first + n_note_rows, total);
		kui_text((KuiRect){ 20, h - 50, w - 40, 44 }, s, KUI_FONT_REG, 26, KUI_ALIGN_CENTER, GRAY_TXT);
	}
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, w, h });
		kui_flush(0);
	}
}

static void redraw_day_bg(void) { draw_day(1); }

/* edit or delete the note shown in row */
static void note_menu(int row)
{
	int idx = note_idx[row];
	const char *ch[] = { T("Düzenle"), T("Sil"), T("Vazgeç") };
	int c = kui_dialog(T("Not"), notes[idx].text, ch, 3, redraw_day_bg);
	if (kui_quit_pending()) {
		quit = 1;
		return;
	}
	if (c == 0) {
		char buf[AGENDA_TEXT_MAX];
		snprintf(buf, sizeof buf, "%s", notes[idx].text);
		if (kui_input_text(T("Notu düzenle"), buf, sizeof buf, 0, 1, redraw_day_bg)) {
			if (strspn(buf, " ") < strlen(buf)) { /* a blank text keeps the note as it was */
				snprintf(notes[idx].text, sizeof notes[idx].text, "%s", buf);
				save_notes();
			}
		}
	} else if (c == 1) {
		agenda_note_delete(notes, &n_notes, idx);
		save_notes();
	}
	draw_day(0); /* in place: the dialog or the keyboard goes, the page stays */
}

static void add_note(void)
{
	char buf[AGENDA_TEXT_MAX] = "";
	if (kui_input_text(T("Yeni not"), buf, sizeof buf, 0, 1, redraw_day_bg)) {
		if (agenda_note_add(notes, &n_notes, AGENDA_MAX_NOTES, dy, dm, dd, buf, 0) >= 0) save_notes();
	}
	if (kui_quit_pending()) quit = 1;
	draw_day(0);
}

static void move_day(int delta)
{
	int om = dm, oy = dy;
	agenda_add_days(&dy, &dm, &dd, delta);
	note_first = 0;
	draw_day(dm != om || dy != oy); /* a new month: a full refresh clears the ghosts */
}

static void day_screen(int y, int m, int d)
{
	int w = kui_width();
	dy = y, dm = m, dd = d;
	note_first = 0;
	draw_day(1);
	while (!quit) {
		KuiEvent e = kui_next_event(ms_to_midnight());
		if (e.type == KUI_EV_QUIT) {
			quit = 1;
			break;
		}
		if (e.type == KUI_EV_NONE) { /* midnight: "Bugün" moves */
			today(&ty, &tm_, &td);
			if (!kui_input_asleep()) draw_day(0);
			continue;
		}
		if (e.type == KUI_EV_WAKE) {
			today(&ty, &tm_, &td);
			draw_day(1);
			continue;
		}
		if (e.type == KUI_EV_SWIPE) {
			if (e.dir == KUI_DIR_LEFT) move_day(1);
			else if (e.dir == KUI_DIR_RIGHT) move_day(-1);
			else if (e.dir == KUI_DIR_UP && n_note_rows && note_first + n_note_rows < n_day_notes) { /* more notes below */
				note_first += n_note_rows;
				draw_day(0);
			} else if (e.dir == KUI_DIR_DOWN && note_first > 0) {
				note_first = note_first > n_note_rows ? note_first - n_note_rows : 0;
				draw_day(0);
			}
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) break;
		if (kui_rect_contains(r_dprev, e.x, e.y)) { move_day(-1); continue; }
		if (kui_rect_contains(r_dnext, e.x, e.y)) { move_day(1); continue; }
		if (kui_rect_contains(r_add, e.x, e.y)) { add_note(); continue; }
		for (int r = 0; r < n_note_rows; r++) {
			if (kui_rect_contains(r_yearly[r], e.x, e.y)) { /* "her yıl": only the box changes */
				notes[note_idx[r]].yearly ^= 1;
				save_notes();
				draw_yearly(r, 0);
				kui_flush(0);
				break;
			}
			if (kui_rect_contains(r_note[r], e.x, e.y)) {
				KuiRect sel = r_note[r];
				kui_frame(sel, 6, 0); /* pressed */
				kui_mark_dirty(sel);
				kui_flush(0);
				note_menu(r);
				break;
			}
		}
	}
}

/* ---- main loop ---- */

static void go_month(int y, int m, int full)
{
	int changed = y != vy || m != vm;
	vy = y, vm = m;
	draw_month(full || changed);
}

int main(void)
{
	if (kui_init("ajanda") != 0) return 1;
	kui_set_refresh_every(0); /* full refreshes only on a new screen or month */
	n_notes = agenda_notes_load(notes_path(), notes, AGENDA_MAX_NOTES);
	today(&ty, &tm_, &td);
	vy = ty, vm = tm_;
	int w = kui_width();
	draw_month(1);
	while (!quit) {
		KuiEvent e = kui_next_event(ms_to_midnight());
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_NONE) { /* midnight: move the today mark and the coming week, in place */
			int oy = ty, om = tm_, od = td;
			today(&ty, &tm_, &td);
			if ((oy != ty || om != tm_ || od != td) && !kui_input_asleep()) draw_month(0);
			continue;
		}
		if (e.type == KUI_EV_WAKE) {
			today(&ty, &tm_, &td);
			draw_month(1);
			continue;
		}
		if (e.type == KUI_EV_SWIPE) {
			int y = vy, m = vm;
			if (e.dir == KUI_DIR_LEFT || e.dir == KUI_DIR_UP) agenda_add_months(&y, &m, 1);
			else agenda_add_months(&y, &m, -1);
			go_month(y, m, 1);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(w, 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) { /* Bugün */
			today(&ty, &tm_, &td);
			if (vy != ty || vm != tm_) go_month(ty, tm_, 1);
			continue;
		}
		if (kui_rect_contains(r_prev, e.x, e.y) || kui_rect_contains(r_next, e.x, e.y)) {
			int y = vy, m = vm;
			agenda_add_months(&y, &m, kui_rect_contains(r_next, e.x, e.y) ? 1 : -1);
			go_month(y, m, 1);
			continue;
		}
		int opened = 0;
		for (int i = 0; i < GRID_ROWS * 7 && !opened; i++) {
			if (!kui_rect_contains(cells[i].r, e.x, e.y)) continue;
			draw_cell(i, 1); /* the tapped day, at once */
			kui_flush(0);
			day_screen(cells[i].y, cells[i].m, cells[i].d);
			opened = 1;
			if (!quit) go_month(dy, dm, 1); /* back on the month of the last day shown */
		}
		for (int k = 0; k < n_up_rows && !opened; k++) {
			if (!kui_rect_contains(r_up[k], e.x, e.y)) continue;
			day_screen(up_items[k].y, up_items[k].m, up_items[k].d);
			opened = 1;
			if (!quit) go_month(dy, dm, 1);
		}
	}
	kui_quit();
	return 0;
}

/* Bulmaca: a Turkish crossword a day plus the archive. The grid, the clue bar and a compact Turkish keyboard share
 * one screen; typing redraws only the cells whose look changed (partial refresh). Fully offline. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "bulmaca.h"

#define GRID_PX 858 /* 11 × 78 */
#define CLUE_Y (KUI_TOPBAR_H + 4)
#define CLUE_H 106
#define GRID_Y (CLUE_Y + CLUE_H + 8)
#define KB_Y (GRID_Y + GRID_PX + 8)
#define ARROW_W 96
#define MAX_KEYS 48
#define LIST_BAR_H 95

enum { ACT_REVEAL = 1, ACT_CHECK, ACT_DONE };

static BmDb db;
static BmPuzzle pz;
static BmState st;
static BmStats stats;
static int pz_index = -1;          /* index of the open puzzle in db */
static int cur_word = -1, cur_cell = -1, cur_dir = BM_ACROSS;
static int cell_px, gx, gy;
static long drawn[BM_CELLS];       /* look of each cell as last drawn, -1 = unknown */
static KuiKey keys[MAX_KEYS];
static int n_keys;
static KuiButton acts[3];
static char notice[160];           /* one-off message in the clue bar */
static long last_tick;

/* ---- files ---- */

static const char *data_path(const char *name)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/%s", kui_data_dir("bulmaca"), name);
	return p;
}

static const char *save_path(int id)
{
	char n[32];
	snprintf(n, sizeof n, "p%d", id);
	return data_path(n);
}

static int load_db(void)
{
	const char *root = getenv("KUI_ROOT");
	char p[300];
	snprintf(p, sizeof p, "%s/apps/bulmaca/puzzles.txt", root && *root ? root : "/mnt/us/kindle-apps");
	if (bm_db_load(p, &db) > 0) return 0;
	bm_db_free(&db);
	return bm_db_load("apps/bulmaca/data/puzzles.txt", &db) > 0 ? 0 : -1; /* development tree (simulator) */
}

static void save_state(void)
{
	if (pz_index < 0) return;
	char buf[BM_SAVE_CAP];
	int n = bm_save_format(&pz, &st, buf, sizeof buf);
	if (n > 0) kui_write_atomic(save_path(pz.id), buf, n);
}

static void save_stats(void)
{
	char buf[256];
	int n = bm_stats_format(&stats, buf, sizeof buf);
	if (n > 0) kui_write_atomic(data_path("stats"), buf, n);
}

/* progress of a puzzle as saved: -1 none, 0..99 percent filled, 100 solved */
static int saved_progress(int index)
{
	static BmPuzzle p;
	static BmState s;
	char buf[BM_SAVE_CAP];
	if (bm_db_get(&db, index, &p) != 0 || kui_read_file(save_path(p.id), buf, sizeof buf) <= 0 || bm_save_parse(buf, &p, &s) != 0) return -1;
	if (s.done) return 100;
	int pct = bm_filled_pct(&p, &s);
	return pct >= 100 ? 99 : pct;
}

static long today(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return bm_days(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static void day_text(long day, char *out, int cap)
{
	struct tm tm = { 0 };
	int y, m, d;
	bm_civil(day, &y, &m, &d);
	tm.tm_year = y - 1900;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_wday = (int) (((day % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
	kui_tr_date(&tm, out, cap);
}

/* the timer counts the time between touches, at most 2 minutes at a time (a pause does not count) */
static void tick(void)
{
	long now = (long) time(NULL);
	if (!st.done && last_tick && now > last_tick) st.seconds += (int) (now - last_tick > 120 ? 120 : now - last_tick);
	last_tick = now;
}

/* ---- geometry ---- */

static void layout(void)
{
	int n = pz.w > pz.h ? pz.w : pz.h;
	cell_px = GRID_PX / n;
	gx = (kui_width() - cell_px * pz.w) / 2;
	gy = GRID_Y + (GRID_PX - cell_px * pz.h) / 2;
	KuiRect area = { 0, KB_Y, kui_width(), kui_height() - KB_Y - 4 };
	n_keys = kui_kb_layout(area, KUI_KB_TR_LETTERS, 1, keys, MAX_KEYS);
	/* the keyboard's bottom row (123 / space / Tamam) becomes the action row */
	int y = 0, h = 0;
	for (int i = 0; i < n_keys; i++)
		if (keys[i].cp == ' ') { y = keys[i].r.y; h = keys[i].r.h; }
	int k = 0;
	for (int i = 0; i < n_keys; i++)
		if (keys[i].cp != KUI_KEY_PAGE && keys[i].cp != ' ' && keys[i].cp != KUI_KEY_ENTER) keys[k++] = keys[i];
	n_keys = k;
	int gap = 8, w = kui_width() - 2 * gap, w1 = (w - 2 * gap) * 27 / 100, w2 = w - 2 * gap - 2 * w1;
	acts[0] = (KuiButton){ { gap, y, w1, h }, T("Harf aç"), ACT_REVEAL };
	acts[1] = (KuiButton){ { 2 * gap + w1, y, w2, h }, T("Kelimeyi kontrol et"), ACT_CHECK };
	acts[2] = (KuiButton){ { 3 * gap + w1 + w2, y, w1, h }, T("Bitti"), ACT_DONE };
}

static KuiRect cell_rect(int i)
{
	int r = i / pz.w, c = i % pz.w;
	return (KuiRect){ gx + c * cell_px + 1, gy + r * cell_px + 1, cell_px - 2, cell_px - 2 };
}

static int cell_at(int x, int y)
{
	if (x < gx || y < gy || x >= gx + cell_px * pz.w || y >= gy + cell_px * pz.h) return -1;
	return (y - gy) / cell_px * pz.w + (x - gx) / cell_px;
}

static KuiRect clue_rect(void) { return (KuiRect){ 0, CLUE_Y, kui_width(), CLUE_H }; }
static KuiRect prev_rect(void) { return (KuiRect){ 8, CLUE_Y + 8, ARROW_W, CLUE_H - 16 }; }
static KuiRect next_rect(void) { return (KuiRect){ kui_width() - 8 - ARROW_W, CLUE_Y + 8, ARROW_W, CLUE_H - 16 }; }

/* ---- drawing ---- */

static int in_word(int i) { return cur_word >= 0 && bm_word_pos(&pz, cur_word, i) >= 0; }

static long cell_look(int i)
{
	return (long) st.fill[i] | (long) st.flags[i] << 21 | (long) (i == cur_cell) << 24 | (long) in_word(i) << 25;
}

static void draw_cell(int i)
{
	KuiRect rc = cell_rect(i);
	drawn[i] = cell_look(i);
	kui_mark_dirty(rc);
	if (!pz.sol[i]) {
		kui_fill(rc, 0);
		return;
	}
	int sel = i == cur_cell;
	unsigned char bg = sel ? 0xAA : in_word(i) ? 0xDD : 0xFF;
	kui_fill(rc, bg);
	if (sel) kui_frame(rc, 3, 0);
	if (pz.num[i]) {
		char n[8];
		snprintf(n, sizeof n, "%d", pz.num[i]);
		kui_text_on((KuiRect){ rc.x + 5, rc.y + 3, rc.w / 2, 22 }, n, KUI_FONT_REG, 20, KUI_ALIGN_LEFT, 0, bg);
	}
	if (st.fill[i]) {
		char s[8];
		int k = kui_utf8_encode(st.fill[i], s);
		s[k] = '\0';
		kui_text_on((KuiRect){ rc.x, rc.y + 12, rc.w, rc.h - 14 }, s, KUI_FONT_BOLD, cell_px * 6 / 10, KUI_ALIGN_CENTER, 0, bg);
	}
	if (st.flags[i] & BM_REVEALED) /* a black corner: the letter was given */
		for (int j = 0; j < 16; j++) kui_fill((KuiRect){ rc.x + rc.w - 20 + j, rc.y + 4 + j, 16 - j, 1 }, 0);
	if (st.flags[i] & BM_WRONG) /* struck through from bottom left to top right */
		for (int t = 6; t < rc.w - 6; t++) kui_fill((KuiRect){ rc.x + t, rc.y + rc.h - 1 - t * rc.h / rc.w - 2, 3, 4 }, 0x33);
}

static void refresh_cells(void)
{
	for (int i = 0; i < pz.w * pz.h; i++)
		if (drawn[i] != cell_look(i)) draw_cell(i);
}

static void draw_arrow(KuiRect r, const char *s)
{
	kui_fill(r, 255);
	kui_frame(r, 3, 0);
	kui_text_on(r, s, KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0, 255);
}

static void draw_clue(void)
{
	KuiRect bar = clue_rect();
	kui_fill(bar, 255);
	kui_fill((KuiRect){ 0, bar.y + bar.h - 2, bar.w, 2 }, 0x80);
	draw_arrow(prev_rect(), "◀");
	draw_arrow(next_rect(), "▶");
	int x = ARROW_W + 24, w = kui_width() - 2 * x;
	if (cur_word >= 0) {
		const BmWord *wd = &pz.words[cur_word];
		char head[192];
		if (notice[0]) snprintf(head, sizeof head, "%s", notice);
		else snprintf(head, sizeof head, "%d %s · %d %s", wd->num, wd->dir == BM_ACROSS ? T("Soldan sağa") : T("Yukarıdan aşağıya"), wd->len, T("harf"));
		kui_text((KuiRect){ x, bar.y + 4, w, 34 }, head, notice[0] ? KUI_FONT_BOLD : KUI_FONT_REG, kui_fit_px(head, KUI_FONT_REG, 28, w), KUI_ALIGN_CENTER,
			 notice[0] ? 0 : 0x44);
		/* the clue: one line when it fits at 38 px, else two lines */
		int px = 38;
		KuiLine ls[3];
		int n = kui_wrap_text(wd->clue, w, KUI_FONT_BOLD, px, ls, 3);
		if (n > 1) {
			px = 32;
			n = kui_wrap_text(wd->clue, w, KUI_FONT_BOLD, px, ls, 3);
			if (n > 2) n = 2;
		}
		int lh = kui_line_height(KUI_FONT_BOLD, px), y = bar.y + 40 + (bar.h - 44 - n * lh) / 2;
		for (int i = 0; i < n; i++) {
			char line[BM_CLUE];
			int len = ls[i].len < BM_CLUE - 1 ? ls[i].len : BM_CLUE - 1;
			memcpy(line, wd->clue + ls[i].off, (size_t) len);
			line[len] = '\0';
			kui_text((KuiRect){ x, y + i * lh, w, lh }, line, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
		}
	} else if (st.done) {
		char tm[32], s[96];
		bm_fmt_time(st.seconds, tm, sizeof tm);
		snprintf(s, sizeof s, "✓ %s · %s", T("Çözüldü"), tm);
		kui_text((KuiRect){ x, bar.y, w, bar.h }, s, KUI_FONT_BOLD, 40, KUI_ALIGN_CENTER, 0);
	} else {
		const char *s = T("Bir kareye dokunun");
		kui_text((KuiRect){ x, bar.y, w, bar.h }, s, KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 0x44);
	}
	kui_mark_dirty(bar);
}

static int key_ok(int cp) { return cp > 0 && bm_is_letter(cp); }

static void draw_key(const KuiKey *k)
{
	kui_fill(k->r, 255);
	if (k->cp == KUI_KEY_SHIFT) { /* the direction of the selected word: ↔ / ↕ */
		kui_frame(k->r, 3, 0);
		kui_text_on(k->r, cur_dir == BM_ACROSS ? "↔" : "↕", KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0, 255);
	} else if (k->cp > 0 && !key_ok(k->cp)) { /* Q W X are not Turkish letters */
		kui_frame(k->r, 1, 0xAA);
		kui_text_on(k->r, k->label, KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 0xAA, 255);
	} else {
		kui_frame(k->r, 3, 0);
		kui_text_on(k->r, k->label, KUI_FONT_BOLD, kui_fit_px(k->label, KUI_FONT_BOLD, 42, k->r.w - 10), KUI_ALIGN_CENTER, 0, 255);
	}
	kui_mark_dirty(k->r);
}

static void draw_dir_key(void)
{
	for (int i = 0; i < n_keys; i++)
		if (keys[i].cp == KUI_KEY_SHIFT) draw_key(&keys[i]);
}

static void draw_title(void)
{
	char t[64];
	snprintf(t, sizeof t, T("Bulmaca No. %d"), pz.id);
	kui_topbar(t, T("Menü"));
}

static void draw_all(int full)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	draw_title();
	draw_clue();
	kui_fill((KuiRect){ gx - 2, gy - 2, cell_px * pz.w + 4, cell_px * pz.h + 4 }, 0); /* the lines show between cells */
	for (int i = 0; i < pz.w * pz.h; i++) draw_cell(i);
	for (int i = 0; i < n_keys; i++) draw_key(&keys[i]);
	for (int i = 0; i < 3; i++) kui_button(&acts[i], 0);
	if (!full) kui_mark_dirty((KuiRect){ 0, 0, w, h });
	kui_flush(full);
}

static void redraw_after_dialog(void) { draw_all(0); }

/* ---- puzzle ---- */

static void select_word(int w, int cell)
{
	cur_word = w;
	if (w >= 0) cur_dir = pz.words[w].dir;
	cur_cell = cell >= 0 ? cell : w >= 0 ? bm_first_empty(&pz, &st, w) : -1;
}

static int open_puzzle(int index)
{
	if (bm_db_get(&db, index, &pz) != 0) return -1;
	pz_index = index;
	bm_state_init(&st, &pz);
	char buf[BM_SAVE_CAP];
	if (kui_read_file(save_path(pz.id), buf, sizeof buf) > 0) bm_save_parse(buf, &pz, &st);
	for (int i = 0; i < BM_CELLS; i++) drawn[i] = -1;
	notice[0] = '\0';
	layout();
	int w = 0; /* carry on at the first word with an empty square */
	while (w < pz.n_words - 1 && bm_word_full(&pz, &st, w)) w++;
	select_word(st.done ? -1 : w, -1);
	last_tick = 0;
	return 0;
}

static void show_notice(const char *msg)
{
	snprintf(notice, sizeof notice, "%s", msg);
	draw_clue();
}

static void clear_notice(void)
{
	if (notice[0]) {
		notice[0] = '\0';
		draw_clue();
	}
}

/* ---- screens: menu, archive, statistics ---- */

/* full-screen list of big buttons; the chosen index, -1 for ✕, -2 on quit */
static int choose(const char *title, const char **labels, int n)
{
	int w = kui_width();
	KuiButton bs[6];
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(title, NULL);
		for (int i = 0; i < n; i++) {
			bs[i] = (KuiButton){ { 120, KUI_TOPBAR_H + 80 + i * 180, w - 240, 150 }, labels[i], i + 1 };
			kui_button(&bs[i], 0);
		}
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -2;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
			int id = kui_buttons_hit(bs, n, e.x, e.y);
			if (id) {
				kui_button(&bs[id - 1], 1);
				kui_flush(0);
				return id - 1;
			}
		}
	}
}

static void show_stats(void)
{
	int w = kui_width(), h = kui_height();
	for (;;) {
		char s[64];
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("İstatistik"), NULL);
		const struct { const char *label; int v; } nums[4] = {
			{ T("Çözülen"), stats.solved }, { T("Yardımsız"), stats.clean }, { T("Seri"), stats.streak }, { T("En iyi seri"), stats.best }
		};
		int cw = (w - 80) / 4;
		for (int i = 0; i < 4; i++) {
			snprintf(s, sizeof s, "%d", nums[i].v);
			kui_text((KuiRect){ 40 + i * cw, KUI_TOPBAR_H + 60, cw, 120 }, s, KUI_FONT_BOLD, 90, KUI_ALIGN_CENTER, 0);
			kui_text((KuiRect){ 40 + i * cw, KUI_TOPBAR_H + 190, cw, 60 }, nums[i].label, KUI_FONT_REG,
				 kui_fit_px(nums[i].label, KUI_FONT_REG, 34, cw - 8), KUI_ALIGN_CENTER, 0);
		}
		char t1[32] = "–", t2[32] = "–", line[160];
		if (stats.best_sec) bm_fmt_time(stats.best_sec, t1, sizeof t1);
		if (stats.solved && stats.total_sec) bm_fmt_time(stats.total_sec / stats.solved, t2, sizeof t2);
		snprintf(line, sizeof line, "%s: %s", T("En kısa süre"), t1);
		kui_text((KuiRect){ 80, KUI_TOPBAR_H + 330, w - 160, 70 }, line, KUI_FONT_REG, 44, KUI_ALIGN_LEFT, 0);
		snprintf(line, sizeof line, "%s: %s", T("Ortalama süre"), t2);
		kui_text((KuiRect){ 80, KUI_TOPBAR_H + 410, w - 160, 70 }, line, KUI_FONT_REG, 44, KUI_ALIGN_LEFT, 0);
		const char *note = T("Seri: günün bulmacasını kendi gününde çözdüğünüz ardışık günler. Yardımsız: hiç harf açmadan.");
		KuiLine ls[6];
		int n = kui_wrap_text(note, w - 160, KUI_FONT_REG, 32, ls, 6), lh = kui_line_height(KUI_FONT_REG, 32);
		for (int i = 0; i < n; i++) {
			char b[256];
			int len = ls[i].len < 255 ? ls[i].len : 255;
			memcpy(b, note + ls[i].off, (size_t) len);
			b[len] = '\0';
			kui_text((KuiRect){ 80, KUI_TOPBAR_H + 540 + i * lh, w - 160, lh }, b, KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0x44);
		}
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type == KUI_EV_TAP && kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		}
	}
}

/* the archive: one row per day, today first, back through every puzzle; returns a puzzle index, -1 back, -2 quit */
static int archive(void)
{
	int w = kui_width(), h = kui_height(), row_h = 104, y0 = KUI_TOPBAR_H + 16;
	int bar_y = h - LIST_BAR_H - 16, per = (bar_y - 16 - y0) / row_h, n = db.n;
	int pages = (n + per - 1) / per, page = 0, full = 1;
	long t0 = today();
	KuiButton prev = { { 20, bar_y, 150, LIST_BAR_H }, "◀", 1 }, next = { { w - 170, bar_y, 150, LIST_BAR_H }, "▶", 2 };
	for (int redraw = 1;;) {
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T("Arşiv"), NULL);
			for (int i = 0; i < per; i++) {
				int k = page * per + i;
				if (k >= n) break;
				long day = t0 - k;
				int idx = bm_daily(day, n), prog = saved_progress(idx);
				KuiRect r = { 40, y0 + i * row_h, w - 80, row_h };
				char a[48], d[96], s[48] = "";
				snprintf(a, sizeof a, T("No. %d"), idx + 1);
				day_text(day, d, sizeof d);
				if (k == 0) snprintf(d, sizeof d, "%s", T("Bugün"));
				if (prog == 100) snprintf(s, sizeof s, "✓ %s", T("Çözüldü"));
				else if (prog >= 0) snprintf(s, sizeof s, "%%%d", prog);
				int sw = 240;
				kui_text((KuiRect){ r.x + 10, r.y, 190, r.h }, a, KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
				kui_text((KuiRect){ r.x + 200, r.y, r.w - 210 - sw, r.h }, d, KUI_FONT_REG, kui_fit_px(d, KUI_FONT_REG, 36, r.w - 210 - sw), KUI_ALIGN_LEFT, 0);
				kui_text((KuiRect){ r.x + r.w - sw, r.y, sw - 10, r.h }, s, prog == 100 ? KUI_FONT_BOLD : KUI_FONT_REG, 36, KUI_ALIGN_RIGHT, 0);
				kui_fill((KuiRect){ r.x, r.y + r.h - 2, r.w, 1 }, 0x80);
			}
			kui_button(&prev, 0);
			kui_button(&next, 0);
			char pl[32];
			snprintf(pl, sizeof pl, "%d / %d", page + 1, pages);
			kui_text((KuiRect){ 190, bar_y, w - 380, LIST_BAR_H }, pl, KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 0);
			if (!full) kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
			kui_flush(full); /* entering: full refresh; page turns: partial */
			full = redraw = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return -2;
		if (e.type == KUI_EV_WAKE) {
			full = redraw = 1;
			continue;
		}
		int dlt = 0;
		if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
			KuiButton bs[2] = { prev, next };
			int id = kui_buttons_hit(bs, 2, e.x, e.y);
			if (id) dlt = id == 1 ? -1 : 1;
			else if (e.y >= y0 && e.y < y0 + per * row_h) {
				int k = page * per + (e.y - y0) / row_h;
				if (k < n) return bm_daily(t0 - k, n);
			}
		}
		if (dlt && page + dlt >= 0 && page + dlt < pages) {
			page += dlt;
			redraw = 1;
		}
	}
}

/* returns 1 to quit */
static int menu(void)
{
	for (;;) {
		const char *labels[] = { T("Günün bulmacası"), T("Arşiv"), T("İstatistik"), T("Bulmacayı sıfırla") };
		int k = choose(T("Menü"), labels, 4);
		if (k == -2) return 1;
		if (k == 0) {
			save_state();
			open_puzzle(bm_daily(today(), db.n));
		} else if (k == 1) {
			int idx = archive();
			if (idx == -2) return 1;
			if (idx < 0) continue;
			save_state();
			open_puzzle(idx);
		} else if (k == 2) {
			show_stats();
			continue;
		} else if (k == 3) {
			const char *ch[] = { T("Sıfırla"), T("Vazgeç") };
			int r = kui_dialog(T("Bulmacayı sıfırla"), T("Bu bulmacadaki bütün harfler silinsin mi?"), ch, 2, NULL);
			if (r == 0) {
				remove(save_path(pz.id));
				open_puzzle(pz_index);
			}
		}
		draw_all(1);
		return 0;
	}
}

/* ---- play ---- */

static int finish(void)
{
	tick();
	st.done = 1;
	long t = today();
	bm_stats_record(&stats, bm_daily(t, db.n) == pz_index ? t : -1, st.seconds, !st.helped);
	save_stats();
	save_state();
	cur_word = cur_cell = -1;
	refresh_cells();
	draw_clue();
	char tm[32], msg[256];
	bm_fmt_time(st.seconds, tm, sizeof tm);
	snprintf(msg, sizeof msg, st.helped ? T("Bulmacayı %s içinde çözdünüz.") : T("Bulmacayı %s içinde, hiç harf açmadan çözdünüz."), tm);
	const char *ch[] = { T("Arşiv"), T("Tamam") };
	int k = kui_dialog(T("Tebrikler!"), msg, ch, 2, redraw_after_dialog);
	if (k == 0) {
		int idx = archive();
		if (idx == -2) return 1;
		if (idx >= 0) open_puzzle(idx);
		draw_all(1);
		return 0;
	}
	draw_all(0);
	return 0;
}

static int check_done(void)
{
	int empty = 0, wrong = bm_check_all(&pz, &st, &empty);
	if (!wrong && !empty) return finish();
	char msg[200];
	if (wrong && empty) snprintf(msg, sizeof msg, T("%d yanlış harf (çizildi), %d boş kare var."), wrong, empty);
	else if (wrong) snprintf(msg, sizeof msg, T("%d yanlış harf var; çizili kareler."), wrong);
	else snprintf(msg, sizeof msg, T("Harfler doğru; %d boş kare kaldı."), empty);
	const char *ok[] = { T("Tamam") };
	if (kui_dialog(T("Henüz bitmedi"), msg, ok, 1, redraw_after_dialog) < 0 && kui_quit_pending()) return 1;
	save_state();
	draw_all(0);
	return 0;
}

static void check_word(void)
{
	if (cur_word < 0) return;
	int empty = 0, wrong = bm_check_word(&pz, &st, cur_word, &empty);
	char msg[160];
	if (wrong) snprintf(msg, sizeof msg, T("%d yanlış harf"), wrong);
	else if (empty) snprintf(msg, sizeof msg, T("Şimdilik doğru, %d boş kare"), empty);
	else snprintf(msg, sizeof msg, "%s", T("Doğru!"));
	refresh_cells();
	show_notice(msg);
}

/* after a letter: the cursor moves on; a completed puzzle ends the game. Returns 1 to quit */
static int after_change(void)
{
	save_state();
	refresh_cells();
	kui_flush(0);
	return bm_solved(&pz, &st) ? finish() : 0;
}

static int on_key(int cp)
{
	if (cp == KUI_KEY_SHIFT) {
		if (cur_cell < 0) return 0;
		int d = cur_dir, w = bm_select(&pz, cur_cell, cur_dir, 1, &d);
		if (w >= 0) {
			cur_word = w;
			cur_dir = d;
		}
		clear_notice();
		draw_clue();
		draw_dir_key();
		refresh_cells();
		kui_flush(0);
		return 0;
	}
	if (cur_cell < 0 || cur_word < 0 || st.done) return 0;
	clear_notice();
	if (cp == KUI_KEY_BACK) {
		if (!bm_erase(&pz, &st, cur_cell)) {
			cur_cell = bm_prev_cell(&pz, cur_word, cur_cell);
			bm_erase(&pz, &st, cur_cell);
		}
		return after_change();
	}
	if (!key_ok(cp)) return 0;
	bm_type(&pz, &st, cur_cell, cp);
	cur_cell = bm_next_cell(&pz, &st, cur_word, cur_cell);
	return after_change();
}

static int on_action(int id)
{
	if (st.done) return 0;
	clear_notice();
	if (id == ACT_REVEAL && cur_word >= 0) {
		/* the selected cell, or (when it is already right) the first wrong or empty one in the word */
		int c = cur_cell;
		if (c < 0 || st.fill[c] == pz.sol[c])
			for (int i = 0; i < pz.words[cur_word].len; i++) {
				int k = bm_word_cell(&pz, cur_word, i);
				if (st.fill[k] != pz.sol[k]) { c = k; break; }
			}
		if (c >= 0 && bm_reveal(&pz, &st, c)) {
			cur_cell = bm_next_cell(&pz, &st, cur_word, c);
			return after_change();
		}
		return 0;
	}
	if (id == ACT_CHECK) {
		check_word();
		save_state();
		kui_flush(0);
		return 0;
	}
	if (id == ACT_DONE) return check_done();
	return 0;
}

int main(void)
{
	if (kui_init("bulmaca") != 0) return 1;
	if (load_db() != 0) {
		const char *ok[] = { T("Tamam") };
		kui_dialog(T("Bulmaca"), T("Bulmacalar yüklenemedi. Uygulamayı yeniden kurun."), ok, 1, NULL);
		kui_quit();
		return 1;
	}
	char buf[256];
	if (kui_read_file(data_path("stats"), buf, sizeof buf) > 0) bm_stats_parse(buf, &stats);
	if (open_puzzle(bm_daily(today(), db.n)) != 0 && open_puzzle(0) != 0) {
		kui_quit();
		return 1;
	}
	draw_all(1);
	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) {
			last_tick = 0;
			draw_all(1);
			continue;
		}
		tick();
		if (e.type == KUI_EV_SWIPE && cur_word >= 0 && (e.dir == KUI_DIR_LEFT || e.dir == KUI_DIR_RIGHT)) {
			clear_notice();
			select_word(bm_step_word(&pz, cur_word, e.dir == KUI_DIR_LEFT ? 1 : -1), -1);
			draw_clue();
			draw_dir_key();
			refresh_cells();
			kui_flush(0);
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		int t = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (t == KUI_BTN_EXIT) break;
		if (t == KUI_BTN_MENU) {
			save_state();
			quit = menu();
			continue;
		}
		int c = cell_at(e.x, e.y);
		if (c >= 0) {
			if (!pz.sol[c]) continue;
			int d = cur_dir, w = bm_select(&pz, c, cur_dir, c == cur_cell, &d);
			if (w < 0) continue;
			notice[0] = '\0';
			cur_dir = d;
			select_word(w, c);
			draw_clue();
			draw_dir_key();
			refresh_cells();
			kui_flush(0);
			continue;
		}
		if (kui_rect_contains(prev_rect(), e.x, e.y) || kui_rect_contains(next_rect(), e.x, e.y)) {
			int dlt = kui_rect_contains(next_rect(), e.x, e.y) ? 1 : -1;
			notice[0] = '\0';
			select_word(cur_word >= 0 ? bm_step_word(&pz, cur_word, dlt) : 0, -1);
			draw_clue();
			draw_dir_key();
			refresh_cells();
			kui_flush(0);
			continue;
		}
		int id = kui_buttons_hit(acts, 3, e.x, e.y);
		if (id) {
			quit = on_action(id);
			continue;
		}
		int k = kui_kb_hit(keys, n_keys, e.x, e.y);
		if (k >= 0) quit = on_key(keys[k].cp);
	}
	tick();
	save_state();
	bm_db_free(&db);
	kui_quit();
	return 0;
}

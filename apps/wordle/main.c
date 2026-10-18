/* Kelime (Turkish Wordle): 5 letters, 6 tries, unlimited games, root words only.
 * Marks for e-ink: correct = black cell with white letter, wrong place = gray cell, absent = struck-through letter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "wordle.h"

#define CELL 140
#define GAP 10
#define GRID_Y (KUI_TOPBAR_H + 15)
#define MSG_Y (GRID_Y + WL_TRIES * (CELL + GAP) + 5)
#define KB_Y (MSG_Y + 65)

static WlList guesses, answers;
static WlGame game;
static WlKb kb;
static WlStats stats;
static char typed[WL_BYTES];
static int over; /* 0 playing, 1 won, 2 lost */
static KuiKey keys[48];
static int n_keys;

static int grid_x(void) { return (kui_width() - (WL_LEN * CELL + (WL_LEN - 1) * GAP)) / 2; }
static KuiRect cell_rect(int row, int col) { return (KuiRect){ grid_x() + col * (CELL + GAP), GRID_Y + row * (CELL + GAP), CELL, CELL }; }

static const char *path(const char *name)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/%s", kui_data_dir("wordle"), name);
	return p;
}

static int load_words(void)
{
	const char *root = getenv("KUI_ROOT");
	char a[300], g[300];
	snprintf(g, sizeof g, "%s/apps/wordle/guesses.txt", root && *root ? root : "/mnt/us/kindle-apps");
	snprintf(a, sizeof a, "%s/apps/wordle/answers.txt", root && *root ? root : "/mnt/us/kindle-apps");
	if (wl_load(g, &guesses) <= 0 || wl_load(a, &answers) <= 0) { /* development tree layout (simulator) */
		wl_free(&guesses);
		wl_free(&answers);
		if (wl_load("apps/wordle/words/guesses.txt", &guesses) <= 0 || wl_load("apps/wordle/words/answers.txt", &answers) <= 0) return -1;
	}
	return 0;
}

static void save_all(void)
{
	char buf[512];
	int n = wl_stats_format(&stats, buf, sizeof buf);
	if (n > 0) kui_write_atomic(path("stats"), buf, n);
	if (!over) {
		n = wl_game_format(&game, buf, sizeof buf);
		if (n > 0) kui_write_atomic(path("game"), buf, n);
	} else {
		remove(path("game"));
	}
}

static void rebuild_kb(void)
{
	memset(&kb, 0, sizeof kb);
	over = 0;
	for (int i = 0; i < game.n; i++) {
		WlMark m[WL_LEN];
		wl_score(game.answer, game.guess[i], m);
		wl_kb_update(&kb, game.guess[i], m);
		if (!strcmp(game.guess[i], game.answer)) over = 1;
	}
	if (!over && game.n == WL_TRIES) over = 2;
}

static void new_game(void)
{
	memset(&game, 0, sizeof game);
	int i = wl_pick(&answers, (unsigned) time(NULL) ^ (unsigned) stats.played * 7919u);
	snprintf(game.answer, sizeof game.answer, "%s", answers.w[i]);
	typed[0] = '\0';
	rebuild_kb();
	save_all();
}

/* one letter cell; mark < 0 = typed but not yet scored */
static void draw_cell(int row, int col, const char *letter, int mark)
{
	KuiRect r = cell_rect(row, col);
	char up[8] = "";
	if (letter && *letter) kui_tr_upper_str(letter, up, sizeof up);
	kui_fill(r, 255);
	if (mark == WL_CORRECT) {
		kui_fill(r, 0);
		kui_text_on(r, up, KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, 255, 0);
	} else if (mark == WL_PRESENT) {
		kui_fill(r, 0xAA);
		kui_frame(r, 3, 0);
		kui_text(r, up, KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, 0);
	} else {
		kui_frame(r, mark == WL_ABSENT ? 2 : (up[0] ? 5 : 2), 0);
		kui_text(r, up, KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, 0);
		if (mark == WL_ABSENT) kui_fill((KuiRect){ r.x + 25, r.y + r.h / 2 - 2, r.w - 50, 5 }, 0);
	}
	kui_mark_dirty(r);
}

/* the n-th letter (UTF-8) of s into out */
static void nth(const char *s, int n, char out[8])
{
	out[0] = '\0';
	for (int i = 0; *s; i++) {
		int cp, k = kui_utf8_decode(s, &cp);
		if (i == n) {
			memcpy(out, s, (size_t) k);
			out[k] = '\0';
			return;
		}
		s += k;
	}
}

static void draw_row(int row)
{
	char l[8];
	if (row < game.n) {
		WlMark m[WL_LEN];
		wl_score(game.answer, game.guess[row], m);
		for (int c = 0; c < WL_LEN; c++) {
			nth(game.guess[row], c, l);
			draw_cell(row, c, l, m[c]);
		}
	} else {
		for (int c = 0; c < WL_LEN; c++) {
			nth(row == game.n ? typed : "", c, l);
			draw_cell(row, c, l, -1);
		}
	}
}

static void message(const char *msg)
{
	KuiRect r = { 0, MSG_Y, kui_width(), 60 };
	kui_fill(r, 255);
	if (msg && *msg) kui_text(r, msg, KUI_FONT_BOLD, 40, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(r);
}

static int key_letter(int cp) { return cp > 0 && cp != 'Q' && cp != 'W' && cp != 'X'; }

static void draw_keyboard(void)
{
	int w = kui_width();
	KuiRect area = { 0, KB_Y, w, kui_height() - KB_Y - 5 };
	n_keys = kui_kb_layout(area, KUI_KB_TR_LETTERS, 1, keys, 48);
	kui_fill(area, 255);
	for (int i = 0; i < n_keys; i++) {
		KuiKey *k = &keys[i];
		const char *label = k->label;
		if (k->cp == KUI_KEY_SHIFT || k->cp == KUI_KEY_PAGE) continue; /* not used here */
		if (k->cp == ' ') label = "Sil";
		if (k->cp == KUI_KEY_BACK) label = "⌫";
		if (k->cp == KUI_KEY_ENTER) label = "Gönder";
		int px = kui_fit_px(label, KUI_FONT_BOLD, 44, k->r.w - 12);
		int m = key_letter(k->cp) ? (int) wl_kb_get(&kb, kui_tr_lower(k->cp)) : WL_UNKNOWN;
		if (k->cp > 0 && !key_letter(k->cp)) { /* Q W X are not Turkish letters */
			kui_frame(k->r, 1, 0xAA);
			kui_text(k->r, label, KUI_FONT_REG, px, KUI_ALIGN_CENTER, 0xAA);
			continue;
		}
		if (m == WL_CORRECT) {
			kui_fill(k->r, 0);
			kui_text_on(k->r, label, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 255, 0);
		} else if (m == WL_PRESENT) {
			kui_fill(k->r, 0xAA);
			kui_frame(k->r, 3, 0);
			kui_text(k->r, label, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, 0);
		} else {
			kui_frame(k->r, 3, m == WL_ABSENT ? 0xAA : 0);
			kui_text(k->r, label, KUI_FONT_BOLD, px, KUI_ALIGN_CENTER, m == WL_ABSENT ? 0x99 : 0);
		}
	}
	kui_mark_dirty(area);
}

static void draw_all(void)
{
	int w = kui_width();
	kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
	kui_topbar("Kelime Oyunu", "İstatistik");
	for (int r = 0; r < WL_TRIES; r++) draw_row(r);
	message("");
	draw_keyboard();
	kui_flush(1);
}

static void show_stats(void)
{
	int w = kui_width(), h = kui_height();
	for (;;) {
		char s[96];
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar("İstatistik", NULL);
		int pct = stats.played ? stats.won * 100 / stats.played : 0;
		const struct { const char *label; int v; } nums[4] = {
			{ "Oynanan", stats.played }, { "Kazanma %", pct }, { "Seri", stats.streak }, { "En iyi seri", stats.best }
		};
		int cw = (w - 80) / 4;
		for (int i = 0; i < 4; i++) {
			snprintf(s, sizeof s, "%d", nums[i].v);
			kui_text((KuiRect){ 40 + i * cw, KUI_TOPBAR_H + 60, cw, 120 }, s, KUI_FONT_BOLD, 90, KUI_ALIGN_CENTER, 0);
			kui_text((KuiRect){ 40 + i * cw, KUI_TOPBAR_H + 190, cw, 60 }, nums[i].label, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
		}
		kui_text((KuiRect){ 40, KUI_TOPBAR_H + 320, w - 80, 70 }, "Tahmin dağılımı", KUI_FONT_BOLD, 44, KUI_ALIGN_LEFT, 0);
		int mx = 1;
		for (int i = 0; i < WL_TRIES; i++)
			if (stats.dist[i] > mx) mx = stats.dist[i];
		for (int i = 0; i < WL_TRIES; i++) {
			int y = KUI_TOPBAR_H + 410 + i * 100, bw = 80 + (w - 300) * stats.dist[i] / mx;
			snprintf(s, sizeof s, "%d", i + 1);
			kui_text((KuiRect){ 40, y, 60, 80 }, s, KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0);
			kui_fill((KuiRect){ 120, y + 5, bw, 70 }, 0);
			snprintf(s, sizeof s, "%d", stats.dist[i]);
			kui_text_on((KuiRect){ 120, y + 5, bw - 20, 70 }, s, KUI_FONT_BOLD, 40, KUI_ALIGN_RIGHT, 255, 0);
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

/* end of a game: record it, show the answer, offer a new one; returns 0 to quit */
static int finish(void)
{
	wl_stats_record(&stats, over == 1, game.n);
	save_all();
	char msg[128], up[WL_BYTES * 2];
	kui_tr_upper_str(game.answer, up, sizeof up);
	static const char *const praise[] = { "Dâhice!", "Muhteşem!", "Harika!", "Çok iyi!", "Güzel!", "Son anda!" };
	if (over == 1) snprintf(msg, sizeof msg, "%s %d/6 tahminde bildin.", praise[game.n - 1], game.n);
	else snprintf(msg, sizeof msg, "Kelime: %s", up);
	const char *ch[] = { "Yeni oyun", "İstatistik", "Çıkış" };
	for (;;) {
		int k = kui_dialog(over == 1 ? "Tebrikler!" : "Bu sefer olmadı", msg, ch, 3, draw_all);
		if (k == 1) {
			show_stats();
			draw_all();
			continue;
		}
		if (k != 0) return 0;
		new_game();
		draw_all();
		return 1;
	}
}

static void submit(void)
{
	char norm[WL_BYTES];
	if (wl_normalize(typed, norm) != WL_LEN) {
		message("5 harf girin");
		kui_flush(0);
		return;
	}
	if (!wl_contains(&guesses, norm)) {
		message("Sözlükte yok (yalnızca kök kelimeler)");
		kui_flush(0);
		return;
	}
	snprintf(game.guess[game.n], sizeof game.guess[game.n], "%s", norm);
	game.n++;
	typed[0] = '\0';
	WlMark m[WL_LEN];
	wl_score(game.answer, norm, m);
	wl_kb_update(&kb, norm, m);
	if (!strcmp(norm, game.answer)) over = 1;
	else if (game.n == WL_TRIES) over = 2;
	draw_row(game.n - 1);
	message("");
	draw_keyboard();
	if (!over) save_all();
	kui_flush(0);
}

int main(void)
{
	if (kui_init("wordle") != 0) return 1;
	if (load_words() != 0) {
		const char *ok[] = { "Tamam" };
		kui_dialog("Kelime Oyunu", "Kelime listesi bulunamadı. Uygulamayı yeniden kurun.", ok, 1, NULL);
		kui_quit();
		return 1;
	}
	char buf[512];
	if (kui_read_file(path("stats"), buf, sizeof buf) > 0) wl_stats_parse(buf, &stats);
	/* unfinished game: carry on where it was left */
	if (kui_read_file(path("game"), buf, sizeof buf) > 0 && wl_game_parse(buf, &game) == 0 && wl_contains(&guesses, game.answer)) {
		rebuild_kb();
		if (over) new_game();
	} else {
		new_game();
	}
	draw_all();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw_all(); continue; }
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			show_stats();
			draw_all();
			continue;
		}
		int k = kui_kb_hit(keys, n_keys, e.x, e.y);
		if (k < 0) continue;
		int cp = keys[k].cp, len = (int) strlen(typed);
		if (cp == KUI_KEY_ENTER) {
			submit();
			if (over && !finish()) break;
			continue;
		}
		if (cp == KUI_KEY_BACK) {
			if (len) {
				do len--;
				while (len > 0 && ((unsigned char) typed[len] & 0xC0) == 0x80);
				typed[len] = '\0';
			}
		} else if (cp == ' ') {
			typed[0] = '\0'; /* "Sil": clear the row */
		} else if (key_letter(cp) && kui_utf8_len(typed) < WL_LEN) {
			char enc[4];
			int n = kui_utf8_encode(kui_tr_lower(cp), enc);
			if (len + n < (int) sizeof typed) {
				memcpy(typed + len, enc, (size_t) n);
				typed[len + n] = '\0';
			}
		} else {
			continue;
		}
		draw_row(game.n);
		message("");
		kui_flush(0);
	}
	save_all();
	wl_free(&guesses);
	wl_free(&answers);
	kui_quit();
	return 0;
}

/* Satranç Bulmacası: Lichess's daily puzzle (and random ones) on e-ink. The day's puzzle and every puzzle opened are
 * cached, so they work offline; solved puzzles and the streak are kept in a history. Each square remembers what was
 * last drawn on it: a move redraws only the squares whose look changed (partial refresh). Puzzles: lichess.org, CC0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "chess/chess_rules.h"
#include "kui.h"
#include "net/net.h"
#include "puzzle.h"
#include "san.h"

#define SQ 120
#define FETCH_TIMEOUT_S 20
#define PAUSE_MS 700 /* the opponent's reply / taking back a wrong move follows after this */

enum { BTN_HINT = 1, BTN_SOLUTION, BTN_REFRESH, BTN_RESTART, BTN_ANOTHER };
enum { PEND_NONE, PEND_REPLY, PEND_TAKEBACK };

static PzPlay play;
static Board shown;    /* what the board shows: play.pos, or a wrong move before it is taken back */
static int cur_daily;  /* the current puzzle is the day's puzzle */
static long cur_when;  /* when it was fetched */
static int cur_offline; /* a fetch failed: showing the cache */
static int selected = -1, last_from = -1, last_to = -1, hint_sq = -1, pending;
static int saved_last_from, saved_last_to; /* the last-move marks under a wrong move */
static int drawn[64];
static char msg[160], msg_drawn[160];
static KuiRect board_r, msg_r;
static KuiButton buttons[3];
static char btn_drawn[3][64];
static PzHist hist[PZ_HIST_MAX];
static int n_hist;

/* ---- files ---- */

static const char *data_path(const char *name)
{
	static char p[2][300];
	static int k;
	char *o = p[k++ & 1];
	snprintf(o, sizeof p[0], "%s/%s", kui_data_dir("puzzle"), name);
	return o;
}

static const char *archive_path(const char *id)
{
	char name[40];
	snprintf(name, sizeof name, "p_%s.json", id);
	return data_path(name);
}

static int today(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

static void load_history(void)
{
	static char buf[PZ_HIST_MAX * 48 + 16];
	n_hist = kui_read_file(data_path("history.txt"), buf, sizeof buf) > 0 ? pz_hist_parse(buf, hist, PZ_HIST_MAX) : 0;
}

static void record_result(void)
{
	if (pz_hist_find(hist, n_hist, play.pz.id) >= 0) return;
	if (n_hist == PZ_HIST_MAX) unlink(archive_path(hist[0].id)); /* the oldest leaves the history */
	PzHist e = { "", today(), pz_play_result(&play), play.pz.rating, cur_daily };
	snprintf(e.id, sizeof e.id, "%s", play.pz.id);
	n_hist = pz_hist_add(hist, n_hist, PZ_HIST_MAX, e);
	static char buf[PZ_HIST_MAX * 48 + 16];
	int len = pz_hist_format(hist, n_hist, buf, sizeof buf);
	if (len > 0) kui_write_atomic(data_path("history.txt"), buf, len);
}

/* parses a cached response; 0 ok */
static int load_file(const char *path, Puzzle *p, long *when)
{
	static char buf[65536];
	struct stat st;
	if (kui_read_file(path, buf, sizeof buf) <= 0 || pz_parse(buf, p) != 0) return -1;
	*when = stat(path, &st) == 0 ? (long) st.st_mtime : 0;
	return 0;
}

/* ---- network: Wi-Fi is switched on for the fetch when it was off, and off again afterwards ---- */

static int fetch_puzzle(const char *url, Puzzle *p)
{
	int was = kui_wifi_enabled();
	if (was == 0) {
		kui_wifi_enable(1);
		kui_wifi_wait(40);
	}
	char *body = NULL;
	int len = 0, ok = 0;
	if (net_get(url, &body, &len, FETCH_TIMEOUT_S) == 0 && pz_parse(body, p) == 0) {
		kui_write_atomic(archive_path(p->id), body, len);
		if (!strcmp(url, PZ_URL_DAILY)) {
			kui_write_atomic(data_path("daily.json"), body, len);
			kui_cfg_set_int(data_path("daily.cfg"), "day", today());
		}
		ok = 1;
	}
	free(body);
	if (was == 0) kui_wifi_enable(0);
	return ok ? 0 : -1;
}

/* ---- geometry ---- */

static KuiRect square_rect(int sq)
{
	int f = sq & 7, r = sq >> 3, white = play.player == 0;
	int col = white ? f : 7 - f, row = white ? 7 - r : r;
	return (KuiRect){ board_r.x + col * SQ, board_r.y + row * SQ, SQ, SQ };
}

static int square_at(int x, int y)
{
	if (!kui_rect_contains(board_r, x, y)) return -1;
	int col = (x - board_r.x) / SQ, row = (y - board_r.y) / SQ, white = play.player == 0;
	int f = white ? col : 7 - col, r = white ? 7 - row : row;
	return r * 8 + f;
}

/* ---- drawing ---- */

static const char *glyph_solid[7] = { "", "♟", "♞", "♝", "♜", "♛", "♚" };
static const char *glyph_outline[7] = { "", "♙", "♘", "♗", "♖", "♕", "♔" };

static int is_target(int sq)
{
	if (selected < 0) return 0;
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&play.pos, ms);
	for (int i = 0; i < n; i++)
		if (ms[i].from == selected && ms[i].to == sq) return 1;
	return 0;
}

static int square_look(int sq)
{
	return shown.sq[sq] | (sq == selected) << 5 | is_target(sq) << 6 | (sq == last_from || sq == last_to) << 7 | (sq == hint_sq) << 8 |
	       play.player << 9;
}

static void draw_square(int sq)
{
	KuiRect r = square_rect(sq);
	int dark = ((sq & 7) + (sq >> 3)) % 2 == 0;
	unsigned char bg = dark ? 0xBB : 0xFF;
	kui_fill(r, bg);
	if (sq == last_from || sq == last_to) kui_frame((KuiRect){ r.x + 4, r.y + 4, r.w - 8, r.h - 8 }, 3, 0x55);
	if (sq == hint_sq) { /* corner brackets: unlike the selection frame */
		int L = 34, t = 9;
		for (int c = 0; c < 4; c++) {
			int x = c & 1 ? r.x + r.w - 2 - L : r.x + 2, y = c & 2 ? r.y + r.h - 2 - t : r.y + 2;
			int vx = c & 1 ? r.x + r.w - 2 - t : r.x + 2, vy = c & 2 ? r.y + r.h - 2 - L : r.y + 2;
			kui_fill((KuiRect){ x, y, L, t }, 0);
			kui_fill((KuiRect){ vx, vy, t, L }, 0);
		}
	}
	int p = shown.sq[sq];
	if (p != EMPTY) {
		int c = chess_piece_color(p), t = c ? p - 6 : p;
		if (c == 0) kui_text_on(r, glyph_solid[t], KUI_FONT_REG, 98, KUI_ALIGN_CENTER, 0xFF, bg); /* white body */
		kui_text(r, c == 0 ? glyph_outline[t] : glyph_solid[t], KUI_FONT_REG, 98, KUI_ALIGN_CENTER, 0);
	}
	if (is_target(sq)) {
		if (p == EMPTY) kui_fill((KuiRect){ r.x + (SQ - 26) / 2, r.y + (SQ - 26) / 2, 26, 26 }, 0x33);
		else kui_frame((KuiRect){ r.x + 2, r.y + 2, r.w - 4, r.h - 4 }, 6, 0x33);
	}
	if (sq == selected) kui_frame(r, 8, 0);
	kui_mark_dirty(r);
	drawn[sq] = square_look(sq);
}

static void draw_msg(int force)
{
	if (!force && !strcmp(msg, msg_drawn)) return;
	kui_fill(msg_r, 255);
	kui_text(msg_r, msg, KUI_FONT_BOLD, kui_fit_px(msg, KUI_FONT_BOLD, 40, msg_r.w), KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(msg_r);
	snprintf(msg_drawn, sizeof msg_drawn, "%s", msg);
}

/* the three buttons' labels follow the puzzle state */
static void set_buttons(void)
{
	int done = pz_play_done(&play);
	buttons[0].label = done ? T("Baştan başla") : T("İpucu");
	buttons[0].id = done ? BTN_RESTART : BTN_HINT;
	buttons[1].label = done ? T("Başka bulmaca") : play.shown ? T("Sonraki hamle") : T("Çözümü göster");
	buttons[1].id = done ? BTN_ANOTHER : BTN_SOLUTION;
	buttons[2].label = T("Yenile");
	buttons[2].id = BTN_REFRESH;
}

static void draw_buttons(int force)
{
	set_buttons();
	for (int i = 0; i < 3; i++) {
		if (!force && !strcmp(btn_drawn[i], buttons[i].label)) continue;
		kui_button(&buttons[i], 0);
		snprintf(btn_drawn[i], sizeof btn_drawn[i], "%s", buttons[i].label);
	}
}

/* redraws only what changed, partial refresh */
static void update(void)
{
	for (int sq = 0; sq < 64; sq++)
		if (drawn[sq] != square_look(sq)) draw_square(sq);
	draw_msg(0);
	draw_buttons(0);
	kui_flush(0);
}

static void fmt_when(long when, char *out, int cap)
{
	time_t t = (time_t) when, now = time(NULL);
	struct tm tm, tn;
	localtime_r(&t, &tm);
	localtime_r(&now, &tn);
	if (tm.tm_yday == tn.tm_yday && tm.tm_year == tn.tm_year) snprintf(out, (size_t) cap, "%02d:%02d", tm.tm_hour, tm.tm_min);
	else snprintf(out, (size_t) cap, "%02d.%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
}

static void draw_all(void)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Satranç Bulmacası"), T("Menü"));
	char s[300], th[256];
	snprintf(s, sizeof s, "%s%s%s · ", cur_daily ? T("Günün bulmacası") : "", cur_daily ? " · " : "", play.player ? T("Siyah oynar") : T("Beyaz oynar"));
	int o = (int) strlen(s);
	snprintf(s + o, sizeof s - (size_t) o, T("Puan %d"), play.pz.rating);
	KuiRect info = { 20, KUI_TOPBAR_H + 8, w - 40, 58 };
	kui_text(info, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 40, info.w), KUI_ALIGN_CENTER, 0);
	pz_themes_text(&play.pz, kui_t, th, sizeof th);
	KuiRect themes = { 20, info.y + info.h, w - 40, 44 };
	kui_text(themes, th, KUI_FONT_REG, kui_fit_px(th, KUI_FONT_REG, 30, themes.w), KUI_ALIGN_CENTER, 0);
	board_r = (KuiRect){ (w - 8 * SQ) / 2, themes.y + themes.h + 12, 8 * SQ, 8 * SQ };
	kui_frame((KuiRect){ board_r.x - 4, board_r.y - 4, board_r.w + 8, board_r.h + 8 }, 4, 0);
	msg_r = (KuiRect){ 20, board_r.y + board_r.h + 8, w - 40, 66 };
	int by = msg_r.y + msg_r.h + 6, bw = (w - 4 * 30) / 3;
	for (int i = 0; i < 3; i++) buttons[i].r = (KuiRect){ 30 + i * (bw + 30), by, bw, 104 };
	draw_buttons(1);
	char when[32];
	fmt_when(cur_when, when, sizeof when);
	snprintf(s, sizeof s, cur_offline ? T("Bulmaca: lichess.org · çevrimdışı, son veri %s") : T("Bulmaca: lichess.org · güncellendi %s"), when);
	KuiRect foot = { 20, by + 104 + 4, w - 40, h - (by + 104 + 4) - 4 };
	kui_text(foot, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 28, foot.w), KUI_ALIGN_CENTER, 0);
	for (int sq = 0; sq < 64; sq++) draw_square(sq);
	draw_msg(1);
	kui_flush(1);
}

static void redraw_all(void)
{
	for (int i = 0; i < 64; i++) drawn[i] = -1;
	draw_all();
}

/* a whole screen with a message (loading, errors) */
static void message_screen(const char *text, int with_retry, KuiButton *retry)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Satranç Bulmacası"), NULL);
	kui_text((KuiRect){ 40, 400, w - 80, 300 }, text, KUI_FONT_REG, 42, KUI_ALIGN_CENTER, 0);
	if (with_retry) {
		*retry = (KuiButton){ { 200, 800, w - 400, 140 }, T("Tekrar dene"), 1 };
		kui_button(retry, 0);
	}
	kui_text((KuiRect){ 20, h - 80, w - 40, 60 }, T("Bulmaca: lichess.org"), KUI_FONT_REG, 28, KUI_ALIGN_CENTER, 0);
	kui_flush(1);
}

/* ---- puzzle flow ---- */

static void streak_msg(void)
{
	PzStats st;
	pz_hist_stats(hist, n_hist, &st);
	int i = pz_hist_find(hist, n_hist, play.pz.id);
	if (i >= 0 && hist[i].res != 'X') snprintf(msg, sizeof msg, T("Daha önce çözüldü · Seri: %d"), st.streak);
	else snprintf(msg, sizeof msg, T("Hamlenizi yapın · Seri: %d"), st.streak);
}

static void begin(const Puzzle *p, int daily, long when, int offline)
{
	pz_play_start(&play, p);
	shown = play.pos;
	cur_daily = daily;
	cur_when = when;
	cur_offline = offline;
	selected = hint_sq = -1;
	pending = PEND_NONE;
	last_from = p->has_last ? p->last.from : -1;
	last_to = p->has_last ? p->last.to : -1;
	streak_msg();
	redraw_all();
}

static void restart(void)
{
	pz_play_restart(&play);
	shown = play.pos;
	selected = hint_sq = -1;
	pending = PEND_NONE;
	last_from = play.pz.has_last ? play.pz.last.from : -1;
	last_to = play.pz.has_last ? play.pz.last.to : -1;
	streak_msg();
	update();
}

/* shows a progress line in the message area while a puzzle is on screen */
static void busy(const char *text, int on_board)
{
	if (on_board) {
		snprintf(msg, sizeof msg, "%s", text);
		draw_msg(0);
		kui_flush(0);
	} else {
		message_screen(text, 0, NULL);
	}
}

/* the day's puzzle: from the cache when it is today's, else fetched (the cache stays as the offline fallback).
 * Returns 0 when a puzzle is on screen. */
static int open_daily(int force, int on_board)
{
	static Puzzle p;
	long when = 0;
	int cached = load_file(data_path("daily.json"), &p, &when) == 0;
	if (!force && cached && kui_cfg_get_int(data_path("daily.cfg"), "day", 0) == today()) {
		begin(&p, 1, when, 0);
		return 0;
	}
	busy(T("Yükleniyor…"), on_board);
	if (fetch_puzzle(PZ_URL_DAILY, &p) == 0) {
		begin(&p, 1, (long) time(NULL), 0);
		return 0;
	}
	if (cached || load_file(data_path("daily.json"), &p, &when) == 0) {
		begin(&p, 1, when, 1);
		return 0;
	}
	return -1;
}

static void open_another(int on_board)
{
	static Puzzle p;
	busy(T("Yükleniyor…"), on_board);
	if (fetch_puzzle(PZ_URL_NEXT, &p) == 0) {
		begin(&p, 0, (long) time(NULL), 0);
		return;
	}
	const char *ch[] = { T("Tamam") };
	kui_dialog(T("Bulmaca alınamadı"), T("Wi-Fi bağlantısını kontrol edin."), ch, 1, draw_all);
	streak_msg();
	redraw_all();
}

static void solved(void)
{
	record_result();
	PzStats st;
	pz_hist_stats(hist, n_hist, &st);
	if (play.shown) snprintf(msg, sizeof msg, "%s", T("Çözüm gösterildi"));
	else if (play.mistakes) snprintf(msg, sizeof msg, T("Çözüldü (hatalı) · Seri: %d"), st.streak);
	else snprintf(msg, sizeof msg, T("Doğru! Çözüldü · Seri: %d"), st.streak);
}

/* the opponent's reply or the take-back of a wrong move */
static void run_pending(void)
{
	int p = pending;
	pending = PEND_NONE;
	if (p == PEND_REPLY) {
		Move r;
		if (pz_play_reply(&play, &r)) {
			last_from = r.from;
			last_to = r.to;
		}
		snprintf(msg, sizeof msg, "%s", T("Doğru! Devam edin"));
	} else if (p == PEND_TAKEBACK) {
		last_from = saved_last_from;
		last_to = saved_last_to;
	}
	shown = play.pos;
	update();
}

static int choose_promotion(void)
{
	const char *ch[] = { T("Vezir"), T("Kale"), T("Fil"), T("At") };
	static const int promo[] = { WQ, WR, WB, WN };
	int k = kui_dialog(T("Terfi"), T("Piyon hangi taşa dönüşsün?"), ch, 4, draw_all);
	redraw_all();
	return k < 0 ? 0 : promo[k];
}

static void tap_square(int sq)
{
	if (sq < 0 || pz_play_done(&play) || play.pos.side != play.player) return;
	int p = play.pos.sq[sq];
	if (selected >= 0 && is_target(sq)) {
		int promo = 0, from = selected;
		if (pz_play_needs_promo(&play, from, sq)) {
			promo = choose_promotion();
			if (!promo) return;
		}
		Move ms[CHESS_MAX_MOVES], m = { (unsigned char) from, (unsigned char) sq, (unsigned char) promo, 0 };
		int n = chess_legal_moves(&play.pos, ms);
		for (int i = 0; i < n; i++)
			if (ms[i].from == m.from && ms[i].to == m.to && ms[i].promo == m.promo) m = ms[i];
		Board before = play.pos;
		selected = hint_sq = -1;
		PzVerdict v = pz_play_try(&play, m);
		if (v == PZ_WRONG) {
			/* show the move, then take it back */
			shown = before;
			chess_make(&shown, m);
			saved_last_from = last_from;
			saved_last_to = last_to;
			last_from = m.from;
			last_to = m.to;
			snprintf(msg, sizeof msg, "%s", T("Doğru değil, tekrar dene"));
			pending = PEND_TAKEBACK;
			update();
			return;
		}
		if (v == PZ_GOOD || v == PZ_WIN) {
			shown = play.pos;
			last_from = m.from;
			last_to = m.to;
			if (v == PZ_WIN) solved();
			else {
				snprintf(msg, sizeof msg, "%s", T("Doğru!"));
				pending = pz_play_expects_reply(&play) ? PEND_REPLY : PEND_NONE;
			}
			update();
			return;
		}
	}
	selected = p != EMPTY && chess_piece_color(p) == play.pos.side && sq != selected ? sq : -1;
	update();
}

/* SAN with figurines instead of the English piece letters ("Nb5#" → "♘b5#") */
static void figurine(const char *san, char *out, int cap)
{
	static const char *const fig[] = { "♘", "♗", "♖", "♕", "♔" };
	int o = 0;
	out[0] = '\0';
	for (const char *p = san; *p && o < cap - 4; p++) {
		const char *k = strchr("NBRQK", *p);
		if (k && *p) o += snprintf(out + o, (size_t) (cap - o), "%s", fig[k - "NBRQK"]);
		else out[o++] = *p, out[o] = '\0';
	}
}

static void show_next(void)
{
	Board before = play.pos;
	Move m;
	if (!pz_play_show_next(&play, &m)) return;
	if (pz_hist_find(hist, n_hist, play.pz.id) < 0) record_result(); /* showing the solution counts as not solved */
	char letters[16], san[40];
	san_format(&before, m, letters, sizeof letters);
	figurine(letters, san, sizeof san);
	shown = play.pos;
	selected = hint_sq = -1;
	last_from = m.from;
	last_to = m.to;
	if (pz_play_done(&play)) snprintf(msg, sizeof msg, T("Çözüm: %s · bitti"), san);
	else snprintf(msg, sizeof msg, T("Çözüm: %s (%d/%d)"), san, play.step, play.pz.n_sol);
	update();
}

/* ---- history screen ---- */

static const char *res_text(char r)
{
	return r == 'S' ? T("Çözüldü") : r == 'F' ? T("Hatalı çözüldü") : T("Çözüm gösterildi");
}

/* returns the index of a history entry to open, or -1 */
static int history_screen(void)
{
	int w = kui_width(), h = kui_height(), page = 0, per = 8, row = 104;
	PzStats st;
	pz_hist_stats(hist, n_hist, &st);
	KuiButton nav[2];
	for (;;) {
		int pages = n_hist ? (n_hist + per - 1) / per : 1;
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Geçmiş"), NULL);
		char s[200];
		snprintf(s, sizeof s, T("Seri: %d · En iyi seri: %d"), st.streak, st.best);
		kui_text((KuiRect){ 20, KUI_TOPBAR_H + 20, w - 40, 70 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 50, w - 40), KUI_ALIGN_CENTER, 0);
		snprintf(s, sizeof s, T("Çözülen: %d / %d"), st.solved, st.total);
		kui_text((KuiRect){ 20, KUI_TOPBAR_H + 95, w - 40, 56 }, s, KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
		int y0 = KUI_TOPBAR_H + 170;
		kui_fill((KuiRect){ 20, y0 - 8, w - 40, 3 }, 0);
		if (!n_hist)
			kui_text((KuiRect){ 40, y0 + 150, w - 80, 200 }, T("Henüz çözülen bulmaca yok."), KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
		for (int i = 0; i < per; i++) {
			int k = n_hist - 1 - (page * per + i); /* newest first */
			if (k < 0) break;
			const PzHist *e = &hist[k];
			int y = y0 + i * row;
			char idtag[20];
			snprintf(idtag, sizeof idtag, "#%s", e->id);
			snprintf(s, sizeof s, "%02d.%02d.%04d · %s · ", e->day % 100, e->day / 100 % 100, e->day / 10000, e->daily ? T("Günün bulmacası") : idtag);
			int o = (int) strlen(s);
			snprintf(s + o, sizeof s - (size_t) o, T("Puan %d"), e->rating);
			kui_text((KuiRect){ 30, y + 6, w - 60, 46 }, s, KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ 30, y + 52, w - 60, 44 }, res_text(e->res), KUI_FONT_BOLD, 32, KUI_ALIGN_LEFT, 0);
			kui_fill((KuiRect){ 30, y + row - 3, w - 60, 1 }, 0);
		}
		nav[0] = (KuiButton){ { 20, h - 110, 160, 90 }, "◀", 1 };
		nav[1] = (KuiButton){ { w - 180, h - 110, 160, 90 }, "▶", 2 };
		if (pages > 1) {
			kui_button(&nav[0], 0);
			kui_button(&nav[1], 0);
			snprintf(s, sizeof s, "%d/%d", page + 1, pages);
			kui_text((KuiRect){ 200, h - 110, w - 400, 90 }, s, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0);
		}
		kui_flush(1);
		for (int again = 0; !again;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -2;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type == KUI_EV_SWIPE) {
				if (e.dir == KUI_DIR_LEFT && page + 1 < pages) page++, again = 1;
				else if (e.dir == KUI_DIR_RIGHT && page > 0) page--, again = 1;
				continue;
			}
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
			int id = pages > 1 ? kui_buttons_hit(nav, 2, e.x, e.y) : 0;
			if (id == 1 && page > 0) { page--; again = 1; continue; }
			if (id == 2 && page + 1 < pages) { page++; again = 1; continue; }
			if (e.y >= y0 && e.y < y0 + per * row) {
				int k = n_hist - 1 - (page * per + (e.y - y0) / row);
				if (k >= 0 && access(archive_path(hist[k].id), R_OK) == 0) return k;
			}
		}
	}
}

/* ---- main ---- */

/* 0 = a puzzle is on screen, -1 = quit */
static int first_puzzle(void)
{
	for (;;) {
		if (open_daily(0, 0) == 0) return 0;
		KuiButton retry;
		message_screen(T("Bulmaca alınamadı.\nWi-Fi bağlantısını kontrol edin."), 1, &retry);
		for (int again = 0; !again;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT) return -1;
			if (kui_buttons_hit(&retry, 1, e.x, e.y)) again = 1;
		}
	}
}

static int menu(void)
{
	const char *ch[] = { T("Günün bulmacası"), T("Başka bulmaca"), T("Geçmiş"), T("Baştan başla") };
	int k = kui_dialog(T("Menü"), "", ch, 4, draw_all);
	if (k == 0) {
		if (open_daily(0, 0) != 0) {
			const char *ok[] = { T("Tamam") };
			kui_dialog(T("Bulmaca alınamadı"), T("Wi-Fi bağlantısını kontrol edin."), ok, 1, draw_all);
			redraw_all();
		}
	} else if (k == 1) {
		open_another(0);
	} else if (k == 2) {
		int i = history_screen();
		if (i == -2) return 1;
		static Puzzle p;
		long when;
		if (i >= 0 && load_file(archive_path(hist[i].id), &p, &when) == 0) begin(&p, hist[i].daily, when, 0);
		else redraw_all();
	} else if (k == 3) {
		redraw_all();
		restart();
	} else {
		redraw_all();
	}
	return 0;
}

int main(void)
{
	if (kui_init("puzzle") != 0) return 1;
	load_history();
	if (first_puzzle() != 0) {
		kui_quit();
		return 0;
	}
	for (int quit = 0; !quit;) {
		KuiEvent e = kui_next_event(pending ? PAUSE_MS : -1);
		if (pending && e.type != KUI_EV_QUIT) run_pending(); /* the pause is over, or a tap ends it at once */
		switch (e.type) {
		case KUI_EV_QUIT: quit = 1; break;
		case KUI_EV_WAKE: redraw_all(); break;
		case KUI_EV_TAP: {
			int t = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (t == KUI_BTN_EXIT) {
				quit = 1;
				break;
			}
			if (t == KUI_BTN_MENU) {
				quit = menu();
				break;
			}
			int id = kui_buttons_hit(buttons, 3, e.x, e.y);
			if (id == BTN_HINT) {
				hint_sq = pz_play_hint(&play);
				selected = -1;
				if (hint_sq >= 0) snprintf(msg, sizeof msg, "%s", T("İpucu: işaretli taşı oynayın"));
				update();
			} else if (id == BTN_SOLUTION) {
				show_next();
			} else if (id == BTN_RESTART) {
				restart();
			} else if (id == BTN_ANOTHER) {
				open_another(1);
			} else if (id == BTN_REFRESH) {
				if (open_daily(1, 1) != 0) {
					streak_msg();
					update();
				}
			} else {
				tap_square(square_at(e.x, e.y));
			}
			break;
		}
		default: break;
		}
	}
	kui_quit();
	return 0;
}

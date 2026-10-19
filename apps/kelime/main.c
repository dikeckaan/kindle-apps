/* Kelime Öğren: a vocabulary game for many language pairs, levels A1–C2. Lessons of ten questions (word → meaning,
 * meaning → word, a definition in the language itself → word, a sentence with a blank, true or false); every answer
 * is followed by the word's definition and an example. Missed words come back by Leitner boxes (Tekrar); XP, a
 * daily goal and a streak. Fully offline: the word lists ship with the app. Every tap inside a screen is a
 * partial refresh; only a new screen gets a full one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "kelime.h"

#define MARGIN 60
#define LESSON_N 10
#define MAX_Q (LESSON_N + 6) /* missed questions come back at the end */

static const struct { const char *code, *name; } LANGS[] = {
	{ "tr", N_("Türkçe") },	   { "en", N_("İngilizce") },	{ "de", N_("Almanca") },    { "fr", N_("Fransızca") },
	{ "es", N_("İspanyolca") }, { "it", N_("İtalyanca") }, { "pt", N_("Portekizce") },
};
#define N_LANGS ((int) (sizeof LANGS / sizeof LANGS[0]))

static const char *LEVEL_NAMES[KL_LEVELS + 1] = { "", N_("Başlangıç"), N_("Temel"), N_("Orta"), N_("Orta üstü"), N_("İleri"), N_("Ustalık") };
static const char *POS_LABELS[KL_POS_N] = { "", N_("isim"), N_("fiil"), N_("sıfat"), N_("zarf"), N_("zamir"), N_("edat"), N_("bağlaç"), N_("sayı"),
					    N_("belirleyici"), N_("ünlem"), N_("deyim") };

static KlDb db;
static KlDeck deck;
static KlStats stats;
static int learn = -1, native = 0; /* indexes into LANGS */
static int has_list[N_LANGS], list_count[N_LANGS];

/* ---- files ---- */

static const char *data_path(const char *name)
{
	static char p[320];
	snprintf(p, sizeof p, "%s/%s", kui_data_dir("kelime"), name);
	return p;
}

static void list_path(int l, char *out, int cap, int dev)
{
	const char *root = getenv("KUI_ROOT");
	if (dev) snprintf(out, (size_t) cap, "apps/kelime/data/%s.txt", LANGS[l].code); /* development tree (simulator) */
	else snprintf(out, (size_t) cap, "%s/apps/kelime/%s.txt", root && *root ? root : "/mnt/us/kindle-apps", LANGS[l].code);
}

/* which word lists are installed, and their sizes (from the "# words=" header line) */
static void scan_lists(void)
{
	for (int l = 0; l < N_LANGS; l++) {
		has_list[l] = list_count[l] = 0;
		for (int dev = 0; dev < 2 && !has_list[l]; dev++) {
			char p[320], head[512];
			list_path(l, p, sizeof p, dev);
			FILE *f = fopen(p, "rb");
			if (!f) continue;
			has_list[l] = 1;
			size_t n = fread(head, 1, sizeof head - 1, f);
			head[n] = '\0';
			fclose(f);
			const char *w = strstr(head, "words=");
			if (w) list_count[l] = atoi(w + 6);
		}
	}
}

static char *big_buf;
static int big_cap;

static void save_deck(void)
{
	if (learn < 0) return;
	int need = 64;
	for (int i = 0; i < deck.n; i++) need += (int) strlen(deck.c[i].key) + 48;
	if (need > big_cap) {
		char *b = realloc(big_buf, (size_t) need);
		if (!b) return;
		big_buf = b;
		big_cap = need;
	}
	char name[32];
	snprintf(name, sizeof name, "cards-%s", LANGS[learn].code);
	int n = kl_deck_format(&deck, big_buf, big_cap);
	if (n >= 0) kui_write_atomic(data_path(name), big_buf, n);
}

static void save_stats(void)
{
	char buf[512];
	int n = kl_stats_format(&stats, buf, sizeof buf);
	if (n > 0) kui_write_atomic(data_path("stats"), buf, n);
	char cfg[160];
	snprintf(cfg, sizeof cfg, "learn=%d\nnative=%d\n", learn, native);
	kui_write_atomic(data_path("settings"), cfg, (int) strlen(cfg));
}

static void load_deck(void)
{
	char name[32];
	snprintf(name, sizeof name, "cards-%s", LANGS[learn].code);
	FILE *f = fopen(data_path(name), "rb");
	char *buf = NULL;
	long sz = 0;
	if (f) {
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		buf = malloc((size_t) (sz > 0 ? sz : 0) + 1);
		if (buf) buf[fread(buf, 1, (size_t) (sz > 0 ? sz : 0), f)] = '\0';
		fclose(f);
	}
	kl_deck_parse(buf ? buf : "", &deck);
	free(buf);
}

/* loads the word list of language l; 0 = ok */
static int open_lang(int l)
{
	if (l == learn && db.n) return 0;
	kl_free(&db);
	for (int dev = 0; dev < 2; dev++) {
		char p[320];
		list_path(l, p, sizeof p, dev);
		if (kl_load(p, &db) > 0) {
			learn = l;
			load_deck();
			return 0;
		}
	}
	return -1;
}

static int today(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return kl_days(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static const char *ncode(void) { return LANGS[native].code; }

/* ---- drawing helpers ---- */

static void draw_n(KuiRect r, const char *s, int len, KuiFont f, int px, KuiAlign a, unsigned char fg, int bg)
{
	char buf[1024];
	if (len > (int) sizeof buf - 1) len = (int) sizeof buf - 1;
	memcpy(buf, s, (size_t) len);
	buf[len] = '\0';
	if (bg >= 0) kui_text_on(r, buf, f, px, a, fg, (unsigned char) bg);
	else kui_text(r, buf, f, px, a, fg);
}

/* wraps text into the box at the largest size from px down to min_px that fits max lines (the last size cuts);
 * lines are centred vertically when vcenter. Returns the height used. */
static int draw_wrap(KuiRect r, const char *text, KuiFont f, int px, int min_px, int max, KuiAlign a, unsigned char fg, int bg, int vcenter)
{
	KuiLine ls[16];
	int n = 0, lh = 0;
	for (;; px -= 2) {
		n = kui_wrap_text(text, r.w, f, px, ls, 16);
		lh = kui_line_height(f, px);
		if ((n <= max && n * lh <= r.h) || px - 2 < min_px) break;
	}
	if (n > max) n = max;
	while (n > 1 && n * lh > r.h) n--;
	int y = r.y + (vcenter ? (r.h - n * lh) / 2 : 0);
	for (int i = 0; i < n; i++) draw_n((KuiRect){ r.x, y + i * lh, r.w, lh }, text + ls[i].off, ls[i].len, f, px, a, fg, bg);
	return n * lh;
}

static void flush_body(int full)
{
	if (full) {
		kui_flush(1);
	} else {
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, kui_width(), kui_height() - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

static void bar(KuiRect r, int num, int den)
{
	kui_fill(r, 255);
	kui_frame(r, 2, 0);
	int fw = den > 0 ? (int) ((long) (r.w - 6) * (num < den ? num : den) / den) : 0;
	if (fw > 0) kui_fill((KuiRect){ r.x + 3, r.y + 3, fw, r.h - 6 }, 0x40);
}

static void level_label(int lv, char *out, int cap) { snprintf(out, (size_t) cap, "%s · %s", kl_level_name(lv), T(LEVEL_NAMES[lv])); }

/* ---- language choice ---- */

static void lang_row(KuiRect r, int l, int sel, int enabled)
{
	kui_fill(r, sel ? 0 : 255);
	kui_frame(r, sel ? 4 : 2, enabled ? 0 : 0xA0);
	kui_text_on((KuiRect){ r.x + 10, r.y, r.w - 20, r.h }, T(LANGS[l].name), sel ? KUI_FONT_BOLD : KUI_FONT_REG,
		    kui_fit_px(T(LANGS[l].name), KUI_FONT_BOLD, 38, r.w - 20), KUI_ALIGN_CENTER, sel ? 255 : enabled ? 0 : 0xA0, sel ? 0 : 255);
	kui_mark_dirty(r);
}

/* returns 0 when a pair was chosen, -1 on ✕ (only allowed when one was chosen before), -2 on quit */
static int lang_screen(int first)
{
	int w = kui_width(), h = kui_height(), colw = (w - 2 * MARGIN - 40) / 2, rh = 110, gap = 18, y0 = KUI_TOPBAR_H + 150;
	int sel_l = learn >= 0 ? learn : -1, sel_n = native;
	if (sel_l < 0)
		for (int l = 1; l < N_LANGS && sel_l < 0; l++)
			if (has_list[l] && l != sel_n) sel_l = l;
	KuiButton ok = { { MARGIN + 100, h - 170, w - 2 * MARGIN - 200, 130 }, T("Başla"), 1 };
	KuiRect rows[2][N_LANGS];
	for (int c = 0; c < 2; c++)
		for (int l = 0; l < N_LANGS; l++) rows[c][l] = (KuiRect){ MARGIN + c * (colw + 40), y0 + l * (rh + gap), colw, rh };
	for (int redraw = 1, full = 1;;) {
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T("Diller"), NULL);
			kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 30, colw, 60 }, T("Öğrendiğim dil"), KUI_FONT_BOLD, kui_fit_px(T("Öğrendiğim dil"), KUI_FONT_BOLD, 40, colw),
				 KUI_ALIGN_CENTER, 0);
			kui_text((KuiRect){ MARGIN + colw + 40, KUI_TOPBAR_H + 30, colw, 60 }, T("Ana dilim"), KUI_FONT_BOLD,
				 kui_fit_px(T("Ana dilim"), KUI_FONT_BOLD, 40, colw), KUI_ALIGN_CENTER, 0);
			kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 88, w - 2 * MARGIN, 44 }, T("Sorular öğrendiğin dilde, açıklamalar iki dilde gelir."), KUI_FONT_REG,
				 kui_fit_px(T("Sorular öğrendiğin dilde, açıklamalar iki dilde gelir."), KUI_FONT_REG, 28, w - 2 * MARGIN), KUI_ALIGN_CENTER, 0x40);
			for (int l = 0; l < N_LANGS; l++) {
				lang_row(rows[0][l], l, l == sel_l, has_list[l] && l != sel_n);
				lang_row(rows[1][l], l, l == sel_n, l != sel_l);
			}
			char info[160] = "";
			if (sel_l >= 0 && list_count[sel_l]) snprintf(info, sizeof info, T("%d kelime, A1'den C2'ye"), list_count[sel_l]);
			kui_text((KuiRect){ MARGIN, ok.r.y - 90, w - 2 * MARGIN, 60 }, info, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0x30);
			kui_button(&ok, 0);
			flush_body(full);
			kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
			redraw = full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return -2;
		if (e.type == KUI_EV_WAKE) {
			redraw = full = 1;
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) {
			if (!first) return -1;
			continue;
		}
		if (kui_rect_contains(ok.r, e.x, e.y) && sel_l >= 0 && sel_l != sel_n) {
			kui_button(&ok, 1);
			kui_flush(0);
			native = sel_n;
			if (open_lang(sel_l) != 0) {
				has_list[sel_l] = 0;
				redraw = 1;
				continue;
			}
			save_stats();
			return 0;
		}
		for (int c = 0; c < 2; c++)
			for (int l = 0; l < N_LANGS; l++)
				if (kui_rect_contains(rows[c][l], e.x, e.y)) {
					if (c == 0 && has_list[l] && l != sel_n && l != sel_l) sel_l = l, redraw = 1;
					if (c == 1 && l != sel_l && l != sel_n) sel_n = l, redraw = 1;
				}
		/* only the rows and the line above the button change: partial refresh of the body */
	}
}

/* ---- lesson ---- */

typedef struct {
	KlQuestion q[MAX_Q];
	int n, at, right, wrong, missed[MAX_Q], n_missed, lv;
	int result[MAX_Q]; /* 0 not yet, 1 right, 2 wrong */
} Lesson;
static Lesson L;
static unsigned rng_state;

#define PROMPT_Y (KUI_TOPBAR_H + 70)
#define PROMPT_H 380
#define OPTS_Y (PROMPT_Y + PROMPT_H + 20)
#define OPT_H 150
#define OPT_GAP 20
#define PANEL_Y (OPTS_Y + 2 * OPT_H + OPT_GAP + 24)
#define NEXT_H 120

static KuiRect opt_rect(int n_opts, int i)
{
	int w = kui_width(), bw = (w - 2 * MARGIN - OPT_GAP) / 2;
	if (n_opts <= 2) return (KuiRect){ MARGIN + i * (bw + OPT_GAP), OPTS_Y + (OPT_H + OPT_GAP) / 2, bw, OPT_H };
	return (KuiRect){ MARGIN + (i % 2) * (bw + OPT_GAP), OPTS_Y + (i / 2) * (OPT_H + OPT_GAP), bw, OPT_H };
}

static KuiRect next_rect(void) { return (KuiRect){ MARGIN, kui_height() - NEXT_H - 30, kui_width() - 2 * MARGIN, NEXT_H }; }

static void opt_label(const KlQuestion *q, int i, char *out, int cap)
{
	if (q->type == KL_Q_TF) {
		snprintf(out, (size_t) cap, "%s", i == 0 ? T("Doğru") : T("Yanlış"));
	} else if (q->type == KL_Q_MEANING) {
		kl_field(db.w[q->opts[i]].tr, ncode(), out, cap);
	} else {
		kl_display(&db.w[q->opts[i]], out, cap);
	}
}

static int n_buttons(const KlQuestion *q) { return q->type == KL_Q_TF ? 2 : q->n_opts; }

static int right_button(const KlQuestion *q) { return q->type == KL_Q_TF ? (q->answer ? 0 : 1) : q->answer; }

/* state: 0 plain, 1 tapped (inverted), 2 the right answer shown after a miss (thick frame, ✓) */
static void draw_opt(const KlQuestion *q, int i, int state)
{
	KuiRect r = opt_rect(n_buttons(q), i);
	char s[300], lab[320];
	opt_label(q, i, s, sizeof s);
	snprintf(lab, sizeof lab, state == 2 ? "✓ %s" : "%s", s);
	kui_fill(r, state == 1 ? 0 : 255);
	kui_frame(r, state == 2 ? 10 : 4, 0);
	draw_wrap((KuiRect){ r.x + 20, r.y + 10, r.w - 40, r.h - 20 }, lab, KUI_FONT_BOLD, 40, 26, 2, KUI_ALIGN_CENTER, state == 1 ? 255 : 0, state == 1 ? 0 : 255, 1);
	kui_mark_dirty(r);
}

static void progress_bar(void)
{
	int w = kui_width(), n = L.n, segw = (w - 2 * MARGIN) / (n ? n : 1);
	KuiRect all = { MARGIN, KUI_TOPBAR_H + 22, w - 2 * MARGIN, 30 };
	kui_fill(all, 255);
	for (int i = 0; i < n; i++) {
		KuiRect s = { MARGIN + i * segw + 3, all.y, segw - 6, all.h };
		kui_frame(s, 2, 0);
		if (L.result[i] == 1) kui_fill(s, 0);
		else if (L.result[i] == 2) kui_fill((KuiRect){ s.x + 2, s.y + 2, s.w - 4, s.h - 4 }, 0xA0);
		else if (i == L.at) kui_fill((KuiRect){ s.x, s.y + s.h - 8, s.w, 8 }, 0);
	}
	kui_mark_dirty(all);
}

static void prompt_texts(const KlQuestion *q, char *head, int hc, char *big, int bc, char *sub, int sc)
{
	const KlWord *w = &db.w[q->word];
	const char *lname = T(LANGS[learn].name);
	sub[0] = '\0';
	switch (q->type) {
	case KL_Q_MEANING:
		snprintf(head, (size_t) hc, "%s", T("Bu kelime ne demek?"));
		kl_display(w, big, bc);
		snprintf(sub, (size_t) sc, "%s", T(POS_LABELS[w->pos]));
		break;
	case KL_Q_WORD:
		snprintf(head, (size_t) hc, T("%s karşılığı hangisi?"), lname);
		kl_field(w->tr, ncode(), big, bc);
		snprintf(sub, (size_t) sc, "%s", T(POS_LABELS[w->pos]));
		break;
	case KL_Q_DEFINE:
		snprintf(head, (size_t) hc, T("Bu tanım hangi kelimenin? (%s)"), lname);
		kl_cloze(w->def, "___", big, bc); /* the word itself is hidden in its definition */
		break;
	case KL_Q_CLOZE:
		snprintf(head, (size_t) hc, "%s", T("Boşluğa hangi kelime gelir?"));
		kl_cloze(w->ex, "_____", big, bc);
		kl_field(w->extr, ncode(), sub, sc);
		break;
	case KL_Q_TF: {
		char d[200], t[200];
		snprintf(head, (size_t) hc, "%s", T("Bu çeviri doğru mu?"));
		kl_display(w, d, sizeof d);
		kl_field(db.w[q->opts[0]].tr, ncode(), t, sizeof t);
		snprintf(big, (size_t) bc, "%s  =  %s", d, t);
		break;
	}
	}
}

static void draw_question(void)
{
	const KlQuestion *q = &L.q[L.at];
	int w = kui_width(), h = kui_height();
	char head[200], big[1024], sub[600];
	prompt_texts(q, head, sizeof head, big, sizeof big, sub, sizeof sub);
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H }, 255);
	progress_bar();
	KuiRect pr = { MARGIN, PROMPT_Y, w - 2 * MARGIN, PROMPT_H };
	kui_frame(pr, 2, 0x80);
	kui_text((KuiRect){ pr.x + 20, pr.y + 14, pr.w - 40, 50 }, head, KUI_FONT_REG, kui_fit_px(head, KUI_FONT_REG, 32, pr.w - 40), KUI_ALIGN_CENTER, 0x30);
	KuiRect body = { pr.x + 30, pr.y + 70, pr.w - 60, pr.h - 90 - (sub[0] ? 60 : 0) };
	int long_text = q->type == KL_Q_DEFINE || q->type == KL_Q_CLOZE;
	draw_wrap(body, big, long_text ? KUI_FONT_REG : KUI_FONT_BOLD, long_text ? 44 : 76, 28, long_text ? 6 : 3, KUI_ALIGN_CENTER, 0, -1, 1);
	if (sub[0])
		draw_wrap((KuiRect){ pr.x + 30, pr.y + pr.h - 80, pr.w - 60, 64 }, sub, KUI_FONT_REG, 30, 22, 2, KUI_ALIGN_CENTER, 0x50, -1, 1);
	for (int i = 0; i < n_buttons(q); i++) draw_opt(q, i, 0);
}

/* the teaching card after an answer: the word, its meaning, the definition in its own language, an example */
static void draw_panel(int correct)
{
	const KlQuestion *q = &L.q[L.at];
	const KlWord *wd = &db.w[q->word];
	int w = kui_width(), x = MARGIN + 24, tw = w - 2 * MARGIN - 48;
	KuiRect nr = next_rect();
	KuiRect panel = { MARGIN, PANEL_Y, w - 2 * MARGIN, nr.y - 20 - PANEL_Y };
	kui_fill(panel, 255);
	kui_frame(panel, 3, 0);
	int y = panel.y + 14;
	char disp[200], tr[300], s[900], ex[700], extr[700];
	kl_display(wd, disp, sizeof disp);
	kl_field(wd->tr, ncode(), tr, sizeof tr);
	if (correct) snprintf(s, sizeof s, "✓ %s", T("Doğru!"));
	else snprintf(s, sizeof s, "✗ %s", T("Yanlış"));
	kui_text((KuiRect){ x, y, tw, 52 }, s, KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
	y += 58;
	const char *pos = T(POS_LABELS[wd->pos]);
	snprintf(s, sizeof s, tr[0] ? "%s%s%s  =  %s" : "%s%s%s", disp, pos[0] ? " · " : "", pos, tr);
	y += draw_wrap((KuiRect){ x, y, tw, 100 }, s, KUI_FONT_BOLD, 36, 26, 2, KUI_ALIGN_LEFT, 0, -1, 0) + 8;
	int bottom = panel.y + panel.h - 12;
	if (wd->def[0] && y < bottom - 40) {
		char def[600];
		kl_unmark(wd->def, def, sizeof def);
		snprintf(s, sizeof s, "%s: %s", T(LANGS[learn].name), def);
		y += draw_wrap((KuiRect){ x, y, tw, bottom - y > 140 ? 140 : bottom - y }, s, KUI_FONT_REG, 32, 24, 3, KUI_ALIGN_LEFT, 0, -1, 0) + 10;
	}
	if (wd->ex[0] && y < bottom - 40) {
		kl_unmark(wd->ex, ex, sizeof ex);
		snprintf(s, sizeof s, "„%s“", ex);
		y += draw_wrap((KuiRect){ x, y, tw, bottom - y > 100 ? 100 : bottom - y }, s, KUI_FONT_REG, 30, 24, 2, KUI_ALIGN_LEFT, 0x20, -1, 0) + 4;
		if (kl_field(wd->extr, ncode(), extr, sizeof extr) && y < bottom - 30)
			draw_wrap((KuiRect){ x, y, tw, bottom - y }, extr, KUI_FONT_REG, 28, 22, 2, KUI_ALIGN_LEFT, 0x50, -1, 0);
	}
	kui_mark_dirty(panel);
	KuiButton nb = { nr, L.at + 1 < L.n ? T("Devam") : T("Sonuç"), 1 };
	kui_button(&nb, 0);
}

static void record(int correct)
{
	const KlQuestion *q = &L.q[L.at];
	int d = today();
	KlCard *c = kl_deck_get(&deck, db.w[q->word].word);
	if (c) kl_card_answer(c, correct, d);
	kl_stats_answer(&stats, correct, d);
	L.result[L.at] = correct ? 1 : 2;
	if (correct) {
		L.right++;
	} else {
		L.wrong++;
		int seen = 0;
		for (int i = 0; i < L.n_missed; i++) seen |= L.missed[i] == q->word;
		if (!seen && L.n_missed < MAX_Q) L.missed[L.n_missed++] = q->word;
		if (L.n < MAX_Q && kl_again(&db, q, ncode(), &rng_state, &L.q[L.n])) L.result[L.n++] = 0; /* asked again at the end */
	}
	save_deck();
	save_stats();
}

enum { LS_DONE, LS_EXIT, LS_QUIT };

static int lesson_loop(const char *title)
{
	int w = kui_width();
	int full = 1;
	for (L.at = 0; L.at < L.n;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(title, NULL);
		draw_question();
		flush_body(full);
		full = 0;
		int answered = 0, tapped = -1;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return LS_QUIT;
			if (e.type == KUI_EV_WAKE) { /* redraw everything as it was */
				kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
				kui_topbar(title, NULL);
				draw_question();
				if (answered) {
					const KlQuestion *q = &L.q[L.at];
					draw_opt(q, tapped, 1);
					if (tapped != right_button(q)) draw_opt(q, right_button(q), 2);
					draw_panel(tapped == right_button(q));
				}
				kui_flush(1);
				continue;
			}
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return LS_EXIT;
			const KlQuestion *q = &L.q[L.at];
			if (!answered) {
				for (int i = 0; i < n_buttons(q); i++)
					if (kui_rect_contains(opt_rect(n_buttons(q), i), e.x, e.y)) tapped = i;
				if (tapped < 0) continue;
				answered = 1;
				int ok = tapped == right_button(q);
				draw_opt(q, tapped, 1); /* first only the tapped button turns black */
				kui_flush(0);
				record(ok);
				if (!ok) draw_opt(q, right_button(q), 2);
				progress_bar();
				draw_panel(ok);
				kui_flush(0);
			} else if (kui_rect_contains(next_rect(), e.x, e.y) || e.y > PANEL_Y) {
				KuiButton nb = { next_rect(), "", 1 };
				nb.label = L.at + 1 < L.n ? T("Devam") : T("Sonuç");
				kui_button(&nb, 1);
				kui_flush(0);
				L.at++;
				break; /* the next question: partial refresh (same screen) */
			}
		}
	}
	return LS_DONE;
}

/* the summary after a lesson; returns 1 for another lesson, 0 for the home screen, -1 quit */
static int summary_screen(int bonus)
{
	int w = kui_width(), h = kui_height(), d = today();
	KuiButton again = { { MARGIN, h - 180, (w - 2 * MARGIN - 30) / 2, 130 }, T("Yeni ders"), 1 };
	KuiButton home = { { MARGIN + again.r.w + 30, h - 180, again.r.w, 130 }, T("Ana ekran"), 2 };
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Ders bitti"), NULL);
		int y = KUI_TOPBAR_H + 50;
		char s[300];
		int asked = L.right + L.wrong;
		snprintf(s, sizeof s, T("%d sorudan %d doğru"), asked, L.right);
		kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN, 90 }, s, KUI_FONT_BOLD, 64, KUI_ALIGN_CENTER, 0);
		y += 110;
		int gained = L.right * KL_XP_RIGHT + bonus;
		if (bonus) snprintf(s, sizeof s, T("+%d XP (hatasız ders: +%d)"), gained, bonus);
		else snprintf(s, sizeof s, T("+%d XP"), gained);
		kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN, 60 }, s, KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
		y += 80;
		snprintf(s, sizeof s, T("Seri: %d gün · bugün %d/%d XP"), kl_streak_now(&stats, d), kl_today_xp(&stats, d), stats.goal);
		kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN, 56 }, s, KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
		y += 70;
		bar((KuiRect){ MARGIN + 100, y, w - 2 * MARGIN - 200, 34 }, kl_today_xp(&stats, d), stats.goal);
		y += 70;
		if (L.n_missed) {
			kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN, 56 }, T("Tekrar edilecek kelimeler"), KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
			y += 66;
			for (int i = 0; i < L.n_missed && y < again.r.y - 80; i++) {
				const KlWord *wd = &db.w[L.missed[i]];
				char disp[200], tr[300];
				kl_display(wd, disp, sizeof disp);
				kl_field(wd->tr, ncode(), tr, sizeof tr);
				snprintf(s, sizeof s, tr[0] ? "%s  —  %s" : "%s", disp, tr);
				kui_text((KuiRect){ MARGIN + 20, y, w - 2 * MARGIN - 20, 54 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - 2 * MARGIN - 20), KUI_ALIGN_LEFT, 0);
				y += 58;
			}
		} else {
			kui_text((KuiRect){ MARGIN, y + 40, w - 2 * MARGIN, 60 }, T("Hiç hata yok, harika!"), KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0);
		}
		kui_button(&again, 0);
		kui_button(&home, 0);
		kui_flush(1);
		for (int wait = 1; wait;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) wait = 0;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
			if (kui_rect_contains(again.r, e.x, e.y)) {
				kui_button(&again, 1);
				kui_flush(0);
				return 1;
			}
			if (kui_rect_contains(home.r, e.x, e.y)) return 0;
		}
	}
}

/* runs lessons at level lv (0 = review) until the user goes home; -1 on quit */
static int play(int lv)
{
	for (;;) {
		memset(&L, 0, sizeof L);
		L.lv = lv;
		rng_state ^= (unsigned) time(NULL) * 2654435761u + 1;
		if (!rng_state) rng_state = 1;
		L.n = kl_lesson(&db, &deck, ncode(), lv, today(), kl_rand(&rng_state), L.q, LESSON_N);
		if (!L.n) {
			const char *ok[1] = { T("Tamam") };
			kui_dialog(lv ? T("Bu seviyede soru yok") : T("Tekrar edilecek kelime yok"),
				   lv ? T("Bu dil ikilisinde bu seviyede sorulacak kelime bulunamadı.") : T("Önce birkaç ders yapın; yanlış bildiğiniz kelimeler burada tekrar sorulur."),
				   ok, 1, NULL);
			return 0;
		}
		char title[80];
		if (lv) level_label(lv, title, sizeof title);
		else snprintf(title, sizeof title, "%s", T("Tekrar"));
		int r = lesson_loop(title);
		if (r == LS_QUIT) return -1;
		if (r == LS_EXIT) return 0;
		int d = today(), bonus = L.wrong == 0 ? KL_XP_PERFECT : 0;
		if (bonus) kl_stats_bonus(&stats, bonus, d);
		kl_streak_touch(&stats, d);
		save_stats();
		r = summary_screen(bonus);
		if (r < 0) return -1;
		if (r == 0) return 0;
	}
}

/* ---- statistics ---- */

static int stats_screen(void)
{
	int w = kui_width(), h = kui_height(), d = today();
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("İstatistik"), NULL);
		int y = KUI_TOPBAR_H + 40, tw = w - 2 * MARGIN;
		char s[300];
		snprintf(s, sizeof s, T("%s öğreniyorum · ana dilim %s"), T(LANGS[learn].name), T(LANGS[native].name));
		kui_text((KuiRect){ MARGIN, y, tw, 56 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 40, tw), KUI_ALIGN_CENTER, 0);
		y += 80;
		int acc = stats.answers ? (int) (100L * stats.right / stats.answers) : 0;
		const char *lines[4];
		char l0[160], l1[160], l2[160], l3[160];
		snprintf(l0, sizeof l0, T("Toplam: %ld XP · bugün %d XP"), stats.xp, kl_today_xp(&stats, d));
		snprintf(l1, sizeof l1, T("Seri: %d gün · en uzun %d gün"), kl_streak_now(&stats, d), stats.best);
		snprintf(l2, sizeof l2, T("%d ders · %d cevap · %%%d doğru"), stats.lessons, stats.answers, acc);
		int learned, seen, due;
		kl_progress(&db, &deck, 0, d, &learned, &seen, &due);
		snprintf(l3, sizeof l3, T("%d kelime görüldü · %d öğrenildi · %d tekrar bekliyor"), seen, learned, due);
		lines[0] = l0, lines[1] = l1, lines[2] = l2, lines[3] = l3;
		for (int i = 0; i < 4; i++, y += 60) kui_text((KuiRect){ MARGIN, y, tw, 56 }, lines[i], KUI_FONT_REG, kui_fit_px(lines[i], KUI_FONT_REG, 36, tw), KUI_ALIGN_LEFT, 0);
		y += 30;
		kui_text((KuiRect){ MARGIN, y, tw, 56 }, T("Seviyeler"), KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
		y += 66;
		for (int lv = 1; lv <= KL_LEVELS; lv++, y += 76) {
			kl_progress(&db, &deck, lv, d, &learned, &seen, &due);
			char lab[100];
			level_label(lv, lab, sizeof lab);
			kui_text((KuiRect){ MARGIN, y, 330, 60 }, lab, KUI_FONT_REG, kui_fit_px(lab, KUI_FONT_REG, 34, 330), KUI_ALIGN_LEFT, 0);
			bar((KuiRect){ MARGIN + 340, y + 14, tw - 340 - 220, 32 }, learned, db.level_n[lv]);
			snprintf(s, sizeof s, "%d / %d", learned, db.level_n[lv]);
			kui_text((KuiRect){ w - MARGIN - 210, y, 210, 60 }, s, KUI_FONT_REG, 32, KUI_ALIGN_RIGHT, 0);
		}
		y += 20;
		kui_text((KuiRect){ MARGIN, y, tw, 56 }, T("Tekrar kutuları"), KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
		y += 66;
		int box[KL_BOX_MAX + 1] = { 0 }, maxb = 1;
		for (int i = 0; i < db.n; i++) {
			const KlCard *c = kl_deck_find(&deck, db.w[i].word);
			if (c && c->seen) box[c->box]++;
		}
		for (int b = 1; b <= KL_BOX_MAX; b++)
			if (box[b] > maxb) maxb = box[b];
		int bw = (tw - 4 * 20) / KL_BOX_MAX, bh = h - 120 - y - 60;
		for (int b = 1; b <= KL_BOX_MAX; b++) {
			int x = MARGIN + (b - 1) * (bw + 20), hh = box[b] * (bh - 50) / maxb;
			kui_fill((KuiRect){ x, y + bh - 50 - hh, bw, hh }, 0x50);
			kui_frame((KuiRect){ x, y, bw, bh - 50 }, 1, 0xA0);
			snprintf(s, sizeof s, "%d", box[b]);
			kui_text((KuiRect){ x, y + bh - 44, bw, 40 }, s, KUI_FONT_BOLD, 30, KUI_ALIGN_CENTER, 0);
		}
		y += bh;
		snprintf(s, sizeof s, "%s", T("1: yeni kaçırılan · 5: iyice öğrenildi (aralık 1, 2, 4, 8, 21 gün)"));
		kui_text((KuiRect){ MARGIN, y, tw, 44 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 26, tw), KUI_ALIGN_CENTER, 0x40);
		kui_flush(1);
		for (int wait = 1; wait;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) wait = 0;
			if (e.type == KUI_EV_TAP && kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
		}
	}
}

/* ---- home ---- */

static int home_screen(void)
{
	int w = kui_width(), h = kui_height(), tw = w - 2 * MARGIN;
	for (int full = 1;;) {
		int d = today();
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Kelime Öğren"), NULL);
		char s[300];
		KuiRect pair = { MARGIN, KUI_TOPBAR_H + 24, tw, 100 };
		kui_frame(pair, 3, 0);
		snprintf(s, sizeof s, "%s  ←  %s", T(LANGS[learn].name), T(LANGS[native].name));
		kui_text((KuiRect){ pair.x + 24, pair.y, pair.w - 260, pair.h }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 44, pair.w - 260), KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ pair.x + pair.w - 230, pair.y, 206, pair.h }, T("Değiştir"), KUI_FONT_REG, kui_fit_px(T("Değiştir"), KUI_FONT_REG, 34, 206), KUI_ALIGN_RIGHT, 0x30);
		int y = pair.y + pair.h + 24;
		snprintf(s, sizeof s, T("Seri: %d gün · %ld XP · bugün %d/%d"), kl_streak_now(&stats, d), stats.xp, kl_today_xp(&stats, d), stats.goal);
		kui_text((KuiRect){ MARGIN, y, tw, 50 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, tw), KUI_ALIGN_CENTER, 0);
		y += 56;
		bar((KuiRect){ MARGIN + 160, y, tw - 320, 26 }, kl_today_xp(&stats, d), stats.goal);
		y += 50;
		KuiRect cards[KL_LEVELS];
		int cw = (tw - 30) / 2, ch = 210;
		for (int lv = 1; lv <= KL_LEVELS; lv++) {
			KuiRect r = { MARGIN + ((lv - 1) % 2) * (cw + 30), y + ((lv - 1) / 2) * (ch + 24), cw, ch };
			cards[lv - 1] = r;
			int learned, seen, due;
			kl_progress(&db, &deck, lv, d, &learned, &seen, &due);
			kui_frame(r, 4, 0);
			kui_text((KuiRect){ r.x + 24, r.y + 14, 140, 90 }, kl_level_name(lv), KUI_FONT_BOLD, 72, KUI_ALIGN_LEFT, 0);
			kui_text((KuiRect){ r.x + 160, r.y + 14, r.w - 180, 90 }, T(LEVEL_NAMES[lv]), KUI_FONT_REG, kui_fit_px(T(LEVEL_NAMES[lv]), KUI_FONT_REG, 34, r.w - 180),
				 KUI_ALIGN_RIGHT, 0x30);
			bar((KuiRect){ r.x + 24, r.y + 116, r.w - 48, 28 }, learned, db.level_n[lv]);
			snprintf(s, sizeof s, T("%d / %d öğrenildi"), learned, db.level_n[lv]);
			kui_text((KuiRect){ r.x + 24, r.y + 150, r.w - 48, 48 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 30, r.w - 48), KUI_ALIGN_LEFT, 0x30);
		}
		y += 3 * (ch + 24) + 6;
		int due_all;
		kl_progress(&db, &deck, 0, d, NULL, NULL, &due_all);
		char rev[80];
		snprintf(rev, sizeof rev, T("Tekrar (%d)"), due_all);
		KuiButton br = { { MARGIN, y, cw, 130 }, rev, 1 }, bs = { { MARGIN + cw + 30, y, cw, 130 }, T("İstatistik"), 2 };
		kui_button(&br, 0);
		kui_button(&bs, 0);
		snprintf(s, sizeof s, "%s", T("Kaynak: Vikisözlük (CC BY-SA), Tatoeba (CC BY), CEFR-J; seviyeler sıklığa göre"));
		kui_text((KuiRect){ MARGIN, h - 70, tw, 44 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 24, tw), KUI_ALIGN_CENTER, 0x50);
		flush_body(full);
		kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
		full = 1; /* coming back from another screen */
		for (int wait = 1; wait;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) break;
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
			int quit = 0, hit = 0;
			if (kui_rect_contains(pair, e.x, e.y)) {
				kui_fill(pair, 0);
				kui_mark_dirty(pair);
				kui_flush(0);
				quit = lang_screen(0) == -2, hit = 1;
			}
			for (int lv = 1; lv <= KL_LEVELS && !hit; lv++)
				if (kui_rect_contains(cards[lv - 1], e.x, e.y)) {
					kui_fill(cards[lv - 1], 0); /* the tapped card turns black until the lesson opens */
					kui_mark_dirty(cards[lv - 1]);
					kui_flush(0);
					quit = play(lv) < 0, hit = 1;
				}
			if (!hit && kui_rect_contains(br.r, e.x, e.y)) {
				kui_button(&br, 1);
				kui_flush(0);
				quit = play(0) < 0, hit = 1;
			}
			if (!hit && kui_rect_contains(bs.r, e.x, e.y)) {
				kui_button(&bs, 1);
				kui_flush(0);
				quit = stats_screen() < 0, hit = 1;
			}
			if (quit) return -1;
			if (hit) wait = 0;
		}
	}
}

int main(void)
{
	if (kui_init("kelime") != 0) return 1;
	scan_lists();
	{
		char buf[4096];
		kl_stats_parse(kui_read_file(data_path("stats"), buf, sizeof buf) > 0 ? buf : "", &stats);
	}
	int saved_learn = kui_cfg_get_int(data_path("settings"), "learn", -1);
	native = kui_cfg_get_int(data_path("settings"), "native", -1);
	if (native < 0 || native >= N_LANGS) { /* the interface language, when it is one of ours */
		native = 0;
		for (int l = 0; l < N_LANGS; l++)
			if (!strcmp(LANGS[l].code, kui_lang())) native = l;
	}
	int any = 0;
	for (int l = 0; l < N_LANGS; l++) any |= has_list[l];
	if (!any) {
		kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
		kui_topbar(T("Kelime Öğren"), NULL);
		kui_text((KuiRect){ MARGIN, 500, kui_width() - 2 * MARGIN, 200 }, T("Kelime listeleri yüklenemedi."), KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT || (e.type == KUI_EV_TAP && kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT)) break;
		}
		kui_quit();
		return 0;
	}
	rng_state = (unsigned) time(NULL) | 1u;
	int ok = saved_learn >= 0 && saved_learn < N_LANGS && saved_learn != native && has_list[saved_learn] && open_lang(saved_learn) == 0;
	if (ok || lang_screen(1) == 0) home_screen();
	kl_deck_free(&deck);
	kl_free(&db);
	free(big_buf);
	kui_quit();
	return 0;
}

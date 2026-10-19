/* Kelime Öğren: a vocabulary course for many language pairs, levels A1–C2. A lesson first TEACHES a few new words
 * ("Yeni kelime" cards: the word with its article, pronunciation, meaning and an example), then practises only words
 * the learner has been taught: the new ones (word → meaning, then meaning → word) and the due reviews, whose
 * question gets harder as the word climbs the Leitner boxes. A missed word shows its card again and comes back later
 * in the lesson. A1 starts with hand-picked starter themes (greetings, pronouns, numbers …), then units of nouns,
 * verbs and other words by frequency. Sözlük lists every word of a level (taught, next, "biliyorum"); a short
 * placement test can skip what the learner already knows. XP, a daily goal and a streak. Fully offline. Every tap
 * inside a screen is a partial refresh; only a new screen gets a full one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "kelime.h"

#define MARGIN 60
#define UNIT_SIZE 15
#define MAX_Q 40

static const struct { const char *code, *name; } LANGS[] = {
	{ "tr", N_("Türkçe") },	   { "en", N_("İngilizce") },	{ "de", N_("Almanca") },    { "fr", N_("Fransızca") },
	{ "es", N_("İspanyolca") }, { "it", N_("İtalyanca") }, { "pt", N_("Portekizce") },
};
#define N_LANGS ((int) (sizeof LANGS / sizeof LANGS[0]))

static const char *LEVEL_NAMES[KL_LEVELS + 1] = { "", N_("Başlangıç"), N_("Temel"), N_("Orta"), N_("Orta üstü"), N_("İleri"), N_("Ustalık") };
static const char *POS_LABELS[KL_POS_N] = { "", N_("isim"), N_("fiil"), N_("sıfat"), N_("zarf"), N_("zamir"), N_("edat"), N_("bağlaç"), N_("sayı"),
					    N_("belirleyici"), N_("ünlem"), N_("deyim") };
/* unit names: the starter themes, then "%d" = the unit's number in its level */
static const char *UNIT_NAMES[KL_U_N] = { N_("Selamlaşma"),  N_("Ben, sen, o"), N_("Sayılar"),	  N_("Temel fiiller"),
					  N_("İnsanlar"),    N_("Günlük şeyler"), N_("Temel sıfatlar"), N_("İsimler %d"),
					  N_("Fiiller %d"),  N_("Sıfatlar %d"),   N_("Sayılar %d"),	  N_("Küçük kelimeler %d") };
static const int NEW_CHOICES[] = { 3, 5, 8 };

static KlDb db;
static KlDeck deck;
static KlCourse course;
static KlStats stats;
static int learn = -1, native = 0; /* indexes into LANGS */
static int course_native = -1;     /* the native language the course was built for */
static int new_n = 5;              /* new words per lesson */
static int has_list[N_LANGS], list_count[N_LANGS];

/* ---- files ---- */

static const char *data_path(const char *name)
{
	static char p[320];
	snprintf(p, sizeof p, "%s/%s", kui_data_dir("kelime"), name);
	return p;
}

static int cfg_get(const char *key, int def) { return kui_cfg_get_int(data_path("settings"), key, def); }
static void cfg_set(const char *key, int v) { kui_cfg_set_int(data_path("settings"), key, v); }

static int lang_cfg(const char *what, int def)
{
	char k[32];
	snprintf(k, sizeof k, "%s_%s", what, LANGS[learn].code);
	return cfg_get(k, def);
}

static void lang_cfg_set(const char *what, int v)
{
	char k[32];
	snprintf(k, sizeof k, "%s_%s", what, LANGS[learn].code);
	cfg_set(k, v);
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
	for (int i = 0; i < deck.n; i++) need += (int) strlen(deck.c[i].key) + 64;
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
	cfg_set("learn", learn);
	cfg_set("native", native);
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

static const char *ncode(void) { return LANGS[native].code; }

/* the course depends on the native language too (only words with a translation into it are taught) */
static void build_course(void)
{
	if (course_native == native && course.word) return;
	kl_course_free(&course);
	kl_course_build(&db, ncode(), UNIT_SIZE, &course);
	course_native = native;
}

/* loads the word list of language l; 0 = ok */
static int open_lang(int l)
{
	if (l == learn && db.n) {
		build_course();
		return 0;
	}
	kl_free(&db);
	for (int dev = 0; dev < 2; dev++) {
		char p[320];
		list_path(l, p, sizeof p, dev);
		if (kl_load(p, &db) > 0) {
			learn = l;
			load_deck();
			course_native = -1;
			build_course();
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

static int cur_level(void)
{
	int lv = lang_cfg("level", 1);
	return lv >= 1 && lv <= KL_LEVELS ? lv : 1;
}

static int any_taught(void)
{
	for (int i = 0; i < deck.n; i++)
		if (kl_card_taught(&deck.c[i])) return 1;
	return 0;
}

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

static void unit_name(int u, char *out, int cap)
{
	if (u < 0 || u >= course.n_unit) {
		out[0] = '\0';
		return;
	}
	const KlUnit *un = &course.unit[u];
	if (un->num) snprintf(out, (size_t) cap, T(UNIT_NAMES[un->kind]), un->num);
	else snprintf(out, (size_t) cap, "%s", T(UNIT_NAMES[un->kind]));
}

/* "Ünite 3: Sayılar" (numbered within its level) */
static void unit_label(int u, char *out, int cap)
{
	char name[96];
	unit_name(u, name, sizeof name);
	int k = 1;
	for (int i = 0; i < u; i++) k += course.unit[i].level == course.unit[u].level;
	snprintf(out, (size_t) cap, T("Ünite %d: %s"), k, name);
}

static void translation(const KlWord *w, char *out, int cap)
{
	if (kl_field(w->tr, ncode(), out, cap)) return;
	char en[200];
	if (strcmp(ncode(), "en") && kl_field(w->tr, "en", en, sizeof en)) snprintf(out, (size_t) cap, "(%s) %s", T("İng."), en);
}

/* "isim · eril" */
static void pos_gender(const KlWord *w, char *out, int cap)
{
	const char *pos = T(POS_LABELS[w->pos]);
	int g = kl_gender(w);
	const char *gl = g == 'm' ? T("eril") : g == 'f' ? T("dişil") : g == 'n' ? T("nötr") : "";
	snprintf(out, (size_t) cap, "%s%s%s", pos, pos[0] && gl[0] ? " · " : "", gl);
}

/* The word's card: the word big (with its article), its pronunciation, part of speech and gender, the meaning, an
 * example with its translation and (show_def) the definition in the language itself. */
static void word_card(KuiRect r, int wi, const char *label, int show_def)
{
	const KlWord *w = &db.w[wi];
	kui_fill(r, 255);
	kui_frame(r, 4, 0);
	int x = r.x + 34, tw = r.w - 68, y = r.y + 22, bottom = r.y + r.h - 20;
	char s[900], disp[200];
	kui_text((KuiRect){ x, y, tw, 44 }, label, KUI_FONT_BOLD, 30, KUI_ALIGN_CENTER, 0x50);
	y += 54;
	kl_display(w, disp, sizeof disp);
	kui_text((KuiRect){ x, y, tw, 136 }, disp, KUI_FONT_BOLD, kui_fit_px(disp, KUI_FONT_BOLD, 108, tw), KUI_ALIGN_CENTER, 0);
	y += 140;
	if (w->say[0] && !strcmp(ncode(), "tr")) {
		snprintf(s, sizeof s, T("okunuşu: %s"), w->say);
		kui_text((KuiRect){ x, y, tw, 54 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 40, tw), KUI_ALIGN_CENTER, 0x20);
		y += 56;
	}
	if (w->ipa[0]) {
		kui_text((KuiRect){ x, y, tw, 44 }, w->ipa, KUI_FONT_REG, kui_fit_px(w->ipa, KUI_FONT_REG, 32, tw), KUI_ALIGN_CENTER, 0x60);
		y += 48;
	}
	pos_gender(w, s, sizeof s);
	if (s[0]) {
		kui_text((KuiRect){ x, y, tw, 46 }, s, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0x40);
		y += 52;
	}
	kui_fill((KuiRect){ x + 100, y + 8, tw - 200, 2 }, 0x90);
	y += 30;
	snprintf(s, sizeof s, T("%s anlamı"), T(LANGS[native].name));
	kui_text((KuiRect){ x, y, tw, 40 }, s, KUI_FONT_REG, 30, KUI_ALIGN_CENTER, 0x50);
	y += 44;
	char tr[300];
	translation(w, tr, sizeof tr);
	y += draw_wrap((KuiRect){ x, y, tw, 160 }, tr[0] ? tr : "—", KUI_FONT_BOLD, 66, 36, 2, KUI_ALIGN_CENTER, 0, -1, 0) + 26;
	if (w->ex[0] && y < bottom - 110) {
		char ex[700], extr[700];
		kl_unmark(w->ex, ex, sizeof ex);
		kui_text((KuiRect){ x, y, tw, 40 }, T("Örnek cümle"), KUI_FONT_BOLD, 30, KUI_ALIGN_LEFT, 0x50);
		y += 44;
		snprintf(s, sizeof s, "„%s“", ex);
		y += draw_wrap((KuiRect){ x, y, tw, bottom - y > 130 ? 130 : bottom - y }, s, KUI_FONT_REG, 40, 28, 3, KUI_ALIGN_LEFT, 0, -1, 0) + 6;
		if (kl_field(w->extr, ncode(), extr, sizeof extr) && y < bottom - 40)
			y += draw_wrap((KuiRect){ x, y, tw, bottom - y > 100 ? 100 : bottom - y }, extr, KUI_FONT_REG, 34, 24, 2, KUI_ALIGN_LEFT, 0x50, -1, 0) + 14;
	}
	if (show_def && w->def[0] && y < bottom - 90) {
		char def[600];
		kl_unmark(w->def, def, sizeof def);
		snprintf(s, sizeof s, T("%s tanımı"), T(LANGS[learn].name));
		kui_text((KuiRect){ x, y, tw, 40 }, s, KUI_FONT_BOLD, 30, KUI_ALIGN_LEFT, 0x50);
		y += 44;
		draw_wrap((KuiRect){ x, y, tw, bottom - y }, def, KUI_FONT_REG, 32, 24, 4, KUI_ALIGN_LEFT, 0x30, -1, 0);
	}
	kui_mark_dirty(r);
}

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
			kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 88, w - 2 * MARGIN, 44 }, T("Önce kelimeler öğretilir, sonra yalnızca öğrendiklerin sorulur."),
				 KUI_FONT_REG, kui_fit_px(T("Önce kelimeler öğretilir, sonra yalnızca öğrendiklerin sorulur."), KUI_FONT_REG, 28, w - 2 * MARGIN),
				 KUI_ALIGN_CENTER, 0x40);
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
			build_course();
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

/* ---- questions (lesson and placement) ---- */

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
	if (q->type == KL_Q_TF) snprintf(out, (size_t) cap, "%s", i == 0 ? T("Doğru") : T("Yanlış"));
	else if (q->type == KL_Q_MEANING) kl_field(db.w[q->opts[i]].tr, ncode(), out, cap);
	else kl_display(&db.w[q->opts[i]], out, cap);
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

static void prompt_texts(const KlQuestion *q, char *head, int hc, char *big, int bc, char *sub, int sc)
{
	const KlWord *w = &db.w[q->word];
	const char *lname = T(LANGS[learn].name);
	sub[0] = '\0';
	switch (q->type) {
	case KL_Q_MEANING:
		snprintf(head, (size_t) hc, "%s", T("Bu kelime ne demek?"));
		kl_display(w, big, bc);
		if (w->say[0] && !strcmp(ncode(), "tr")) snprintf(sub, (size_t) sc, "%s · %s", T(POS_LABELS[w->pos]), w->say);
		else snprintf(sub, (size_t) sc, "%s", T(POS_LABELS[w->pos]));
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

/* the prompt box and the options (the body below the progress line) */
static void draw_question(const KlQuestion *q)
{
	int w = kui_width(), h = kui_height();
	char head[200], big[1024], sub[600];
	prompt_texts(q, head, sizeof head, big, sizeof big, sub, sizeof sub);
	kui_fill((KuiRect){ 0, PROMPT_Y - 10, w, h - PROMPT_Y + 10 }, 255);
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

/* ---- lesson ---- */

typedef struct {
	KlPlan plan;
	KlQuestion q[MAX_Q];
	int n, at, right, wrong, missed[MAX_Q], n_missed, lv;
	int result[MAX_Q]; /* 0 not yet, 1 right, 2 wrong */
	int answered[MAX_Q], n_answered; /* words answered at least once in this lesson */
	int reasked[MAX_Q];              /* per answered[] word: how often a miss brought it back */
	char title[120];
} Lesson;
static Lesson L;
static unsigned rng_state;
static int seeded; /* KELIME_SEED (simulator runs): the same lessons every time, answers logged to stderr */

enum { LS_DONE, LS_EXIT, LS_QUIT, LS_NEXT, LS_BACK };

static void progress_bar(void)
{
	int w = kui_width(), n = L.n, segw = (w - 2 * MARGIN) / (n ? n : 1);
	KuiRect all = { MARGIN, KUI_TOPBAR_H + 22, w - 2 * MARGIN, 30 };
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H, w, 60 }, 255);
	for (int i = 0; i < n; i++) {
		KuiRect s = { MARGIN + i * segw + 3, all.y, segw - 6, all.h };
		kui_frame(s, 2, 0);
		if (L.result[i] == 1) kui_fill(s, 0);
		else if (L.result[i] == 2) kui_fill((KuiRect){ s.x + 2, s.y + 2, s.w - 4, s.h - 4 }, 0xA0);
		else if (i == L.at) kui_fill((KuiRect){ s.x, s.y + s.h - 8, s.w, 8 }, 0);
	}
	kui_mark_dirty(all);
}

/* the feedback after an answer: right or wrong, the word = meaning and its example */
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
	translation(wd, tr, sizeof tr);
	if (correct) snprintf(s, sizeof s, "✓ %s", T("Doğru!"));
	else snprintf(s, sizeof s, "✗ %s", T("Yanlış — kartına bir daha bakalım"));
	kui_text((KuiRect){ x, y, tw, 52 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 40, tw), KUI_ALIGN_LEFT, 0);
	y += 58;
	snprintf(s, sizeof s, tr[0] ? "%s  =  %s" : "%s", disp, tr);
	y += draw_wrap((KuiRect){ x, y, tw, 100 }, s, KUI_FONT_BOLD, 38, 26, 2, KUI_ALIGN_LEFT, 0, -1, 0) + 10;
	int bottom = panel.y + panel.h - 12;
	if (wd->ex[0] && y < bottom - 40) {
		kl_unmark(wd->ex, ex, sizeof ex);
		snprintf(s, sizeof s, "„%s“", ex);
		y += draw_wrap((KuiRect){ x, y, tw, bottom - y > 100 ? 100 : bottom - y }, s, KUI_FONT_REG, 32, 24, 2, KUI_ALIGN_LEFT, 0x20, -1, 0) + 4;
		if (kl_field(wd->extr, ncode(), extr, sizeof extr) && y < bottom - 30)
			draw_wrap((KuiRect){ x, y, tw, bottom - y }, extr, KUI_FONT_REG, 28, 22, 2, KUI_ALIGN_LEFT, 0x50, -1, 0);
	}
	kui_mark_dirty(panel);
	KuiButton nb = { nr, L.at + 1 < L.n || !correct ? T("Devam ▶") : T("Sonuç"), 1 };
	kui_button(&nb, 0);
}

/* the "Yeni kelime" card inside the lesson: idx of total (0 = no counter), with ◀ when back is allowed */
static int teach_screen(int wi, int idx, int total, const char *label, int back, int full)
{
	int w = kui_width(), h = kui_height();
	KuiButton next = { { MARGIN + (back ? 230 : 0), h - 160, w - 2 * MARGIN - (back ? 230 : 0), 130 }, T("Anladım ▶"), 1 };
	KuiButton prev = { { MARGIN, h - 160, 200, 130 }, "◀", 2 };
	for (int redraw = 1;;) {
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(L.title, NULL);
			char s[160];
			if (total) {
				snprintf(s, sizeof s, T("Yeni kelime %d / %d"), idx + 1, total);
				kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 14, w - 2 * MARGIN - 300, 56 }, s, KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
				for (int i = 0; i < total; i++) { /* a dot per new word */
					KuiRect d = { w - MARGIN - (total - i) * 36, KUI_TOPBAR_H + 30, 24, 24 };
					if (i <= idx) kui_fill(d, 0);
					else kui_frame(d, 2, 0);
				}
			} else {
				kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 14, w - 2 * MARGIN, 56 }, T("Bu kelimeyi tekrar çalışalım"), KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
			}
			word_card((KuiRect){ MARGIN, KUI_TOPBAR_H + 84, w - 2 * MARGIN, h - KUI_TOPBAR_H - 84 - 190 }, wi, label, db.w[wi].level >= 3);
			if (back) kui_button(&prev, 0);
			kui_button(&next, 0);
			flush_body(full);
			kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
			redraw = full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return LS_QUIT;
		if (e.type == KUI_EV_WAKE) {
			redraw = full = 1;
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return LS_EXIT;
		if (kui_rect_contains(next.r, e.x, e.y)) {
			kui_button(&next, 1);
			kui_flush(0);
			return LS_NEXT;
		}
		if (back && kui_rect_contains(prev.r, e.x, e.y)) {
			kui_button(&prev, 1);
			kui_flush(0);
			return LS_BACK;
		}
	}
}

static void record(int correct)
{
	const KlQuestion *q = &L.q[L.at];
	int d = today(), again = 0, slot = L.n_answered;
	for (int i = 0; i < L.n_answered; i++)
		if (L.answered[i] == q->word) again = 1, slot = i;
	KlCard *c = kl_deck_get(&deck, db.w[q->word].word);
	if (c) {
		if (again) kl_card_again(c, correct, d);
		else kl_card_answer(c, correct, d);
	}
	if (!again && L.n_answered < MAX_Q) L.answered[L.n_answered++] = q->word;
	kl_stats_answer(&stats, correct, d);
	L.result[L.at] = correct ? 1 : 2;
	if (correct) {
		L.right++;
	} else {
		L.wrong++;
		int seen = 0;
		for (int i = 0; i < L.n_missed; i++) seen |= L.missed[i] == q->word;
		if (!seen && L.n_missed < MAX_Q) L.missed[L.n_missed++] = q->word;
		KlQuestion nq;
		if (slot < MAX_Q && L.reasked[slot] < 2 && kl_again(&db, q, ncode(), &rng_state, &nq)) { /* asked again a little later */
			L.reasked[slot]++;
			int pos = kl_insert(L.q, &L.n, MAX_Q, L.at, 3, &nq);
			if (pos >= 0) {
				memmove(&L.result[pos + 1], &L.result[pos], (size_t) (L.n - 1 - pos) * sizeof L.result[0]);
				L.result[pos] = 0;
			}
		}
	}
	save_deck();
	save_stats();
}

static void draw_lesson_screen(void)
{
	int w = kui_width();
	kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
	kui_topbar(L.title, NULL);
	progress_bar();
	draw_question(&L.q[L.at]);
}

/* the practice part; full: the first question enters the screen */
static int lesson_loop(int full)
{
	int w = kui_width();
	for (L.at = 0; L.at < L.n;) {
		draw_lesson_screen();
		flush_body(full);
		kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
		full = 0;
		if (seeded) fprintf(stderr, "kelime: question %d/%d answer %d\n", L.at + 1, L.n, right_button(&L.q[L.at]));
		int answered = 0, tapped = -1, ok = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return LS_QUIT;
			if (e.type == KUI_EV_WAKE) { /* redraw everything as it was */
				draw_lesson_screen();
				if (answered) {
					const KlQuestion *q = &L.q[L.at];
					draw_opt(q, tapped, 1);
					if (!ok) draw_opt(q, right_button(q), 2);
					draw_panel(ok);
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
				ok = tapped == right_button(q);
				draw_opt(q, tapped, 1); /* first only the tapped button turns black */
				kui_flush(0);
				record(ok);
				q = &L.q[L.at];
				if (!ok) draw_opt(q, right_button(q), 2);
				progress_bar();
				draw_panel(ok);
				kui_flush(0);
			} else if (kui_rect_contains(next_rect(), e.x, e.y) || e.y > PANEL_Y) {
				KuiButton nb = { next_rect(), "", 1 };
				nb.label = L.at + 1 < L.n || !ok ? T("Devam ▶") : T("Sonuç");
				kui_button(&nb, 1);
				kui_flush(0);
				if (!ok) { /* a miss: the word's card once more, then on */
					int r = teach_screen(L.q[L.at].word, 0, 0, T("TEKRAR BAKALIM"), 0, 0);
					if (r == LS_QUIT || r == LS_EXIT) return r;
				}
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
	KuiButton again = { { MARGIN, h - 180, (w - 2 * MARGIN - 30) / 2, 130 }, L.lv ? T("Sonraki ders ▶") : T("Yeni tekrar"), 1 };
	KuiButton home = { { MARGIN + again.r.w + 30, h - 180, again.r.w, 130 }, T("Ana ekran"), 2 };
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Ders bitti"), NULL);
		int y = KUI_TOPBAR_H + 40, tw = w - 2 * MARGIN;
		char s[300];
		if (L.plan.n_new) snprintf(s, sizeof s, T("%d yeni kelime öğrendin!"), L.plan.n_new);
		else snprintf(s, sizeof s, T("%d sorudan %d doğru"), L.right + L.wrong, L.right);
		kui_text((KuiRect){ MARGIN, y, tw, 90 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 60, tw), KUI_ALIGN_CENTER, 0);
		y += 100;
		int gained = L.right * KL_XP_RIGHT + bonus;
		if (L.plan.n_new) snprintf(s, sizeof s, T("%d sorudan %d doğru · +%d XP"), L.right + L.wrong, L.right, gained);
		else snprintf(s, sizeof s, T("+%d XP"), gained);
		kui_text((KuiRect){ MARGIN, y, tw, 60 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 40, tw), KUI_ALIGN_CENTER, 0);
		y += 70;
		snprintf(s, sizeof s, T("Seri: %d gün · bugün %d/%d XP"), kl_streak_now(&stats, d), kl_today_xp(&stats, d), stats.goal);
		kui_text((KuiRect){ MARGIN, y, tw, 56 }, s, KUI_FONT_REG, 36, KUI_ALIGN_CENTER, 0);
		y += 64;
		bar((KuiRect){ MARGIN + 100, y, tw - 200, 30 }, kl_today_xp(&stats, d), stats.goal);
		y += 60;
		/* this lesson's words: the new ones, then the missed reviews (↻: comes back soon) */
		int words[KL_NEW_MAX + MAX_Q], nw = 0;
		for (int i = 0; i < L.plan.n_new; i++) words[nw++] = L.plan.new_w[i];
		for (int i = 0; i < L.n_missed; i++) {
			int dup = 0;
			for (int k = 0; k < nw; k++) dup |= words[k] == L.missed[i];
			if (!dup) words[nw++] = L.missed[i];
		}
		if (nw) {
			kui_text((KuiRect){ MARGIN, y, tw, 52 }, L.plan.n_new ? T("Bu dersin kelimeleri") : T("Tekrar edilecek kelimeler"), KUI_FONT_BOLD, 38,
				 KUI_ALIGN_LEFT, 0);
			y += 60;
			for (int i = 0; i < nw && y < again.r.y - 70; i++) {
				const KlWord *wd = &db.w[words[i]];
				char disp[200], tr[300];
				int miss = 0;
				for (int k = 0; k < L.n_missed; k++) miss |= L.missed[k] == words[i];
				kl_display(wd, disp, sizeof disp);
				translation(wd, tr, sizeof tr);
				snprintf(s, sizeof s, "%s %s  —  %s", miss ? "↻" : "✓", disp, tr);
				kui_text((KuiRect){ MARGIN + 20, y, tw - 20, 54 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, tw - 20), KUI_ALIGN_LEFT, 0);
				y += 58;
			}
			if (L.n_missed && y < again.r.y - 60) {
				kui_text((KuiRect){ MARGIN, y + 6, tw, 44 }, T("↻ yanlış bilinenler bir sonraki derste yine sorulur"), KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0x50);
			}
		} else {
			kui_text((KuiRect){ MARGIN, y + 40, tw, 60 }, T("Hiç hata yok, harika!"), KUI_FONT_BOLD, 44, KUI_ALIGN_CENTER, 0);
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

/* runs lessons from level lv (0 = reviews only) until the user goes home; -1 on quit */
static int play(int lv)
{
	for (;;) {
		memset(&L, 0, sizeof L);
		L.lv = lv;
		if (!seeded) rng_state ^= (unsigned) time(NULL) * 2654435761u + 1;
		if (!rng_state) rng_state = 1;
		int d = today();
		kl_plan(&db, &course, &deck, lv, d, lv ? new_n : 0, lv ? 8 : KL_REV_MAX, &L.plan);
		if (!L.plan.n_new && !L.plan.n_rev) {
			const char *ok[1] = { T("Tamam") };
			kui_dialog(lv ? T("Tüm kelimeler öğretildi") : T("Tekrar edilecek kelime yok"),
				   lv ? T("Bu dil ikilisinin bütün kelimelerini gördün. Tekrarlarla devam et.") : T("Bugünlük tekrar bitti. Yeni kelimeler için derse devam et."),
				   ok, 1, NULL);
			return 0;
		}
		if (lv && L.plan.level != lv) lang_cfg_set("level", L.plan.level); /* the level was finished: on to the next */
		if (lv && L.plan.unit >= 0) {
			char u[120];
			unit_label(L.plan.unit, u, sizeof u);
			snprintf(L.title, sizeof L.title, "%s · %s", kl_level_name(L.plan.level), u);
		} else {
			snprintf(L.title, sizeof L.title, "%s", T("Tekrar"));
		}
		/* 1: teach the new words, one card each */
		int full = 1;
		for (int i = 0; i < L.plan.n_new;) {
			int r = teach_screen(L.plan.new_w[i], i, L.plan.n_new, T("YENİ KELİME"), i > 0, full);
			full = 0;
			if (r == LS_QUIT) return -1;
			if (r == LS_EXIT) {
				save_deck();
				return 0;
			}
			KlCard *c = kl_deck_get(&deck, db.w[L.plan.new_w[i]].word);
			if (c) kl_card_teach(c, d);
			i += r == LS_BACK ? -1 : 1;
		}
		if (L.plan.n_new) save_deck();
		/* 2: practise what was taught */
		L.n = kl_practice(&db, &deck, ncode(), &L.plan, kl_rand(&rng_state), L.q, MAX_Q - 8);
		if (!L.n) return 0;
		int r = lesson_loop(full);
		if (r == LS_QUIT) return -1;
		if (r == LS_EXIT) return 0;
		int bonus = L.wrong == 0 ? KL_XP_PERFECT : 0;
		if (bonus) kl_stats_bonus(&stats, bonus, d);
		kl_streak_touch(&stats, d);
		save_stats();
		r = summary_screen(bonus);
		if (r < 0) return -1;
		if (r == 0) return 0;
	}
}

/* ---- placement ---- */

/* "Nereden başlayalım?": 0 from zero, 1 the test, -1 quit */
static int start_choice(void)
{
	int w = kui_width(), h = kui_height(), tw = w - 2 * MARGIN;
	KuiRect zero = { MARGIN, KUI_TOPBAR_H + 330, tw, 300 }, test = { MARGIN, zero.y + zero.h + 50, tw, 300 };
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Kelime Öğren"), NULL);
		char s[200];
		snprintf(s, sizeof s, T("%s öğrenmeye nereden başlayalım?"), T(LANGS[learn].name));
		draw_wrap((KuiRect){ MARGIN, KUI_TOPBAR_H + 60, tw, 140 }, s, KUI_FONT_BOLD, 56, 36, 2, KUI_ALIGN_CENTER, 0, -1, 1);
		kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 220, tw, 50 }, T("Her derste önce yeni kelimeler öğretilir, sonra alıştırma yapılır."), KUI_FONT_REG,
			 kui_fit_px(T("Her derste önce yeni kelimeler öğretilir, sonra alıştırma yapılır."), KUI_FONT_REG, 32, tw), KUI_ALIGN_CENTER, 0x40);
		const KuiRect rs[2] = { zero, test };
		const char *ti[2] = { T("Hiç bilmiyorum"), T("Biraz biliyorum") };
		const char *su[2] = { T("Sıfırdan başla: selamlaşma, sayılar, temel fiiller…"), T("Kısa seviye testi: her seviyeden 5 kelime") };
		for (int i = 0; i < 2; i++) {
			kui_fill(rs[i], 255);
			kui_frame(rs[i], 5, 0);
			kui_text((KuiRect){ rs[i].x + 30, rs[i].y + 50, rs[i].w - 60, 100 }, ti[i], KUI_FONT_BOLD, kui_fit_px(ti[i], KUI_FONT_BOLD, 66, rs[i].w - 60),
				 KUI_ALIGN_CENTER, 0);
			draw_wrap((KuiRect){ rs[i].x + 30, rs[i].y + 170, rs[i].w - 60, 100 }, su[i], KUI_FONT_REG, 36, 26, 2, KUI_ALIGN_CENTER, 0x30, -1, 0);
		}
		kui_flush(1);
		for (int wait = 1; wait;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) wait = 0;
			if (e.type != KUI_EV_TAP) continue;
			for (int i = 0; i < 2; i++)
				if (kui_rect_contains(rs[i], e.x, e.y)) {
					kui_fill(rs[i], 0);
					kui_mark_dirty(rs[i]);
					kui_flush(0);
					return i;
				}
		}
	}
}

/* level by level, KL_PLACE_PER_LEVEL words each, until a level is not passed; returns 0, or -1 on quit */
static int placement_test(void)
{
	int w = kui_width(), h = kui_height();
	int asked[KL_LEVELS + 1] = { 0 }, right[KL_LEVELS + 1] = { 0 }, known[KL_LEVELS * KL_PLACE_PER_LEVEL], n_known = 0, more = 1, full = 1;
	KuiButton dunno = { { MARGIN + 140, PANEL_Y + 30, w - 2 * MARGIN - 280, 120 }, T("Bilmiyorum"), 9 };
	for (int lv = 1; lv <= KL_LEVELS && more; lv++) {
		int words[KL_PLACE_PER_LEVEL];
		int k = kl_place_pick(&course, lv, &rng_state, words, KL_PLACE_PER_LEVEL);
		if (!k) break;
		for (int i = 0; i < k; i++) {
			KlQuestion q;
			if (!kl_make(&db, words[i], KL_Q_MEANING, ncode(), &rng_state, &q)) continue;
			if (seeded) fprintf(stderr, "kelime: placement %s %d answer %d\n", kl_level_name(lv), i + 1, q.answer);
			for (int redraw = 1, done = 0; !done;) {
				if (redraw) {
					kui_fill((KuiRect){ 0, 0, w, h }, 255);
					kui_topbar(T("Seviye testi"), NULL);
					char s[120];
					snprintf(s, sizeof s, T("%s · kelime %d / %d"), kl_level_name(lv), i + 1, k);
					kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 10, w - 2 * MARGIN, 50 }, s, KUI_FONT_BOLD, 34, KUI_ALIGN_CENTER, 0x30);
					draw_question(&q);
					kui_button(&dunno, 0);
					flush_body(full);
					kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
					redraw = full = 0;
				}
				KuiEvent e = kui_next_event(-1);
				if (e.type == KUI_EV_QUIT) return -1;
				if (e.type == KUI_EV_WAKE) {
					redraw = full = 1;
					continue;
				}
				if (e.type != KUI_EV_TAP) continue;
				if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0; /* stop the test: start from zero */
				int tapped = -1;
				for (int b = 0; b < q.n_opts; b++)
					if (kui_rect_contains(opt_rect(q.n_opts, b), e.x, e.y)) tapped = b;
				if (tapped >= 0) {
					draw_opt(&q, tapped, 1);
					kui_flush(0);
				} else if (kui_rect_contains(dunno.r, e.x, e.y)) {
					kui_button(&dunno, 1);
					kui_flush(0);
				} else {
					continue;
				}
				asked[lv]++;
				if (tapped == q.answer) {
					right[lv]++;
					known[n_known++] = q.word;
				}
				done = 1;
			}
		}
		kl_place_level(asked, right, &more);
	}
	int start = kl_place_level(asked, right, &more);
	int d = today();
	for (int i = 0; i < n_known; i++) {
		KlCard *c = kl_deck_get(&deck, db.w[known[i]].word);
		if (c) kl_card_know(c, d);
	}
	save_deck();
	lang_cfg_set("level", start);
	/* the result */
	KuiButton go = { { MARGIN + 100, h - 200, w - 2 * MARGIN - 200, 140 }, T("Derslere başla ▶"), 1 };
	for (;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Seviye testi"), NULL);
		char s[300], lab[100];
		kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 120, w - 2 * MARGIN, 70 }, T("Başlangıç seviyen"), KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0x30);
		level_label(start, lab, sizeof lab);
		kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 210, w - 2 * MARGIN, 140 }, lab, KUI_FONT_BOLD, kui_fit_px(lab, KUI_FONT_BOLD, 100, w - 2 * MARGIN), KUI_ALIGN_CENTER, 0);
		int y = KUI_TOPBAR_H + 400;
		for (int lv = 1; lv <= KL_LEVELS; lv++) {
			if (!asked[lv]) continue;
			snprintf(s, sizeof s, T("%s: %d / %d doğru"), kl_level_name(lv), right[lv], asked[lv]);
			kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN, 56 }, s, KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0);
			y += 62;
		}
		if (n_known) {
			snprintf(s, sizeof s, T("Bildiğin %d kelime \"biliyorum\" olarak işaretlendi; ara sıra tekrar sorulur."), n_known);
			draw_wrap((KuiRect){ MARGIN, y + 30, w - 2 * MARGIN, 120 }, s, KUI_FONT_REG, 34, 26, 3, KUI_ALIGN_CENTER, 0x30, -1, 0);
		}
		kui_button(&go, 0);
		kui_flush(1);
		for (int wait = 1; wait;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) wait = 0;
			if (e.type == KUI_EV_TAP && (kui_rect_contains(go.r, e.x, e.y) || kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT)) return 0;
		}
	}
}

/* the first start with a language: from zero or the placement test; -1 on quit */
static int ensure_placed(void)
{
	if (lang_cfg("placed", 0) || any_taught()) return 0;
	int c = start_choice();
	if (c < 0) return -1;
	if (c == 1 && placement_test() < 0) return -1;
	if (c == 0) lang_cfg_set("level", 1);
	lang_cfg_set("placed", 1);
	return 0;
}

/* ---- dictionary ---- */

/* the status of a word in the course: glyph and gray */
static const char *word_status(const KlCard *c, unsigned char *fg)
{
	*fg = 0;
	if (c && (c->flags & KL_F_KNOWN)) return "★";
	if (c && kl_card_taught(c)) return c->box >= KL_LEARNED_BOX ? "✓" : "•";
	*fg = 0x70;
	return "○";
}

/* one word's full card, with "I know it" / "teach me"; -1 quit */
static int word_screen(int wi)
{
	int w = kui_width(), h = kui_height();
	KuiButton tog = { { MARGIN, h - 160, w - 2 * MARGIN, 130 }, "", 1 };
	for (int full = 1, redraw = 1;;) {
		KlCard *c = kl_deck_find(&deck, db.w[wi].word);
		int known = c && (c->flags & KL_F_KNOWN);
		tog.label = known ? T("Bilmiyorum, bana öğret") : T("Bunu zaten biliyorum ★");
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(T("Sözlük"), NULL);
			char s[200];
			if (known) snprintf(s, sizeof s, "%s", T("★ Biliyorum olarak işaretli"));
			else if (kl_card_taught(c)) snprintf(s, sizeof s, T("Öğretildi · tekrar kutusu %d / %d"), c->box, KL_BOX_MAX);
			else snprintf(s, sizeof s, "%s", T("Henüz öğretilmedi"));
			kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 14, w - 2 * MARGIN, 56 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 36, w - 2 * MARGIN), KUI_ALIGN_CENTER, 0x30);
			char lab[120];
			level_label(db.w[wi].level, lab, sizeof lab);
			word_card((KuiRect){ MARGIN, KUI_TOPBAR_H + 84, w - 2 * MARGIN, h - KUI_TOPBAR_H - 84 - 190 }, wi, lab, 1);
			kui_button(&tog, 0);
			flush_body(full);
			kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
			redraw = full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return -1;
		if (e.type == KUI_EV_WAKE) {
			redraw = full = 1;
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
		if (kui_rect_contains(tog.r, e.x, e.y)) {
			kui_button(&tog, 1);
			kui_flush(0);
			KlCard *k = kl_deck_get(&deck, db.w[wi].word);
			if (k) {
				if (known) kl_card_forget(k);
				else kl_card_know(k, today());
				save_deck();
			}
			redraw = 1; /* partial: same screen */
		}
	}
}

/* every word of a level, unit by unit: taught ones, the next ones; -1 quit */
static int dict_screen(void)
{
	int w = kui_width(), h = kui_height(), tw = w - 2 * MARGIN;
	int lv = cur_level(), page = -1, rows_y = KUI_TOPBAR_H + 170, row_h = 68;
	int rows = (h - 170 - rows_y) / row_h;
	static int items[8192]; /* course position, -1 - unit for a unit heading, SPACER for an empty row */
	enum { SPACER = -100000 };
	KuiRect tabs[KL_LEVELS];
	int tabw = (tw - 5 * 12) / KL_LEVELS;
	for (int i = 0; i < KL_LEVELS; i++) tabs[i] = (KuiRect){ MARGIN + i * (tabw + 12), KUI_TOPBAR_H + 20, tabw, 76 };
	KuiButton prev = { { MARGIN, h - 150, 240, 120 }, "◀", 1 }, next = { { w - MARGIN - 240, h - 150, 240, 120 }, "▶", 2 };
	for (int full = 1;;) {
		int n = 0, first_new = -1;
		for (int u = 0; u < course.n_unit && n < 8000; u++) {
			if (course.unit[u].level != lv) continue;
			if (n % rows == rows - 1) items[n++] = SPACER; /* a heading does not end a page */
			items[n++] = -1 - u;
			for (int k = 0; k < course.unit[u].n; k++) {
				int pos = course.unit[u].start + k;
				if (first_new < 0 && !kl_card_taught(kl_deck_find(&deck, db.w[course.word[pos]].word))) first_new = n;
				items[n++] = pos;
			}
		}
		int pages = n ? (n + rows - 1) / rows : 1;
		if (page < 0) page = first_new >= 0 ? first_new / rows : 0; /* open where the next new words are */
		if (page >= pages) page = pages - 1;
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Sözlük"), NULL);
		for (int i = 0; i < KL_LEVELS; i++) {
			int sel = i + 1 == lv;
			kui_fill(tabs[i], sel ? 0 : 255);
			kui_frame(tabs[i], sel ? 4 : 2, 0);
			kui_text_on(tabs[i], kl_level_name(i + 1), KUI_FONT_BOLD, 40, KUI_ALIGN_CENTER, sel ? 255 : 0, sel ? 0 : 255);
		}
		int taught, total;
		kl_course_count(&course, &db, &deck, lv, &taught, &total);
		char s[300];
		snprintf(s, sizeof s, T("%d / %d kelime öğretildi · ✓ öğrenildi · • öğretildi · ★ biliyorum · ○ sırada"), taught, total);
		kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 108, tw, 48 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 28, tw), KUI_ALIGN_CENTER, 0x30);
		for (int r = 0; r < rows && page * rows + r < n; r++) {
			int it = items[page * rows + r], y = rows_y + r * row_h;
			if (it == SPACER) continue;
			if (it < 0) {
				char u[120], lab[160];
				unit_label(-1 - it, u, sizeof u);
				snprintf(lab, sizeof lab, "%s", u);
				kui_fill((KuiRect){ MARGIN, y + 6, tw, row_h - 10 }, 0xD8);
				kui_text_on((KuiRect){ MARGIN + 16, y + 6, tw - 32, row_h - 10 }, lab, KUI_FONT_BOLD, 32, KUI_ALIGN_LEFT, 0, 0xD8);
				continue;
			}
			const KlWord *wd = &db.w[course.word[it]];
			unsigned char fg;
			const char *st = word_status(kl_deck_find(&deck, wd->word), &fg);
			char disp[200], tr[300];
			kl_display(wd, disp, sizeof disp);
			translation(wd, tr, sizeof tr);
			kui_text((KuiRect){ MARGIN + 6, y, 50, row_h }, st, KUI_FONT_BOLD, 34, KUI_ALIGN_CENTER, fg);
			kui_text((KuiRect){ MARGIN + 66, y, tw / 2 - 70, row_h }, disp, KUI_FONT_BOLD, kui_fit_px(disp, KUI_FONT_BOLD, 36, tw / 2 - 70), KUI_ALIGN_LEFT, fg);
			kui_text((KuiRect){ MARGIN + tw / 2, y, tw / 2, row_h }, tr, KUI_FONT_REG, kui_fit_px(tr, KUI_FONT_REG, 32, tw / 2), KUI_ALIGN_LEFT, fg ? fg : 0x30);
			kui_fill((KuiRect){ MARGIN + 66, y + row_h - 2, tw - 66, 1 }, 0xC0);
		}
		snprintf(s, sizeof s, T("Sayfa %d / %d"), page + 1, pages);
		kui_text((KuiRect){ prev.r.x + prev.r.w, prev.r.y, next.r.x - prev.r.x - prev.r.w, prev.r.h }, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
		if (page > 0) kui_button(&prev, 0);
		if (page + 1 < pages) kui_button(&next, 0);
		flush_body(full);
		kui_mark_dirty((KuiRect){ 0, 0, w, KUI_TOPBAR_H });
		full = 0;
		for (int wait = 1; wait;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return -1;
			if (e.type == KUI_EV_WAKE) {
				full = 1;
				break;
			}
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
			for (int i = 0; i < KL_LEVELS; i++)
				if (kui_rect_contains(tabs[i], e.x, e.y) && i + 1 != lv) lv = i + 1, page = -1, wait = 0;
			if (wait && page > 0 && kui_rect_contains(prev.r, e.x, e.y)) page--, wait = 0;
			if (wait && page + 1 < pages && kui_rect_contains(next.r, e.x, e.y)) page++, wait = 0;
			if (wait && e.y >= rows_y && e.y < rows_y + rows * row_h) {
				int k = page * rows + (e.y - rows_y) / row_h;
				if (k < n && items[k] >= 0) {
					if (word_screen(course.word[items[k]]) < 0) return -1;
					full = 1, wait = 0;
				}
			}
		}
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
		snprintf(l3, sizeof l3, T("%d kelime öğretildi · %d öğrenildi · %d tekrar bekliyor"), seen, learned, due);
		lines[0] = l0, lines[1] = l1, lines[2] = l2, lines[3] = l3;
		for (int i = 0; i < 4; i++, y += 60) kui_text((KuiRect){ MARGIN, y, tw, 56 }, lines[i], KUI_FONT_REG, kui_fit_px(lines[i], KUI_FONT_REG, 36, tw), KUI_ALIGN_LEFT, 0);
		y += 30;
		kui_text((KuiRect){ MARGIN, y, tw, 56 }, T("Tekrar kutuları"), KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
		y += 66;
		int box[KL_BOX_MAX + 1] = { 0 }, maxb = 1;
		for (int i = 0; i < db.n; i++) {
			const KlCard *c = kl_deck_find(&deck, db.w[i].word);
			if (kl_card_taught(c)) box[c->box]++;
		}
		for (int b = 0; b <= KL_BOX_MAX; b++)
			if (box[b] > maxb) maxb = box[b];
		int bw = (tw - 5 * 20) / (KL_BOX_MAX + 1), bh = h - 250 - y;
		for (int b = 0; b <= KL_BOX_MAX; b++) {
			int x = MARGIN + b * (bw + 20), hh = box[b] * (bh - 50) / maxb;
			kui_fill((KuiRect){ x, y + bh - 50 - hh, bw, hh }, 0x50);
			kui_frame((KuiRect){ x, y, bw, bh - 50 }, 1, 0xA0);
			snprintf(s, sizeof s, "%d", box[b]);
			kui_text((KuiRect){ x, y + bh - 44, bw, 40 }, s, KUI_FONT_BOLD, 30, KUI_ALIGN_CENTER, 0);
		}
		y += bh;
		snprintf(s, sizeof s, "%s", T("0: yeni öğretildi · 1: yeni kaçırılan · 5: iyice öğrenildi (aralık 1, 2, 4, 8, 21 gün)"));
		kui_text((KuiRect){ MARGIN, y, tw, 44 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 26, tw), KUI_ALIGN_CENTER, 0x40);
		snprintf(s, sizeof s, "%s", T("Kaynak: Vikisözlük (CC BY-SA), Tatoeba (CC BY), CEFR-J; seviyeler sıklığa göre"));
		kui_text((KuiRect){ MARGIN, h - 120, tw, 44 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 24, tw), KUI_ALIGN_CENTER, 0x50);
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
		int d = today(), lv = cur_level();
		KlPlan p;
		kl_plan(&db, &course, &deck, lv, d, new_n, KL_REV_MAX, &p);
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Kelime Öğren"), NULL);
		char s[400];
		KuiRect pair = { MARGIN, KUI_TOPBAR_H + 20, tw, 84 };
		kui_frame(pair, 3, 0);
		snprintf(s, sizeof s, "%s  ←  %s", T(LANGS[learn].name), T(LANGS[native].name));
		kui_text((KuiRect){ pair.x + 24, pair.y, pair.w - 260, pair.h }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 40, pair.w - 260), KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ pair.x + pair.w - 230, pair.y, 206, pair.h }, T("Değiştir"), KUI_FONT_REG, kui_fit_px(T("Değiştir"), KUI_FONT_REG, 34, 206), KUI_ALIGN_RIGHT, 0x30);
		/* today's plan */
		KuiRect card = { MARGIN, pair.y + pair.h + 24, tw, 420 };
		kui_frame(card, 5, 0);
		int y = card.y + 20;
		kui_text((KuiRect){ card.x + 30, y, card.w - 60, 44 }, T("BUGÜNÜN DERSİ"), KUI_FONT_BOLD, 30, KUI_ALIGN_LEFT, 0x50);
		y += 50;
		if (p.unit >= 0) {
			char u[120];
			unit_label(p.unit, u, sizeof u);
			snprintf(s, sizeof s, "%s · %s", kl_level_name(p.level), u);
		} else {
			snprintf(s, sizeof s, "%s", p.n_rev ? T("Tekrar zamanı") : T("Tüm kelimeler öğretildi"));
		}
		kui_text((KuiRect){ card.x + 30, y, card.w - 60, 64 }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 46, card.w - 60), KUI_ALIGN_LEFT, 0);
		y += 72;
		int rev = p.due_all;
		if (p.n_new && rev) snprintf(s, sizeof s, T("Bugün: %d yeni kelime + %d tekrar"), p.n_new, rev);
		else if (p.n_new) snprintf(s, sizeof s, T("Bugün: %d yeni kelime"), p.n_new);
		else snprintf(s, sizeof s, T("Bugün: %d tekrar"), rev);
		kui_text((KuiRect){ card.x + 30, y, card.w - 60, 56 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 40, card.w - 60), KUI_ALIGN_LEFT, 0);
		y += 60;
		if (p.n_new) { /* a preview of the new words */
			int o = 0;
			s[0] = '\0';
			for (int i = 0; i < p.n_new && o < (int) sizeof s - 80; i++) {
				char disp[120];
				kl_display(&db.w[p.new_w[i]], disp, sizeof disp);
				o += snprintf(s + o, sizeof s - (size_t) o, i ? ", %s" : "%s", disp);
			}
			kui_text((KuiRect){ card.x + 30, y, card.w - 60, 50 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, card.w - 60), KUI_ALIGN_LEFT, 0x40);
		}
		KuiButton start = { { card.x + 30, card.y + card.h - 150, card.w - 60, 126 }, p.n_new || rev ? T("Derse başla ▶") : T("Sözlüğe bak"), 1 };
		kui_button(&start, 0);
		/* streak and daily goal */
		y = card.y + card.h + 22;
		snprintf(s, sizeof s, T("Seri: %d gün · %ld XP · bugün %d/%d XP"), kl_streak_now(&stats, d), stats.xp, kl_today_xp(&stats, d), stats.goal);
		kui_text((KuiRect){ MARGIN, y, tw, 46 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 32, tw), KUI_ALIGN_CENTER, 0);
		y += 50;
		bar((KuiRect){ MARGIN + 160, y, tw - 320, 22 }, kl_today_xp(&stats, d), stats.goal);
		y += 40;
		/* levels: taught words per level; a tap makes it the lesson level */
		KuiRect lrows[KL_LEVELS];
		for (int l = 1; l <= KL_LEVELS; l++, y += 70) {
			KuiRect r = { MARGIN, y, tw, 62 };
			lrows[l - 1] = r;
			int taught, total;
			kl_course_count(&course, &db, &deck, l, &taught, &total);
			char lab[100];
			level_label(l, lab, sizeof lab);
			if (l == lv) kui_frame(r, 3, 0);
			kui_text((KuiRect){ r.x + 14, r.y, 330, r.h }, lab, l == lv ? KUI_FONT_BOLD : KUI_FONT_REG, kui_fit_px(lab, KUI_FONT_BOLD, 32, 330), KUI_ALIGN_LEFT, 0);
			bar((KuiRect){ r.x + 350, r.y + 18, r.w - 350 - 230, 26 }, taught, total);
			snprintf(s, sizeof s, "%d / %d", taught, total);
			kui_text((KuiRect){ r.x + r.w - 220, r.y, 206, r.h }, s, KUI_FONT_REG, 30, KUI_ALIGN_RIGHT, 0x30);
		}
		y += 6;
		/* review, dictionary, statistics */
		int bw3 = (tw - 2 * 24) / 3;
		snprintf(s, sizeof s, T("Tekrar (%d)"), p.due_all);
		KuiButton br = { { MARGIN, y, bw3, 110 }, s, 2 }, bd = { { MARGIN + bw3 + 24, y, bw3, 110 }, T("Sözlük"), 3 },
			  bs = { { MARGIN + 2 * (bw3 + 24), y, bw3, 110 }, T("İstatistik"), 4 };
		kui_button(&br, 0);
		kui_button(&bd, 0);
		kui_button(&bs, 0);
		y += 126;
		/* new words per lesson */
		kui_text((KuiRect){ MARGIN, y, 520, 76 }, T("Derste yeni kelime:"), KUI_FONT_REG, kui_fit_px(T("Derste yeni kelime:"), KUI_FONT_REG, 32, 520), KUI_ALIGN_LEFT, 0);
		KuiRect nb[3];
		for (int i = 0; i < 3; i++) {
			nb[i] = (KuiRect){ MARGIN + 540 + i * 140, y, 120, 76 };
			int sel = NEW_CHOICES[i] == new_n;
			kui_fill(nb[i], sel ? 0 : 255);
			kui_frame(nb[i], sel ? 4 : 2, 0);
			char k[8];
			snprintf(k, sizeof k, "%d", NEW_CHOICES[i]);
			kui_text_on(nb[i], k, KUI_FONT_BOLD, 38, KUI_ALIGN_CENTER, sel ? 255 : 0, sel ? 0 : 255);
		}
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
				int r = lang_screen(0);
				quit = r == -2 || (r == 0 && ensure_placed() < 0), hit = 1;
			}
			if (!hit && kui_rect_contains(start.r, e.x, e.y)) {
				kui_button(&start, 1);
				kui_flush(0);
				quit = (p.n_new || rev ? play(lv) : dict_screen()) < 0, hit = 1;
			}
			for (int l = 1; l <= KL_LEVELS && !hit; l++)
				if (kui_rect_contains(lrows[l - 1], e.x, e.y)) {
					lang_cfg_set("level", l);
					hit = 1, full = 0; /* same screen: partial */
				}
			for (int i = 0; i < 3 && !hit; i++)
				if (kui_rect_contains(nb[i], e.x, e.y)) {
					new_n = NEW_CHOICES[i];
					cfg_set("new_n", new_n);
					hit = 1, full = 0;
				}
			if (!hit && kui_rect_contains(br.r, e.x, e.y)) {
				kui_button(&br, 1);
				kui_flush(0);
				quit = play(0) < 0, hit = 1;
			}
			if (!hit && kui_rect_contains(bd.r, e.x, e.y)) {
				kui_button(&bd, 1);
				kui_flush(0);
				quit = dict_screen() < 0, hit = 1;
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
	int saved_learn = cfg_get("learn", -1);
	native = cfg_get("native", -1);
	new_n = cfg_get("new_n", 5);
	if (new_n != 3 && new_n != 5 && new_n != 8) new_n = 5;
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
	const char *seed = getenv("KELIME_SEED");
	seeded = seed && *seed;
	rng_state = seeded ? (unsigned) atoi(seed) | 1u : (unsigned) time(NULL) | 1u;
	int ok = saved_learn >= 0 && saved_learn < N_LANGS && saved_learn != native && has_list[saved_learn] && open_lang(saved_learn) == 0;
	if ((ok || lang_screen(1) == 0) && ensure_placed() == 0) home_screen();
	kl_deck_free(&deck);
	kl_course_free(&course);
	kl_free(&db);
	free(big_buf);
	kui_quit();
	return 0;
}

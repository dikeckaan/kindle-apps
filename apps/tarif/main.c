/* Tarifler: Turkish and world recipes. A bundled set from Wikibooks works offline; Vikikitap / Wikibooks (and
 * TheMealDB with the user's own key) are searched online, and every recipe opened online is kept for offline use.
 * Search by name or by what is in the kitchen, a servings scaler, ticking off ingredients, a step-by-step cooking
 * mode that keeps the Kindle awake, named kitchen timers (no speaker: the alarm box flashes), favourites, recents. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kui.h"
#include "net/net.h"
#include "tarif.h"
#include "vendor/cjson/cJSON.h"

#define MARGIN 50
#define BAR_Y (kui_height() - 115) /* bottom button row */
#define BAR_H 95
#define MAX_LIST 2000
#define CACHE_MAX (2 * 1024 * 1024)
#define RECENT_KEEP 40
#define NET_TIMEOUT_S 25

typedef struct { TarifDb *d; int i; } Ref;

static TarifDb db, cache; /* bundled set, recipes seen online */
static TarifKeys favs, recent;
static TarifTimers timers;
static char mealdb_key[64];
static int quitting, awake, cooking, alarm_redraw;

/* ---- files ---- */

static const char *path_of(const char *name)
{
	static char p[4][300];
	static int k;
	k = (k + 1) % 4;
	snprintf(p[k], sizeof p[k], "%s/%s", kui_data_dir("tarif"), name);
	return p[k];
}

static void load_keys(const char *name, TarifKeys *k)
{
	static char buf[TARIF_MAX_KEYS * 200];
	tarif_keys_parse(kui_read_file(path_of(name), buf, sizeof buf) > 0 ? buf : "", k);
}

static void save_keys(const char *name, const TarifKeys *k)
{
	static char buf[TARIF_MAX_KEYS * 200];
	int n = tarif_keys_format(k, buf, sizeof buf);
	if (n >= 0) kui_write_atomic(path_of(name), buf, n);
}

static void save_timers(void)
{
	char buf[2048];
	int n = tarif_timers_format(&timers, buf, sizeof buf);
	if (n >= 0) kui_write_atomic(path_of("timers"), buf, n);
}

static void load_timers(void)
{
	char buf[2048];
	tarif_timers_parse(kui_read_file(path_of("timers"), buf, sizeof buf) > 0 ? buf : "", &timers);
	/* alarms that went off while the app was closed are not rung again */
	long now = (long) time(NULL);
	for (int i = timers.n - 1; i >= 0; i--)
		if (timers.t[i].end < now - 60) tarif_timer_remove(&timers, i);
}

static int load_db(void)
{
	const char *root = getenv("KUI_ROOT");
	char p[300];
	snprintf(p, sizeof p, "%s/apps/tarif/recipes.txt", root && *root ? root : "/mnt/us/kindle-apps");
	if (tarif_load(p, &db) > 0) return 0;
	return tarif_load("apps/tarif/data/recipes.txt", &db) > 0 ? 0 : -1; /* development tree (simulator) */
}

static void load_cache(void)
{
	tarif_free(&cache);
	tarif_load(path_of("cache.txt"), &cache);
}

/* appends one record to the cache (dropping the oldest records past CACHE_MAX) and reloads it */
static void cache_add(const char *rec, int len)
{
	char *old = malloc(CACHE_MAX + 1);
	if (!old) return;
	int n = kui_read_file(path_of("cache.txt"), old, CACHE_MAX + 1);
	if (n < 0) n = 0;
	char *start = old;
	while (n - (start - old) + len > CACHE_MAX) {
		char *nx = strstr(start + 1, "\n@R\t");
		if (!nx) { start = old + n; break; }
		start = nx + 1;
	}
	int keep = n - (int) (start - old);
	char *out = malloc((size_t) keep + (size_t) len + 1);
	if (out) {
		memcpy(out, start, (size_t) keep);
		memcpy(out + keep, rec, (size_t) len);
		kui_write_atomic(path_of("cache.txt"), out, keep + len);
		free(out);
	}
	free(old);
	load_cache();
}

static Ref find_url(const char *url)
{
	int i = tarif_find_url(&db, url);
	if (i >= 0) return (Ref){ &db, i };
	i = tarif_find_url(&cache, url);
	return i >= 0 ? (Ref){ &cache, i } : (Ref){ NULL, -1 };
}

static const TarifRecipe *R(Ref r) { return &r.d->r[r.i]; }

/* ---- staying awake: in cooking mode and while a timer runs ---- */

static int timers_active(void)
{
	for (int i = 0; i < timers.n; i++)
		if (!timers.t[i].done) return 1;
	return 0;
}

static void awake_update(void)
{
	int want = cooking || timers_active();
	if (want != awake) {
		kui_prevent_sleep(want);
		awake = want;
	}
}

/* ---- drawing helpers ---- */

static void draw_n(KuiRect r, const char *s, int len, KuiFont f, int px, KuiAlign a, unsigned char fg)
{
	char buf[1400];
	if (len > (int) sizeof buf - 1) len = (int) sizeof buf - 1;
	memcpy(buf, s, (size_t) len);
	buf[len] = '\0';
	kui_text(r, buf, f, px, a, fg);
}

/* draws (when draw) at most max wrapped lines of text from y; returns the height */
static int draw_block(int x, int y, int w, const char *text, KuiFont f, int px, KuiAlign a, int max, unsigned char fg, int draw)
{
	static KuiLine ls[80];
	int n = kui_wrap_text(text, w, f, px, ls, 80), lh = kui_line_height(f, px);
	if (n > max) n = max;
	for (int i = 0; i < n && draw; i++) draw_n((KuiRect){ x, y + i * lh, w, lh }, text + ls[i].off, ls[i].len, f, px, a, fg);
	return n * lh;
}

/* s cut to width with "…" */
static void ellipsize(const char *in, KuiFont f, int px, int w, char *s, int cap)
{
	snprintf(s, (size_t) cap, "%s", in);
	if (kui_text_width(s, f, px) <= w) return;
	for (int len = (int) strlen(s); len > 0;) {
		len--;
		while (len > 0 && ((unsigned char) s[len] & 0xC0) == 0x80) len--;
		s[len] = '\0';
		char t[600];
		snprintf(t, sizeof t, "%s…", s);
		if (kui_text_width(t, f, px) <= w || len == 0) {
			snprintf(s, (size_t) cap, "%s", t);
			return;
		}
	}
}

static void flush_screen(int full)
{
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, 0, kui_width(), kui_height() });
		kui_flush(0);
	}
}

static void page_label(int pg, int pages, KuiRect r)
{
	char s[32];
	snprintf(s, sizeof s, "%d / %d", pg + 1, pages);
	kui_text(r, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
}

static void message(const char *title, const char *msg)
{
	const char *ok[] = { T("Tamam") };
	kui_dialog(title, msg, ok, 1, NULL);
}

static void busy_screen(const char *title, const char *msg)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(title, NULL);
	draw_block(60, 560, w - 120, msg, KUI_FONT_BOLD, 46, KUI_ALIGN_CENTER, 4, 0, 1);
	kui_flush(1);
}

/* ---- events: timers ring from every screen ---- */

static long mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

/* the alarm: a box that flashes (black/white) until it is tapped — the Kindle has no speaker */
static void ring_alarm(int i)
{
	int w = kui_width(), h = kui_height();
	KuiRect box = { 70, h / 2 - 280, w - 140, 560 };
	char name[120];
	ellipsize(timers.t[i].name, KUI_FONT_BOLD, 50, box.w - 80, name, sizeof name);
	for (int k = 0, inv = 1;; k++) {
		unsigned char bg = inv ? 0 : 255, fg = inv ? 255 : 0;
		kui_fill(box, bg);
		kui_frame(box, 8, 0);
		kui_frame((KuiRect){ box.x + 16, box.y + 16, box.w - 32, box.h - 32 }, 4, fg);
		kui_text_on((KuiRect){ box.x + 40, box.y + 60, box.w - 80, 120 }, T("Süre doldu!"), KUI_FONT_BOLD, 80, KUI_ALIGN_CENTER, fg, bg);
		kui_text_on((KuiRect){ box.x + 40, box.y + 220, box.w - 80, 90 }, name, KUI_FONT_BOLD, 50, KUI_ALIGN_CENTER, fg, bg);
		kui_text_on((KuiRect){ box.x + 40, box.y + 400, box.w - 80, 70 }, T("Kapatmak için dokunun"), KUI_FONT_REG, 36, KUI_ALIGN_CENTER, fg, bg);
		kui_mark_dirty(box);
		kui_flush(0);
		/* flashes for five minutes, then stays dark until tapped */
		KuiEvent e = kui_next_event(k < 360 ? 800 : -1);
		if (e.type == KUI_EV_QUIT) { quitting = 1; break; }
		if (e.type == KUI_EV_TAP || e.type == KUI_EV_LONGPRESS || e.type == KUI_EV_POWER) break;
		inv = k < 360 ? !inv : 1;
	}
	timers.t[i].done = 1;
	save_timers();
	awake_update();
}

/* kui_next_event that rings due timers first (then returns KUI_EV_WAKE with alarm_redraw = 1: the caller repaints,
 * a partial refresh is enough) and polls once a second while a timer runs. timeout_ms < 0: waits for an event. */
static KuiEvent wait_event(int timeout_ms)
{
	long t0 = mono_ms();
	for (;;) {
		if (quitting) return (KuiEvent){ KUI_EV_QUIT, 0, 0, KUI_DIR_NONE };
		int ring = tarif_timers_ringing(&timers, (long) time(NULL));
		if (ring >= 0 && !kui_input_asleep()) {
			ring_alarm(ring);
			alarm_redraw = 1;
			return (KuiEvent){ quitting ? KUI_EV_QUIT : KUI_EV_WAKE, 0, 0, KUI_DIR_NONE };
		}
		int to = timeout_ms;
		if (timers_active() && (to < 0 || to > 1000)) to = 1000;
		KuiEvent e = kui_next_event(to);
		if (e.type != KUI_EV_NONE) {
			if (e.type == KUI_EV_WAKE) alarm_redraw = 0;
			if (e.type == KUI_EV_QUIT) quitting = 1;
			return e;
		}
		if (timeout_ms >= 0 && mono_ms() - t0 >= timeout_ms) return e;
	}
}

static int wake_full(void) { return !alarm_redraw; } /* after KUI_EV_WAKE: full refresh unless it was our alarm */

/* ---- recipe labels ---- */

static const char *kind_label(const TarifRecipe *r)
{
	static const char *const K[][2] = { { "c", N_("Çorba") }, { "a", N_("Ana yemek") }, { "t", N_("Tatlı") }, { "s", N_("Salata, meze") },
					    { "h", N_("Hamur işi") }, { "k", N_("Kahvaltılık") }, { "i", N_("İçecek") } };
	for (size_t i = 0; i < sizeof K / sizeof K[0]; i++)
		if (strchr(r->tags, K[i][0][0])) return T(K[i][1]);
	return NULL;
}

static const char *source_name(const TarifRecipe *r)
{
	if (strstr(r->url, "tr.wikibooks.org")) return "Vikikitap";
	if (strstr(r->url, "wikibooks.org")) return "Wikibooks";
	if (strstr(r->url, "themealdb.com")) return "TheMealDB";
	return "";
}

/* appends " · part" to s, never past cap (snprintf's return value is not used as an offset) */
static void add_part(char *s, size_t cap, const char *part)
{
	size_t o = strlen(s);
	if (o + 1 < cap) snprintf(s + o, cap - o, " · %s", part);
}

/* "Türkiye · Çorba · Vegan · İngilizce" */
static void meta_line(const TarifRecipe *r, char *s, size_t cap, const char *english)
{
	snprintf(s, cap, "%s", r->cuisine);
	const char *k = kind_label(r);
	if (k) add_part(s, cap, k);
	if (tarif_has_tag(r, 'v')) add_part(s, cap, T("Vegan"));
	else if (tarif_has_tag(r, 'g')) add_part(s, cap, T("Vejetaryen"));
	if (strcmp(r->lang, "tr")) add_part(s, cap, english);
}

static void sub_line(const TarifRecipe *r, char *s, int cap) { meta_line(r, s, (size_t) cap, T("İngilizce")); }

/* ---- lists ---- */

enum { L_BACK = -1, L_QUIT = -2, L_ACTION = -3 };

typedef struct {
	const char *title, *head, *action; /* head: a line above the rows; action: a button in the bottom bar */
	int n, row_h;
	void (*row)(int i, KuiRect r);
	const char *empty;
} ListDef;

/* returns the tapped row, L_BACK, L_QUIT or L_ACTION; *page is kept between calls */
static int list_screen(const ListDef *L, int *page)
{
	int w = kui_width(), h = kui_height();
	int y0 = KUI_TOPBAR_H + (L->head ? 80 : 16);
	int per = (BAR_Y - 16 - y0) / L->row_h;
	if (per < 1) per = 1;
	int pages = L->n ? (L->n + per - 1) / per : 1;
	if (*page >= pages) *page = pages - 1;
	if (*page < 0) *page = 0;
	KuiButton prev = { { 20, BAR_Y, 150, BAR_H }, "◀", 1 }, next = { { w - 170, BAR_Y, 150, BAR_H }, "▶", 2 };
	KuiButton act = { { 200, BAR_Y, w - 400, BAR_H }, L->action, 3 };
	for (int full = 1, redraw = 1;;) {
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar(L->title, NULL);
			if (L->head) {
				char s[300];
				ellipsize(L->head, KUI_FONT_REG, 32, w - 2 * MARGIN, s, sizeof s);
				kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 8, w - 2 * MARGIN, 64 }, s, KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0x30);
				kui_fill((KuiRect){ MARGIN - 20, y0 - 6, w - 2 * (MARGIN - 20), 3 }, 0);
			}
			for (int i = 0; i < per; i++) {
				int k = *page * per + i;
				if (k >= L->n) break;
				KuiRect r = { MARGIN - 20, y0 + i * L->row_h, w - 2 * (MARGIN - 20), L->row_h };
				L->row(k, r);
				kui_fill((KuiRect){ r.x, r.y + r.h - 2, r.w, 1 }, 0x80);
			}
			if (!L->n && L->empty) draw_block(MARGIN, y0 + 140, w - 2 * MARGIN, L->empty, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 6, 0, 1);
			if (pages > 1) {
				if (*page > 0) kui_button(&prev, 0);
				if (*page + 1 < pages) kui_button(&next, 0);
			}
			if (L->action) kui_button(&act, 0);
			else if (pages > 1) page_label(*page, pages, (KuiRect){ 200, BAR_Y, w - 400, BAR_H });
			if (L->action && pages > 1) { /* the page number goes to the head line's right end */
				char s[32];
				snprintf(s, sizeof s, "%d / %d", *page + 1, pages);
				kui_text((KuiRect){ w - MARGIN - 200, BAR_Y - 60, 200, 50 }, s, KUI_FONT_REG, 30, KUI_ALIGN_RIGHT, 0x40);
			}
			flush_screen(full);
			full = redraw = 0;
		}
		KuiEvent e = wait_event(-1);
		if (e.type == KUI_EV_QUIT) return L_QUIT;
		if (e.type == KUI_EV_WAKE) {
			full = wake_full();
			redraw = 1;
			continue;
		}
		int dlt = 0;
		if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return L_BACK;
			KuiButton bs[3] = { prev, next, act };
			int id = kui_buttons_hit(bs, L->action ? 3 : 2, e.x, e.y);
			if (id == 3) return L_ACTION;
			if (id && pages > 1) dlt = id == 1 ? -1 : 1;
			else if (!id && e.y >= y0 && e.y < y0 + per * L->row_h) {
				int k = *page * per + (e.y - y0) / L->row_h;
				if (k < L->n) return k;
			}
		}
		if (dlt && *page + dlt >= 0 && *page + dlt < pages) {
			*page += dlt;
			redraw = 1; /* page turn: partial refresh */
		}
	}
}

static Ref list[MAX_LIST];
static int list_matched[MAX_LIST], list_words;

static void recipe_row(int i, KuiRect r)
{
	const TarifRecipe *x = R(list[i]);
	char t[400], s[400], sub[300];
	int fav = tarif_keys_has(&favs, x->url);
	snprintf(t, sizeof t, "%s%s", fav ? "★ " : "", x->title);
	ellipsize(t, KUI_FONT_BOLD, 36, r.w - 40, s, sizeof s);
	kui_text((KuiRect){ r.x + 20, r.y + 8, r.w - 40, 56 }, s, KUI_FONT_BOLD, 36, KUI_ALIGN_LEFT, 0);
	sub_line(x, sub, sizeof sub);
	if (list_words) {
		snprintf(t, sizeof t, T("%d/%d malzeme"), list_matched[i], list_words);
		add_part(t, sizeof t, sub);
	} else {
		snprintf(t, sizeof t, "%s", sub);
	}
	ellipsize(t, KUI_FONT_REG, 28, r.w - 40, s, sizeof s);
	kui_text((KuiRect){ r.x + 20, r.y + 64, r.w - 40, 44 }, s, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0x40);
}

static int recipe_screen(Ref ref);

/* shows list[0..n) until ✕; returns L_QUIT on quit, L_ACTION when the action button was tapped */
static int recipes_list(const char *title, const char *head, int n, const char *action, const char *empty)
{
	static Ref saved[MAX_LIST];
	static int saved_m[MAX_LIST];
	int saved_words = list_words;
	memcpy(saved, list, (size_t) n * sizeof *list); /* nested lists reuse list[] */
	memcpy(saved_m, list_matched, (size_t) n * sizeof *list_matched);
	for (int page = 0;;) {
		memcpy(list, saved, (size_t) n * sizeof *list);
		memcpy(list_matched, saved_m, (size_t) n * sizeof *list_matched);
		list_words = saved_words;
		ListDef L = { title, head, action, n, 118, recipe_row, empty };
		int k = list_screen(&L, &page);
		if (k == L_BACK || k == L_QUIT || k == L_ACTION) {
			list_words = 0;
			return k;
		}
		list_words = 0;
		if (recipe_screen(saved[k]) == L_QUIT) return L_QUIT;
	}
}

/* ids of a set as refs into list[] after `at`; returns the new count */
static int add_refs(TarifDb *d, const int *ids, int n, int at)
{
	for (int i = 0; i < n && at < MAX_LIST; i++) list[at++] = (Ref){ d, ids[i] };
	return at;
}

/* ---- recipe page ---- */

enum { IT_TITLE, IT_META, IT_INTRO, IT_SERV, IT_SECTION, IT_HEAD, IT_ING, IT_STEP, IT_SOURCE };
typedef struct { int kind, idx, from, to, h; const char *text; } Item; /* from/to: wrapped lines of a step part */

#define ING_PX 32
#define STEP_PX 32
#define MAX_ITEMS 600
#define MAX_ING 120
static Item items[MAX_ITEMS];
static int n_items, starts[64], n_pages;
static char ing_txt[MAX_ING][400];
static unsigned char ticked[MAX_ING];
static int serv_now;
static char section_txt[2][120], meta_txt[300], source_txt[900];

static int content_w(void) { return kui_width() - 2 * MARGIN; }

/* the ingredient lines of r at the chosen servings */
static void scale_ings(Ref ref)
{
	const TarifRecipe *r = R(ref);
	double f = (double) serv_now / tarif_base_servings(r);
	int comma = !strcmp(r->lang, "tr");
	for (int i = 0; i < r->n_lines && i < MAX_ING; i++) {
		const char *s = ref.d->line[r->line0 + i];
		if (ref.d->kind[r->line0 + i] == '-') tarif_scale_line(s, f, comma, ing_txt[i], sizeof ing_txt[i]);
		else snprintf(ing_txt[i], sizeof ing_txt[i], "%s", s);
	}
}

static void add_item(int kind, int idx, int h, const char *text)
{
	if (n_items < MAX_ITEMS) items[n_items++] = (Item){ kind, idx, 0, 0, h, text };
}

static void layout_recipe(Ref ref)
{
	const TarifRecipe *r = R(ref);
	int tw = content_w(), lh;
	KuiLine ls[200];
	n_items = 0;
	scale_ings(ref);
	add_item(IT_TITLE, 0, draw_block(0, 0, tw, r->title, KUI_FONT_BOLD, 46, KUI_ALIGN_LEFT, 3, 0, 0) + 10, r->title);
	meta_line(r, meta_txt, sizeof meta_txt, T("İngilizce tarif"));
	add_item(IT_META, 0, 50, meta_txt);
	if (r->intro[0]) add_item(IT_INTRO, 0, draw_block(0, 0, tw, r->intro, KUI_FONT_SERIF, 29, KUI_ALIGN_LEFT, 8, 0, 0) + 14, r->intro);
	add_item(IT_SERV, 0, 120, NULL);
	if (r->servings > 0) snprintf(section_txt[0], sizeof section_txt[0], T("Malzemeler (%d kişilik)"), serv_now);
	else snprintf(section_txt[0], sizeof section_txt[0], T("Malzemeler (%d kişilik, tahmini)"), serv_now);
	add_item(IT_SECTION, 0, 76, section_txt[0]);
	for (int i = 0; i < r->n_lines && i < MAX_ING; i++) {
		if (ref.d->kind[r->line0 + i] == '+') add_item(IT_HEAD, i, 62, ing_txt[i]);
		else add_item(IT_ING, i, draw_block(0, 0, tw - 70, ing_txt[i], KUI_FONT_REG, ING_PX, KUI_ALIGN_LEFT, 8, 0, 0) + 18, ing_txt[i]);
	}
	snprintf(section_txt[1], sizeof section_txt[1], "%s", T("Hazırlanışı"));
	add_item(IT_SECTION, 1, 90, section_txt[1]);
	lh = kui_line_height(KUI_FONT_SERIF, STEP_PX);
	int per_page = (BAR_Y - 20 - (KUI_TOPBAR_H + 24)) / lh - 1;
	for (int s = 0; s < r->n_steps; s++) {
		const char *t = ref.d->line[r->step0 + s];
		int n = kui_wrap_text(t, tw - 70, KUI_FONT_SERIF, STEP_PX, ls, 200);
		for (int a = 0; a < n; a += per_page) { /* a step taller than a page is split into parts */
			int b = a + per_page < n ? a + per_page : n;
			if (n_items < MAX_ITEMS) items[n_items++] = (Item){ IT_STEP, s, a, b, (b - a) * lh + (b == n ? 18 : 0), t };
		}
	}
	snprintf(source_txt, sizeof source_txt, T("Kaynak: %s · %s · %s"), source_name(r), r->licence, r->url);
	if (!strcmp(r->licence, "CC BY-SA 4.0"))
		snprintf(source_txt + strlen(source_txt), sizeof source_txt - strlen(source_txt), "%s",
			 T(" — Wikibooks katkıcılarının metni, biçimi değiştirildi; aynı lisansla paylaşılır."));
	add_item(IT_SOURCE, 0, draw_block(0, 0, tw, source_txt, KUI_FONT_REG, 24, KUI_ALIGN_LEFT, 8, 0, 0) + 30, source_txt);
	/* pages: items are never split; a section or group heading does not end a page */
	int page_h = BAR_Y - 20 - (KUI_TOPBAR_H + 24), used = 0;
	n_pages = 1;
	starts[0] = 0;
	for (int i = 0; i < n_items; i++) {
		int need = items[i].h;
		if ((items[i].kind == IT_SECTION || items[i].kind == IT_HEAD) && i + 1 < n_items) need += items[i + 1].h;
		if (used && used + need > page_h && n_pages < 64) {
			starts[n_pages++] = i;
			used = 0;
		}
		used += items[i].h;
	}
}

static KuiButton serv_btn[4], cook_btn;
static int serv_opt[4], n_serv; /* 2, 4, 6 and the recipe's own count */

static void serv_options(const TarifRecipe *r)
{
	int base = tarif_base_servings(r);
	n_serv = 0;
	for (int v = 2; v <= 6; v += 2) {
		if (base < v && (n_serv == 0 || serv_opt[n_serv - 1] < base) && base != 2 && base != 4 && base != 6) serv_opt[n_serv++] = base;
		serv_opt[n_serv++] = v;
	}
	if (base > 6) serv_opt[n_serv++] = base;
}
static KuiRect item_rect[MAX_ITEMS];

static void draw_tick(KuiRect r, int on)
{
	KuiRect b = { r.x, r.y + 8, 40, 40 };
	kui_fill(b, on ? 0 : 255);
	kui_frame(b, 3, 0);
	if (on) kui_text_on(b, "✓", KUI_FONT_BOLD, 34, KUI_ALIGN_CENTER, 255, 0);
}

static void draw_item(Ref ref, int i, int x, int y)
{
	const Item *it = &items[i];
	int tw = content_w(), w = kui_width();
	item_rect[i] = (KuiRect){ x, y, tw, it->h };
	switch (it->kind) {
	case IT_TITLE: draw_block(x, y, tw, it->text, KUI_FONT_BOLD, 46, KUI_ALIGN_LEFT, 3, 0, 1); break;
	case IT_META: {
		char s[300];
		ellipsize(it->text, KUI_FONT_REG, 28, tw, s, sizeof s);
		kui_text((KuiRect){ x, y, tw, 44 }, s, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0x40);
		break;
	}
	case IT_INTRO: draw_block(x, y, tw, it->text, KUI_FONT_SERIF, 29, KUI_ALIGN_LEFT, 8, 0x20, 1); break;
	case IT_SERV: {
		kui_text((KuiRect){ x, y + 10, 120, 90 }, T("Kişi:"), KUI_FONT_BOLD, 34, KUI_ALIGN_LEFT, 0);
		for (int k = 0; k < n_serv; k++) {
			static char lab[4][8];
			snprintf(lab[k], sizeof lab[k], "%d", serv_opt[k]);
			serv_btn[k] = (KuiButton){ { x + 110 + k * 112, y + 10, 98, 90 }, lab[k], 10 + k };
			kui_button(&serv_btn[k], serv_now == serv_opt[k]);
		}
		cook_btn = (KuiButton){ { w - MARGIN - 370, y + 10, 370, 90 }, T("Pişirme modu ▶"), 20 };
		kui_button(&cook_btn, 0);
		break;
	}
	case IT_SECTION:
		kui_text((KuiRect){ x, y + 16, tw, 56 }, it->text, KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
		kui_fill((KuiRect){ x, y + it->h - 6, tw, 2 }, 0);
		break;
	case IT_HEAD: kui_text((KuiRect){ x, y + 6, tw, 50 }, it->text, KUI_FONT_BOLD, 32, KUI_ALIGN_LEFT, 0x20); break;
	case IT_ING:
		kui_fill(item_rect[i], 255);
		draw_tick(item_rect[i], ticked[it->idx]);
		draw_block(x + 64, y + 8, tw - 70, it->text, KUI_FONT_REG, ING_PX, KUI_ALIGN_LEFT, 8, ticked[it->idx] ? 0x90 : 0, 1);
		break;
	case IT_STEP: {
		static KuiLine ls[200];
		int lh = kui_line_height(KUI_FONT_SERIF, STEP_PX);
		int n = kui_wrap_text(it->text, tw - 70, KUI_FONT_SERIF, STEP_PX, ls, 200);
		if (it->from == 0) {
			char num[16];
			snprintf(num, sizeof num, "%d.", it->idx + 1);
			kui_text((KuiRect){ x, y, 60, lh }, num, KUI_FONT_SERIF_BOLD, STEP_PX, KUI_ALIGN_LEFT, 0);
		}
		for (int l = it->from; l < it->to && l < n; l++)
			draw_n((KuiRect){ x + 64, y + (l - it->from) * lh, tw - 70, lh }, it->text + ls[l].off, ls[l].len, KUI_FONT_SERIF, STEP_PX, KUI_ALIGN_LEFT, 0);
		break;
	}
	case IT_SOURCE:
		kui_fill((KuiRect){ x, y + 6, tw, 1 }, 0x80);
		draw_block(x, y + 16, tw, it->text, KUI_FONT_REG, 24, KUI_ALIGN_LEFT, 8, 0x50, 1);
		break;
	}
	(void) ref;
}

static int cook_mode(Ref ref);
static int timers_screen(void);

static void fav_label(const TarifRecipe *r, char *s, int cap) { snprintf(s, (size_t) cap, "%s", tarif_keys_has(&favs, r->url) ? "★" : "☆"); }

static int recipe_screen(Ref ref)
{
	const TarifRecipe *r = R(ref);
	char url[600];
	snprintf(url, sizeof url, "%s", r->url);
	tarif_keys_touch(&recent, url, RECENT_KEEP);
	save_keys("recent", &recent);
	memset(ticked, 0, sizeof ticked);
	serv_now = tarif_base_servings(r);
	serv_options(r);
	layout_recipe(ref);
	int w = kui_width(), pg = 0;
	KuiButton prev = { { 20, BAR_Y, 150, BAR_H }, "◀", 1 }, next = { { w - 170, BAR_Y, 150, BAR_H }, "▶", 2 };
	char fl[8];
	for (int full = 1, redraw = 1;;) {
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
			fav_label(r, fl, sizeof fl);
			kui_topbar(T("Tarif"), fl);
			int y = KUI_TOPBAR_H + 24, end = pg + 1 < n_pages ? starts[pg + 1] : n_items;
			for (int i = 0; i < n_items; i++) item_rect[i] = (KuiRect){ 0, 0, 0, 0 };
			for (int i = starts[pg]; i < end; i++) {
				draw_item(ref, i, MARGIN, y);
				y += items[i].h;
			}
			kui_fill((KuiRect){ MARGIN, BAR_Y - 14, w - 2 * MARGIN, 1 }, 0);
			if (n_pages > 1) {
				if (pg > 0) kui_button(&prev, 0);
				if (pg + 1 < n_pages) kui_button(&next, 0);
				page_label(pg, n_pages, (KuiRect){ 200, BAR_Y, w - 400, BAR_H });
			}
			flush_screen(full);
			full = redraw = 0;
		}
		KuiEvent e = wait_event(-1);
		if (e.type == KUI_EV_QUIT) return L_QUIT;
		if (e.type == KUI_EV_WAKE) {
			full = wake_full();
			redraw = 1;
			continue;
		}
		int dlt = 0;
		if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			int hit = kui_topbar_hit(w, 1, e.x, e.y);
			if (hit == KUI_BTN_EXIT) return L_BACK;
			if (hit == KUI_BTN_MENU) { /* favourite: only the top bar changes */
				if (tarif_keys_toggle(&favs, url) >= 0) save_keys("favs", &favs);
				fav_label(r, fl, sizeof fl);
				kui_topbar(T("Tarif"), fl);
				kui_flush(0);
				continue;
			}
			KuiButton nav[2] = { prev, next };
			int id = n_pages > 1 ? kui_buttons_hit(nav, 2, e.x, e.y) : 0;
			if ((id == 1 && pg > 0) || (id == 2 && pg + 1 < n_pages)) dlt = id == 1 ? -1 : 1;
			int end = pg + 1 < n_pages ? starts[pg + 1] : n_items;
			for (int i = starts[pg]; i < end && !id; i++) {
				if (!kui_rect_contains(item_rect[i], e.x, e.y)) continue;
				if (items[i].kind == IT_ING) { /* tick off: only this row changes */
					ticked[items[i].idx] ^= 1;
					draw_item(ref, i, item_rect[i].x, item_rect[i].y);
					kui_mark_dirty(item_rect[i]);
					kui_flush(0);
				} else if (items[i].kind == IT_SERV) {
					int s = kui_buttons_hit(serv_btn, n_serv, e.x, e.y);
					if (s >= 10 && serv_now != serv_opt[s - 10]) {
						serv_now = serv_opt[s - 10];
						layout_recipe(ref);
						if (pg >= n_pages) pg = n_pages - 1;
						redraw = 1; /* quantities changed: partial refresh of the page */
					} else if (kui_rect_contains(cook_btn.r, e.x, e.y)) {
						if (cook_mode(ref) == L_QUIT) return L_QUIT;
						full = redraw = 1;
					}
				}
				break;
			}
		}
		if (dlt && pg + dlt >= 0 && pg + dlt < n_pages) {
			pg += dlt;
			redraw = 1;
		}
	}
}

/* ---- cooking mode: one step per screen, big serif text ---- */

#define COOK_Y (KUI_TOPBAR_H + 150)
#define COOK_H 790
#define STRIP_Y (COOK_Y + COOK_H + 120)

static KuiButton strip_btn[3];
static int strip_n;

/* active timers as small boxes (tap: Zamanlayıcılar) */
static void draw_strip(void)
{
	int w = kui_width();
	KuiRect area = { 0, STRIP_Y, w, 110 };
	kui_fill(area, 255);
	strip_n = 0;
	int bw = (w - 2 * MARGIN - 2 * 20) / 3;
	long now = (long) time(NULL);
	for (int i = 0; i < timers.n && strip_n < 3; i++) {
		if (timers.t[i].done) continue;
		KuiRect r = { MARGIN + strip_n * (bw + 20), STRIP_Y + 10, bw, 90 };
		char c[24], nm[80];
		tarif_fmt_clock(tarif_timer_left(&timers.t[i], now), c, sizeof c);
		kui_frame(r, 3, 0);
		ellipsize(timers.t[i].name, KUI_FONT_REG, 24, r.w - 20, nm, sizeof nm);
		kui_text((KuiRect){ r.x + 10, r.y + 4, r.w - 20, 34 }, nm, KUI_FONT_REG, 24, KUI_ALIGN_CENTER, 0x30);
		kui_text((KuiRect){ r.x + 10, r.y + 38, r.w - 20, 48 }, c, KUI_FONT_BOLD, 40, KUI_ALIGN_CENTER, 0);
		strip_btn[strip_n++] = (KuiButton){ r, "", 1 };
	}
	kui_mark_dirty(area);
}

/* the step text (or the ingredient list for step -1) at the largest size that fits COOK_H */
static void draw_cook_body(Ref ref, int step, int nsteps)
{
	const TarifRecipe *r = R(ref);
	int w = kui_width(), tw = w - 2 * MARGIN;
	kui_fill((KuiRect){ 0, KUI_TOPBAR_H + 10, w, STRIP_Y - KUI_TOPBAR_H - 10 }, 255);
	char s[300], t[300];
	ellipsize(r->title, KUI_FONT_REG, 30, tw, t, sizeof t);
	kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 16, tw, 44 }, t, KUI_FONT_REG, 30, KUI_ALIGN_LEFT, 0x40);
	if (step < 0) snprintf(s, sizeof s, T("Hazırlık · %d kişilik"), serv_now);
	else snprintf(s, sizeof s, T("Adım %d / %d"), step + 1, nsteps);
	kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 66, tw, 64 }, s, KUI_FONT_BOLD, 48, KUI_ALIGN_LEFT, 0);
	kui_fill((KuiRect){ MARGIN, COOK_Y - 14, tw, 3 }, 0);
	if (step >= 0) {
		const char *text = ref.d->line[r->step0 + step];
		static const int sizes[] = { 50, 46, 42, 38, 34, 30, 27 };
		int px = 27;
		for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
			KuiLine ls[80];
			int n = kui_wrap_text(text, tw, KUI_FONT_SERIF, sizes[i], ls, 80);
			if (n * kui_line_height(KUI_FONT_SERIF, sizes[i]) <= COOK_H) { px = sizes[i]; break; }
		}
		draw_block(MARGIN, COOK_Y + 10, tw, text, KUI_FONT_SERIF, px, KUI_ALIGN_LEFT, COOK_H / kui_line_height(KUI_FONT_SERIF, px), 0, 1);
	} else { /* ingredients, scaled, with their ticks */
		static const int sizes[] = { 36, 32, 28, 25 };
		int px = 25, n_ing = r->n_lines < MAX_ING ? r->n_lines : MAX_ING;
		for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
			int hgt = 0;
			for (int i = 0; i < n_ing; i++) hgt += draw_block(0, 0, tw - 60, ing_txt[i], KUI_FONT_REG, sizes[k], KUI_ALIGN_LEFT, 3, 0, 0) + 8;
			if (hgt <= COOK_H) { px = sizes[k]; break; }
		}
		int y = COOK_Y;
		for (int i = 0; i < n_ing && y < COOK_Y + COOK_H - 30; i++) {
			int head = ref.d->kind[r->line0 + i] == '+';
			const char *mark = head ? "" : ticked[i] ? "✓" : "•";
			kui_text((KuiRect){ MARGIN, y, 50, kui_line_height(KUI_FONT_REG, px) }, mark, KUI_FONT_BOLD, px, KUI_ALIGN_LEFT, 0);
			y += draw_block(MARGIN + 54, y, tw - 60, ing_txt[i], head ? KUI_FONT_BOLD : KUI_FONT_REG, px, KUI_ALIGN_LEFT, 3,
					ticked[i] ? 0x90 : 0, 1) + 8;
		}
	}
}

static int cook_mode(Ref ref)
{
	const TarifRecipe *r = R(ref);
	int w = kui_width(), n = r->n_steps, step = -1, bw = (w - 2 * MARGIN - 30) / 2;
	cooking = 1;
	awake_update(); /* the screensaver must not come up while cooking */
	KuiButton prev = { { MARGIN, BAR_Y, bw, BAR_H }, T("◀ Önceki"), 1 }, next = { { MARGIN + bw + 30, BAR_Y, bw, BAR_H }, T("Sonraki ▶"), 2 };
	KuiButton tbtn = { { MARGIN, STRIP_Y - 110, w - 2 * MARGIN, 90 }, "", 3 };
	char tlabel[120];
	int ret = L_BACK;
	long last_tick = 0;
	for (int full = 1, redraw = 1;;) {
		int secs = step >= 0 ? tarif_step_seconds(ref.d->line[r->step0 + step]) : 0;
		if (redraw) {
			if (full) kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
			kui_topbar(T("Pişirme modu"), T("Süreler"));
			draw_cook_body(ref, step, n);
			kui_fill((KuiRect){ 0, STRIP_Y - 112, w, 112 }, 255);
			if (secs) {
				char d[32];
				if (secs % 60) snprintf(d, sizeof d, T("%d sn"), secs);
				else snprintf(d, sizeof d, T("%d dk"), secs / 60);
				snprintf(tlabel, sizeof tlabel, T("Zamanlayıcı kur: %s"), d);
				tbtn.label = tlabel;
				kui_button(&tbtn, 0);
			}
			draw_strip();
			kui_fill((KuiRect){ 0, BAR_Y - 10, w, BAR_H + 20 }, 255);
			prev.label = step < 0 ? "" : T("◀ Önceki");
			if (step < 0) kui_fill(prev.r, 255);
			else kui_button(&prev, 0);
			next.label = step + 1 < n ? T("Sonraki ▶") : T("Bitti ✓");
			kui_button(&next, 0);
			flush_screen(full);
			full = redraw = 0;
		}
		KuiEvent e = wait_event(timers_active() ? 1000 : -1);
		if (e.type == KUI_EV_QUIT) { ret = L_QUIT; break; }
		if (e.type == KUI_EV_WAKE) {
			full = wake_full();
			redraw = 1;
			continue;
		}
		if (e.type == KUI_EV_NONE) { /* a second passed: only the timer boxes change */
			long now = (long) time(NULL);
			if (now != last_tick && timers_active()) {
				last_tick = now;
				draw_strip();
				kui_flush(0);
			}
			continue;
		}
		int dlt = 0;
		if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			int hit = kui_topbar_hit(w, 1, e.x, e.y);
			if (hit == KUI_BTN_EXIT) break;
			if (hit == KUI_BTN_MENU || (strip_n && kui_buttons_hit(strip_btn, strip_n, e.x, e.y))) {
				if (timers_screen() == L_QUIT) { ret = L_QUIT; break; }
				full = redraw = 1;
				continue;
			}
			KuiButton bs[3] = { prev, next, tbtn };
			int id = kui_buttons_hit(bs, secs ? 3 : 2, e.x, e.y);
			if (id == 1 && step >= 0) dlt = -1;
			if (id == 2) {
				if (step + 1 < n) dlt = 1;
				else break; /* Bitti */
			}
			if (id == 3) {
				char name[48], t[60];
				ellipsize(r->title, KUI_FONT_REG, 24, 300, t, sizeof t);
				snprintf(name, sizeof name, T("Adım %d · %s"), step + 1, t);
				if (tarif_timer_add(&timers, name, secs, (long) time(NULL)) < 0) message(T("Zamanlayıcılar"), T("En çok 6 zamanlayıcı kurulabilir."));
				save_timers();
				awake_update();
				draw_strip();
				kui_flush(0);
				continue;
			}
			if (!id && step < 0 && e.y >= COOK_Y && e.y < COOK_Y + COOK_H) { /* nothing to hit precisely: next on a tap */
				dlt = 1;
			}
		}
		if (dlt && step + dlt >= -1 && step + dlt < n) {
			step += dlt;
			redraw = 1; /* next / previous step: partial refresh */
		}
	}
	cooking = 0;
	awake_update();
	return ret;
}

/* ---- timers ---- */

static char next_name[48];

static int timers_screen(void)
{
	int w = kui_width();
	static const int presets[9] = { 1, 3, 5, 10, 15, 20, 30, 45, 60 };
	KuiButton pre[9], del[TARIF_MAX_TIMERS], name_btn = { { w - MARGIN - 300, 860, 300, 90 }, T("Ad ver"), 30 };
	static char plab[9][16];
	int bw = (w - 2 * MARGIN - 40) / 3;
	for (int i = 0; i < 9; i++) {
		snprintf(plab[i], sizeof plab[i], T("%d dk"), presets[i]);
		pre[i] = (KuiButton){ { MARGIN + i % 3 * (bw + 20), 980 + i / 3 * 115, bw, 95 }, plab[i], 100 + i };
	}
	long last = 0;
	for (int full = 1, redraw = 1;;) {
		long now = (long) time(NULL);
		if (!next_name[0]) snprintf(next_name, sizeof next_name, T("Zamanlayıcı %d"), timers.n + 1);
		if (redraw) {
			kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
			kui_topbar(T("Zamanlayıcılar"), NULL);
			if (!timers.n)
				kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 60, w - 2 * MARGIN, 80 }, T("Kurulu zamanlayıcı yok."), KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0x40);
			kui_fill((KuiRect){ MARGIN, 840, w - 2 * MARGIN, 3 }, 0);
			char s[120];
			snprintf(s, sizeof s, T("Yeni: %s"), next_name);
			char e2[120];
			ellipsize(s, KUI_FONT_BOLD, 36, w - 2 * MARGIN - 320, e2, sizeof e2);
			kui_text((KuiRect){ MARGIN, 860, w - 2 * MARGIN - 320, 90 }, e2, KUI_FONT_BOLD, 36, KUI_ALIGN_LEFT, 0);
			kui_button(&name_btn, 0);
			for (int i = 0; i < 9; i++) kui_button(&pre[i], 0);
			draw_block(MARGIN, 1335, w - 2 * MARGIN, T("Süre dolunca ekrandaki kutu yanıp söner (Kindle'da hoparlör yok). Zamanlayıcılar Tarifler açıkken çalışır."),
				   KUI_FONT_REG, 26, KUI_ALIGN_CENTER, 3, 0x40, 1);
		}
		if (redraw || now != last) { /* the rows: remaining times change every second */
			last = now;
			kui_fill((KuiRect){ 0, KUI_TOPBAR_H + 10, w, 820 - KUI_TOPBAR_H }, 255);
			if (!timers.n)
				kui_text((KuiRect){ MARGIN, KUI_TOPBAR_H + 60, w - 2 * MARGIN, 80 }, T("Kurulu zamanlayıcı yok."), KUI_FONT_REG, 38, KUI_ALIGN_CENTER, 0x40);
			for (int i = 0; i < timers.n; i++) {
				int y = KUI_TOPBAR_H + 20 + i * 115;
				char c[24], nm[80];
				long left = tarif_timer_left(&timers.t[i], now);
				if (timers.t[i].done || !left) snprintf(c, sizeof c, "%s", T("Bitti"));
				else tarif_fmt_clock(left, c, sizeof c);
				ellipsize(timers.t[i].name, KUI_FONT_BOLD, 36, w - 2 * MARGIN - 470, nm, sizeof nm);
				kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN - 470, 100 }, nm, KUI_FONT_BOLD, 36, KUI_ALIGN_LEFT, 0);
				kui_text((KuiRect){ w - MARGIN - 450, y, 270, 100 }, c, KUI_FONT_BOLD, 52, KUI_ALIGN_RIGHT, 0);
				del[i] = (KuiButton){ { w - MARGIN - 160, y + 5, 160, 90 }, T("Sil"), 200 + i };
				kui_button(&del[i], 0);
				kui_fill((KuiRect){ MARGIN, y + 108, w - 2 * MARGIN, 1 }, 0x80);
			}
			if (redraw) flush_screen(full);
			else {
				kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H + 10, w, 820 - KUI_TOPBAR_H });
				kui_flush(0);
			}
			full = redraw = 0;
		}
		KuiEvent e = wait_event(timers_active() ? 1000 : -1);
		if (e.type == KUI_EV_QUIT) return L_QUIT;
		if (e.type == KUI_EV_WAKE) {
			full = wake_full();
			redraw = 1;
			continue;
		}
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return L_BACK;
		int id = kui_buttons_hit(pre, 9, e.x, e.y);
		if (!id) id = kui_buttons_hit(del, timers.n, e.x, e.y);
		if (!id && kui_rect_contains(name_btn.r, e.x, e.y)) id = 30;
		if (id == 30) {
			char buf[48];
			snprintf(buf, sizeof buf, "%s", next_name);
			if (kui_input_text(T("Zamanlayıcının adı"), buf, sizeof buf, 0, 1, NULL)) snprintf(next_name, sizeof next_name, "%s", buf);
			full = redraw = 1;
		} else if (id >= 200) {
			tarif_timer_remove(&timers, id - 200);
			save_timers();
			awake_update();
			redraw = 1;
		} else if (id >= 100) {
			if (tarif_timer_add(&timers, next_name, presets[id - 100] * 60, (long) time(NULL)) < 0) {
				message(T("Zamanlayıcılar"), T("En çok 6 zamanlayıcı kurulabilir."));
				full = 1;
			}
			next_name[0] = '\0';
			save_timers();
			awake_update();
			redraw = 1;
		}
	}
}

/* ---- online search: Vikikitap, Wikibooks and (with the user's key) TheMealDB ---- */

enum { SRC_TRWB, SRC_ENWB, SRC_MEAL };
typedef struct { int src; char title[160], id[16]; } Hit;
#define MAX_HITS 45
static Hit hits[MAX_HITS];
static int n_hits;

static int json_get(const char *url, char **body)
{
	int len = 0;
	*body = NULL;
	return net_get(url, body, &len, NET_TIMEOUT_S);
}

static void wiki_search(int src, const char *q)
{
	char enc[400], url[700], *body;
	net_urlencode(q, enc, sizeof enc);
	snprintf(url, sizeof url, "https://%s/w/api.php?action=query&list=search&format=json&formatversion=2&srlimit=15&srnamespace=%d&srsearch=%s",
		 src == SRC_TRWB ? "tr.wikibooks.org" : "en.wikibooks.org", src == SRC_TRWB ? 100 : 102, enc);
	if (json_get(url, &body) != 0) return;
	static char titles[15][160];
	int n = tarif_wiki_search_titles(body, titles, 15);
	free(body);
	for (int i = 0; i < n && n_hits < MAX_HITS; i++) {
		if (strstr(titles[i], "Cuisine") || strstr(titles[i], "Mutfağı") || strchr(titles[i], '/')) continue; /* index pages */
		hits[n_hits].src = src;
		snprintf(hits[n_hits].title, sizeof hits[n_hits].title, "%s", titles[i]);
		hits[n_hits++].id[0] = '\0';
	}
}

static void mealdb_search(const char *q_en, int by_ing)
{
	if (!mealdb_key[0]) return;
	char enc[300], url[600], *body, word[100];
	if (by_ing) { /* the first ingredient, as TheMealDB writes it ("chicken_breast") */
		snprintf(word, sizeof word, "%s", q_en);
		char *sp = strchr(word, ' ');
		if (sp) *sp = '\0';
		net_urlencode(word, enc, sizeof enc);
	} else net_urlencode(q_en, enc, sizeof enc);
	snprintf(url, sizeof url, "https://www.themealdb.com/api/json/v1/%s/%s%s", mealdb_key, by_ing ? "filter.php?i=" : "search.php?s=", enc);
	if (json_get(url, &body) != 0) return;
	static char ids[15][16], names[15][160];
	int n = tarif_mealdb_list(body, ids, names, 15);
	free(body);
	for (int i = 0; i < n && n_hits < MAX_HITS; i++) {
		hits[n_hits].src = SRC_MEAL;
		snprintf(hits[n_hits].title, sizeof hits[n_hits].title, "%s", names[i]);
		snprintf(hits[n_hits++].id, sizeof hits[0].id, "%s", ids[i]);
	}
}

static void hit_url(const Hit *h, char *url, int cap)
{
	if (h->src == SRC_MEAL) snprintf(url, (size_t) cap, "https://www.themealdb.com/meal/%s", h->id);
	else tarif_wiki_url(h->src == SRC_TRWB ? "tr.wikibooks.org" : "en.wikibooks.org", h->title, url, cap);
}

static void hit_row(int i, KuiRect r)
{
	const Hit *h = &hits[i];
	const char *t = h->title;
	if (!strncmp(t, "Yemek:", 6)) t += 6;
	if (!strncmp(t, "Cookbook:", 9)) t += 9;
	char s[300], url[600];
	ellipsize(t, KUI_FONT_BOLD, 36, r.w - 40, s, sizeof s);
	kui_text((KuiRect){ r.x + 20, r.y + 8, r.w - 40, 56 }, s, KUI_FONT_BOLD, 36, KUI_ALIGN_LEFT, 0);
	static const char *const SRC[] = { "Vikikitap", "Wikibooks Cookbook", "TheMealDB" };
	hit_url(h, url, sizeof url);
	snprintf(s, sizeof s, "%s · %s%s", SRC[h->src], h->src == SRC_TRWB ? T("Türkçe") : T("İngilizce"), find_url(url).d ? T(" · çevrimdışı kayıtlı") : "");
	kui_text((KuiRect){ r.x + 20, r.y + 64, r.w - 40, 44 }, s, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0x40);
}

/* fetches the hit (or finds it saved) into the cache; returns its ref, d == NULL on failure */
static Ref fetch_hit(const Hit *h)
{
	char url[600], api[900], enc[500], *body;
	hit_url(h, url, sizeof url);
	Ref ref = find_url(url);
	if (ref.d) return ref;
	TarifBuf b = { 0 };
	if (h->src == SRC_MEAL) {
		snprintf(api, sizeof api, "https://www.themealdb.com/api/json/v1/%s/lookup.php?i=%s", mealdb_key, h->id);
		if (json_get(api, &body) == 0) {
			tarif_mealdb_records(&b, body);
			free(body);
		}
	} else {
		const char *host = h->src == SRC_TRWB ? "tr.wikibooks.org" : "en.wikibooks.org";
		net_urlencode(h->title, enc, sizeof enc);
		snprintf(api, sizeof api, "https://%s/w/api.php?action=query&prop=revisions&rvprop=content&rvslots=main&format=json&formatversion=2&titles=%s", host, enc);
		if (json_get(api, &body) == 0) {
			cJSON *root = cJSON_Parse(body);
			cJSON *pages = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "query"), "pages");
			cJSON *rev = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(pages, 0), "revisions"), 0);
			cJSON *c = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(rev, "slots"), "main"), "content");
			const char *t = h->title;
			if (!strncmp(t, "Yemek:", 6)) t += 6;
			if (!strncmp(t, "Cookbook:", 9)) t += 9;
			if (cJSON_IsString(c)) tarif_wiki_record(&b, c->valuestring, h->src == SRC_TRWB ? "tr" : "en", t, url);
			cJSON_Delete(root);
			free(body);
		}
	}
	if (b.len) {
		cache_add(b.s, b.len);
		ref = find_url(url);
	}
	tarif_buf_free(&b);
	return ref;
}

static int online_search(const char *query, int by_ing)
{
	int was_off = kui_wifi_enabled() == 0, ret = L_BACK;
	busy_screen(T("Çevrimiçi aranıyor…"), query);
	if (was_off) {
		kui_wifi_enable(1);
		kui_wifi_wait(40);
	}
	char en[300];
	tarif_translate_query(query, en, sizeof en);
	n_hits = 0;
	wiki_search(SRC_TRWB, query);
	wiki_search(SRC_ENWB, en);
	mealdb_search(en, by_ing);
	char head[300];
	snprintf(head, sizeof head, T("“%s” · %d sonuç · seçilen tarif çevrimdışı için saklanır"), query, n_hits);
	ListDef L = { T("Çevrimiçi sonuçlar"), head, NULL, n_hits, 118, hit_row,
		      T("Sonuç yok ya da bağlantı kurulamadı.\nWi-Fi bağlantısını kontrol edin.") };
	for (int page = 0;;) {
		int k = list_screen(&L, &page);
		if (k < 0) { ret = k; break; }
		kui_fill((KuiRect){ 0, BAR_Y - 10, kui_width(), BAR_H + 20 }, 255);
		kui_text((KuiRect){ 0, BAR_Y, kui_width(), BAR_H }, T("Yükleniyor…"), KUI_FONT_BOLD, 38, KUI_ALIGN_CENTER, 0);
		kui_mark_dirty((KuiRect){ 0, BAR_Y - 10, kui_width(), BAR_H + 20 });
		kui_flush(0);
		Ref ref = fetch_hit(&hits[k]);
		if (!ref.d) {
			message(T("Tarif açılamadı"), T("Bu sayfada malzeme listesi bulunamadı ya da bağlantı kesildi."));
			continue;
		}
		if (recipe_screen(ref) == L_QUIT) { ret = L_QUIT; break; }
	}
	if (was_off) kui_wifi_enable(0);
	return ret;
}

/* ---- search ---- */

static int ids[MAX_LIST];

/* the keyboard starts in capitals: an all-capitals query is shown in lower case (Turkish rules) */
static void lower_if_caps(char *s)
{
	char out[200];
	int o = 0, cp;
	for (const char *p = s; *p;) {
		p += kui_utf8_decode(p, &cp);
		if (kui_tr_lower(cp) == cp && kui_tr_upper(cp) != cp) return; /* has a lower-case letter */
	}
	for (const char *p = s; *p && o < (int) sizeof out - 5;) {
		p += kui_utf8_decode(p, &cp);
		o += kui_utf8_encode(kui_tr_lower(cp), out + o);
	}
	out[o] = '\0';
	strcpy(s, out);
}

static int search_flow(int by_ing)
{
	static char q[2][120];
	for (;;) {
		const char *prompt = by_ing ? T("Elinizdeki malzemeler (ör. domates, yumurta)") : T("Tarif ara (ad ya da ülke)");
		if (!kui_input_text(prompt, q[by_ing], sizeof q[by_ing], 0, 2, NULL)) return quitting ? L_QUIT : L_BACK;
		lower_if_caps(q[by_ing]);
		for (;;) {
			int n = 0, k;
			list_words = 0;
			if (by_ing) {
				static int m[MAX_LIST];
				int nw;
				k = tarif_by_ingredients(&db, q[1], ids, m, MAX_LIST, &nw);
				for (int i = 0; i < k; i++) list_matched[n] = m[i], list[n++] = (Ref){ &db, ids[i] };
				k = tarif_by_ingredients(&cache, q[1], ids, m, MAX_LIST - n, &nw);
				for (int i = 0; i < k && n < MAX_LIST; i++) list_matched[n] = m[i], list[n++] = (Ref){ &cache, ids[i] };
				list_words = nw;
				/* merged: most matched first (each set is sorted already) */
				for (int i = 1; i < n; i++)
					for (int j = i; j > 0 && list_matched[j] > list_matched[j - 1]; j--) {
						Ref t = list[j];
						list[j] = list[j - 1], list[j - 1] = t;
						int x = list_matched[j];
						list_matched[j] = list_matched[j - 1], list_matched[j - 1] = x;
					}
			} else {
				k = tarif_search(&db, q[0], ids, MAX_LIST);
				n = add_refs(&db, ids, k, 0);
				k = tarif_search(&cache, q[0], ids, MAX_LIST - n);
				n = add_refs(&cache, ids, k, n);
			}
			char head[300];
			snprintf(head, sizeof head, T("“%s” · %d tarif"), q[by_ing], n);
			int r = recipes_list(by_ing ? T("Elimde ne var?") : T("Arama"), head, n, T("Çevrimiçi ara"),
					     T("Çevrimdışı tariflerde bulunamadı.\nAşağıdan çevrimiçi arayabilirsiniz."));
			if (r == L_QUIT) return L_QUIT;
			if (r == L_ACTION) {
				if (online_search(q[by_ing], by_ing) == L_QUIT) return L_QUIT;
				continue; /* the cache may have grown: search again */
			}
			break; /* ✕: a new query */
		}
	}
}

/* ---- favourites, recents, cuisines ---- */

static int keys_list(TarifKeys *k, const char *title, const char *empty)
{
	int n = 0;
	for (int i = k->n - 1; i >= 0 && n < MAX_LIST; i--) { /* newest first; recipes no longer available are skipped */
		Ref r = find_url(k->key[i]);
		if (r.d) list[n++] = r;
	}
	return recipes_list(title, NULL, n, NULL, empty);
}

static const char *cu_names[200];
static int cu_counts[200], n_cu;

static void cuisine_row(int i, KuiRect r)
{
	char c[64];
	kui_text((KuiRect){ r.x + 20, r.y, r.w - 200, r.h }, cu_names[i], KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
	snprintf(c, sizeof c, T("%d tarif"), cu_counts[i]);
	kui_text((KuiRect){ r.x + r.w - 220, r.y, 200, r.h }, c, KUI_FONT_REG, 30, KUI_ALIGN_RIGHT, 0x40);
}

static int show_filter(const char *title, const char *cuisine, char tag)
{
	int k = tarif_filter(&db, cuisine, tag, ids, MAX_LIST), n = add_refs(&db, ids, k, 0);
	k = tarif_filter(&cache, cuisine, tag, ids, MAX_LIST - n);
	n = add_refs(&cache, ids, k, n);
	char head[160];
	int tr = 0;
	for (int i = 0; i < n; i++) tr += !strcmp(R(list[i])->lang, "tr");
	snprintf(head, sizeof head, T("%d tarif · %d Türkçe, %d İngilizce"), n, tr, n - tr);
	return recipes_list(title, head, n, NULL, NULL);
}

static int world_screen(void)
{
	/* cuisines of the bundled set and the cache together */
	n_cu = tarif_cuisines(&db, cu_names, cu_counts, 200);
	const char *cn[200];
	int cc[200], m = tarif_cuisines(&cache, cn, cc, 200);
	for (int i = 0; i < m; i++) {
		int k = 0;
		while (k < n_cu && strcmp(cu_names[k], cn[i])) k++;
		if (k < n_cu) cu_counts[k] += cc[i];
		else if (n_cu < 200) cu_names[n_cu] = cn[i], cu_counts[n_cu++] = cc[i];
	}
	ListDef L = { T("Dünya mutfağı"), T("Ülkeler, tarif sayısına göre"), NULL, n_cu, 96, cuisine_row, NULL };
	for (int page = 0;;) {
		int k = list_screen(&L, &page);
		if (k < 0) return k;
		static char c[100];
		snprintf(c, sizeof c, "%s", cu_names[k]);
		if (show_filter(c, c, 0) == L_QUIT) return L_QUIT;
	}
}

/* ---- sources and the TheMealDB key ---- */

static int sources_screen(void)
{
	int w = kui_width();
	KuiButton key_btn = { { MARGIN, 1180, (w - 2 * MARGIN - 30) / 2, 100 }, T("Anahtar gir"), 1 };
	KuiButton del_btn = { { MARGIN + key_btn.r.w + 30, 1180, key_btn.r.w, 100 }, T("Anahtarı sil"), 2 };
	for (int full = 1;;) {
		kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
		kui_topbar(T("Kaynaklar"), NULL);
		int y = KUI_TOPBAR_H + 30, tw = w - 2 * MARGIN, tr = 0;
		for (int i = 0; i < db.n; i++) tr += !strcmp(db.r[i].lang, "tr");
		char s[600];
		snprintf(s, sizeof s, T("Çevrimdışı: %d tarif (%d Türkçe, %d İngilizce) · kaydedilen: %d"), db.n, tr, db.n - tr, cache.n);
		y += draw_block(MARGIN, y, tw, s, KUI_FONT_BOLD, 32, KUI_ALIGN_LEFT, 3, 0, 1) + 20;
		y += draw_block(MARGIN, y, tw,
				T("Vikikitap Yemek Kitabı (tr.wikibooks.org) ve Wikibooks Cookbook (en.wikibooks.org): Wikibooks katkıcılarının metinleri, "
				  "CC BY-SA 4.0 lisansıyla. Biçim sadeleştirildi; her tarifin altında sayfa adresi var. İnternette de bu iki kaynakta aranır."),
				KUI_FONT_REG, 29, KUI_ALIGN_LEFT, 8, 0, 1) + 26;
		y += draw_block(MARGIN, y, tw,
				T("TheMealDB (themealdb.com) isteğe bağlıdır: ücretsiz deneme anahtarı \"1\" yalnızca geliştirme ve eğitim içindir, herkese açık "
				  "uygulamalar destekçi anahtarı ister. Kendi anahtarınızı girerseniz çevrimiçi aramada TheMealDB de kullanılır (veri ve görseller: TheMealDB)."),
				KUI_FONT_REG, 29, KUI_ALIGN_LEFT, 8, 0, 1) + 26;
		y += draw_block(MARGIN, y, tw, T("Miktarlar kişi sayısına göre yaklaşık ölçeklenir; vegan/vejetaryen etiketleri malzemelerden çıkarılır, alerjiler için tarifi kontrol edin."),
				KUI_FONT_REG, 27, KUI_ALIGN_LEFT, 5, 0x40, 1);
		snprintf(s, sizeof s, T("TheMealDB anahtarı: %s"), mealdb_key[0] ? mealdb_key : T("yok"));
		kui_text((KuiRect){ MARGIN, 1100, tw, 60 }, s, KUI_FONT_BOLD, 32, KUI_ALIGN_LEFT, 0);
		kui_button(&key_btn, 0);
		if (mealdb_key[0]) kui_button(&del_btn, 0);
		flush_screen(full);
		full = 1;
		for (int again = 1; again;) {
			KuiEvent e = wait_event(-1);
			if (e.type == KUI_EV_QUIT) return L_QUIT;
			if (e.type == KUI_EV_WAKE) { full = wake_full(); break; }
			if (e.type != KUI_EV_TAP) continue;
			if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return L_BACK;
			KuiButton bs[2] = { key_btn, del_btn };
			int id = kui_buttons_hit(bs, mealdb_key[0] ? 2 : 1, e.x, e.y);
			if (id == 1) {
				char buf[64];
				snprintf(buf, sizeof buf, "%s", mealdb_key);
				if (kui_input_text(T("TheMealDB API anahtarı"), buf, sizeof buf, 0, 1, NULL)) {
					snprintf(mealdb_key, sizeof mealdb_key, "%s", buf);
					kui_write_atomic(path_of("mealdb_key"), mealdb_key, (int) strlen(mealdb_key));
				}
				again = 0;
			} else if (id == 2) {
				mealdb_key[0] = '\0';
				remove(path_of("mealdb_key"));
				again = 0;
			}
		}
	}
}

/* ---- home ---- */

typedef struct { const char *label; const char *cuisine; char tag; } Cat;
static const Cat CATS[] = {
	{ N_("Türk mutfağı"), "Türkiye", 0 }, { N_("Dünya mutfağı"), NULL, 0 }, { N_("Çorbalar"), NULL, 'c' }, { N_("Ana yemekler"), NULL, 'a' },
	{ N_("Tatlılar"), NULL, 't' }, { N_("Salata ve mezeler"), NULL, 's' }, { N_("Hamur işleri"), NULL, 'h' }, { N_("Kahvaltılık"), NULL, 'k' },
	{ N_("Vegan"), NULL, 'v' }, { N_("Vejetaryen"), NULL, 'g' },
};
#define N_CATS ((int) (sizeof CATS / sizeof CATS[0]))

enum { H_SEARCH = 1, H_INGR, H_FAVS, H_RECENT, H_TIMERS, H_SOURCES, H_CAT = 100 };

static int home(void)
{
	int w = kui_width(), h = kui_height(), bw = (w - 2 * MARGIN - 24) / 2;
	KuiButton bs[32];
	int nb = 0;
	for (int full = 1;;) {
		nb = 0;
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Tarifler"), T("Ara"));
		bs[nb++] = (KuiButton){ { MARGIN, 150, bw, 120 }, T("Tarif ara"), H_SEARCH };
		bs[nb++] = (KuiButton){ { MARGIN + bw + 24, 150, bw, 120 }, T("Elimde ne var?"), H_INGR };
		for (int i = 0; i < nb; i++) kui_button(&bs[i], 0);
		int y = 300;
		for (int i = 0; i < N_CATS; i++) {
			KuiRect r = { MARGIN + i % 2 * (bw + 24), y + i / 2 * 140, bw, 122 };
			int n = 0;
			if (CATS[i].cuisine) n = tarif_filter(&db, CATS[i].cuisine, 0, ids, MAX_LIST) + tarif_filter(&cache, CATS[i].cuisine, 0, ids, MAX_LIST);
			else if (CATS[i].tag) n = tarif_filter(&db, NULL, CATS[i].tag, ids, MAX_LIST) + tarif_filter(&cache, NULL, CATS[i].tag, ids, MAX_LIST);
			else n = tarif_cuisines(&db, cu_names, cu_counts, 200);
			kui_frame(r, 3, 0);
			kui_text((KuiRect){ r.x + 24, r.y + 10, r.w - 48, 56 }, T(CATS[i].label), KUI_FONT_BOLD,
				 kui_fit_px(T(CATS[i].label), KUI_FONT_BOLD, 38, r.w - 48), KUI_ALIGN_LEFT, 0);
			char c[64];
			snprintf(c, sizeof c, CATS[i].cuisine || CATS[i].tag ? T("%d tarif") : T("%d ülke"), n);
			kui_text((KuiRect){ r.x + 24, r.y + 66, r.w - 48, 38 }, c, KUI_FONT_REG, 28, KUI_ALIGN_LEFT, 0x40);
			bs[nb++] = (KuiButton){ r, "", H_CAT + i };
		}
		y += 5 * 140 + 16;
		int b3 = (w - 2 * MARGIN - 2 * 20) / 3, nf = 0, nr = 0, nt = 0;
		for (int i = 0; i < favs.n; i++) nf += find_url(favs.key[i]).d != NULL;
		for (int i = 0; i < recent.n; i++) nr += find_url(recent.key[i]).d != NULL;
		for (int i = 0; i < timers.n; i++) nt += !timers.t[i].done;
		static char l1[64], l2[64], l3[64];
		snprintf(l1, sizeof l1, T("★ Favoriler (%d)"), nf);
		snprintf(l2, sizeof l2, T("Son bakılanlar (%d)"), nr);
		snprintf(l3, sizeof l3, nt ? T("Zamanlayıcılar (%d)") : T("Zamanlayıcılar"), nt);
		int first = nb;
		bs[nb++] = (KuiButton){ { MARGIN, y, b3, 120 }, l1, H_FAVS };
		bs[nb++] = (KuiButton){ { MARGIN + b3 + 20, y, b3, 120 }, l2, H_RECENT };
		bs[nb++] = (KuiButton){ { MARGIN + 2 * (b3 + 20), y, b3, 120 }, l3, H_TIMERS };
		for (int i = first; i < nb; i++) kui_button(&bs[i], 0);
		y += 160;
		int tr = 0;
		for (int i = 0; i < db.n; i++) tr += !strcmp(db.r[i].lang, "tr");
		char foot[300];
		snprintf(foot, sizeof foot, T("%d tarif çevrimdışı (%d Türkçe) · Vikikitap, Wikibooks · CC BY-SA 4.0"), db.n + cache.n, tr);
		kui_text((KuiRect){ MARGIN, y, w - 2 * MARGIN, 44 }, foot, KUI_FONT_REG, kui_fit_px(foot, KUI_FONT_REG, 28, w - 2 * MARGIN), KUI_ALIGN_CENTER, 0x40);
		bs[nb++] = (KuiButton){ { w / 2 - 200, y + 56, 400, 80 }, T("Kaynaklar ve ayarlar"), H_SOURCES };
		kui_button(&bs[nb - 1], 0);
		flush_screen(full);
		full = 1; /* every way back to the home screen is a new screen */
		for (int again = 1; again;) {
			KuiEvent e = wait_event(-1);
			if (e.type == KUI_EV_QUIT) return L_QUIT;
			if (e.type == KUI_EV_WAKE) { full = wake_full(); break; }
			if (e.type != KUI_EV_TAP) continue;
			int hit = kui_topbar_hit(w, 1, e.x, e.y);
			if (hit == KUI_BTN_EXIT) return L_BACK;
			int id = hit == KUI_BTN_MENU ? H_SEARCH : kui_buttons_hit(bs, nb, e.x, e.y), r = 0;
			if (!id) continue;
			again = 0;
			if (id == H_SEARCH) r = search_flow(0);
			else if (id == H_INGR) r = search_flow(1);
			else if (id == H_FAVS) r = keys_list(&favs, T("Favoriler"), T("Henüz favori tarif yok.\nBir tarifte ☆ düğmesine dokunun."));
			else if (id == H_RECENT) r = keys_list(&recent, T("Son bakılanlar"), T("Henüz tarif açılmadı."));
			else if (id == H_TIMERS) r = timers_screen();
			else if (id == H_SOURCES) r = sources_screen();
			else if (id >= H_CAT) {
				const Cat *c = &CATS[id - H_CAT];
				if (!c->cuisine && !c->tag) r = world_screen();
				else r = show_filter(T(c->label), c->cuisine, c->tag);
			}
			if (r == L_QUIT) return L_QUIT;
		}
	}
}

int main(void)
{
	if (kui_init("tarif") != 0) return 1;
	load_keys("favs", &favs);
	load_keys("recent", &recent);
	load_timers();
	if (kui_read_file(path_of("mealdb_key"), mealdb_key, sizeof mealdb_key) <= 0) mealdb_key[0] = '\0';
	mealdb_key[strcspn(mealdb_key, "\r\n /?&")] = '\0';
	if (load_db() != 0) {
		kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
		kui_topbar(T("Tarifler"), NULL);
		kui_text((KuiRect){ MARGIN, 500, kui_width() - 2 * MARGIN, 200 }, T("Tarifler yüklenemedi."), KUI_FONT_REG, 44, KUI_ALIGN_CENTER, 0);
		kui_flush(1);
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT || (e.type == KUI_EV_TAP && kui_topbar_hit(kui_width(), 0, e.x, e.y) == KUI_BTN_EXIT)) break;
		}
		kui_quit();
		return 0;
	}
	load_cache();
	awake_update();
	home();
	cooking = 0;
	if (awake) kui_prevent_sleep(0);
	tarif_keys_free(&favs);
	tarif_keys_free(&recent);
	tarif_free(&db);
	tarif_free(&cache);
	kui_quit();
	return 0;
}

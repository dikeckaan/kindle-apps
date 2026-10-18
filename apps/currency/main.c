/* Döviz & Altın: TCMB daily rates, gold prices and a converter with a number pad. Data is cached for offline use. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "fx.h"
#include "kui.h"
#include "net/net.h"

#define TCMB_URL "https://www.tcmb.gov.tr/kurlar/today.xml"
#define GOLD_URL "https://finans.truncgil.com/today.json"
#define FRESH_S (60 * 60)

static FxRate rates[FX_MAX];
static int n_rates;
static char tcmb_date[11];
static FxGold gold[FX_GOLD_MAX];
static int n_gold;
static long fetched;
static int offline;
static KuiButton bottom[2];
enum { BTN_ALL = 1, BTN_REFRESH };

static const char *const MAIN[] = { "USD", "EUR", "GBP", "CHF", "JPY", "SAR", "AUD", "RUB" };

static void path_of(const char *name, char *out, int cap) { snprintf(out, (size_t) cap, "%s/%s", kui_data_dir("currency"), name); }

static void parse_cached(void)
{
	static char buf[65536];
	char p[300];
	struct stat st;
	path_of("tcmb.xml", p, sizeof p);
	n_rates = kui_read_file(p, buf, sizeof buf) > 0 ? fx_parse_tcmb(buf, rates, FX_MAX, tcmb_date) : 0;
	fetched = n_rates && stat(p, &st) == 0 ? (long) st.st_mtime : 0;
	path_of("gold.json", p, sizeof p);
	n_gold = kui_read_file(p, buf, sizeof buf) > 0 ? fx_parse_gold(buf, gold, FX_GOLD_MAX) : 0;
}

/* fetches one source; stores it only when it parses */
static int fetch_one(const char *url, const char *name, int is_xml)
{
	char *body = NULL, p[300], d[11];
	int len = 0, ok = 0;
	if (net_get(url, &body, &len, 20) == 0) {
		static FxRate r[FX_MAX];
		static FxGold g[FX_GOLD_MAX];
		ok = is_xml ? fx_parse_tcmb(body, r, FX_MAX, d) > 0 : fx_parse_gold(body, g, FX_GOLD_MAX) > 0;
		if (ok) {
			path_of(name, p, sizeof p);
			kui_write_atomic(p, body, len);
		}
	}
	free(body);
	return ok;
}

static void fetch(void)
{
	int a = fetch_one(TCMB_URL, "tcmb.xml", 1);
	fetch_one(GOLD_URL, "gold.json", 0); /* gold is optional: its section hides when missing */
	offline = !a;
	parse_cached();
}

static const FxRate *rate(const char *code)
{
	if (!strcmp(code, "TRY")) return fx_try();
	for (int i = 0; i < n_rates; i++)
		if (!strcmp(rates[i].code, code)) return &rates[i];
	return NULL;
}

static void row(int y, int h, const char *a, const char *b, const char *c, KuiFont f)
{
	int w = kui_width();
	kui_text((KuiRect){ 40, y, 440, h }, a, f, kui_fit_px(a, f, 40, 440), KUI_ALIGN_LEFT, 0);
	kui_text((KuiRect){ 480, y, 260, h }, b, f, 40, KUI_ALIGN_RIGHT, 0);
	kui_text((KuiRect){ 760, y, w - 800, h }, c, f, 40, KUI_ALIGN_RIGHT, 0);
}

static void rate_row(int y, const FxRate *r)
{
	char a[96], b[32], c[32];
	if (r->unit != 1) snprintf(a, sizeof a, "%d %s", r->unit, r->code);
	else snprintf(a, sizeof a, "%s", r->code);
	kui_fmt_num(r->buy, 4, b, sizeof b);
	kui_fmt_num(r->sell, 4, c, sizeof c);
	row(y, 62, a, b, c, KUI_FONT_REG);
}

static void draw_status(int y)
{
	int w = kui_width();
	char s[128] = "";
	if (!n_rates) {
		snprintf(s, sizeof s, "%s", offline ? "Bağlantı yok, kurlar alınamadı" : "Yükleniyor…");
	} else {
		time_t t = (time_t) fetched;
		struct tm tm;
		localtime_r(&t, &tm);
		snprintf(s, sizeof s, "%s · güncellendi %d %.3s %02d:%02d", offline ? "Çevrimdışı" : "TCMB", tm.tm_mday, kui_tr_month(tm.tm_mon),
			 tm.tm_hour, tm.tm_min);
	}
	kui_fill((KuiRect){ 0, y, w, 60 }, 255);
	kui_text((KuiRect){ 40, y, w - 80, 60 }, s, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty((KuiRect){ 0, y, w, 60 });
}

static void draw(int full)
{
	int w = kui_width(), h = kui_height();
	char s[96];
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar("Döviz & Altın", "Çevirici");
	int y = KUI_TOPBAR_H + 10;
	snprintf(s, sizeof s, "TCMB döviz kurları%s%s", tcmb_date[0] ? " · " : "", tcmb_date);
	kui_text((KuiRect){ 40, y, w - 80, 60 }, s, KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
	y += 65;
	row(y, 50, "", "Alış", "Satış", KUI_FONT_BOLD);
	y += 55;
	kui_fill((KuiRect){ 40, y, w - 80, 2 }, 0);
	for (size_t i = 0; i < sizeof MAIN / sizeof MAIN[0]; i++) {
		const FxRate *r = rate(MAIN[i]);
		if (!r) continue;
		rate_row(y + 4, r);
		y += 64;
	}
	y += 20;
	if (n_gold) {
		kui_text((KuiRect){ 40, y, w - 80, 60 }, "Altın (serbest piyasa, ₺)", KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
		y += 65;
		kui_fill((KuiRect){ 40, y, w - 80, 2 }, 0);
		for (int i = 0; i < n_gold; i++) {
			char b[32], c[32];
			kui_fmt_num(gold[i].buy, 2, b, sizeof b);
			kui_fmt_num(gold[i].sell, 2, c, sizeof c);
			row(y + 4, 62, gold[i].label, b, c, KUI_FONT_REG);
			y += 64;
		}
	}
	draw_status(h - 190);
	bottom[0] = (KuiButton){ { 40, h - 120, (w - 120) / 2, 100 }, "Tüm kurlar", BTN_ALL };
	bottom[1] = (KuiButton){ { 80 + (w - 120) / 2, h - 120, (w - 120) / 2, 100 }, "Yenile", BTN_REFRESH };
	kui_button(&bottom[0], 0);
	kui_button(&bottom[1], 0);
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

static void refresh(void)
{
	kui_fill((KuiRect){ 0, kui_height() - 190, kui_width(), 60 }, 255);
	kui_text((KuiRect){ 40, kui_height() - 190, kui_width() - 80, 60 }, "Güncelleniyor…", KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty((KuiRect){ 0, kui_height() - 190, kui_width(), 60 });
	kui_flush(0);
	fetch();
	draw(0);
}

/* ---- all rates, paged ---- */
static void all_rates(void)
{
	int w = kui_width(), h = kui_height(), per = 16, page = 0, pages = (n_rates + per - 1) / per;
	if (!pages) return;
	KuiButton nav[2];
	for (int full = 1;;) {
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar("Tüm kurlar", NULL);
		int y = KUI_TOPBAR_H + 10;
		row(y, 50, "", "Alış", "Satış", KUI_FONT_BOLD);
		y += 55;
		kui_fill((KuiRect){ 40, y, w - 80, 2 }, 0);
		for (int i = page * per; i < n_rates && i < (page + 1) * per; i++) {
			char a[96], b[32], c[32];
			const FxRate *r = &rates[i];
			if (r->unit != 1) snprintf(a, sizeof a, "%d %s %s", r->unit, r->code, r->name);
			else snprintf(a, sizeof a, "%s %s", r->code, r->name);
			kui_fmt_num(r->buy, 4, b, sizeof b);
			kui_fmt_num(r->sell, 4, c, sizeof c);
			row(y + 4, 62, a, b, c, KUI_FONT_REG);
			y += 64;
		}
		nav[0] = (KuiButton){ { 40, h - 120, 200, 100 }, "◀", 1 };
		nav[1] = (KuiButton){ { w - 240, h - 120, 200, 100 }, "▶", 2 };
		if (pages > 1) {
			char s[16];
			snprintf(s, sizeof s, "%d / %d", page + 1, pages);
			kui_button(&nav[0], 0);
			kui_button(&nav[1], 0);
			kui_text((KuiRect){ 240, h - 120, w - 480, 100 }, s, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
		}
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) return;
			if (e.type == KUI_EV_WAKE) { full = 1; break; }
			int next = 0;
			if (e.type == KUI_EV_SWIPE) next = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
			if (e.type == KUI_EV_TAP) {
				if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
				int id = pages > 1 ? kui_buttons_hit(nav, 2, e.x, e.y) : 0;
				next = id == 1 ? -1 : id == 2 ? 1 : 0;
			}
			if (next && page + next >= 0 && page + next < pages) {
				page += next;
				break;
			}
		}
	}
}

/* ---- currency picker ---- */
static const char *pick_currency(const char *title)
{
	int w = kui_width(), cols = 4, bw = (w - 40 * (cols + 1)) / cols, bh = 110;
	static KuiButton bs[FX_MAX + 1];
	static char codes[FX_MAX + 1][4];
	int n = 0;
	snprintf(codes[n++], 4, "TRY");
	for (int i = 0; i < n_rates && n < FX_MAX + 1; i++) snprintf(codes[n++], 4, "%s", rates[i].code);
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
			kui_topbar(title, NULL);
			for (int i = 0; i < n; i++) {
				bs[i] = (KuiButton){ { 40 + (i % cols) * (bw + 40), KUI_TOPBAR_H + 30 + (i / cols) * (bh + 25), bw, bh }, codes[i], i + 1 };
				kui_button(&bs[i], 0);
			}
			kui_flush(1);
			full = 0;
		}
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return NULL;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return NULL;
		int id = kui_buttons_hit(bs, n, e.x, e.y);
		if (id) return codes[id - 1];
	}
}

/* ---- converter with a number pad ---- */
static void converter(void)
{
	int w = kui_width(), h = kui_height();
	static char from[4] = "USD", to[4] = "TRY", amount[24] = "100";
	if (!rate(from)) snprintf(from, sizeof from, "TRY");
	if (!rate(to)) snprintf(to, sizeof to, "TRY");
	KuiButton top[3], keys[12];
	static const char *const labels[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ",", "0", "⌫" };
	int fresh = 1; /* the first key replaces the amount shown on opening */
	KuiRect amt = { 40, KUI_TOPBAR_H + 190, w - 80, 130 }, res = { 40, KUI_TOPBAR_H + 340, w - 80, 130 };
	for (int full = 1;;) {
		if (full) {
			kui_fill((KuiRect){ 0, 0, w, h }, 255);
			kui_topbar("Çevirici", NULL);
			int bw = (w - 80 - 2 * 30 - 160) / 2;
			top[0] = (KuiButton){ { 40, KUI_TOPBAR_H + 40, bw, 120 }, from, 1 };
			top[1] = (KuiButton){ { 40 + bw + 30, KUI_TOPBAR_H + 40, 160, 120 }, "⇄", 2 };
			top[2] = (KuiButton){ { 40 + bw + 30 + 160 + 30, KUI_TOPBAR_H + 40, bw, 120 }, to, 3 };
			for (int i = 0; i < 3; i++) kui_button(&top[i], 0);
			int kw = (w - 80 - 2 * 30) / 3, kh = 150, ky = h - 4 * (kh + 25) - 20;
			for (int i = 0; i < 12; i++)
				keys[i] = (KuiButton){ { 40 + (i % 3) * (kw + 30), ky + (i / 3) * (kh + 25), kw, kh }, labels[i], 10 + i };
			for (int i = 0; i < 12; i++) kui_button(&keys[i], 0);
		}
		/* amount and result */
		char s[64], r[48];
		kui_fill(amt, 255);
		kui_frame(amt, 3, 0);
		snprintf(s, sizeof s, "%s %s", amount[0] ? amount : "0", from);
		kui_text((KuiRect){ amt.x + 20, amt.y, amt.w - 40, amt.h }, s, KUI_FONT_BOLD, 64, KUI_ALIGN_RIGHT, 0);
		kui_fill(res, 255);
		const FxRate *a = rate(from), *b = rate(to);
		double v = fx_parse_trnum(amount[0] ? amount : "0");
		if (a && b && !isnan(v)) {
			kui_fmt_num(fx_convert(v, a, b), 2, r, sizeof r);
			snprintf(s, sizeof s, "= %s %s", r, to);
		} else {
			snprintf(s, sizeof s, "—");
		}
		kui_text((KuiRect){ res.x + 20, res.y, res.w - 40, res.h }, s, KUI_FONT_BOLD, kui_fit_px(s, KUI_FONT_BOLD, 64, res.w - 40), KUI_ALIGN_RIGHT, 0);
		kui_text((KuiRect){ 40, res.y + res.h, w - 80, 60 }, "TCMB satış kuru ile", KUI_FONT_REG, 30, KUI_ALIGN_RIGHT, 0);
		kui_mark_dirty(amt);
		kui_mark_dirty(res);
		kui_flush(full);
		full = 0;
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) return;
		if (e.type == KUI_EV_WAKE) { full = 1; continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(top, 3, e.x, e.y);
		if (!id) id = kui_buttons_hit(keys, 12, e.x, e.y);
		size_t len = strlen(amount);
		if (id == 1 || id == 3) {
			const char *c = pick_currency(id == 1 ? "Kaynak para" : "Hedef para");
			if (c) snprintf(id == 1 ? from : to, 4, "%s", c);
			full = 1;
		} else if (id == 2) {
			char t[4];
			memcpy(t, from, 4);
			memcpy(from, to, 4);
			memcpy(to, t, 4);
			full = 1;
		} else if (id >= 10) {
			const char *k = labels[id - 10];
			if (fresh && strcmp(k, "⌫") != 0) amount[0] = '\0';
			fresh = 0;
			len = strlen(amount);
			if (!strcmp(k, "⌫")) {
				if (len) amount[len - 1] = '\0';
			} else if (!strcmp(k, ",")) {
				if (!strchr(amount, ',') && len + 1 < sizeof amount) strcat(amount, len ? "," : "0,");
			} else if (len + 1 < sizeof amount && len < 12) {
				if (!strcmp(amount, "0")) amount[0] = '\0';
				strcat(amount, k);
			}
		}
	}
}

int main(void)
{
	if (kui_init("currency") != 0) return 1;
	parse_cached();
	draw(1);
	if (!n_rates || time(NULL) - fetched > FRESH_S) refresh();
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw(1); continue; }
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		if (hit == KUI_BTN_MENU) {
			if (n_rates) converter();
			draw(1);
			continue;
		}
		int id = kui_buttons_hit(bottom, 2, e.x, e.y);
		if (id == BTN_REFRESH) refresh();
		if (id == BTN_ALL && n_rates) {
			all_rates();
			draw(1);
		}
	}
	kui_quit();
	return 0;
}

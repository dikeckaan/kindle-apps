/* E-Kitap: search Project Gutenberg's public-domain catalogue (OPDS) and download a book into the Kindle library,
 * as AZW3 for the Kindle reader or EPUB for KOReader. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "kui.h"
#include "net/net.h"
#include "opds.h"

#define MAX_BOOKS 200
#define PER_PAGE 6
#define ROW_H 180
#define LIST_Y (KUI_TOPBAR_H + 90)
#define SEARCH_TIMEOUT_S 30
#define DOWNLOAD_TIMEOUT_S 120
#define MAX_BYTES (100L * 1024 * 1024)
#define HISTORY_KEEP 50
#define HISTORY_SHOW 5

static const char *fmt_label[2] = { "Kindle (AZW3)", "EPUB (KOReader)" };
static int fmt;                    /* OpdsFmt, remembered in settings */
static Book books[MAX_BOOKS];
static int n_books, next_start, page;
static char query[96];
static int quitting; /* KUI_EV_QUIT seen in a nested screen */

static const char *settings(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/settings", kui_data_dir("ebooks"));
	return p;
}

static const char *history_path(void)
{
	static char p[300];
	snprintf(p, sizeof p, "%s/history", kui_data_dir("ebooks"));
	return p;
}

static const char *book_dir(void)
{
	const char *d = getenv("EBOOK_DIR");
	return d && *d ? d : "/mnt/us/documents";
}

static void flush(int full)
{
	if (full) kui_flush(1);
	else {
		kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, kui_width(), kui_height() - KUI_TOPBAR_H });
		kui_flush(0);
	}
}

/* draws up to max_lines lines of text wrapped to box.w starting at box.y (dark text); returns the height used */
static int draw_wrapped(KuiRect box, const char *text, KuiFont f, int px, int max_lines, KuiAlign a)
{
	static KuiLine lines[64];
	int n = kui_wrap_text(text, box.w, f, px, lines, 64), lh = kui_line_height(f, px);
	if (n > max_lines) n = max_lines;
	for (int i = 0; i < n; i++) {
		char buf[600];
		int len = lines[i].len < (int) sizeof buf - 4 ? lines[i].len : (int) sizeof buf - 4;
		memcpy(buf, text + lines[i].off, (size_t) len);
		buf[len] = '\0';
		if (i == max_lines - 1 && lines[i].off + lines[i].len < (int) strlen(text)) strcat(buf, "…");
		kui_text((KuiRect){ box.x, box.y + i * lh, box.w, lh }, buf, f, px, a, 0);
	}
	return n * lh;
}

/* ---- download history: one file name per line, newest last ---- */

#define HLEN 200 /* file names are <= 150 bytes plus a " (N)" suffix */

/* copies src into dst (cap bytes), cutting only between UTF-8 characters */
static void copy_utf8(char *dst, int cap, const char *src)
{
	int n = (int) strlen(src);
	if (n > cap - 1) {
		n = cap - 1;
		while (n > 0 && ((unsigned char) src[n] & 0xC0) == 0x80) n--;
	}
	memcpy(dst, src, (size_t) n);
	dst[n] = '\0';
}

static int history_load(char lines[][HLEN], int max)
{
	static char buf[HISTORY_KEEP * (HLEN + 1) + 1];
	int len = kui_read_file(history_path(), buf, sizeof buf), n = 0;
	if (len <= 0) return 0;
	for (char *s = buf, *nl; *s && n < max; s = nl + 1) {
		nl = strchr(s, '\n');
		if (nl) *nl = '\0';
		if (*s) copy_utf8(lines[n++], HLEN, s);
		if (!nl) break;
	}
	return n;
}

static void history_add(const char *name)
{
	static char lines[HISTORY_KEEP + 1][HLEN];
	static char out[(HISTORY_KEEP + 1) * (HLEN + 1)];
	int n = history_load(lines, HISTORY_KEEP), o = 0;
	copy_utf8(lines[n++], HLEN, name);
	for (int i = n > HISTORY_KEEP ? n - HISTORY_KEEP : 0; i < n; i++) o += snprintf(out + o, sizeof out - (size_t) o, "%s\n", lines[i]);
	kui_write_atomic(history_path(), out, o);
}

/* ---- first screen ---- */

static KuiButton home_btns[3];
enum { BTN_SEARCH = 1, BTN_AZW3, BTN_EPUB };

static void draw_formats(void)
{
	int w = kui_width(), bw = (w - 100) / 2, y = 760;
	home_btns[1] = (KuiButton){ { 40, y, bw, 110 }, fmt_label[0], BTN_AZW3 };
	home_btns[2] = (KuiButton){ { 60 + bw, y, bw, 110 }, fmt_label[1], BTN_EPUB };
	kui_button(&home_btns[1], fmt == OPDS_AZW3);
	kui_button(&home_btns[2], fmt == OPDS_EPUB);
}

static void draw_home(int full)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("E-Kitap"), T("Ara"));
	int y = KUI_TOPBAR_H + 50;
	y += draw_wrapped((KuiRect){ 50, y, w - 100, 0 }, T("Project Gutenberg'deki telifsiz kitaplar"), KUI_FONT_BOLD, 44, 2, KUI_ALIGN_CENTER);
	y += 15;
	draw_wrapped((KuiRect){ 50, y, w - 100, 0 },
		     T("70.000'den fazla ücretsiz e-kitap. Arama İngilizce başlık ve yazar adlarıyla en iyi sonucu verir."), KUI_FONT_REG, 34, 3,
		     KUI_ALIGN_CENTER);
	home_btns[0] = (KuiButton){ { 120, 450, w - 240, 170 }, T("Kitap ara"), BTN_SEARCH };
	kui_button(&home_btns[0], 0);
	kui_text((KuiRect){ 40, 680, w - 80, 70 }, T("Biçim:"), KUI_FONT_BOLD, 38, KUI_ALIGN_LEFT, 0);
	draw_formats();
	kui_text((KuiRect){ 40, 880, w - 80, 70 }, T("AZW3 Kindle'ın kendi okuyucusunda, EPUB KOReader'da açılır."), KUI_FONT_REG, 28,
		 KUI_ALIGN_LEFT, 0);
	static char hist[HISTORY_KEEP][HLEN];
	int n = history_load(hist, HISTORY_KEEP);
	y = 980;
	kui_fill((KuiRect){ 30, y, w - 60, 3 }, 0);
	kui_text((KuiRect){ 40, y + 15, w - 80, 70 }, T("İndirilenler"), KUI_FONT_BOLD, 40, KUI_ALIGN_LEFT, 0);
	y += 95;
	if (!n) kui_text((KuiRect){ 40, y, w - 80, 60 }, T("Henüz kitap indirilmedi."), KUI_FONT_REG, 32, KUI_ALIGN_LEFT, 0);
	for (int i = 0; i < HISTORY_SHOW && i < n; i++) {
		const char *s = hist[n - 1 - i];
		kui_text((KuiRect){ 40, y + i * 66, w - 80, 62 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 32, w - 80), KUI_ALIGN_LEFT, 0);
	}
	flush(full);
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
	draw_wrapped((KuiRect){ 60, 560, w - 120, 0 }, msg, KUI_FONT_BOLD, 46, 4, KUI_ALIGN_CENTER);
	kui_flush(1);
}

/* fetches one OPDS page into books[n_books…]; 0 = ok, -1 = no connection / not a catalogue page */
static int fetch_page(int start)
{
	char url[800], *body = NULL;
	int len = 0, more = 0;
	opds_search_url(query, start, url, sizeof url);
	if (net_get(url, &body, &len, SEARCH_TIMEOUT_S) != 0) return -1;
	if (!strstr(body, "<feed")) {
		free(body);
		return -1;
	}
	int n = opds_parse(body, len, books + n_books, MAX_BOOKS - n_books, &more);
	free(body);
	/* pages can overlap: keep each book once */
	for (int i = 0, end = n_books + n; i < n; i++) {
		const Book *b = &books[end - n + i];
		int dup = 0;
		for (int j = 0; j < n_books && !dup; j++) dup = books[j].id == b->id;
		if (dup) continue;
		if (&books[n_books] != b) books[n_books] = *b;
		n_books++;
	}
	next_start = n > 0 && n_books < MAX_BOOKS ? more : 0;
	return 0;
}

/* ---- results ---- */

static KuiButton nav[3];
enum { NAV_PREV = 1, NAV_NEXT, NAV_MORE };

static int pages(void) { return n_books ? (n_books + PER_PAGE - 1) / PER_PAGE : 1; }

static void draw_status_bar(const char *msg)
{
	int w = kui_width(), h = kui_height();
	KuiRect bar = { 0, h - 110, w, 110 };
	kui_fill(bar, 255);
	nav[0] = (KuiButton){ { 20, h - 100, 160, 90 }, "◀", NAV_PREV };
	nav[1] = (KuiButton){ { w - 180, h - 100, 160, 90 }, "▶", NAV_NEXT };
	nav[2] = (KuiButton){ { 240, h - 100, w - 480, 90 }, T("Daha fazla"), NAV_MORE };
	if (page > 0) kui_button(&nav[0], 0);
	if (page + 1 < pages()) kui_button(&nav[1], 0);
	char s[64];
	if (msg) kui_text((KuiRect){ 190, h - 100, w - 380, 90 }, msg, KUI_FONT_REG, 32, KUI_ALIGN_CENTER, 0);
	else if (page + 1 == pages() && next_start) kui_button(&nav[2], 0);
	else if (n_books) {
		snprintf(s, sizeof s, "%d / %d", page + 1, pages());
		kui_text((KuiRect){ 190, h - 100, w - 380, 90 }, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
	}
	kui_mark_dirty(bar);
}

static void draw_results(int full)
{
	int w = kui_width(), h = kui_height();
	kui_fill((KuiRect){ 0, 0, w, h }, 255);
	kui_topbar(T("Sonuçlar"), T("Ara"));
	char s[200];
	snprintf(s, sizeof s, T("“%s” · %d kitap%s"), query, n_books, next_start ? "+" : "");
	kui_text((KuiRect){ 30, KUI_TOPBAR_H, w - 60, 80 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 34, w - 60), KUI_ALIGN_LEFT, 0);
	kui_fill((KuiRect){ 20, LIST_Y - 6, w - 40, 3 }, 0);
	if (!n_books)
		draw_wrapped((KuiRect){ 60, 520, w - 120, 0 }, T("Sonuç bulunamadı.\nBaşka bir başlık ya da yazar adı deneyin."), KUI_FONT_REG, 42, 4,
			     KUI_ALIGN_CENTER);
	for (int i = 0; i < PER_PAGE; i++) {
		int k = page * PER_PAGE + i;
		if (k >= n_books) break;
		int y = LIST_Y + i * ROW_H + 12;
		int th = draw_wrapped((KuiRect){ 30, y, w - 60, 0 }, books[k].title, KUI_FONT_BOLD, 36, 2, KUI_ALIGN_LEFT);
		const char *a = books[k].author[0] ? books[k].author : T("Yazar bilinmiyor");
		kui_text((KuiRect){ 30, y + th + 4, w - 60, 44 }, a, KUI_FONT_REG, kui_fit_px(a, KUI_FONT_REG, 30, w - 60), KUI_ALIGN_LEFT, 0);
		kui_fill((KuiRect){ 30, LIST_Y + (i + 1) * ROW_H - 2, w - 60, 1 }, 0);
	}
	draw_status_bar(NULL);
	flush(full);
}


/* ---- confirm and download ---- */

static void draw_confirm(const Book *b, const char *status, const KuiButton *bs, int nb, int full)
{
	int w = kui_width();
	kui_fill((KuiRect){ 0, 0, w, kui_height() }, 255);
	kui_topbar(T("Kitap"), NULL);
	int y = KUI_TOPBAR_H + 50;
	y += draw_wrapped((KuiRect){ 50, y, w - 100, 0 }, b->title, KUI_FONT_BOLD, 50, 6, KUI_ALIGN_LEFT) + 20;
	if (b->author[0]) y += draw_wrapped((KuiRect){ 50, y, w - 100, 0 }, b->author, KUI_FONT_REG, 40, 2, KUI_ALIGN_LEFT) + 30;
	char s[160];
	snprintf(s, sizeof s, T("Biçim: %s"), fmt_label[fmt]);
	kui_text((KuiRect){ 50, y, w - 100, 60 }, s, KUI_FONT_REG, 36, KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, T("Kaynak: gutenberg.org/ebooks/%d"), b->id);
	kui_text((KuiRect){ 50, y + 65, w - 100, 60 }, s, KUI_FONT_REG, 30, KUI_ALIGN_LEFT, 0);
	for (int i = 0; i < nb; i++) kui_button(&bs[i], 0);
	if (status) draw_wrapped((KuiRect){ 50, 1100, w - 100, 0 }, status, KUI_FONT_BOLD, 38, 6, KUI_ALIGN_CENTER);
	flush(full);
}

static const Book *confirm_book;
static const char *confirm_status;
static KuiButton confirm_btns[2];
static int confirm_nb;
static void redraw_confirm(void) { draw_confirm(confirm_book, confirm_status, confirm_btns, confirm_nb, 1); }

/* downloads b; returns 0 and the saved file name in name on success */
static int download(const Book *b, char *name, int cap)
{
	char url[200], path[1200];
	const char *dir = book_dir();
	mkdir(dir, 0755); /* exists on the device; created for the simulator */
	opds_file_name(b->title, opds_has_author(b) ? b->author : "", opds_ext((OpdsFmt) fmt), name, cap);
	if (opds_unique_path(dir, name, path, sizeof path) != 0) return -1;
	const char *slash = strrchr(path, '/');
	copy_utf8(name, cap, slash ? slash + 1 : path);
	opds_download_url(b->id, (OpdsFmt) fmt, url, sizeof url);
	kui_prevent_sleep(1); /* a slow download must not be cut by the screensaver */
	int r = net_download(url, path, MAX_BYTES, DOWNLOAD_TIMEOUT_S);
	kui_prevent_sleep(0);
	if (r != 0) return -1;
	kui_lipc_set_int("com.lab126.scanner", "doFullScan", 1); /* best effort: the library picks the book up */
	history_add(name);
	return 0;
}

static void confirm(const Book *b)
{
	int w = kui_width(), bw = (w - 140) / 2;
	static char status[400];
	char name[HLEN];
	enum { C_GET = 1, C_CANCEL, C_OK };
	confirm_book = b;
	confirm_status = NULL;
	confirm_btns[0] = (KuiButton){ { 50, 900, bw, 150 }, T("İndir"), C_GET };
	confirm_btns[1] = (KuiButton){ { 90 + bw, 900, bw, 150 }, T("Vazgeç"), C_CANCEL };
	confirm_nb = 2;
	draw_confirm(b, NULL, confirm_btns, confirm_nb, 1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) { quitting = 1; return; }
		if (e.type == KUI_EV_WAKE) { redraw_confirm(); continue; }
		if (e.type != KUI_EV_TAP) continue;
		if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return;
		int id = kui_buttons_hit(confirm_btns, confirm_nb, e.x, e.y);
		if (id == C_CANCEL || id == C_OK) return;
		if (id != C_GET) continue;
		confirm_status = T("İndiriliyor…\nGutenberg yavaş olabilir, lütfen bekleyin.");
		confirm_nb = 0;
		draw_confirm(b, confirm_status, confirm_btns, 0, 0);
		if (download(b, name, sizeof name) == 0) {
			snprintf(status, sizeof status, T("Kitaplığa eklendi:\n%s"), name);
			confirm_btns[0] = (KuiButton){ { 120, 900, w - 240, 150 }, T("Tamam"), C_OK };
			confirm_nb = 1;
		} else {
			snprintf(status, sizeof status, "%s", T("İndirilemedi.\nWi-Fi bağlantısını kontrol edip yeniden deneyin."));
			confirm_btns[0] = (KuiButton){ { 50, 900, bw, 150 }, T("Yeniden dene"), C_GET };
			confirm_btns[1] = (KuiButton){ { 90 + bw, 900, bw, 150 }, T("Vazgeç"), C_CANCEL };
			confirm_nb = 2;
		}
		confirm_status = status;
		if (!kui_input_asleep()) draw_confirm(b, confirm_status, confirm_btns, confirm_nb, 0);
	}
}

/* ---- search flow ---- */

/* results screen; returns 1 when the user asks for a new search, 0 to leave */
static int results_loop(void)
{
	draw_results(1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) { quitting = 1; return 0; }
		if (e.type == KUI_EV_WAKE) { draw_results(1); continue; }
		int d = 0;
		if (e.type == KUI_EV_SWIPE) d = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
		if (e.type == KUI_EV_TAP) {
			int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
			if (hit == KUI_BTN_EXIT) return 0;
			if (hit == KUI_BTN_MENU) return 1;
			int id = kui_buttons_hit(nav, 3, e.x, e.y);
			if (id == NAV_PREV && page > 0) d = -1;
			else if (id == NAV_NEXT && page + 1 < pages()) d = 1;
			else if (id == NAV_MORE && page + 1 == pages() && next_start) {
				draw_status_bar(T("Yükleniyor…"));
				kui_flush(0);
				int before = n_books;
				if (fetch_page(next_start) != 0) {
					draw_status_bar(T("Bağlantı yok"));
					kui_flush(0);
					continue;
				}
				if (n_books > before && before % PER_PAGE == 0) page++;
				draw_results(0);
				continue;
			} else if (!id && e.y >= LIST_Y && e.y < LIST_Y + PER_PAGE * ROW_H) {
				int k = page * PER_PAGE + (e.y - LIST_Y) / ROW_H;
				if (k < n_books) {
					confirm(&books[k]);
					if (quitting) return 0;
					draw_results(1);
				}
				continue;
			}
		}
		if (d && page + d >= 0 && page + d < pages()) {
			page += d;
			draw_results(0);
		}
	}
}

/* asks for a query and shows its results; a cancelled new search goes back to the shown results */
static void search(void)
{
	int have = 0;
	for (;;) {
		char q[sizeof query];
		snprintf(q, sizeof q, "%s", query);
		if (!kui_input_text(T("Kitap ara (başlık veya yazar)"), q, sizeof q, 0, 2, NULL)) {
			if (!have) return;
		} else {
			snprintf(query, sizeof query, "%s", q);
			busy_screen(T("Aranıyor…"), query);
			n_books = next_start = page = 0;
			if (fetch_page(1) != 0) {
				message(T("Bağlantı yok"), T("Gutenberg'e ulaşılamadı. Wi-Fi bağlantısını kontrol edip yeniden deneyin."));
				return;
			}
			have = 1;
		}
		if (!results_loop()) return;
	}
}

int main(void)
{
	if (kui_init("ebooks") != 0) return 1;
	fmt = kui_cfg_get_int(settings(), "fmt", OPDS_AZW3) == OPDS_EPUB ? OPDS_EPUB : OPDS_AZW3;
	draw_home(1);
	for (;;) {
		KuiEvent e = kui_next_event(-1);
		if (e.type == KUI_EV_QUIT) break;
		if (e.type == KUI_EV_WAKE) { draw_home(1); continue; }
		if (e.type != KUI_EV_TAP) continue;
		int hit = kui_topbar_hit(kui_width(), 1, e.x, e.y);
		if (hit == KUI_BTN_EXIT) break;
		int id = hit == KUI_BTN_MENU ? BTN_SEARCH : kui_buttons_hit(home_btns, 3, e.x, e.y);
		if (id == BTN_SEARCH) {
			search();
			if (quitting) break;
			draw_home(1);
		} else if ((id == BTN_AZW3 || id == BTN_EPUB) && fmt != (id == BTN_EPUB ? OPDS_EPUB : OPDS_AZW3)) {
			fmt = id == BTN_EPUB ? OPDS_EPUB : OPDS_AZW3;
			kui_cfg_set_int(settings(), "fmt", fmt);
			draw_formats();
			kui_flush(0);
		}
	}
	kui_quit();
	return 0;
}

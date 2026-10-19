/* Akış: an RSS/Atom reader for the user's own feeds — folders, unread counts, a readable article view (plain text,
 * no images), the last fetch of every feed kept for offline reading, a catalogue of popular feeds and OPML import
 * from the documents folder (e.g. sent with Wi-Fi ile Yükle). Feeds are parsed by Gazete's news_parse_rss. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "akis.h"
#include "kui.h"
#include "net/net.h"
#include "news/news.h"

#define ROW_H 132
#define LIST_Y (KUI_TOPBAR_H + 10)
#define BAR_H 100
#define BAR_Y (kui_height() - BAR_H - 14)
#define PER_PAGE ((BAR_Y - 10 - LIST_Y) / ROW_H)

static AkisFeed feeds[AKIS_MAX_FEEDS];
static int n_feeds;
static AkisRead rd;
static int unread[AKIS_MAX_FEEDS], total[AKIS_MAX_FEEDS], failed[AKIS_MAX_FEEDS];
static long fetched[AKIS_MAX_FEEDS]; /* cache mtime, 0 = never fetched */
static int quit, wifi_turned_on;

/* ---- files ---- */

static const char *data_path(const char *name)
{
	static char p[2][400];
	static int k;
	k ^= 1;
	snprintf(p[k], sizeof p[k], "%s/%s", kui_data_dir("akis"), name);
	return p[k];
}

static const char *cache_path(int i)
{
	char id[17], name[40];
	akis_feed_id(feeds[i].url, id);
	snprintf(name, sizeof name, "c_%s.xml", id);
	return data_path(name);
}

static const char *docs_dir(void)
{
	const char *d = getenv("AKIS_DOCS");
	return d && *d ? d : "/mnt/us/documents";
}

static void save_feeds(void)
{
	static char buf[AKIS_MAX_FEEDS * (AKIS_FOLDER_MAX + AKIS_TITLE_MAX + AKIS_URL_MAX + 4) + 100];
	int n = akis_feeds_format(feeds, n_feeds, buf, sizeof buf);
	if (n >= 0) kui_write_atomic(data_path("feeds.txt"), buf, n);
}

static void save_read(void)
{
	static char buf[AKIS_READ_MAX * 17 + 1];
	int n = akis_read_format(&rd, buf, sizeof buf);
	if (n >= 0) kui_write_atomic(data_path("read.txt"), buf, n);
}

static void load_all_files(void)
{
	static char buf[AKIS_MAX_FEEDS * (AKIS_FOLDER_MAX + AKIS_TITLE_MAX + AKIS_URL_MAX + 4) + 100];
	n_feeds = kui_read_file(data_path("feeds.txt"), buf, sizeof buf) > 0 ? akis_feeds_parse(buf, feeds, AKIS_MAX_FEEDS) : 0;
	static char rbuf[AKIS_READ_MAX * 17 + 1];
	akis_read_parse(kui_read_file(data_path("read.txt"), rbuf, sizeof rbuf) > 0 ? rbuf : "", &rd);
}

/* parses feed i's cached copy; returns the item count (free with news_free) */
static int load_items(int i, NewsItem *items)
{
	static char buf[NET_MAX_BODY + 1];
	struct stat st;
	const char *p = cache_path(i);
	int len = kui_read_file(p, buf, sizeof buf);
	fetched[i] = len > 0 && stat(p, &st) == 0 ? (long) st.st_mtime : 0;
	return len > 0 ? news_parse_rss(buf, len, items, NEWS_MAX) : 0;
}

static unsigned long long key_of(int i, const NewsItem *it) { return akis_item_key(feeds[i].url, it->link, it->title); }

/* unread counts of every feed; drops read marks of articles no feed has any more (only when every feed has a copy) */
static void recount(void)
{
	static NewsItem items[NEWS_MAX];
	static unsigned long long keys[AKIS_MAX_FEEDS * NEWS_MAX];
	int nk = 0, all_cached = 1;
	for (int i = 0; i < n_feeds; i++) {
		int n = load_items(i, items);
		total[i] = n;
		unread[i] = 0;
		if (!fetched[i]) all_cached = 0;
		for (int k = 0; k < n; k++) {
			unsigned long long key = key_of(i, &items[k]);
			if (!akis_read_has(&rd, key)) unread[i]++;
			if (nk < (int) (sizeof keys / sizeof keys[0])) keys[nk++] = key;
		}
		news_free(items, n);
	}
	if (all_cached && n_feeds && akis_read_prune(&rd, keys, nk) > 0) save_read();
}

/* ---- time ---- */

static int local_off(void)
{
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_gmtoff || getenv("TZ") ? (int) tm.tm_gmtoff : 3 * 3600; /* no zone set on the device: Türkiye's */
}

static void fmt_when(long t, char *out, int cap)
{
	time_t tt = (time_t) (t + local_off());
	struct tm tm;
	gmtime_r(&tt, &tm);
	snprintf(out, (size_t) cap, "%d %s %02d:%02d", tm.tm_mday, kui_tr_month(tm.tm_mon), tm.tm_hour, tm.tm_min);
}

/* ---- drawing helpers ---- */

/* up to max_lines of text wrapped into box (top-aligned), the last one ending in … when cut; returns the height */
static int draw_wrapped(KuiRect box, const char *text, KuiFont f, int px, int max_lines, unsigned char fg)
{
	static KuiLine lines[200];
	int n = kui_wrap_text(text, box.w, f, px, lines, 200), lh = kui_line_height(f, px);
	if (n > max_lines) n = max_lines;
	for (int i = 0; i < n; i++) {
		char buf[1024];
		int len = lines[i].len < (int) sizeof buf - 4 ? lines[i].len : (int) sizeof buf - 4;
		memcpy(buf, text + lines[i].off, (size_t) len);
		buf[len] = '\0';
		if (i == max_lines - 1 && lines[i].off + lines[i].len < (int) strlen(text)) {
			strcat(buf, "…");
			while (len > 0 && kui_text_width(buf, f, px) > box.w) {
				do len--;
				while (len > 0 && ((unsigned char) buf[len] & 0xC0) == 0x80);
				strcpy(buf + len, "…");
			}
		}
		kui_text((KuiRect){ box.x, box.y + i * lh, box.w, lh }, buf, f, px, KUI_ALIGN_LEFT, fg);
	}
	return n * lh;
}

static void message_box(const char *msg)
{
	int w = kui_width(), h = kui_height();
	KuiRect box = { 80, h / 2 - 150, w - 160, 300 };
	kui_fill(box, 255);
	kui_frame(box, 4, 0);
	kui_text((KuiRect){ box.x + 30, box.y + 20, box.w - 60, box.h - 40 }, msg, KUI_FONT_REG, 40, KUI_ALIGN_CENTER, 0);
	kui_mark_dirty(box);
	kui_flush(0);
}

static void info(const char *title, const char *msg)
{
	const char *ok[] = { T("Tamam") };
	kui_dialog(title, msg, ok, 1, NULL);
}

static const char *folder_name(const char *f) { return f[0] ? f : T("Genel"); }

/* ---- a paged list: rows drawn by a callback; returns a row index, LIST_EXIT, LIST_MENU or LIST_BAR(k) ---- */

enum { LIST_EXIT = -1, LIST_MENU = -2, LIST_QUIT = -3 };
#define LIST_BAR(k) (-100 - (k))
typedef void (*RowFn)(int i, KuiRect r, void *ctx);

static int list_screen(const char *title, const char *menu, int n, RowFn row, void *ctx, const char *const *bar, int nbar, int *page,
		       int full, int *longpress, void (*empty)(void))
{
	int w = kui_width(), h = kui_height(), per = PER_PAGE, pages = akis_pages(n, per);
	if (*page >= pages) *page = pages - 1;
	if (*page < 0) *page = 0;
	for (;;) {
		KuiButton bs[6];
		int nb = 0, nav = pages > 1, bx = 30, bw_nav = 140, x1 = w - 30;
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(title, menu);
		if (!n && empty) empty();
		for (int k = 0; k < per; k++) {
			int i = *page * per + k;
			if (i >= n) break;
			KuiRect r = { 30, LIST_Y + k * ROW_H, w - 60, ROW_H };
			row(i, r, ctx);
			kui_fill((KuiRect){ 30, r.y + ROW_H - 2, w - 60, 2 }, 160);
		}
		if (nav) {
			bs[nb++] = (KuiButton){ { bx, BAR_Y, bw_nav, BAR_H }, "◀", 1 };
			bs[nb++] = (KuiButton){ { x1 - bw_nav, BAR_Y, bw_nav, BAR_H }, "▶", 2 };
			bx += bw_nav + 20;
			x1 -= bw_nav + 20;
		}
		int mw = nbar ? (x1 - bx - (nbar - 1) * 20) / nbar : 0;
		for (int k = 0; k < nbar; k++) bs[nb++] = (KuiButton){ { bx + k * (mw + 20), BAR_Y, mw, BAR_H }, bar[k], 10 + k };
		for (int k = 0; k < nb; k++) kui_button(&bs[k], 0);
		if (nav && !nbar) {
			char s[32];
			snprintf(s, sizeof s, "%d / %d", *page + 1, pages);
			kui_text((KuiRect){ bx, BAR_Y, x1 - bx, BAR_H }, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
		}
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, 0, w, h });
			kui_flush(0);
		}
		full = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) {
				quit = 1;
				return LIST_QUIT;
			}
			if (e.type == KUI_EV_WAKE) { full = 1; break; }
			int d = 0;
			if (e.type == KUI_EV_SWIPE) d = e.dir == KUI_DIR_LEFT || e.dir == KUI_DIR_UP ? 1 : e.dir == KUI_DIR_RIGHT || e.dir == KUI_DIR_DOWN ? -1 : 0;
			if (e.type == KUI_EV_TAP || e.type == KUI_EV_LONGPRESS) {
				int t = kui_topbar_hit(w, menu != NULL, e.x, e.y);
				if (t == KUI_BTN_EXIT) return LIST_EXIT;
				if (t == KUI_BTN_MENU) return LIST_MENU;
				int id = kui_buttons_hit(bs, nb, e.x, e.y);
				if (id == 1) d = -1;
				else if (id == 2) d = 1;
				else if (id >= 10) return LIST_BAR(id - 10);
				else if (e.y >= LIST_Y && e.y < LIST_Y + per * ROW_H) {
					int i = *page * per + (e.y - LIST_Y) / ROW_H;
					if (i < n) {
						if (longpress) *longpress = e.type == KUI_EV_LONGPRESS;
						return i;
					}
				}
			}
			if (d && *page + d >= 0 && *page + d < pages) {
				*page += d;
				break; /* page turn: in place, no flash */
			}
		}
	}
}

/* a list of plain choices (menus, folder picker); returns the index or -1 */
typedef struct { const char *const *labels, *const *subs; const int *checked; } PickCtx;

static void pick_row(int i, KuiRect r, void *vctx)
{
	PickCtx *c = vctx;
	int right = c->checked ? 100 : 0;
	const char *l = c->labels[i];
	if (c->subs && c->subs[i] && c->subs[i][0]) {
		kui_text((KuiRect){ r.x + 10, r.y + 14, r.w - 20 - right, 60 }, l, KUI_FONT_BOLD, kui_fit_px(l, KUI_FONT_BOLD, 40, r.w - 20 - right), KUI_ALIGN_LEFT, 0);
		kui_text((KuiRect){ r.x + 10, r.y + 76, r.w - 20 - right, 40 }, c->subs[i], KUI_FONT_REG, kui_fit_px(c->subs[i], KUI_FONT_REG, 28, r.w - 20 - right),
			 KUI_ALIGN_LEFT, 70);
	} else {
		kui_text((KuiRect){ r.x + 10, r.y, r.w - 20 - right, r.h }, l, KUI_FONT_BOLD, kui_fit_px(l, KUI_FONT_BOLD, 42, r.w - 20 - right), KUI_ALIGN_LEFT, 0);
	}
	if (c->checked) kui_text((KuiRect){ r.x + r.w - right, r.y, right, r.h }, c->checked[i] ? "☑" : "☐", KUI_FONT_REG, 60, KUI_ALIGN_CENTER, 0);
}

static int pick(const char *title, const char *const *labels, const char *const *subs, int n)
{
	PickCtx c = { labels, subs, NULL };
	int page = 0, r = list_screen(title, NULL, n, pick_row, &c, NULL, 0, &page, 1, NULL, NULL);
	return r >= 0 ? r : -1;
}

/* ---- network ---- */

/* Wi-Fi on (asks first when it is off); 1 when connected or unknown (simulator) */
static int ensure_wifi(void)
{
	int on = kui_wifi_enabled();
	if (on != 0) return 1;
	const char *ch[] = { T("Wi-Fi'ı aç"), T("Vazgeç") };
	if (kui_dialog(T("Wi-Fi kapalı"), T("Akışları indirmek için Wi-Fi açılsın mı?"), ch, 2, NULL) != 0) return 0;
	kui_wifi_enable(1);
	wifi_turned_on = 1;
	message_box(T("Wi-Fi bağlanıyor…"));
	return kui_wifi_wait(40);
}

/* downloads url; returns the body (malloc'd) when it is a feed with at least one article */
static char *fetch_feed_body(const char *url, int *len)
{
	char *body = NULL;
	if (net_get(url, &body, len, 25) != 0) return NULL;
	static NewsItem it[NEWS_MAX];
	int k = akis_is_feed(body, *len) ? news_parse_rss(body, *len, it, NEWS_MAX) : 0;
	news_free(it, k);
	if (k > 0) return body;
	free(body);
	return NULL;
}

static int fetch_one(int i)
{
	int len;
	char *body = fetch_feed_body(feeds[i].url, &len);
	failed[i] = !body;
	if (!body) return -1;
	int rc = kui_write_atomic(cache_path(i), body, len);
	free(body);
	return rc;
}

/* fetches the listed feeds with a progress box; a tap stops it after the current feed */
static void refresh(const int *idx, int n)
{
	if (!n || !ensure_wifi()) return;
	int bad = 0, done = 0;
	for (int k = 0; k < n; k++) {
		char s[300];
		snprintf(s, sizeof s, T("Yenileniyor %d / %d\n%s\n\nDurdurmak için dokunun"), k + 1, n, feeds[idx[k]].title);
		message_box(s);
		if (fetch_one(idx[k]) != 0) bad++;
		done++;
		KuiEvent e = kui_next_event(0);
		if (e.type == KUI_EV_QUIT) {
			quit = 1;
			break;
		}
		if (e.type == KUI_EV_TAP) break;
	}
	if (done == n_feeds || n == n_feeds) kui_cfg_set_int(data_path("settings"), "last_refresh", (int) time(NULL));
	recount();
	if (bad && !quit) {
		char s[160];
		snprintf(s, sizeof s, T("%d akış alınamadı.\nWi-Fi bağlantısını kontrol edin."), bad);
		info(T("Yenileme"), s);
	}
}

static void refresh_all(void)
{
	static int idx[AKIS_MAX_FEEDS];
	for (int i = 0; i < n_feeds; i++) idx[i] = i;
	refresh(idx, n_feeds);
}

/* ---- adding feeds ---- */

/* picks a folder for a new feed: the existing ones, Genel or a new name typed in; 0 = chosen */
static int choose_folder(char *out, int cap)
{
	char folders[AKIS_MAX_FOLDERS][AKIS_FOLDER_MAX];
	int nf = akis_folders(feeds, n_feeds, folders, AKIS_MAX_FOLDERS), n = 0;
	const char *labels[AKIS_MAX_FOLDERS + 2];
	int has_general = 0;
	for (int k = 0; k < nf; k++) {
		labels[n++] = folder_name(folders[k]);
		if (!folders[k][0]) has_general = 1;
	}
	int general = -1;
	if (!has_general) {
		general = n;
		labels[n++] = T("Genel");
	}
	int fresh = n;
	labels[n++] = T("Yeni klasör…");
	int r = pick(T("Klasör seçin"), labels, NULL, n);
	if (r < 0) return -1;
	if (r == fresh) {
		out[0] = '\0';
		return kui_input_text(T("Klasör adı"), out, cap, 0, 1, NULL) ? 0 : -1;
	}
	snprintf(out, (size_t) cap, "%s", r == general ? "" : folders[r]);
	return 0;
}

static void add_by_url(void)
{
	char typed[AKIS_URL_MAX] = "", url[AKIS_URL_MAX];
	if (!kui_input_text(T("Akış adresi (RSS / Atom)"), typed, sizeof typed, 0, 4, NULL)) return;
	char fixed[AKIS_URL_MAX];
	akis_url_from_typed(typed, fixed, sizeof fixed);
	if (akis_normalize_url(fixed, url, sizeof url) != 0) {
		info(T("Akış eklenemedi"), T("Bu bir web adresi değil."));
		return;
	}
	if (akis_feed_find(feeds, n_feeds, url) >= 0) {
		info(T("Akış ekle"), T("Bu akış zaten listede."));
		return;
	}
	if (!ensure_wifi()) return;
	kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
	kui_topbar(T("Akış ekle"), NULL);
	kui_flush(1);
	message_box(T("Akış kontrol ediliyor…"));
	int len;
	char *body = fetch_feed_body(url, &len);
	if (!body && !strncmp(url, "https://", 8)) { /* an old site without TLS */
		char alt[AKIS_URL_MAX];
		snprintf(alt, sizeof alt, "http://%s", url + 8);
		if ((body = fetch_feed_body(alt, &len))) snprintf(url, sizeof url, "%s", alt);
	}
	if (!body) {
		info(T("Akış eklenemedi"), T("Bu adreste RSS ya da Atom akışı bulunamadı."));
		return;
	}
	char title[AKIS_TITLE_MAX], folder[AKIS_FOLDER_MAX];
	akis_feed_title(body, len, title, sizeof title);
	if (!title[0]) akis_url_host(url, title, sizeof title);
	if (choose_folder(folder, sizeof folder) != 0) {
		free(body);
		return;
	}
	int i = akis_feed_add(feeds, &n_feeds, AKIS_MAX_FEEDS, folder, title, url);
	if (i >= 0) {
		kui_write_atomic(cache_path(i), body, len);
		save_feeds();
		recount();
	} else {
		info(T("Akış eklenemedi"), T("Akış listesi dolu."));
	}
	free(body);
}

/* ---- catalogue ---- */

static void catalog_screen(void)
{
	const char *cats[16];
	int nc = akis_catalog_cats(cats, 16), ncat;
	const AkisCatFeed *cat = akis_catalog(&ncat);
	for (;;) {
		const char *labels[16];
		for (int k = 0; k < nc; k++) labels[k] = T(cats[k]);
		int c = pick(T("Katalog"), labels, NULL, nc);
		if (c < 0 || quit) return;
		/* the category's feeds: tap to add (into a folder named after the category) or remove */
		int idx[64], n = 0, page = 0, full = 1, added = 0;
		for (int i = 0; i < ncat && n < 64; i++)
			if (!strcmp(cat[i].cat, cats[c])) idx[n++] = i;
		for (;;) {
			const char *names[64], *subs[64];
			int checked[64];
			for (int k = 0; k < n; k++) {
				names[k] = cat[idx[k]].name;
				subs[k] = cat[idx[k]].url;
				checked[k] = akis_feed_find(feeds, n_feeds, cat[idx[k]].url) >= 0;
			}
			PickCtx ctx = { names, subs, checked };
			int r = list_screen(T(cats[c]), NULL, n, pick_row, &ctx, NULL, 0, &page, full, NULL, NULL);
			if (r < 0) break;
			full = 0;
			const AkisCatFeed *e = &cat[idx[r]];
			int have = akis_feed_find(feeds, n_feeds, e->url);
			if (have >= 0) akis_feed_remove(feeds, &n_feeds, have);
			else if (akis_feed_add(feeds, &n_feeds, AKIS_MAX_FEEDS, T(e->cat), e->name, e->url) >= 0) added++;
			save_feeds();
		}
		recount();
		if (quit) return;
		if (added) { /* fetch the new ones now */
			static int todo[AKIS_MAX_FEEDS];
			int nt = 0;
			for (int i = 0; i < n_feeds; i++)
				if (!fetched[i]) todo[nt++] = i;
			refresh(todo, nt);
		}
	}
}

/* ---- OPML ---- */

static void opml_import(void)
{
	static char names[64][256];
	int n = 0;
	DIR *d = opendir(docs_dir());
	struct dirent *de;
	while (d && n < 64 && (de = readdir(d)))
		if (akis_is_opml_name(de->d_name)) snprintf(names[n++], sizeof names[0], "%s", de->d_name);
	if (d) closedir(d);
	if (!n) {
		info(T("OPML içe aktar"), T("documents klasöründe .opml dosyası yok. Wi-Fi ile Yükle ya da USB ile gönderin."));
		return;
	}
	const char *labels[64];
	for (int k = 0; k < n; k++) labels[k] = names[k];
	int r = pick(T("OPML içe aktar"), labels, NULL, n);
	if (r < 0) return;
	char path[600];
	snprintf(path, sizeof path, "%s/%s", docs_dir(), names[r]);
	static char buf[1024 * 1024];
	int len = kui_read_file(path, buf, sizeof buf);
	static AkisFeed in[AKIS_MAX_FEEDS];
	int k = len > 0 ? akis_opml_parse(buf, len, in, AKIS_MAX_FEEDS) : 0, added = 0;
	for (int i = 0; i < k; i++)
		if (akis_feed_add(feeds, &n_feeds, AKIS_MAX_FEEDS, in[i].folder, in[i].title, in[i].url) >= 0) added++;
	save_feeds();
	recount();
	char s[160];
	snprintf(s, sizeof s, T("%d akış eklendi (%d akış dosyada)."), added, k);
	const char *ch[] = { T("Şimdi indir"), T("Sonra") };
	if (added && kui_dialog(T("OPML içe aktar"), s, ch, 2, NULL) == 0) {
		static int todo[AKIS_MAX_FEEDS];
		int nt = 0;
		for (int i = 0; i < n_feeds; i++)
			if (!fetched[i]) todo[nt++] = i;
		refresh(todo, nt);
	} else if (!added) {
		info(T("OPML içe aktar"), s);
	}
}

static void opml_export(void)
{
	static char buf[AKIS_MAX_FEEDS * 800 + 1000];
	int n = akis_opml_format(feeds, n_feeds, buf, sizeof buf);
	char path[600], s[700];
	snprintf(path, sizeof path, "%s/akis.opml", docs_dir());
	if (n > 0 && kui_write_atomic(path, buf, n) == 0) snprintf(s, sizeof s, T("Akışlar %s dosyasına yazıldı."), "documents/akis.opml");
	else snprintf(s, sizeof s, "%s", T("Dosya yazılamadı."));
	info(T("OPML dışa aktar"), s);
}

/* ---- article view ---- */

typedef struct { const char *s; int len; KuiFont f; int px; } RLine;

/* full-screen reader, paged; returns 1 to open the next article, 0 to go back */
static int read_article(const NewsItem *it, const char *source, int has_next)
{
	int w = kui_width(), h = kui_height(), top = KUI_TOPBAR_H + 30, bottom = h - 130, tw = w - 100;
	static RLine ls[1600];
	static KuiLine tmp[800];
	int n = 0;
	char head[200], d[40] = "", link[320];
	news_date_local(it->date, (long) time(NULL), local_off(), d, sizeof d);
	if (d[0]) snprintf(head, sizeof head, "%s · %s", source, d);
	else snprintf(head, sizeof head, "%s", source);
	snprintf(link, sizeof link, T("Kaynak: %s"), it->link);
	const char *body = it->text && it->text[0] ? it->text : T("Bu yazının akışta metni yok; tamamı kaynağında.");
	struct { const char *t; KuiFont f; int px; } parts[] = {
		{ head, KUI_FONT_BOLD, 28 }, { it->title, KUI_FONT_SERIF_BOLD, 48 }, { "", KUI_FONT_REG, 18 },
		{ body, KUI_FONT_SERIF, 38 }, { "", KUI_FONT_REG, 20 }, { link, KUI_FONT_REG, 26 },
	};
	for (size_t p = 0; p < sizeof parts / sizeof parts[0]; p++) {
		int k = parts[p].t[0] ? kui_wrap_text(parts[p].t, tw, parts[p].f, parts[p].px, tmp, 800) : 1;
		for (int i = 0; i < k && n < 1600; i++, n++)
			ls[n] = (RLine){ parts[p].t[0] ? parts[p].t + tmp[i].off : "", parts[p].t[0] ? tmp[i].len : 0, parts[p].f, parts[p].px };
	}
	int starts[200], n_pages = 0;
	for (int i = 0, y = top; i < n; i++) {
		int lh = kui_line_height(ls[i].f, ls[i].px);
		if (i == 0 || y + lh > bottom) {
			if (n_pages == 200) break;
			starts[n_pages++] = i;
			y = top;
		}
		y += lh;
	}
	for (int pg = 0, full = 1;;) {
		int last = pg == n_pages - 1;
		KuiButton bnav[2] = { { { 20, h - 110, 180, 95 }, "◀", 1 },
				      { { w - (last && has_next ? 340 : 200), h - 110, last && has_next ? 320 : 180, 95 }, last && has_next ? T("Sonraki ▶") : "▶", 2 } };
		kui_fill((KuiRect){ 0, 0, w, h }, 255);
		kui_topbar(T("Akış"), NULL);
		int end = pg + 1 < n_pages ? starts[pg + 1] : n;
		for (int i = starts[pg], y = top; i < end; i++) {
			int lh = kui_line_height(ls[i].f, ls[i].px);
			if (ls[i].len) {
				char buf[1024];
				int len = ls[i].len < (int) sizeof buf - 1 ? ls[i].len : (int) sizeof buf - 1;
				memcpy(buf, ls[i].s, (size_t) len);
				buf[len] = '\0';
				kui_text((KuiRect){ 50, y, tw, lh }, buf, ls[i].f, ls[i].px, KUI_ALIGN_LEFT, 0);
			}
			y += lh;
		}
		if (pg == 0) kui_fill((KuiRect){ 50, top + kui_line_height(KUI_FONT_BOLD, 28) - 4, tw, 2 }, 0);
		int show_next = n_pages > 1 || has_next;
		if (show_next) {
			if (pg > 0) kui_button(&bnav[0], 0);
			if (!last || has_next) kui_button(&bnav[1], 0);
			char s[32];
			snprintf(s, sizeof s, "%d / %d", pg + 1, n_pages);
			int gap = w - bnav[1].r.x; /* centred between the two buttons' places */
			kui_text((KuiRect){ gap, h - 110, w - 2 * gap, 95 }, s, KUI_FONT_REG, 34, KUI_ALIGN_CENTER, 0);
		}
		if (full) kui_flush(1);
		else {
			kui_mark_dirty((KuiRect){ 0, KUI_TOPBAR_H, w, h - KUI_TOPBAR_H });
			kui_flush(0);
		}
		full = 0;
		for (;;) {
			KuiEvent e = kui_next_event(-1);
			if (e.type == KUI_EV_QUIT) {
				quit = 1;
				return 0;
			}
			if (e.type == KUI_EV_WAKE) { full = 1; break; }
			int dlt = 0;
			if (e.type == KUI_EV_SWIPE) dlt = e.dir == KUI_DIR_LEFT ? 1 : e.dir == KUI_DIR_RIGHT ? -1 : 0;
			if (e.type == KUI_EV_TAP) {
				if (kui_topbar_hit(w, 0, e.x, e.y) == KUI_BTN_EXIT) return 0;
				int id = show_next ? kui_buttons_hit(bnav, 2, e.x, e.y) : 0;
				dlt = id == 1 ? -1 : id == 2 ? 1 : 0;
			}
			if (dlt > 0 && last && has_next) return 1;
			if (dlt && pg + dlt >= 0 && pg + dlt < n_pages) {
				pg += dlt;
				break; /* a page turn: in place */
			}
		}
	}
}

/* ---- articles of one feed ---- */

typedef struct { int feed; NewsItem *items; } ArtCtx;

static void article_row(int i, KuiRect r, void *vctx)
{
	ArtCtx *c = vctx;
	const NewsItem *it = &c->items[i];
	int is_read = akis_read_has(&rd, key_of(c->feed, it));
	if (!is_read) kui_text((KuiRect){ r.x, r.y + 6, 40, 50 }, "●", KUI_FONT_BOLD, 28, KUI_ALIGN_LEFT, 0);
	draw_wrapped((KuiRect){ r.x + 40, r.y + 8, r.w - 40, 0 }, it->title, is_read ? KUI_FONT_SERIF : KUI_FONT_SERIF_BOLD, 32, 2, is_read ? 90 : 0);
	char d[40] = "";
	news_date_local(it->date, (long) time(NULL), local_off(), d, sizeof d);
	if (d[0]) kui_text((KuiRect){ r.x + 40, r.y + r.h - 38, r.w - 40, 30 }, d, KUI_FONT_REG, 22, KUI_ALIGN_LEFT, 90);
}

static void articles_empty(void)
{
	kui_text((KuiRect){ 60, 400, kui_width() - 120, 300 }, T("Bu akışın kayıtlı yazısı yok.\nYenile'ye dokunun."), KUI_FONT_SERIF, 40, KUI_ALIGN_CENTER, 0);
}

static void articles_screen(int f)
{
	static NewsItem items[NEWS_MAX];
	int n = load_items(f, items), page = 0, full = 1;
	ArtCtx c = { f, items };
	for (;;) {
		const char *bar[] = { T("Yenile"), T("Tümü okundu") };
		int lp = 0, r = list_screen(feeds[f].title, NULL, n, article_row, &c, bar, 2, &page, full, &lp, articles_empty);
		full = 0;
		if (r == LIST_EXIT || r == LIST_QUIT) break;
		if (r == LIST_BAR(0)) {
			refresh(&f, 1);
			news_free(items, n);
			n = load_items(f, items);
			full = 1;
			continue;
		}
		if (r == LIST_BAR(1)) {
			for (int k = 0; k < n; k++) akis_read_add(&rd, key_of(f, &items[k]));
			save_read();
			continue;
		}
		if (r < 0) continue;
		if (lp) { /* long press: read ↔ unread */
			unsigned long long key = key_of(f, &items[r]);
			if (akis_read_has(&rd, key)) akis_read_remove(&rd, key);
			else akis_read_add(&rd, key);
			save_read();
			continue;
		}
		for (int k = r; k < n && !quit; k++) {
			akis_read_add(&rd, key_of(f, &items[k]));
			save_read();
			if (!read_article(&items[k], feeds[f].title, k + 1 < n)) break;
			page = (k + 1) / PER_PAGE;
		}
		if (quit) break;
		full = 1;
	}
	news_free(items, n);
	recount();
}

/* ---- feeds of one folder ---- */

typedef struct { int *idx; } FolderCtx;

static void feed_row(int i, KuiRect r, void *vctx)
{
	FolderCtx *c = vctx;
	int f = c->idx[i];
	char s[160], when[40];
	const char *t = feeds[f].title[0] ? feeds[f].title : feeds[f].url;
	int cw = 150;
	kui_text((KuiRect){ r.x + 10, r.y + 14, r.w - 20 - cw, 60 }, t, KUI_FONT_BOLD, kui_fit_px(t, KUI_FONT_BOLD, 40, r.w - 20 - cw), KUI_ALIGN_LEFT, 0);
	if (failed[f] && fetched[f]) {
		fmt_when(fetched[f], when, sizeof when);
		snprintf(s, sizeof s, T("alınamadı · kayıtlı: %s"), when);
	} else if (failed[f]) {
		snprintf(s, sizeof s, "%s", T("alınamadı"));
	} else if (fetched[f]) {
		fmt_when(fetched[f], when, sizeof when);
		snprintf(s, sizeof s, T("%d yazı · %s"), total[f], when);
	} else {
		snprintf(s, sizeof s, "%s", T("henüz indirilmedi"));
	}
	kui_text((KuiRect){ r.x + 10, r.y + 78, r.w - 20 - cw, 40 }, s, KUI_FONT_REG, kui_fit_px(s, KUI_FONT_REG, 26, r.w - 20 - cw), KUI_ALIGN_LEFT, 70);
	if (unread[f]) {
		KuiRect b = { r.x + r.w - cw + 20, r.y + 33, cw - 30, 66 };
		snprintf(s, sizeof s, "%d", unread[f]);
		kui_fill(b, 0);
		kui_text_on(b, s, KUI_FONT_BOLD, 36, KUI_ALIGN_CENTER, 255, 0);
	}
}

static void feed_menu(int f)
{
	const char *labels[] = { T("Tümünü okundu say"), T("Klasörünü değiştir"), T("Adını değiştir"), T("Akışı sil") };
	const char *subs[] = { NULL, folder_name(feeds[f].folder), feeds[f].title, feeds[f].url };
	int r = pick(feeds[f].title, labels, subs, 4);
	if (r == 0) {
		static NewsItem items[NEWS_MAX];
		int n = load_items(f, items);
		for (int k = 0; k < n; k++) akis_read_add(&rd, key_of(f, &items[k]));
		news_free(items, n);
		save_read();
	} else if (r == 1) {
		char folder[AKIS_FOLDER_MAX];
		if (choose_folder(folder, sizeof folder) == 0) snprintf(feeds[f].folder, sizeof feeds[f].folder, "%s", folder);
	} else if (r == 2) {
		char t[AKIS_TITLE_MAX];
		snprintf(t, sizeof t, "%s", feeds[f].title);
		if (kui_input_text(T("Akışın adı"), t, sizeof t, 0, 1, NULL)) snprintf(feeds[f].title, sizeof feeds[f].title, "%s", t);
	} else if (r == 3) {
		const char *ch[] = { T("Sil"), T("Vazgeç") };
		if (kui_dialog(T("Akışı sil"), feeds[f].title, ch, 2, NULL) == 0) {
			remove(cache_path(f));
			akis_feed_remove(feeds, &n_feeds, f);
		}
	}
	save_feeds();
	recount();
}

static void folder_screen(const char *folder_in)
{
	char folder[AKIS_FOLDER_MAX];
	snprintf(folder, sizeof folder, "%s", folder_in);
	int page = 0, full = 1;
	for (;;) {
		static int idx[AKIS_MAX_FEEDS];
		int n = akis_folder_feeds(feeds, n_feeds, folder, idx, AKIS_MAX_FEEDS);
		if (!n) return;
		FolderCtx c = { idx };
		const char *bar[] = { T("Yenile"), T("Tümü okundu") };
		int lp = 0, r = list_screen(folder_name(folder), NULL, n, feed_row, &c, bar, 2, &page, full, &lp, NULL);
		full = 1;
		if (r == LIST_EXIT || r == LIST_QUIT) return;
		if (r == LIST_BAR(0)) refresh(idx, n);
		else if (r == LIST_BAR(1)) {
			static NewsItem items[NEWS_MAX];
			for (int k = 0; k < n; k++) {
				int m = load_items(idx[k], items);
				for (int j = 0; j < m; j++) akis_read_add(&rd, key_of(idx[k], &items[j]));
				news_free(items, m);
			}
			save_read();
			recount();
			full = 0;
		} else if (r >= 0 && lp) feed_menu(idx[r]);
		else if (r >= 0) articles_screen(idx[r]);
		if (quit) return;
	}
}

/* ---- home: folders ---- */

typedef struct { char (*names)[AKIS_FOLDER_MAX]; } HomeCtx;

static void folder_row(int i, KuiRect r, void *vctx)
{
	HomeCtx *c = vctx;
	int nfe = 0, un = 0;
	for (int f = 0; f < n_feeds; f++)
		if (!strcmp(feeds[f].folder, c->names[i])) nfe++, un += unread[f];
	const char *name = folder_name(c->names[i]);
	char s[96];
	int cw = 150;
	kui_text((KuiRect){ r.x + 10, r.y + 14, r.w - 20 - cw, 64 }, name, KUI_FONT_BOLD, kui_fit_px(name, KUI_FONT_BOLD, 46, r.w - 20 - cw), KUI_ALIGN_LEFT, 0);
	snprintf(s, sizeof s, T("%d akış"), nfe);
	kui_text((KuiRect){ r.x + 10, r.y + 80, r.w - 20 - cw, 38 }, s, KUI_FONT_REG, 26, KUI_ALIGN_LEFT, 70);
	if (un) {
		KuiRect b = { r.x + r.w - cw + 20, r.y + 33, cw - 30, 66 };
		snprintf(s, sizeof s, "%d", un);
		kui_fill(b, 0);
		kui_text_on(b, s, KUI_FONT_BOLD, 36, KUI_ALIGN_CENTER, 255, 0);
	}
}

static void home_empty(void)
{
	int w = kui_width();
	kui_text((KuiRect){ 60, 300, w - 120, 100 }, T("Henüz akış yok"), KUI_FONT_SERIF_BOLD, 56, KUI_ALIGN_CENTER, 0);
	kui_text((KuiRect){ 80, 430, w - 160, 260 },
		 T("Akış ekle'ye dokunun: katalogdan popüler bir akış seçin, bir sitenin RSS adresini yazın ya da documents "
		   "klasörüne attığınız bir .opml dosyasını içe aktarın."),
		 KUI_FONT_SERIF, 38, KUI_ALIGN_CENTER, 0);
}

static void add_menu(void)
{
	const char *labels[] = { T("Katalogdan seç"), T("Adres yaz"), T("OPML içe aktar") };
	const char *subs[] = { T("Türkçe ve dünyadan teknoloji, bilim, blog ve haber akışları"), T("Bir sitenin RSS / Atom adresi"),
			       T("documents klasöründeki .opml dosyası") };
	int r = pick(T("Akış ekle"), labels, subs, 3);
	if (r == 0) catalog_screen();
	else if (r == 1) add_by_url();
	else if (r == 2) opml_import();
}

static void main_menu(void)
{
	const char *labels[] = { T("Akış ekle"), T("Hepsini yenile"), T("Hepsini okundu say"), T("OPML dışa aktar") };
	const char *subs[] = { NULL, NULL, NULL, T("documents/akis.opml: yedek ya da başka bir okuyucu için") };
	int r = pick(T("Menü"), labels, subs, 4);
	if (r == 0) add_menu();
	else if (r == 1) refresh_all();
	else if (r == 2) {
		static NewsItem items[NEWS_MAX];
		for (int f = 0; f < n_feeds; f++) {
			int m = load_items(f, items);
			for (int j = 0; j < m; j++) akis_read_add(&rd, key_of(f, &items[j]));
			news_free(items, m);
		}
		save_read();
		recount();
	} else if (r == 3) opml_export();
}

int main(void)
{
	if (kui_init("akis") != 0) return 1;
	load_all_files();
	recount();
	/* the last fetch is shown at once; with Wi-Fi already on, old feeds are fetched again when the app opens */
	long last = kui_cfg_get_int(data_path("settings"), "last_refresh", 0);
	if (n_feeds && kui_wifi_enabled() == 1 && time(NULL) - last > 60 * 60 && kui_wifi_wait(0)) { /* connected, not just on */
		kui_fill((KuiRect){ 0, 0, kui_width(), kui_height() }, 255);
		kui_topbar(T("Akış"), NULL);
		kui_flush(1);
		refresh_all();
	}
	int page = 0;
	while (!quit) {
		static char names[AKIS_MAX_FOLDERS][AKIS_FOLDER_MAX];
		int nf = akis_folders(feeds, n_feeds, names, AKIS_MAX_FOLDERS);
		HomeCtx c = { names };
		const char *bar[] = { T("Hepsini yenile"), T("Akış ekle") };
		int r = list_screen(T("Akış"), T("Menü"), nf, folder_row, &c, n_feeds ? bar : bar + 1, n_feeds ? 2 : 1, &page, 1, NULL, home_empty);
		if (r == LIST_EXIT || r == LIST_QUIT) break;
		if (r == LIST_MENU) main_menu();
		else if (r == LIST_BAR(0)) n_feeds ? refresh_all() : add_menu();
		else if (r == LIST_BAR(1)) add_menu();
		else if (r >= 0) folder_screen(names[r]);
	}
	if (wifi_turned_on && kui_wifi_enabled() == 1) kui_wifi_enable(0); /* leave Wi-Fi as we found it */
	kui_quit();
	return 0;
}

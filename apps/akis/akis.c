#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "akis.h"
#include "kui.h"
#include "net/net.h"

/* ---- small string helpers ---- */

/* copies at most cap-1 bytes without splitting a UTF-8 sequence; tabs and newlines become spaces, ends trimmed */
static void copy_clean(char *out, int cap, const char *s, int len)
{
	int o = 0, i = 0;
	while (len > 0 && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++, len--;
	for (; i < len && o < cap - 1 && s[i]; i++) {
		char c = s[i];
		if (c == '\t' || c == '\n' || c == '\r') c = ' ';
		out[o++] = c;
	}
	if (i < len && s[i] && ((unsigned char) s[i] & 0xC0) == 0x80) { /* cut inside a character: drop its start */
		while (o > 0 && ((unsigned char) out[o - 1] & 0xC0) == 0x80) o--;
		if (o > 0) o--;
	}
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = '\0';
}

/* ---- feeds file ---- */

int akis_feed_find(const AkisFeed *f, int n, const char *url)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(f[i].url, url)) return i;
	return -1;
}

int akis_feed_add(AkisFeed *f, int *n, int max, const char *folder, const char *title, const char *url)
{
	if (!url || !*url || (int) strlen(url) >= AKIS_URL_MAX || strpbrk(url, " \t\r\n")) return -1;
	if (akis_feed_find(f, *n, url) >= 0) return -2;
	if (*n >= max) return -1;
	AkisFeed *e = &f[*n];
	copy_clean(e->folder, sizeof e->folder, folder ? folder : "", folder ? (int) strlen(folder) : 0);
	copy_clean(e->title, sizeof e->title, title ? title : "", title ? (int) strlen(title) : 0);
	snprintf(e->url, sizeof e->url, "%s", url);
	return (*n)++;
}

void akis_feed_remove(AkisFeed *f, int *n, int idx)
{
	if (idx < 0 || idx >= *n) return;
	memmove(&f[idx], &f[idx + 1], (size_t) (*n - idx - 1) * sizeof *f);
	(*n)--;
}

int akis_feeds_parse(const char *buf, AkisFeed *out, int max)
{
	int n = 0;
	for (const char *p = buf; p && *p;) {
		const char *eol = strchr(p, '\n');
		int len = eol ? (int) (eol - p) : (int) strlen(p);
		const char *t1 = memchr(p, '\t', (size_t) len), *t2 = t1 ? memchr(t1 + 1, '\t', (size_t) (p + len - t1 - 1)) : NULL;
		if (*p != '#' && t2) {
			char folder[AKIS_FOLDER_MAX], title[AKIS_TITLE_MAX], url[AKIS_URL_MAX];
			copy_clean(folder, sizeof folder, p, (int) (t1 - p));
			copy_clean(title, sizeof title, t1 + 1, (int) (t2 - t1 - 1));
			copy_clean(url, sizeof url, t2 + 1, (int) (p + len - t2 - 1));
			if (url[0]) akis_feed_add(out, &n, max, folder, title, url);
		}
		p = eol ? eol + 1 : NULL;
	}
	return n;
}

int akis_feeds_format(const AkisFeed *f, int n, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "# Akış: klasör<TAB>başlık<TAB>adres\n");
	if (o >= cap) return -1;
	for (int i = 0; i < n; i++) {
		int k = snprintf(buf + o, (size_t) (cap - o), "%s\t%s\t%s\n", f[i].folder, f[i].title, f[i].url);
		if (k >= cap - o) return -1;
		o += k;
	}
	return o;
}

/* ---- folders ---- */

int akis_folders(const AkisFeed *f, int n, char out[][AKIS_FOLDER_MAX], int max)
{
	int k = 0;
	for (int i = 0; i < n && k < max; i++) {
		int seen = 0;
		for (int j = 0; j < k && !seen; j++) seen = !strcmp(out[j], f[i].folder);
		if (!seen) snprintf(out[k++], AKIS_FOLDER_MAX, "%s", f[i].folder);
	}
	return k;
}

int akis_folder_feeds(const AkisFeed *f, int n, const char *folder, int *idx, int max)
{
	int k = 0;
	for (int i = 0; i < n && k < max; i++)
		if (!strcmp(f[i].folder, folder)) idx[k++] = i;
	return k;
}

/* ---- ids ---- */

unsigned long long akis_hash(const char *s)
{
	unsigned long long h = 1469598103934665603ULL;
	for (; s && *s; s++) {
		h ^= (unsigned char) *s;
		h *= 1099511628211ULL;
	}
	return h;
}

void akis_feed_id(const char *url, char out[17]) { snprintf(out, 17, "%016llx", akis_hash(url)); }

unsigned long long akis_item_key(const char *feed_url, const char *link, const char *title)
{
	/* the link identifies an article; titles get corrected, so they are only the fallback */
	unsigned long long h = akis_hash(feed_url);
	const char *s = link && *link ? link : title ? title : "";
	h ^= 0xff;
	h *= 1099511628211ULL;
	for (; *s; s++) {
		h ^= (unsigned char) *s;
		h *= 1099511628211ULL;
	}
	return h ? h : 1;
}

/* ---- URLs ---- */

int akis_normalize_url(const char *in, char *out, int cap)
{
	while (*in == ' ' || *in == '\t' || *in == '\n' || *in == '\r') in++;
	int len = (int) strlen(in);
	while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t' || in[len - 1] == '\n' || in[len - 1] == '\r')) len--;
	if (len == 0) return -1;
	for (int i = 0; i < len; i++)
		if (in[i] == ' ' || in[i] == '\t' || (unsigned char) in[i] < 0x20) return -1;
	char tmp[AKIS_URL_MAX + 16];
	if (len >= (int) sizeof tmp - 10) return -1;
	memcpy(tmp, in, (size_t) len);
	tmp[len] = '\0';
	const char *s = tmp;
	if (!strncasecmp(s, "feed:", 5)) { /* feed://host/…, feed:https://host/… */
		s += 5;
		if (!strncmp(s, "//", 2)) s += 2;
	}
	char scheme[8] = "https";
	const char *rest = s, *sep = strstr(s, "://");
	if (sep) {
		int sl = (int) (sep - s);
		if (sl == 4 && !strncasecmp(s, "http", 4)) strcpy(scheme, "http");
		else if (sl == 5 && !strncasecmp(s, "https", 5)) strcpy(scheme, "https");
		else return -1;
		rest = sep + 3;
	}
	int hl = (int) strcspn(rest, "/?#");
	if (hl == 0) return -1;
	char host[256];
	if (hl >= (int) sizeof host) return -1;
	for (int i = 0; i < hl; i++) host[i] = (char) tolower((unsigned char) rest[i]);
	host[hl] = '\0';
	if (!strchr(host, '.') && strncmp(host, "localhost", 9) != 0) return -1;
	if (host[0] == '.' || host[hl - 1] == '.' || strchr(host, '@')) return -1;
	int n = snprintf(out, (size_t) cap, "%s://%s%s", scheme, host, rest + hl);
	return n < cap && n < AKIS_URL_MAX ? 0 : -1;
}

void akis_url_host(const char *url, char *out, int cap)
{
	const char *s = strstr(url, "://");
	s = s ? s + 3 : url;
	if (!strncasecmp(s, "www.", 4)) s += 4;
	int hl = (int) strcspn(s, "/?#:");
	if (hl >= cap) hl = cap - 1;
	memcpy(out, s, (size_t) hl);
	out[hl] = '\0';
}

void akis_url_from_typed(const char *in, char *out, int cap)
{
	int o = 0;
	for (const char *s = in; *s && o < cap - 1; s++) {
		if ((unsigned char) s[0] == 0xC4 && ((unsigned char) s[1] == 0xB1 || (unsigned char) s[1] == 0xB0)) { /* ı, İ */
			out[o++] = 'i';
			s++;
		} else { /* a whole character or nothing */
			unsigned char c = (unsigned char) *s;
			int n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
			if (o + n > cap - 1) break;
			int k = 0;
			while (k < n && s[k]) out[o++] = s[k++];
			s += k - 1; /* a truncated sequence at the end stops at its NUL */
		}
	}
	out[o] = '\0';
}

/* ---- feed documents ---- */

static const char *find_bytes(const char *s, int len, const char *needle)
{
	int nl = (int) strlen(needle);
	for (int i = 0; i + nl <= len; i++)
		if (s[i] == needle[0] && !memcmp(s + i, needle, (size_t) nl)) return s + i;
	return NULL;
}

int akis_is_feed(const char *xml, int len)
{
	XmlEl el;
	const char *end = xml + len;
	return xml_next(xml, end, "rss", &el) || xml_next(xml, end, "feed", &el) || xml_next(xml, end, "rdf:RDF", &el) ||
	       xml_next(xml, end, "RDF", &el) || find_bytes(xml, len, "<channel") != NULL;
}

void akis_feed_title(const char *xml, int len, char *out, int cap)
{
	out[0] = '\0';
	const char *end = xml + len, *item = find_bytes(xml, len, "<item"), *entry = find_bytes(xml, len, "<entry");
	const char *first = item && (!entry || item < entry) ? item : entry;
	XmlEl t;
	const char *after = xml_next(xml, end, "title", &t);
	if (!after || (first && t.inner > first)) return; /* only an article's title: none of the feed's own */
	char raw[512];
	xml_text(t.inner, t.inner_end, raw, sizeof raw);
	copy_clean(out, cap, raw, (int) strlen(raw));
}

/* ---- read state ---- */

void akis_read_parse(const char *buf, AkisRead *r)
{
	r->n = 0;
	for (const char *p = buf; p && *p;) {
		char *e;
		unsigned long long k = strtoull(p, &e, 16);
		if (e != p && k && !akis_read_has(r, k)) akis_read_add(r, k);
		p = strchr(p, '\n');
		if (p) p++;
	}
}

int akis_read_format(const AkisRead *r, char *buf, int cap)
{
	int o = 0;
	if (cap < 1) return -1;
	buf[0] = '\0';
	for (int i = 0; i < r->n; i++) {
		int k = snprintf(buf + o, (size_t) (cap - o), "%016llx\n", r->k[i]);
		if (k >= cap - o) return -1;
		o += k;
	}
	return o;
}

int akis_read_has(const AkisRead *r, unsigned long long k)
{
	for (int i = r->n - 1; i >= 0; i--)
		if (r->k[i] == k) return 1;
	return 0;
}

void akis_read_add(AkisRead *r, unsigned long long k)
{
	if (akis_read_has(r, k)) return;
	if (r->n == AKIS_READ_MAX) {
		memmove(r->k, r->k + 1, (size_t) (AKIS_READ_MAX - 1) * sizeof r->k[0]);
		r->n--;
	}
	r->k[r->n++] = k;
}

void akis_read_remove(AkisRead *r, unsigned long long k)
{
	int o = 0;
	for (int i = 0; i < r->n; i++)
		if (r->k[i] != k) r->k[o++] = r->k[i];
	r->n = o;
}

int akis_read_prune(AkisRead *r, const unsigned long long *keep, int nk)
{
	int o = 0;
	for (int i = 0; i < r->n; i++) {
		int found = 0;
		for (int j = 0; j < nk && !found; j++) found = keep[j] == r->k[i];
		if (found) r->k[o++] = r->k[i];
	}
	int dropped = r->n - o;
	r->n = o;
	return dropped;
}

/* ---- OPML ---- */

static int outline_attr(const XmlEl *el, const char *a, const char *b, char *out, int cap)
{
	return xml_attr(el, a, out, cap) || xml_attr(el, b, out, cap);
}

static void opml_walk(const char *p, const char *end, const char *folder, AkisFeed *out, int *n, int max, int depth)
{
	XmlEl el;
	while (*n < max && (p = xml_next(p, end, "outline", &el))) {
		char url[AKIS_URL_MAX], title[AKIS_TITLE_MAX] = "", norm[AKIS_URL_MAX];
		if (!outline_attr(&el, "title", "text", title, sizeof title)) title[0] = '\0';
		if (outline_attr(&el, "xmlUrl", "xmlurl", url, sizeof url)) {
			if (akis_normalize_url(url, norm, sizeof norm) == 0) {
				if (!title[0]) akis_url_host(norm, title, sizeof title);
				akis_feed_add(out, n, max, folder, title, norm);
			}
		} else if (depth < 4 && el.inner < el.inner_end) { /* a folder: its feeds */
			char t[AKIS_FOLDER_MAX] = "";
			if (!outline_attr(&el, "text", "title", t, sizeof t)) t[0] = '\0';
			opml_walk(el.inner, el.inner_end, t[0] ? t : folder, out, n, max, depth + 1);
		}
	}
}

int akis_opml_parse(const char *xml, int len, AkisFeed *out, int max)
{
	const char *end = xml + len;
	XmlEl body;
	int n = 0;
	if (xml_next(xml, end, "body", &body)) opml_walk(body.inner, body.inner_end, "", out, &n, max, 0);
	else opml_walk(xml, end, "", out, &n, max, 0);
	return n;
}

static int put_esc(char *buf, int cap, int o, const char *s)
{
	for (; *s && o >= 0; s++) {
		const char *r = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : NULL;
		int k = r ? (int) strlen(r) : 1;
		if (o + k >= cap) return -1;
		if (r) memcpy(buf + o, r, (size_t) k);
		else buf[o] = *s;
		o += k;
	}
	if (o >= 0) buf[o] = '\0';
	return o;
}

static int put_str(char *buf, int cap, int o, const char *s)
{
	int k = (int) strlen(s);
	if (o < 0 || o + k >= cap) return -1;
	memcpy(buf + o, s, (size_t) k + 1);
	return o + k;
}

int akis_opml_format(const AkisFeed *f, int n, char *buf, int cap)
{
	char folders[AKIS_MAX_FOLDERS][AKIS_FOLDER_MAX];
	int nf = akis_folders(f, n, folders, AKIS_MAX_FOLDERS), o = 0;
	o = put_str(buf, cap, o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<opml version=\"2.0\">\n<head><title>Akış</title></head>\n<body>\n");
	for (int k = 0; k < nf; k++) {
		const char *ind = folders[k][0] ? "  " : "";
		if (folders[k][0]) {
			o = put_str(buf, cap, o, "<outline text=\"");
			o = put_esc(buf, cap, o, folders[k]);
			o = put_str(buf, cap, o, "\">\n");
		}
		for (int i = 0; i < n; i++) {
			if (strcmp(f[i].folder, folders[k]) != 0) continue;
			o = put_str(buf, cap, o, ind);
			o = put_str(buf, cap, o, "<outline type=\"rss\" text=\"");
			o = put_esc(buf, cap, o, f[i].title);
			o = put_str(buf, cap, o, "\" xmlUrl=\"");
			o = put_esc(buf, cap, o, f[i].url);
			o = put_str(buf, cap, o, "\"/>\n");
		}
		if (folders[k][0]) o = put_str(buf, cap, o, "</outline>\n");
	}
	/* feeds of folders beyond AKIS_MAX_FOLDERS: at the top level */
	for (int i = 0; i < n; i++) {
		int listed = 0;
		for (int k = 0; k < nf && !listed; k++) listed = !strcmp(f[i].folder, folders[k]);
		if (listed) continue;
		o = put_str(buf, cap, o, "<outline type=\"rss\" text=\"");
		o = put_esc(buf, cap, o, f[i].title);
		o = put_str(buf, cap, o, "\" xmlUrl=\"");
		o = put_esc(buf, cap, o, f[i].url);
		o = put_str(buf, cap, o, "\"/>\n");
	}
	o = put_str(buf, cap, o, "</body>\n</opml>\n");
	return o;
}

int akis_is_opml_name(const char *name)
{
	size_t n = strlen(name);
	return name[0] != '.' && n > 5 && !strcasecmp(name + n - 5, ".opml");
}

/* ---- catalogue ---- */

static const AkisCatFeed catalog[] = {
	/* Turkish */
	{ N_("Teknoloji (Türkçe)"), "Webtekno", "https://www.webtekno.com/rss.xml" },
	{ N_("Teknoloji (Türkçe)"), "ShiftDelete.Net", "https://shiftdelete.net/feed" },
	{ N_("Teknoloji (Türkçe)"), "DonanımHaber", "https://www.donanimhaber.com/rss/tum/" },
	{ N_("Teknoloji (Türkçe)"), "Webrazzi", "https://webrazzi.com/feed/" },
	{ N_("Teknoloji (Türkçe)"), "Teknoblog", "https://www.teknoblog.com/feed/" },
	{ N_("Teknoloji (Türkçe)"), "LOG", "https://www.log.com.tr/feed/" },
	{ N_("Bilim (Türkçe)"), "Evrim Ağacı", "https://evrimagaci.org/rss.xml" },
	{ N_("Bilim (Türkçe)"), "Kozmik Anafor", "https://www.kozmikanafor.com/feed/" },
	{ N_("Bilim (Türkçe)"), "Açık Bilim", "https://www.acikbilim.com/feed" },
	{ N_("Bilim (Türkçe)"), "BBC Türkçe", "https://feeds.bbci.co.uk/turkce/rss.xml" },
	{ N_("Kültür ve blog (Türkçe)"), "Medyascope", "https://medyascope.tv/feed/" },
	{ N_("Kültür ve blog (Türkçe)"), "Fikir Turu", "https://fikirturu.com/feed/" },
	{ N_("Kültür ve blog (Türkçe)"), "Serbestiyet", "https://serbestiyet.com/feed/" },
	{ N_("Kültür ve blog (Türkçe)"), "DW Türkçe", "https://rss.dw.com/xml/rss-tur-all" },
	/* world */
	{ N_("Teknoloji (dünya)"), "Ars Technica", "https://feeds.arstechnica.com/arstechnica/index" },
	{ N_("Teknoloji (dünya)"), "The Verge", "https://www.theverge.com/rss/index.xml" },
	{ N_("Teknoloji (dünya)"), "Hacker News", "https://hnrss.org/frontpage" },
	{ N_("Teknoloji (dünya)"), "TechCrunch", "https://techcrunch.com/feed/" },
	{ N_("Teknoloji (dünya)"), "WIRED", "https://www.wired.com/feed/rss" },
	{ N_("Teknoloji (dünya)"), "LWN.net", "https://lwn.net/headlines/rss" },
	{ N_("Teknoloji (dünya)"), "Slashdot", "https://rss.slashdot.org/Slashdot/slashdotMain" },
	{ N_("Bilim (dünya)"), "Quanta Magazine", "https://www.quantamagazine.org/feed/" },
	{ N_("Bilim (dünya)"), "NASA", "https://www.nasa.gov/feed/" },
	{ N_("Bilim (dünya)"), "ScienceDaily", "https://www.sciencedaily.com/rss/all.xml" },
	{ N_("Bilim (dünya)"), "Nature", "https://www.nature.com/nature.rss" },
	{ N_("Bilim (dünya)"), "New Scientist", "https://www.newscientist.com/feed/home/" },
	{ N_("Bilim (dünya)"), "Phys.org", "https://phys.org/rss-feed/" },
	{ N_("Bloglar (dünya)"), "xkcd", "https://xkcd.com/atom.xml" },
	{ N_("Bloglar (dünya)"), "Daring Fireball", "https://daringfireball.net/feeds/main" },
	{ N_("Bloglar (dünya)"), "kottke.org", "https://feeds.kottke.org/main" },
	{ N_("Bloglar (dünya)"), "Simon Willison", "https://simonwillison.net/atom/everything/" },
	{ N_("Bloglar (dünya)"), "Julia Evans", "https://jvns.ca/atom.xml" },
	{ N_("Bloglar (dünya)"), "Marginal Revolution", "https://marginalrevolution.com/feed" },
	{ N_("Haber (dünya)"), "BBC News", "https://feeds.bbci.co.uk/news/world/rss.xml" },
	{ N_("Haber (dünya)"), "The Guardian", "https://www.theguardian.com/world/rss" },
	{ N_("Haber (dünya)"), "NPR", "https://feeds.npr.org/1001/rss.xml" },
	{ N_("Haber (dünya)"), "Al Jazeera", "https://www.aljazeera.com/xml/rss/all.xml" },
};

const AkisCatFeed *akis_catalog(int *n)
{
	*n = (int) (sizeof catalog / sizeof catalog[0]);
	return catalog;
}

int akis_catalog_cats(const char **out, int max)
{
	int k = 0;
	for (size_t i = 0; i < sizeof catalog / sizeof catalog[0] && k < max; i++) {
		int seen = 0;
		for (int j = 0; j < k && !seen; j++) seen = !strcmp(out[j], catalog[i].cat);
		if (!seen) out[k++] = catalog[i].cat;
	}
	return k;
}

int akis_pages(int n, int per_page) { return per_page <= 0 || n <= 0 ? 1 : (n + per_page - 1) / per_page; }

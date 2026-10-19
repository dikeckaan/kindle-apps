/* Tarifler: recipe sets (bundled, cached and fetched), search, quantity scaling, step timers, favourites. Pure logic. */
#ifndef TARIF_H
#define TARIF_H

/* ---- recipe sets ----
 * Text format (UTF-8), one recipe per "@R" line followed by its lines; '#' lines before a record are comments:
 *   @R<TAB>lang<TAB>title<TAB>cuisine<TAB>tags<TAB>servings<TAB>url<TAB>licence
 *   =intro      +ingredient group heading      -ingredient      *step
 * cuisine: the country's Turkish name ("Türkiye", "İtalya"); servings 0 = unknown.
 * tags: c soup, a main dish, t dessert, s salad/meze, h pastry/bread, k breakfast, i drink, v vegan, g vegetarian. */
typedef struct {
	const char *lang, *title, *cuisine, *tags, *url, *licence, *intro;
	int servings;
	int line0, n_lines; /* ingredient lines (headings included) in TarifDb.line/kind */
	int n_ing;          /* ingredients without the headings */
	int step0, n_steps;
} TarifRecipe;
typedef struct {
	char *buf;
	TarifRecipe *r;
	int n;
	const char **line; /* ingredient lines and steps, without their prefix */
	char *kind;        /* '+' heading, '-' ingredient, '*' step */
	int n_line;
} TarifDb;
int  tarif_parse(char *buf, TarifDb *db); /* in place (buf is kept, freed by tarif_free); recipe count, -1 malformed */
int  tarif_load(const char *path, TarifDb *db); /* -1 if missing or malformed */
void tarif_free(TarifDb *db);
int  tarif_find_url(const TarifDb *db, const char *url); /* index or -1 */
int  tarif_has_tag(const TarifRecipe *r, char tag);
int  tarif_base_servings(const TarifRecipe *r); /* servings, or 4 when unknown */

/* ---- writing one record (for the cache of recipes seen online) ---- */
typedef struct { char *s; int len, cap; } TarifBuf; /* grows with realloc; s NUL-terminated */
void tarif_buf_add(TarifBuf *b, const char *s, int n); /* n < 0: strlen */
void tarif_buf_free(TarifBuf *b);
/* "@R …" header line; tab/newline characters inside fields become spaces */
void tarif_rec_head(TarifBuf *b, const char *lang, const char *title, const char *cuisine, const char *tags, int servings, const char *url,
		    const char *licence);
void tarif_rec_line(TarifBuf *b, char kind, const char *text, int n); /* one '=', '+', '-' or '*' line (n < 0: strlen) */

/* ---- text folding and search ---- */
/* lower case by Turkish rules, Turkish letters and accents folded to ASCII (ç→c, ğ→g, ı/İ→i, ö→o, ş→s, ü→u, â→a, é→e …),
 * everything that is not a letter or digit becomes one space; trimmed */
void tarif_fold(const char *in, char *out, int cap);
/* recipes whose title or cuisine has every query word as a word prefix; title prefix matches first, then
 * Turkish-language recipes, then by title. Returns the count (<= max). */
int  tarif_search(const TarifDb *db, const char *query, int *out, int max);
/* "Elimde ne var": query words (comma/space separated; Turkish words also match their English names) are looked up
 * in the ingredient lines; recipes using at least one, most matched first, then Turkish-language ones, then fewest other ingredients.
 * matched[i] (may be NULL) = how many query words recipe out[i] uses. Returns the count. *n_words = query words. */
int  tarif_by_ingredients(const TarifDb *db, const char *query, int *out, int *matched, int max, int *n_words);
/* recipes with cuisine (NULL = any; "!Türkiye" = any but) and tag (0 = any), Turkish-language first, then title */
int  tarif_filter(const TarifDb *db, const char *cuisine, char tag, int *out, int max);
/* distinct cuisines but "Türkiye", by recipe count (desc) then name; returns the count */
int  tarif_cuisines(const TarifDb *db, const char **names, int *counts, int max);
/* English words for a Turkish query (for searching English sources): words with a known translation are replaced,
 * others kept; returns 1 when anything was translated */
int  tarif_translate_query(const char *query, char *out, int cap);

/* ---- quantities ---- */
/* the quantity at the start of s ("2", "1,5", "1.5", "½", "1 ½", "1/2", "2-3", "yarım", "bir buçuk", …):
 * returns its length in bytes (0 = none) with *lo and *hi (equal unless a range) and *frac = 1 when written as a fraction */
int  tarif_qty_parse(const char *s, double *lo, double *hi, int *frac);
/* v as text: whole numbers plain, halves/quarters/thirds as ½ ¼ ¾ ⅓ ⅔ when frac (or below 1), otherwise one decimal
 * with comma (Turkish) or point */
void tarif_qty_format(double v, int frac, int comma, char *out, int cap);
/* an ingredient line with its quantity multiplied by f: the leading quantity, or else a number followed by a unit
 * ("Mercimek 30gr"); lines without one are copied. comma: decimal comma. Returns 1 when something was scaled. */
int  tarif_scale_line(const char *line, double f, int comma, char *out, int cap);

/* ---- step timers ---- */
/* the cooking time a step mentions, in seconds ("30 dk", "25 dakika", "1 saat", "1,5 saat", "10 minutes", "1 hour",
 * "2-3 dk" → the upper end, "45 saniye"); 0 when none. Temperatures and amounts are not times. */
int  tarif_step_seconds(const char *step);
#define TARIF_MAX_TIMERS 6
typedef struct { char name[48]; long end; int dur; int done; } TarifTimer; /* end: epoch seconds; done: alarm dismissed */
typedef struct { TarifTimer t[TARIF_MAX_TIMERS]; int n; } TarifTimers;
int  tarif_timer_add(TarifTimers *ts, const char *name, int seconds, long now); /* index, -1 when full */
void tarif_timer_remove(TarifTimers *ts, int i);
long tarif_timer_left(const TarifTimer *t, long now); /* seconds left, 0 once due */
int  tarif_timers_ringing(const TarifTimers *ts, long now); /* first due and not dismissed timer, -1 */
void tarif_fmt_clock(long s, char *out, int cap); /* "4:05", "1:02:09" */
int  tarif_timers_format(const TarifTimers *ts, char *out, int cap); /* "end dur done name" lines; bytes, -1 */
void tarif_timers_parse(const char *buf, TarifTimers *ts);

/* ---- key lists (favourites, recently viewed): urls, newest last ---- */
#define TARIF_MAX_KEYS 500
typedef struct { char *key[TARIF_MAX_KEYS]; int n; } TarifKeys;
void tarif_keys_parse(const char *buf, TarifKeys *k); /* blanks and duplicates dropped */
int  tarif_keys_has(const TarifKeys *k, const char *key);
int  tarif_keys_toggle(TarifKeys *k, const char *key); /* 1 added, 0 removed, -1 full */
void tarif_keys_touch(TarifKeys *k, const char *key, int keep); /* moves/adds key to the end, keeps the newest `keep` */
int  tarif_keys_format(const TarifKeys *k, char *out, int cap); /* bytes, -1 if too small */
void tarif_keys_free(TarifKeys *k);

/* ---- online sources ---- */
/* A Wikibooks recipe page (wikitext) as one record: sections whose heading names ingredients (Malzemeler, Gerekli…,
 * Ingredients) and method (Hazırlanışı, Yapılışı, Procedure, Directions…); links keep their label, templates,
 * references, files and tables are dropped. lang "tr" / "en". Returns 0, or -1 when there is no ingredient list
 * or no step (an index or article page). */
int  tarif_wiki_record(TarifBuf *b, const char *wikitext, const char *lang, const char *title, const char *url);
/* titles from a MediaWiki list=search JSON answer (formatversion 1 or 2); returns the count */
int  tarif_wiki_search_titles(const char *json, char (*titles)[160], int max);
/* TheMealDB: every meal of a search.php / lookup.php answer as records (ingredients "measure name", instructions
 * split into steps, area as the Turkish country name, category as tags); returns the count */
int  tarif_mealdb_records(TarifBuf *b, const char *json);
/* meals of a filter.php answer: ids and names; returns the count */
int  tarif_mealdb_list(const char *json, char (*ids)[16], char (*names)[160], int max);
const char *tarif_area_tr(const char *area); /* "Italian" → "İtalya"; unknown → area */
/* page URL of a wiki title as the bundled set writes it: spaces → '_', bytes outside A-Z a-z 0-9 -._~:/(),' as %XX */
void tarif_wiki_url(const char *host, const char *title, char *out, int cap);
#endif

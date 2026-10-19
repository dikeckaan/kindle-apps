#include <math.h>
#include <stdlib.h>
#include "t.h"
#include "tarif/tarif.h"

static const char *SAMPLE =
	"# comment\n"
	"@R\ttr\tMercimek Çorbası\tTürkiye\tcvg\t4\thttps://tr.wikibooks.org/wiki/Yemek:Mercimek\tCC BY-SA 4.0\n"
	"=Klasik bir çorba.\n"
	"-1 su bardağı kırmızı mercimek\n"
	"-1 adet soğan\n"
	"-2 yemek kaşığı tereyağı\n"
	"*Soğanı kavurun.\n"
	"*Mercimeği ekleyip 30 dk pişirin.\n"
	"@R\ten\tLentil Soup\tHindistan\tc\t0\thttps://en.wikibooks.org/wiki/Cookbook:Lentil_Soup\tCC BY-SA 4.0\n"
	"+Soup\n"
	"-1 cup red lentils\n"
	"-1 onion\n"
	"-2 tomatoes\n"
	"*Boil for 20 minutes.\n"
	"@R\ttr\tİmam Bayıldı\tTürkiye\tavg\t0\thttps://tr.wikibooks.org/wiki/Yemek:Imam\tCC BY-SA 4.0\n"
	"-4 patlıcan\n"
	"-2 soğan\n"
	"-3 domates\n"
	"-½ su bardağı zeytinyağı\n"
	"*Patlıcanları kızartın.\n"
	"@R\ttr\tFettuccine Alfredo\tİtalya\ta\t2\thttps://tr.wikibooks.org/wiki/Yemek:F\tCC BY-SA 4.0\n"
	"-200 gr makarna\n"
	"-1 su bardağı krema\n"
	"*Haşlayın.\n";

static void load(TarifDb *db)
{
	char *b = strdup(SAMPLE);
	T_EQ_INT(tarif_parse(b, db), 4);
}

static void test_parse(void)
{
	TarifDb db;
	load(&db);
	const TarifRecipe *r = &db.r[0];
	T_EQ_STR(r->title, "Mercimek Çorbası");
	T_EQ_STR(r->intro, "Klasik bir çorba.");
	T_EQ_INT(r->n_ing, 3);
	T_EQ_INT(r->n_lines, 3);
	T_EQ_INT(r->n_steps, 2);
	T_EQ_STR(db.line[r->step0 + 1], "Mercimeği ekleyip 30 dk pişirin.");
	T_EQ_INT(r->servings, 4);
	r = &db.r[1];
	T_EQ_INT(r->n_lines, 4);
	T_EQ_INT(r->n_ing, 3);
	T_EQ_INT(db.kind[r->line0], '+');
	T_EQ_STR(db.line[r->line0], "Soup");
	T_EQ_INT(tarif_base_servings(r), 4);
	T_ASSERT(tarif_has_tag(&db.r[0], 'v'));
	T_ASSERT(!tarif_has_tag(&db.r[1], 'v'));
	T_EQ_INT(tarif_find_url(&db, "https://tr.wikibooks.org/wiki/Yemek:Imam"), 2);
	T_EQ_INT(tarif_find_url(&db, "x"), -1);
	tarif_free(&db);
	char bad1[] = "@R\ttr\tonly three\tfields\n";
	T_EQ_INT(tarif_parse(bad1, &db), -1);
	char bad2[] = "@R\ttr\tA\tT\t\t0\tu\tl\n*step\n-ingredient after a step\n";
	T_EQ_INT(tarif_parse(bad2, &db), -1);
	char bad3[] = "no record\n";
	T_EQ_INT(tarif_parse(bad3, &db), -1);
}

static void test_fold(void)
{
	char o[100];
	tarif_fold("  Şiş KEBABI, İmam—bayıldı!  ", o, sizeof o);
	T_EQ_STR(o, "sis kebabi imam bayildi");
	tarif_fold("Crème brûlée", o, sizeof o);
	T_EQ_STR(o, "creme brulee");
	tarif_fold("i̇şkembe", o, sizeof o); /* i + combining dot (Python's lower("İ")) */
	T_EQ_STR(o, "iskembe");
}

static void test_search(void)
{
	TarifDb db;
	load(&db);
	int out[10];
	int n = tarif_search(&db, "çorba", out, 10);
	T_EQ_INT(n, 1);
	T_EQ_INT(out[0], 0);
	n = tarif_search(&db, "imam bayıldı", out, 10);
	T_EQ_INT(n, 1);
	T_EQ_INT(out[0], 2);
	n = tarif_search(&db, "SOUP", out, 10);
	T_EQ_INT(n, 1);
	n = tarif_search(&db, "italya", out, 10); /* by cuisine */
	T_EQ_INT(n, 1);
	T_EQ_INT(out[0], 3);
	T_EQ_INT(tarif_search(&db, "orba", out, 10), 0); /* word prefixes only */
	T_EQ_INT(tarif_search(&db, "", out, 10), 0);
	tarif_free(&db);
}

static void test_ingredients(void)
{
	TarifDb db;
	load(&db);
	int out[10], m[10], nw;
	int n = tarif_by_ingredients(&db, "soğan, domates", out, m, 10, &nw);
	T_EQ_INT(nw, 2);
	T_EQ_INT(n, 3); /* Mercimek (soğan), Lentil (onion, tomatoes), İmam (soğan, domates) */
	T_EQ_INT(m[0], 2);
	T_EQ_INT(m[1], 2);
	T_EQ_INT(out[0], 2); /* both use two: the Turkish-language one first */
	T_EQ_INT(m[2], 1);
	T_EQ_INT(out[2], 0);
	n = tarif_by_ingredients(&db, "mercimek", out, m, 10, &nw);
	T_EQ_INT(n, 2); /* lentil too */
	n = tarif_by_ingredients(&db, "ve", out, m, 10, &nw);
	T_EQ_INT(nw, 0);
	T_EQ_INT(n, 0);
	n = tarif_by_ingredients(&db, "zeytinyağı", out, m, 10, &nw);
	T_EQ_INT(n, 1);
	T_EQ_INT(out[0], 2);
	tarif_free(&db);
}

static void test_filter(void)
{
	TarifDb db;
	load(&db);
	int out[10];
	T_EQ_INT(tarif_filter(&db, "Türkiye", 0, out, 10), 2);
	T_EQ_INT(out[0], 2); /* İmam < Mercimek by folded title */
	T_EQ_INT(tarif_filter(&db, "!Türkiye", 0, out, 10), 2);
	T_EQ_INT(out[0], 3); /* Turkish-language first */
	T_EQ_INT(tarif_filter(&db, NULL, 'c', out, 10), 2);
	T_EQ_INT(tarif_filter(&db, NULL, 'v', out, 10), 2);
	const char *names[10];
	int counts[10];
	T_EQ_INT(tarif_cuisines(&db, names, counts, 10), 2);
	T_EQ_STR(names[0], "Hindistan");
	T_EQ_STR(names[1], "İtalya");
	tarif_free(&db);
	char q[100];
	T_EQ_INT(tarif_translate_query("Domates soğan peynirli", q, sizeof q), 1);
	T_EQ_STR(q, "tomato onion cheese");
	T_EQ_INT(tarif_translate_query("pizza", q, sizeof q), 0);
	T_EQ_STR(q, "pizza");
}

static void qty(const char *s, int len, double lo, double hi, int frac)
{
	double a, b;
	int f;
	int n = tarif_qty_parse(s, &a, &b, &f);
	if (n != len || (len && (fabs(a - lo) > 1e-6 || fabs(b - hi) > 1e-6 || f != frac)))
		T_FAIL_("qty(\"%s\") = %d %g %g %d, expected %d %g %g %d", s, n, a, b, f, len, lo, hi, frac);
}

static void test_qty(void)
{
	qty("2 domates", 1, 2, 2, 0);
	qty("1,5 su bardağı", 3, 1.5, 1.5, 0);
	qty("1.5 cups", 3, 1.5, 1.5, 0);
	qty("½ su bardağı", 2, 0.5, 0.5, 1);
	qty("1 ½ cup", 4, 1.5, 1.5, 1);
	qty("1½ lb", 3, 1.5, 1.5, 1);
	qty("1/2 tsp", 3, 0.5, 0.5, 1);
	qty("1 1/2 tsp", 5, 1.5, 1.5, 1);
	qty("2-3 domates", 3, 2, 3, 0);
	qty("1 - 1,5 kepçe", 7, 1, 1.5, 0);
	qty("1–2 tsp", 5, 1, 2, 0);
	qty("yarım limon", 6, 0.5, 0.5, 1);
	qty("Bir buçuk su bardağı", 10, 1.5, 1.5, 0);
	qty("2 buçuk", 8, 2.5, 2.5, 0);
	qty("Tuz", 0, 0, 0, 0);
	qty("birkaç", 0, 0, 0, 0);
	qty("300g un", 3, 300, 300, 0);
	char o[64];
	tarif_qty_format(3, 0, 1, o, sizeof o);
	T_EQ_STR(o, "3");
	tarif_qty_format(1.5, 0, 1, o, sizeof o);
	T_EQ_STR(o, "1,5");
	tarif_qty_format(1.5, 1, 1, o, sizeof o);
	T_EQ_STR(o, "1½");
	tarif_qty_format(0.5, 0, 1, o, sizeof o);
	T_EQ_STR(o, "½");
	tarif_qty_format(1.0 / 3, 0, 0, o, sizeof o);
	T_EQ_STR(o, "⅓");
	tarif_qty_format(2.25, 0, 0, o, sizeof o);
	T_EQ_STR(o, "2.3");
	tarif_qty_format(450.4, 0, 1, o, sizeof o);
	T_EQ_STR(o, "450");
	tarif_qty_format(0.999, 0, 1, o, sizeof o);
	T_EQ_STR(o, "1");
}

static void scale(const char *in, double f, const char *want, int changed)
{
	char o[200];
	int c = tarif_scale_line(in, f, 1, o, sizeof o);
	if (strcmp(o, want) || c != changed) T_FAIL_("scale(\"%s\", %g) = \"%s\" (%d), expected \"%s\"", in, f, o, c, want);
}

static void test_scale(void)
{
	scale("1,5 su bardağı un", 2, "3 su bardağı un", 1);
	scale("1,5 su bardağı un", 0.5, "¾ su bardağı un", 1);
	scale("1 su bardağı süt", 0.5, "½ su bardağı süt", 1);
	scale("½ su bardağı yağ", 3, "1½ su bardağı yağ", 1);
	scale("2-3 domates", 2, "4-6 domates", 1);
	scale("Kırmızı Mercimek 30gr", 2, "Kırmızı Mercimek 60gr", 1);
	scale("Tuz 2 g", 1.5, "Tuz 3 g", 1);
	scale("Tuz, karabiber", 2, "Tuz, karabiber", 0);
	scale("2 domates", 1, "2 domates", 0);
	scale("yarım limon", 2, "1 limon", 1);
	scale("3 yumurta", 1.0 / 3 * 2, "2 yumurta", 1);
	scale("1 kg. kuşbaşı et", 1.5, "1,5 kg. kuşbaşı et", 1);
	scale("Domates (2 adet)", 2, "Domates (2 adet)", 0);
	scale("1 kilo ahududu", 1.0 / 7, "150 gram ahududu", 1); /* below a kilo: grams */
	scale("1 kg. kuşbaşı et", 0.5, "500 g. kuşbaşı et", 1);
	scale("1 litre süt", 0.25, "250 ml süt", 1);
	scale("2 kilo şeker", 2.0 / 14, "300 gram şeker", 1);
	scale("1 kilometre", 0.5, "½ kilometre", 1); /* only the unit word itself */
}

static void test_step_seconds(void)
{
	T_EQ_INT(tarif_step_seconds("Mercimeği ekleyip 30 dk pişirin."), 1800);
	T_EQ_INT(tarif_step_seconds("25 dakika kaynatın"), 1500);
	T_EQ_INT(tarif_step_seconds("1 saat dinlendirin"), 3600);
	T_EQ_INT(tarif_step_seconds("1,5 saat pişirin"), 5400);
	T_EQ_INT(tarif_step_seconds("Boil for 10 minutes."), 600);
	T_EQ_INT(tarif_step_seconds("bake for 1 hour or until golden"), 3600);
	T_EQ_INT(tarif_step_seconds("3-4 dk. kavurunuz"), 240);
	T_EQ_INT(tarif_step_seconds("45 saniye çırpın"), 45);
	T_EQ_INT(tarif_step_seconds("175 derecede fırında pişirin"), 0);
	T_EQ_INT(tarif_step_seconds("2 su bardağı su ekleyin"), 0);
	T_EQ_INT(tarif_step_seconds("1 saat 30 dakika pişirin"), 5400);
	T_EQ_INT(tarif_step_seconds("yarım saat bekletin"), 1800);
	T_EQ_INT(tarif_step_seconds("Simmer (about 20 min)"), 1200);
	T_EQ_INT(tarif_step_seconds("No time here"), 0);
}

static void test_timers(void)
{
	TarifTimers ts = { 0 };
	T_EQ_INT(tarif_timer_add(&ts, "Makarna", 600, 1000), 0);
	T_EQ_INT(tarif_timer_add(&ts, "Fırın\tüst", 60, 1000), 1);
	T_EQ_STR(ts.t[1].name, "Fırın üst");
	T_EQ_INT(tarif_timer_add(&ts, "x", 0, 1000), -1);
	T_EQ_INT(tarif_timer_left(&ts.t[0], 1100), 500);
	T_EQ_INT(tarif_timer_left(&ts.t[1], 1100), 0);
	T_EQ_INT(tarif_timers_ringing(&ts, 1030), -1);
	T_EQ_INT(tarif_timers_ringing(&ts, 1060), 1);
	ts.t[1].done = 1;
	T_EQ_INT(tarif_timers_ringing(&ts, 1060), -1);
	T_EQ_INT(tarif_timers_ringing(&ts, 1600), 0);
	char buf[512];
	T_ASSERT(tarif_timers_format(&ts, buf, sizeof buf) > 0);
	TarifTimers back;
	tarif_timers_parse(buf, &back);
	T_EQ_INT(back.n, 2);
	T_EQ_STR(back.t[1].name, "Fırın üst");
	T_EQ_INT(back.t[1].done, 1);
	T_EQ_INT(back.t[0].end, 1600);
	tarif_timer_remove(&back, 0);
	T_EQ_INT(back.n, 1);
	T_EQ_STR(back.t[0].name, "Fırın üst");
	for (int i = 0; i < TARIF_MAX_TIMERS; i++) tarif_timer_add(&ts, "t", 5, 0);
	T_EQ_INT(ts.n, TARIF_MAX_TIMERS);
	T_EQ_INT(tarif_timers_format(&ts, buf, 10), -1);
	char c[32];
	tarif_fmt_clock(245, c, sizeof c);
	T_EQ_STR(c, "4:05");
	tarif_fmt_clock(3729, c, sizeof c);
	T_EQ_STR(c, "1:02:09");
	tarif_fmt_clock(-3, c, sizeof c);
	T_EQ_STR(c, "0:00");
}

static void test_keys(void)
{
	TarifKeys k = { 0 };
	tarif_keys_parse("a\n\nb\r\na\nc", &k);
	T_EQ_INT(k.n, 3);
	T_EQ_INT(tarif_keys_toggle(&k, "b"), 0);
	T_EQ_INT(tarif_keys_toggle(&k, "d"), 1);
	T_ASSERT(tarif_keys_has(&k, "d"));
	T_ASSERT(!tarif_keys_has(&k, "b"));
	tarif_keys_touch(&k, "a", 10); /* a moves to the end */
	T_EQ_STR(k.key[k.n - 1], "a");
	tarif_keys_touch(&k, "e", 3); /* keeps 3: c d a e → d a e */
	T_EQ_INT(k.n, 3);
	T_EQ_STR(k.key[0], "d");
	char out[64];
	T_EQ_INT(tarif_keys_format(&k, out, sizeof out), 6);
	T_EQ_STR(out, "d\na\ne\n");
	T_EQ_INT(tarif_keys_format(&k, out, 4), -1);
	tarif_keys_free(&k);
	T_EQ_INT(k.n, 0);
}

static void test_wiki(void)
{
	const char *w = "{{recipesummary|category=Turkish recipes|servings=2|time=25 minutes}}\n"
			"{{recipe}}| [[Cookbook:Cuisine of Turkey|Turkish Cuisine]]\n\n"
			"'''Menemen''' is a Turkish dish.<ref>Note</ref>\n"
			"[[File:Menemen.jpg|thumb|A [[pan]] of menemen]]\n"
			"==Ingredients==\n"
			"*4 [[Cookbook:Egg|eggs]]\n"
			"*2 [[Cookbook:Onion]]s,\n"
			"===For the top===\n"
			"*1&nbsp;tsp [[Cookbook:Paprika|paprika]]<ref name=\"x\"/>\n"
			"{|\n| a table || row\n|}\n"
			"==Procedure==\n"
			"# Dice the onions and ''sauté'' for 5 minutes.\n"
			"# Add the eggs.<!-- hidden -->\n"
			"==Notes==\n"
			"* not an ingredient\n"
			"[[Category:Turkish recipes]]\n[[Category:Vegetarian recipes]]\n[[Category:Breakfast recipes]]\n";
	TarifBuf b = { 0 };
	T_EQ_INT(tarif_wiki_record(&b, w, "en", "Menemen", "https://en.wikibooks.org/wiki/Cookbook:Menemen"), 0);
	TarifDb db;
	char *copy = strdup(b.s);
	T_EQ_INT(tarif_parse(copy, &db), 1);
	const TarifRecipe *r = &db.r[0];
	T_EQ_STR(r->cuisine, "Türkiye");
	T_EQ_INT(r->servings, 2);
	T_ASSERT(strchr(r->tags, 'g') && strchr(r->tags, 'k'));
	T_EQ_STR(r->intro, "Menemen is a Turkish dish.");
	T_EQ_INT(r->n_ing, 3);
	T_EQ_STR(db.line[r->line0], "4 eggs");
	T_EQ_STR(db.line[r->line0 + 1], "2 Onions");
	T_EQ_INT(db.kind[r->line0 + 2], '+');
	T_EQ_STR(db.line[r->line0 + 2], "For the top");
	T_EQ_STR(db.line[r->line0 + 3], "1 tsp paprika");
	T_EQ_INT(r->n_steps, 2);
	T_EQ_STR(db.line[r->step0], "Dice the onions and sauté for 5 minutes.");
	T_EQ_STR(db.line[r->step0 + 1], "Add the eggs.");
	tarif_free(&db);
	tarif_buf_free(&b);

	const char *tr = "{{Çorba-n}}\n==Hazırlanışı==\nSoğanı doğrayınız. Mercimeği yıkayınız.\n\n"
			 "==Malzemeler==\n\n4 kişilik:\n\nKırmızı Mercimek 30gr, Un, Tuz (iri, deniz), Limon\n"
			 "[[Kategori:Çorba tarifleri|Mercimek]]\n";
	T_EQ_INT(tarif_wiki_record(&b, tr, "tr", "Mercimek Çorbası", "u"), 0);
	copy = strdup(b.s);
	T_EQ_INT(tarif_parse(copy, &db), 1);
	r = &db.r[0];
	T_EQ_STR(r->cuisine, "Türkiye");
	T_EQ_INT(r->servings, 4);
	T_ASSERT(strchr(r->tags, 'c') != NULL);
	T_EQ_INT(r->n_ing, 4);
	T_EQ_STR(db.line[r->line0 + 2], "Tuz (iri, deniz)");
	T_EQ_INT(r->n_steps, 1);
	tarif_free(&db);
	tarif_buf_free(&b);

	T_EQ_INT(tarif_wiki_record(&b, "==Malzemeler==\n*a b\n*c d\n==Yapılışı==\n#Pişir.\n[[Kategori:Kolombiya Mutfağı]]\n", "tr", "Arepa", "u"), 0);
	copy = strdup(b.s);
	T_EQ_INT(tarif_parse(copy, &db), 1);
	T_EQ_STR(db.r[0].cuisine, "Kolombiya");
	tarif_free(&db);
	tarif_buf_free(&b);
	T_EQ_INT(tarif_wiki_record(&b, "* [[Amerikan Mutfağı]]\n* [[Fransız Mutfağı]]\n", "tr", "Yemek:İçindekiler", "u"), -1);
	T_EQ_INT(b.len, 0);
	/* a long method paragraph becomes several steps */
	char lw[3000] = "==Ingredients==\n*a b\n*c d\n==Method==\n";
	for (int i = 0; i < 12; i++) strcat(lw, "Stir the pot gently and wait a little while. ");
	strcat(lw, "\n");
	T_EQ_INT(tarif_wiki_record(&b, lw, "en", "Long", "u"), 0);
	copy = strdup(b.s);
	T_EQ_INT(tarif_parse(copy, &db), 1);
	T_ASSERT(db.r[0].n_steps >= 2);
	tarif_free(&db);
	tarif_buf_free(&b);
}

static void test_json(void)
{
	char t[5][160];
	T_EQ_INT(tarif_wiki_search_titles("{\"query\":{\"search\":[{\"title\":\"Yemek:Kısır\"},{\"title\":\"Yemek:Cacık\"}]}}", t, 5), 2);
	T_EQ_STR(t[1], "Yemek:Cacık");
	T_EQ_INT(tarif_wiki_search_titles("not json", t, 5), 0);
	const char *meal = "{\"meals\":[{\"idMeal\":\"52771\",\"strMeal\":\"Spicy Arrabiata Penne\",\"strCategory\":\"Vegetarian\",\"strArea\":\"Italian\","
			   "\"strInstructions\":\"STEP 1\\r\\nBring a pot of water to the boil.\\r\\n\\r\\nSTEP 2 Add the penne and cook 10 minutes.\","
			   "\"strIngredient1\":\"penne rigate\",\"strMeasure1\":\"1 pound\",\"strIngredient2\":\"olive oil\",\"strMeasure2\":\"1/4 cup \","
			   "\"strIngredient3\":\"\",\"strMeasure3\":\" \",\"strIngredient4\":null}]}";
	TarifBuf b = { 0 };
	T_EQ_INT(tarif_mealdb_records(&b, meal), 1);
	TarifDb db;
	char *copy = strdup(b.s);
	T_EQ_INT(tarif_parse(copy, &db), 1);
	const TarifRecipe *r = &db.r[0];
	T_EQ_STR(r->cuisine, "İtalya");
	T_EQ_STR(r->tags, "g");
	T_EQ_STR(r->licence, "TheMealDB");
	T_EQ_STR(r->url, "https://www.themealdb.com/meal/52771");
	T_EQ_INT(r->n_ing, 2);
	T_EQ_STR(db.line[r->line0 + 1], "1/4 cup olive oil");
	T_EQ_INT(r->n_steps, 2);
	T_EQ_STR(db.line[r->step0 + 1], "Add the penne and cook 10 minutes.");
	tarif_free(&db);
	tarif_buf_free(&b);
	T_EQ_INT(tarif_mealdb_records(&b, "{\"meals\":null}"), 0);
	char ids[3][16], names[3][160];
	T_EQ_INT(tarif_mealdb_list("{\"meals\":[{\"strMeal\":\"Cacik\",\"idMeal\":\"53256\"}]}", ids, names, 3), 1);
	T_EQ_STR(ids[0], "53256");
	T_EQ_STR(tarif_area_tr("Turkish"), "Türkiye");
	T_EQ_STR(tarif_area_tr("Martian"), "Martian");
	T_EQ_STR(tarif_area_tr("Argentina"), "Arjantin");
	char u[200];
	tarif_wiki_url("tr.wikibooks.org", "Yemek:Hünkar Beğendi", u, sizeof u);
	T_EQ_STR(u, "https://tr.wikibooks.org/wiki/Yemek:H%C3%BCnkar_Be%C4%9Fendi");
	tarif_wiki_url("en.wikibooks.org", "Cookbook:Menemen (Eggs with Onion, Green Pepper, and Tomato)", u, sizeof u);
	T_EQ_STR(u, "https://en.wikibooks.org/wiki/Cookbook:Menemen_(Eggs_with_Onion,_Green_Pepper,_and_Tomato)");
}

static void test_record_writer(void)
{
	TarifBuf b = { 0 };
	tarif_rec_head(&b, "tr", "Bad\ttitle", "Türkiye", "c", 4, "u\nrl", "CC BY-SA 4.0");
	tarif_rec_line(&b, '-', "a\tb", -1);
	tarif_rec_line(&b, '*', "step", -1);
	T_EQ_STR(b.s, "@R\ttr\tBad title\tTürkiye\tc\t4\tu rl\tCC BY-SA 4.0\n-a b\n*step\n");
	tarif_buf_free(&b);
}

static void test_bundled_file(void)
{
	TarifDb db;
	int n = tarif_load("apps/tarif/data/recipes.txt", &db);
	T_ASSERT(n > 500);
	int tr = 0, out[2000];
	for (int i = 0; i < db.n; i++) {
		tr += !strcmp(db.r[i].lang, "tr");
		T_ASSERT(db.r[i].n_ing >= 2 && db.r[i].n_steps >= 1);
	}
	T_ASSERT(tr > 100);
	T_ASSERT(tarif_filter(&db, "Türkiye", 0, out, 2000) > 150);
	T_ASSERT(tarif_search(&db, "mercimek", out, 2000) > 0);
	tarif_free(&db);
}

int main(void)
{
	T_RUN(test_parse);
	T_RUN(test_fold);
	T_RUN(test_search);
	T_RUN(test_ingredients);
	T_RUN(test_filter);
	T_RUN(test_qty);
	T_RUN(test_scale);
	T_RUN(test_step_seconds);
	T_RUN(test_timers);
	T_RUN(test_keys);
	T_RUN(test_wiki);
	T_RUN(test_json);
	T_RUN(test_record_writer);
	T_RUN(test_bundled_file);
	return T_DONE();
}

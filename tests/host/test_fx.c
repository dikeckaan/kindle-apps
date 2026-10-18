#include <math.h>
#include "currency/fx.h"
#include "t.h"

static char *slurp(const char *path)
{
	static char buf[65536];
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	return buf;
}

static const FxRate *find(const FxRate *r, int n, const char *code)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(r[i].code, code)) return &r[i];
	return NULL;
}

static void test_tcmb(void)
{
	FxRate r[FX_MAX];
	char date[11];
	int n = fx_parse_tcmb(slurp("tests/host/fixtures/tcmb.xml"), r, FX_MAX, date);
	T_ASSERT(n >= 20);
	T_EQ_STR(date, "16.10.2026");
	const FxRate *usd = find(r, n, "USD"), *jpy = find(r, n, "JPY"), *eur = find(r, n, "EUR");
	T_ASSERT(usd && jpy && eur);
	if (!usd || !jpy || !eur) return;
	T_ASSERT(fabs(usd->buy - 49.3573) < 1e-9);
	T_ASSERT(fabs(usd->sell - 49.4463) < 1e-9);
	T_EQ_STR(usd->name, "ABD DOLARI");
	T_EQ_INT(jpy->unit, 100);
	T_EQ_STR(r[0].code, "USD"); /* TCMB order kept */
}

static void test_tcmb_broken(void)
{
	FxRate r[FX_MAX];
	char date[11];
	T_EQ_INT(fx_parse_tcmb("<html>hata</html>", r, FX_MAX, date), 0);
	T_EQ_INT(fx_parse_tcmb("<Tarih_Date Tarih=\"1.1.2026\"><Currency Kod=\"USD\"><Unit>1</Unit><ForexBuying>4", r, FX_MAX, date), 0);
	/* a currency without any rate is skipped */
	T_EQ_INT(fx_parse_tcmb("<Tarih_Date Tarih=\"1.1.2026\"><Currency Kod=\"XDR\"><Unit>1</Unit><ForexBuying/><ForexSelling/></Currency></Tarih_Date>",
			       r, FX_MAX, date), 0);
}

static void test_trnum(void)
{
	T_ASSERT(fabs(fx_parse_trnum("6.632,39") - 6632.39) < 1e-9);
	T_ASSERT(fabs(fx_parse_trnum("49,4840") - 49.484) < 1e-9);
	T_ASSERT(fabs(fx_parse_trnum("0,5915") - 0.5915) < 1e-9);
	T_ASSERT(fabs(fx_parse_trnum("$4.165,78") - 4165.78) < 1e-9); /* Truncgil prefixes the ounce price */
	T_ASSERT(isnan(fx_parse_trnum("")));
	T_ASSERT(isnan(fx_parse_trnum("abc")));
}

static void test_gold(void)
{
	FxGold g[FX_GOLD_MAX];
	int n = fx_parse_gold(slurp("tests/host/fixtures/gold.json"), g, FX_GOLD_MAX);
	T_EQ_INT(n, 6);
	T_EQ_STR(g[0].label, "Gram altın");
	T_ASSERT(fabs(g[0].buy - 6632.39) < 1e-6);
	T_ASSERT(fabs(g[0].sell - 6633.21) < 1e-6);
	T_EQ_STR(g[1].label, "Çeyrek altın");
	T_EQ_INT(fx_parse_gold("{}", g, FX_GOLD_MAX), 0);
	T_EQ_INT(fx_parse_gold("not json", g, FX_GOLD_MAX), 0);
}

static void test_convert(void)
{
	FxRate usd = { "USD", "ABD DOLARI", 1, 49.3573, 49.4463 }, jpy = { "JPY", "JAPON YENİ", 100, 31.1708, 31.3773 };
	const FxRate *try_ = fx_try();
	T_ASSERT(fabs(fx_convert(100, &usd, try_) - 4944.63) < 1e-6);
	T_ASSERT(fabs(fx_convert(4944.63, try_, &usd) - 100) < 1e-6); /* round trip */
	T_ASSERT(fabs(fx_convert(1000, &jpy, try_) - 313.773) < 1e-6); /* JPY is quoted per 100 */
	T_ASSERT(fabs(fx_convert(1, &usd, &jpy) - 49.4463 / 31.3773 * 100) < 1e-9);
}

int main(void)
{
	T_RUN(test_tcmb);
	T_RUN(test_tcmb_broken);
	T_RUN(test_trnum);
	T_RUN(test_gold);
	T_RUN(test_convert);
	return T_DONE();
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fx.h"
#include "net/net.h"
#include "vendor/cjson/cJSON.h"

static double child_num(const XmlEl *el, const char *tag)
{
	XmlEl c;
	char buf[32], *end;
	if (!xml_next(el->inner, el->inner_end, tag, &c)) return NAN;
	xml_text(c.inner, c.inner_end, buf, sizeof buf);
	double v = strtod(buf, &end);
	return end != buf && v > 0 ? v : NAN;
}

int fx_parse_tcmb(const char *xml, FxRate *out, int max, char date[11])
{
	const char *end = xml + strlen(xml);
	XmlEl root, el;
	date[0] = '\0';
	if (!xml_next(xml, end, "Tarih_Date", &root)) return 0;
	if (!xml_attr(&root, "Tarih", date, 11)) date[0] = '\0';
	int n = 0;
	for (const char *p = root.inner; n < max && (p = xml_next(p, root.inner_end, "Currency", &el));) {
		FxRate *r = &out[n];
		char code[8];
		if (!xml_attr(&el, "Kod", code, sizeof code) || strlen(code) != 3) continue;
		memcpy(r->code, code, 4); /* 3 letters + NUL, checked above */
		XmlEl c;
		r->name[0] = '\0';
		if (xml_next(el.inner, el.inner_end, "Isim", &c)) xml_text(c.inner, c.inner_end, r->name, sizeof r->name);
		double unit = child_num(&el, "Unit");
		r->unit = isnan(unit) ? 1 : (int) unit;
		r->buy = child_num(&el, "ForexBuying");
		r->sell = child_num(&el, "ForexSelling");
		if (isnan(r->buy)) r->buy = child_num(&el, "BanknoteBuying");
		if (isnan(r->sell)) r->sell = child_num(&el, "BanknoteSelling");
		if (isnan(r->buy) || isnan(r->sell)) continue;
		n++;
	}
	return n;
}

double fx_parse_trnum(const char *s)
{
	char buf[32];
	int o = 0;
	while (*s == '$' || *s == ' ') s++;
	for (; *s && o < (int) sizeof buf - 1; s++) {
		if (*s == '.') continue; /* thousands */
		buf[o++] = *s == ',' ? '.' : *s;
	}
	buf[o] = '\0';
	char *end;
	double v = strtod(buf, &end);
	return end != buf && *end == '\0' ? v : NAN;
}

int fx_parse_gold(const char *json, FxGold *out, int max)
{
	static const struct { const char *key, *label; } kinds[] = {
		{ "gram-altin", "Gram altın" },   { "ceyrek-altin", "Çeyrek altın" },          { "yarim-altin", "Yarım altın" },
		{ "tam-altin", "Tam altın" },     { "cumhuriyet-altini", "Cumhuriyet altını" }, { "ons", "Ons (USD)" },
	};
	cJSON *root = cJSON_Parse(json);
	int n = 0;
	for (size_t i = 0; i < sizeof kinds / sizeof kinds[0] && n < max; i++) {
		const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, kinds[i].key);
		const cJSON *b = cJSON_GetObjectItemCaseSensitive(o, "Alış"), *s = cJSON_GetObjectItemCaseSensitive(o, "Satış");
		if (!cJSON_IsString(b) || !cJSON_IsString(s)) continue;
		double bv = fx_parse_trnum(b->valuestring), sv = fx_parse_trnum(s->valuestring);
		if (isnan(bv) || isnan(sv)) continue;
		snprintf(out[n].label, sizeof out[n].label, "%s", kinds[i].label);
		out[n].buy = bv;
		out[n].sell = sv;
		n++;
	}
	cJSON_Delete(root);
	return n;
}

const FxRate *fx_try(void)
{
	static const FxRate t = { "TRY", "TÜRK LİRASI", 1, 1, 1 };
	return &t;
}

double fx_convert(double amount, const FxRate *from, const FxRate *to)
{
	double lira = amount * from->sell / from->unit;
	return lira / to->sell * to->unit;
}

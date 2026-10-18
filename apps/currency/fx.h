/* Currency data: TCMB daily rates (XML), gold prices (Truncgil JSON), Turkish numbers, conversion. */
#ifndef FX_H
#define FX_H

#define FX_MAX 32
#define FX_GOLD_MAX 8
typedef struct { char code[4], name[64]; int unit; double buy, sell; } FxRate; /* TRY per `unit` of the currency */
typedef struct { char label[32]; double buy, sell; } FxGold; /* label: Turkish source text, show with T() */

int fx_parse_tcmb(const char *xml, FxRate *out, int max, char date[11]); /* rates in TCMB order; 0 if unusable */
int fx_parse_gold(const char *json, FxGold *out, int max);               /* gram, çeyrek, yarım, tam, cumhuriyet, ons */
double fx_parse_trnum(const char *s);                                    /* "6.632,39" → 6632.39; NAN if not a number */
const FxRate *fx_try(void);                                              /* Turkish lira itself (rate 1) */
/* converts with the selling rate both ways, so A→B→A gives the amount back */
double fx_convert(double amount, const FxRate *from, const FxRate *to);
#endif

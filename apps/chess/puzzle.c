#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzle.h"
#include "san.h"
#include "vendor/cjson/cJSON.h"

#ifndef N_
#define N_(s) (s) /* translatable; shown with T() */
#endif

/* ---- parsing ---- */

static int parse_sq(const char *s)
{
	if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
	return (s[0] - 'a') + 8 * (s[1] - '1');
}

/* the legal move on b for UCI text, -1 if none */
static int uci_move(const Board *b, const char *uci, Move *out)
{
	size_t len = strlen(uci);
	if (len != 4 && len != 5) return -1;
	int from = parse_sq(uci), to = parse_sq(uci + 2), promo = 0;
	if (from < 0 || to < 0) return -1;
	if (len == 5) {
		const char *p = strchr("nbrq", uci[4]);
		if (!p || !uci[4]) return -1;
		promo = WN + (int) (p - "nbrq");
	}
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(b, ms);
	for (int i = 0; i < n; i++)
		if (ms[i].from == from && ms[i].to == to && ms[i].promo == promo) {
			*out = ms[i];
			return 0;
		}
	return -1;
}

int pz_setup(Puzzle *p, const Board *start, const char **uci, int n)
{
	if (n < 1 || n > PZ_MAX_SOL) return -1;
	Board b = *start;
	for (int i = 0; i < n; i++) {
		if (uci_move(&b, uci[i], &p->sol[i]) != 0) return -1;
		chess_make(&b, p->sol[i]);
	}
	p->n_sol = n;
	p->start = *start;
	return 0;
}

int pz_valid_id(const char *id)
{
	if (!id || !*id || strlen(id) >= 16) return 0;
	for (; *id; id++)
		if (!isalnum((unsigned char) *id)) return 0;
	return 1;
}

static void copy_str(char *dst, int cap, const cJSON *item)
{
	dst[0] = '\0';
	if (cJSON_IsString(item)) snprintf(dst, (size_t) cap, "%s", item->valuestring);
}

int pz_parse(const char *json, Puzzle *out)
{
	static Puzzle p;
	memset(&p, 0, sizeof p);
	cJSON *root = cJSON_Parse(json);
	const cJSON *game = cJSON_GetObjectItemCaseSensitive(root, "game");
	const cJSON *pz = cJSON_GetObjectItemCaseSensitive(root, "puzzle");
	const cJSON *pgn = cJSON_GetObjectItemCaseSensitive(game, "pgn");
	const cJSON *sol = cJSON_GetObjectItemCaseSensitive(pz, "solution");
	const cJSON *ply = cJSON_GetObjectItemCaseSensitive(pz, "initialPly");
	int ok = cJSON_IsString(pgn) && cJSON_IsArray(sol) && cJSON_IsNumber(ply);
	if (ok) {
		copy_str(p.id, sizeof p.id, cJSON_GetObjectItemCaseSensitive(pz, "id"));
		copy_str(p.game_id, sizeof p.game_id, cJSON_GetObjectItemCaseSensitive(game, "id"));
		const cJSON *r = cJSON_GetObjectItemCaseSensitive(pz, "rating"), *pl = cJSON_GetObjectItemCaseSensitive(pz, "plays");
		p.rating = cJSON_IsNumber(r) ? r->valueint : 0;
		p.plays = cJSON_IsNumber(pl) ? pl->valueint : 0;
		p.initial_ply = ply->valueint;
		const cJSON *t;
		cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(pz, "themes"))
		{
			if (p.n_themes < PZ_MAX_THEMES && cJSON_IsString(t)) snprintf(p.themes[p.n_themes++], sizeof p.themes[0], "%s", t->valuestring);
		}
		ok = pz_valid_id(p.id) && p.initial_ply >= 0;
	}
	Board b;
	chess_from_fen(&b, CHESS_START_FEN);
	if (ok) {
		/* Lichess: the puzzle starts after ply initialPly (0-based), i.e. after initialPly + 1 moves */
		int n = san_replay(&b, pgn->valuestring, p.initial_ply + 1, &p.last);
		ok = n > 0;
		p.has_last = n > 0;
	}
	const char *uci[PZ_MAX_SOL];
	int n_sol = 0;
	if (ok) {
		const cJSON *m;
		cJSON_ArrayForEach(m, sol)
		{
			if (!cJSON_IsString(m) || n_sol >= PZ_MAX_SOL) {
				ok = 0;
				break;
			}
			uci[n_sol++] = m->valuestring;
		}
	}
	ok = ok && pz_setup(&p, &b, uci, n_sol) == 0;
	cJSON_Delete(root);
	if (!ok) return -1;
	*out = p;
	return 0;
}

/* ---- themes ---- */

static const struct { const char *key, *name; } themes[] = {
	{ "mateIn1", N_("1 hamlede mat") },
	{ "mateIn2", N_("2 hamlede mat") },
	{ "mateIn3", N_("3 hamlede mat") },
	{ "mateIn4", N_("4 hamlede mat") },
	{ "mateIn5", N_("5+ hamlede mat") },
	{ "mate", N_("Mat") },
	{ "fork", N_("Çatal") },
	{ "pin", N_("Açmaz") },
	{ "skewer", N_("Şiş") },
	{ "sacrifice", N_("Fedâ") },
	{ "opening", N_("Açılış") },
	{ "middlegame", N_("Oyun ortası") },
	{ "endgame", N_("Oyun sonu") },
	{ "discoveredAttack", N_("Açarak saldırı") },
	{ "discoveredCheck", N_("Açarak şah") },
	{ "doubleCheck", N_("Çifte şah") },
	{ "hangingPiece", N_("Korumasız taş") },
	{ "trappedPiece", N_("Kapana kısılan taş") },
	{ "attraction", N_("Çekme") },
	{ "deflection", N_("Saptırma") },
	{ "interference", N_("Araya girme") },
	{ "intermezzo", N_("Ara hamle") },
	{ "clearance", N_("Yol açma") },
	{ "quietMove", N_("Sessiz hamle") },
	{ "defensiveMove", N_("Savunma hamlesi") },
	{ "xRayAttack", N_("Röntgen saldırısı") },
	{ "zugzwang", N_("Zugzwang") },
	{ "promotion", N_("Terfi") },
	{ "underPromotion", N_("Küçük terfi") },
	{ "castling", N_("Rok") },
	{ "enPassant", N_("Geçerken alma") },
	{ "backRankMate", N_("Son sıra matı") },
	{ "smotheredMate", N_("Boğmaca mat") },
	{ "anastasiaMate", N_("Anastasia matı") },
	{ "arabianMate", N_("Arap matı") },
	{ "bodenMate", N_("Boden matı") },
	{ "doubleBishopMate", N_("Çifte fil matı") },
	{ "dovetailMate", N_("Kırlangıç matı") },
	{ "hookMate", N_("Kanca matı") },
	{ "cornerMate", N_("Köşe matı") },
	{ "kingsideAttack", N_("Şah kanadı saldırısı") },
	{ "queensideAttack", N_("Vezir kanadı saldırısı") },
	{ "attackingF2F7", N_("f2/f7 saldırısı") },
	{ "capturingDefender", N_("Savunanı alma") },
	{ "exposedKing", N_("Açıkta şah") },
	{ "advancedPawn", N_("İlerlemiş piyon") },
	{ "crushing", N_("Ezici üstünlük") },
	{ "advantage", N_("Üstünlük") },
	{ "equality", N_("Eşitlik") },
	{ "pawnEndgame", N_("Piyon oyun sonu") },
	{ "rookEndgame", N_("Kale oyun sonu") },
	{ "queenEndgame", N_("Vezir oyun sonu") },
	{ "bishopEndgame", N_("Fil oyun sonu") },
	{ "knightEndgame", N_("At oyun sonu") },
	{ "queenRookEndgame", N_("Vezir ve kale oyun sonu") },
};

const char *pz_theme_name(const char *key)
{
	for (size_t i = 0; i < sizeof themes / sizeof themes[0]; i++)
		if (!strcmp(themes[i].key, key)) return themes[i].name;
	return NULL; /* length (short, long …), source (master …) and unknown keys */
}

void pz_themes_text(const Puzzle *p, const char *(*tr)(const char *), char *out, int cap)
{
	int o = 0;
	out[0] = '\0';
	for (int i = 0; i < p->n_themes; i++) {
		const char *name = pz_theme_name(p->themes[i]);
		if (!name) continue;
		if (tr) name = tr(name);
		int need = (int) strlen(name) + (o ? (int) strlen(" · ") : 0);
		if (o + need >= cap) break;
		o += snprintf(out + o, (size_t) (cap - o), "%s%s", o ? " · " : "", name);
	}
}

/* ---- solving ---- */

static int same(Move a, Move b) { return a.from == b.from && a.to == b.to && a.promo == b.promo; }

static int is_mate(const Board *b)
{
	Move ms[CHESS_MAX_MOVES];
	return chess_legal_moves(b, ms) == 0 && chess_in_check(b, b->side);
}

void pz_play_start(PzPlay *s, const Puzzle *p)
{
	memset(s, 0, sizeof *s);
	s->pz = *p;
	s->pos = p->start;
	s->player = p->start.side;
}

void pz_play_restart(PzPlay *s)
{
	s->pos = s->pz.start;
	s->step = 0;
}

int pz_play_done(const PzPlay *s) { return s->step >= s->pz.n_sol; }

PzVerdict pz_play_try(PzPlay *s, Move m)
{
	if (pz_play_done(s) || s->pos.side != s->player) return PZ_ILLEGAL;
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&s->pos, ms), k = -1;
	for (int i = 0; i < n; i++)
		if (same(ms[i], m)) k = i;
	if (k < 0) return PZ_ILLEGAL;
	Board after = s->pos;
	chess_make(&after, ms[k]);
	int mate = is_mate(&after);
	if (!same(ms[k], s->pz.sol[s->step]) && !mate) {
		s->mistakes++;
		return PZ_WRONG;
	}
	s->pos = after;
	s->step = mate ? s->pz.n_sol : s->step + 1;
	return pz_play_done(s) ? PZ_WIN : PZ_GOOD;
}

int pz_play_expects_reply(const PzPlay *s) { return !pz_play_done(s) && s->pos.side != s->player; }

int pz_play_reply(PzPlay *s, Move *out)
{
	if (!pz_play_expects_reply(s)) return 0;
	*out = s->pz.sol[s->step++];
	chess_make(&s->pos, *out);
	return 1;
}

int pz_play_hint(const PzPlay *s)
{
	if (pz_play_done(s) || s->pos.side != s->player) return -1;
	return s->pz.sol[s->step].from;
}

int pz_play_needs_promo(const PzPlay *s, int from, int to)
{
	int p = s->pos.sq[from];
	return (p == WP && (to >> 3) == 7) || (p == BP && (to >> 3) == 0);
}

int pz_play_show_next(PzPlay *s, Move *out)
{
	if (pz_play_done(s)) return 0;
	s->shown = 1;
	*out = s->pz.sol[s->step++];
	chess_make(&s->pos, *out);
	return 1;
}

char pz_play_result(const PzPlay *s) { return s->shown ? 'X' : s->mistakes ? 'F' : 'S'; }

/* ---- history ---- */

int pz_hist_parse(const char *buf, PzHist *h, int max)
{
	if (strncmp(buf, "v1\n", 3) != 0) return 0;
	int n = 0;
	for (const char *p = buf + 3; *p && n < max;) {
		const char *nl = strchr(p, '\n');
		int len = nl ? (int) (nl - p) : (int) strlen(p);
		char line[96], id[32], res;
		int day, rating, daily;
		if (len < (int) sizeof line) {
			memcpy(line, p, (size_t) len);
			line[len] = '\0';
			if (sscanf(line, "%31s %d %c %d %d", id, &day, &res, &rating, &daily) == 5 && pz_valid_id(id) && strchr("SFX", res) &&
			    (daily == 0 || daily == 1)) {
				memset(&h[n], 0, sizeof h[n]);
				snprintf(h[n].id, sizeof h[n].id, "%.15s", id); /* pz_valid_id: < 16 */
				h[n].day = day;
				h[n].res = res;
				h[n].rating = rating;
				h[n].daily = daily;
				n++;
			}
		}
		if (!nl) break;
		p = nl + 1;
	}
	return n;
}

int pz_hist_format(const PzHist *h, int n, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "v1\n");
	for (int i = 0; i < n && o < cap; i++)
		o += snprintf(buf + o, (size_t) (cap - o), "%s %d %c %d %d\n", h[i].id, h[i].day, h[i].res, h[i].rating, h[i].daily);
	return o < cap ? o : -1;
}

int pz_hist_find(const PzHist *h, int n, const char *id)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(h[i].id, id)) return i;
	return -1;
}

int pz_hist_add(PzHist *h, int n, int max, PzHist e)
{
	if (pz_hist_find(h, n, e.id) >= 0 || max < 1) return n;
	if (n >= max) {
		memmove(h, h + 1, sizeof *h * (size_t) (max - 1));
		n = max - 1;
	}
	h[n++] = e;
	return n;
}

void pz_hist_stats(const PzHist *h, int n, PzStats *st)
{
	memset(st, 0, sizeof *st);
	st->total = n;
	int run = 0;
	for (int i = 0; i < n; i++) {
		if (h[i].res != 'X') st->solved++;
		run = h[i].res == 'S' ? run + 1 : 0;
		if (run > st->best) st->best = run;
	}
	st->streak = run;
}

/* Türk daması rules and AI (see dama.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dama.h"

/* directions: 0 up, 1 down, 2 left, 3 right; d ^ 1 is the reverse */
static int next_sq(int s, int d)
{
	int r = s >> 3, c = s & 7;
	switch (d) {
	case 0: r--; break;
	case 1: r++; break;
	case 2: c--; break;
	default: c++; break;
	}
	return r < 0 || r > 7 || c < 0 || c > 7 ? -1 : r * 8 + c;
}

int dm_color(int p) { return p == DM_EMPTY ? -1 : p <= DM_WKING ? 0 : 1; }
int dm_is_king(int p) { return p == DM_WKING || p == DM_BKING; }

void dm_clear(Dm *d, int side)
{
	memset(d, 0, sizeof *d);
	d->side = side;
}

void dm_init(Dm *d)
{
	dm_clear(d, 0);
	for (int c = 0; c < 8; c++) {
		d->b[1 * 8 + c] = d->b[2 * 8 + c] = DM_BMAN;
		d->b[5 * 8 + c] = d->b[6 * 8 + c] = DM_WMAN;
	}
}

/* ---- move generation ---- */

typedef struct {
	DmMove *out;
	int max, n, best; /* best: most captures seen so far */
} Gen;

static void emit(Gen *g, const DmMove *m)
{
	if (m->ncap < g->best) return;
	if (m->ncap > g->best) {
		g->best = m->ncap;
		g->n = 0; /* everything found so far captured fewer */
	}
	if (g->n < g->max) g->out[g->n++] = *m;
}

/* capture sequences continuing from cur (the moving piece is already lifted off the board b) */
static void captures(Gen *g, unsigned char *b, int piece, int cur, int lastdir, DmMove *m)
{
	int me = dm_color(piece), found = 0;
	if (m->n >= DM_MAX_PATH) { emit(g, m); return; }
	for (int d = 0; d < 4; d++) {
		if (!dm_is_king(piece)) {
			if (d == (me == 0 ? 1 : 0)) continue; /* men never capture backwards */
			int a = next_sq(cur, d);
			if (a < 0 || dm_color(b[a]) != !me) continue;
			int l = next_sq(a, d);
			if (l < 0 || b[l] != DM_EMPTY) continue;
			unsigned char taken = b[a];
			b[a] = DM_EMPTY;
			m->path[m->n++] = (unsigned char) l;
			m->cap[m->ncap++] = (unsigned char) a;
			captures(g, b, piece, l, d, m);
			m->n--;
			m->ncap--;
			b[a] = taken;
			found = 1;
		} else {
			if (lastdir >= 0 && d == (lastdir ^ 1)) continue; /* no 180° turn */
			int a = next_sq(cur, d);
			while (a >= 0 && b[a] == DM_EMPTY) a = next_sq(a, d);
			if (a < 0 || dm_color(b[a]) != !me) continue;
			unsigned char taken = b[a];
			b[a] = DM_EMPTY;
			for (int l = next_sq(a, d); l >= 0 && b[l] == DM_EMPTY; l = next_sq(l, d)) {
				m->path[m->n++] = (unsigned char) l;
				m->cap[m->ncap++] = (unsigned char) a;
				captures(g, b, piece, l, d, m);
				m->n--;
				m->ncap--;
				found = 1;
			}
			b[a] = taken;
		}
	}
	if (!found && m->ncap) emit(g, m);
}

int dm_moves(const Dm *d, DmMove *out, int max)
{
	Gen g = { out, max, 0, 1 };
	unsigned char b[64];
	memcpy(b, d->b, sizeof b);
	for (int s = 0; s < 64; s++) {
		int p = b[s];
		if (dm_color(p) != d->side) continue;
		DmMove m = { (unsigned char) s, 0, 0, { 0 }, { 0 } };
		b[s] = DM_EMPTY;
		captures(&g, b, p, s, -1, &m);
		b[s] = (unsigned char) p;
	}
	if (g.n) return g.n;
	/* no capture: plain moves */
	for (int s = 0; s < 64 && g.n < max; s++) {
		int p = b[s];
		if (dm_color(p) != d->side) continue;
		for (int dir = 0; dir < 4; dir++) {
			if (!dm_is_king(p)) {
				if (dir == (d->side == 0 ? 1 : 0)) continue;
				int t = next_sq(s, dir);
				if (t >= 0 && b[t] == DM_EMPTY && g.n < max)
					out[g.n++] = (DmMove){ (unsigned char) s, 1, 0, { (unsigned char) t }, { 0 } };
			} else {
				for (int t = next_sq(s, dir); t >= 0 && b[t] == DM_EMPTY && g.n < max; t = next_sq(t, dir))
					out[g.n++] = (DmMove){ (unsigned char) s, 1, 0, { (unsigned char) t }, { 0 } };
			}
		}
	}
	return g.n;
}

void dm_apply(Dm *d, const DmMove *m)
{
	int p = d->b[m->from], to = m->path[m->n - 1], was_king = dm_is_king(p);
	d->b[m->from] = DM_EMPTY;
	for (int i = 0; i < m->ncap; i++) d->b[m->cap[i]] = DM_EMPTY;
	if (p == DM_WMAN && to < 8) p = DM_WKING;
	if (p == DM_BMAN && to >= 56) p = DM_BKING;
	d->quiet = m->ncap || !was_king ? 0 : d->quiet + 1;
	d->b[to] = (unsigned char) p;
	d->side = !d->side;
}

DmResult dm_result(const Dm *d)
{
	int cnt[2] = { 0, 0 };
	for (int s = 0; s < 64; s++)
		if (d->b[s]) cnt[dm_color(d->b[s])]++;
	if (cnt[d->side] == 0) return d->side ? DM_WHITE_WINS : DM_BLACK_WINS;
	if (cnt[!d->side] == 0) return d->side ? DM_BLACK_WINS : DM_WHITE_WINS;
	if (cnt[0] == 1 && cnt[1] == 1) return DM_DRAW;
	if (d->quiet >= DM_QUIET_DRAW) return DM_DRAW;
	DmMove ms[DM_MAX_MOVES];
	if (dm_moves(d, ms, DM_MAX_MOVES) == 0) return d->side ? DM_WHITE_WINS : DM_BLACK_WINS;
	return DM_ONGOING;
}

/* ---- evaluation and search ---- */

int dm_eval(const Dm *d)
{
	int s = 0, cnt[2] = { 0, 0 };
	for (int q = 0; q < 64; q++) {
		int p = d->b[q];
		if (!p) continue;
		int c = dm_color(p), row = q >> 3, col = q & 7, v;
		cnt[c]++;
		if (dm_is_king(p)) {
			v = 420;
		} else {
			int adv = c == 0 ? 6 - row : row - 1; /* rows advanced from the start's back line */
			v = 100 + adv * 6 + (adv >= 4 ? adv * 6 : 0);
			if (col >= 2 && col <= 5) v += 3;
		}
		s += c == 0 ? v : -v;
	}
	/* trading down helps the side ahead in material */
	int diff = s;
	if (diff > 150 && cnt[1] < 8) s += (8 - cnt[1]) * 8;
	if (diff < -150 && cnt[0] < 8) s -= (8 - cnt[0]) * 8;
	return d->side == 0 ? s : -s;
}

#define WIN 1000000

typedef struct {
	long nodes, deadline;
	int aborted;
} Ctx;

static int search(Ctx *c, const Dm *d, int depth, int ply, int alpha, int beta)
{
	if ((++c->nodes & 1023) == 0 && c->deadline && masa_now_ms() > c->deadline) c->aborted = 1;
	if (c->aborted) return 0;
	DmMove ms[DM_MAX_MOVES];
	int n = dm_moves(d, ms, DM_MAX_MOVES);
	if (n == 0) return -WIN + ply;
	int cnt[2] = { 0, 0 };
	for (int s = 0; s < 64; s++)
		if (d->b[s]) cnt[dm_color(d->b[s])]++;
	if ((cnt[0] == 1 && cnt[1] == 1) || d->quiet >= DM_QUIET_DRAW) return 0;
	/* forced captures are searched past the horizon */
	if (depth <= 0 && (ms[0].ncap == 0 || ply > 40)) return dm_eval(d);
	int best = -WIN * 2;
	for (int i = 0; i < n; i++) {
		Dm nd = *d;
		dm_apply(&nd, &ms[i]);
		int v = -search(c, &nd, depth - 1, ply + 1, -beta, -alpha);
		if (c->aborted) return 0;
		if (v > best) best = v;
		if (v > alpha) alpha = v;
		if (alpha >= beta) break;
	}
	return best;
}

int dm_ai(const Dm *d, const MasaLimits *lim, DmMove *best, MasaStats *st)
{
	long t0 = masa_now_ms();
	MasaStats dummy;
	if (!st) st = &dummy;
	memset(st, 0, sizeof *st);
	DmMove ms[DM_MAX_MOVES];
	int n = dm_moves(d, ms, DM_MAX_MOVES);
	if (n == 0) return -1;
	int level = lim ? lim->level : 2;
	int maxd = lim && lim->max_depth ? lim->max_depth : level <= 1 ? 2 : level == 2 ? 4 : 40;
	int tms = lim && lim->time_ms ? lim->time_ms : level <= 1 ? 300 : level == 2 ? 1200 : 2500;
	uint32_t seed = lim && lim->seed ? lim->seed : (uint32_t) t0 | 1;
	/* shuffle so equal moves vary from game to game */
	for (int i = n - 1; i > 0; i--) {
		int j = (int) (masa_rand(&seed) % (uint32_t) (i + 1));
		DmMove t = ms[i]; ms[i] = ms[j]; ms[j] = t;
	}
	int bi = 0;
	Ctx c = { 0, t0 + tms, 0 };
	if (n > 1)
		for (int depth = 1; depth <= maxd; depth++) {
			int alpha = -WIN * 2, b = bi;
			for (int i = 0; i < n; i++) {
				Dm nd = *d;
				dm_apply(&nd, &ms[i]);
				int v = -search(&c, &nd, depth - 1, 1, -WIN * 2, level <= 1 ? WIN * 2 : -alpha);
				if (c.aborted) break;
				if (level <= 1) v += (int) (masa_rand(&seed) % 161) - 80; /* Kolay: makes mistakes */
				if (v > alpha) { alpha = v; b = i; }
			}
			if (c.aborted) break;
			bi = b;
			st->depth = depth;
			st->score = alpha;
			DmMove t = ms[0]; ms[0] = ms[bi]; ms[bi] = t;
			bi = 0;
			if (alpha > WIN / 2 || alpha < -WIN / 2) break;
		}
	*best = ms[bi];
	st->nodes = c.nodes;
	st->ms = masa_now_ms() - t0;
	return 0;
}

/* ---- game record ---- */

void dm_game_new(DmGame *g, int mode, int level, int human)
{
	memset(g, 0, sizeof *g);
	dm_init(&g->pos);
	g->mode = mode;
	g->level = level;
	g->human = human;
}

static int same_move(const DmMove *a, const DmMove *b)
{
	return a->from == b->from && a->n == b->n && memcmp(a->path, b->path, a->n) == 0;
}

int dm_game_play(DmGame *g, const DmMove *m)
{
	if (g->n >= DM_MAX_PLIES || dm_result(&g->pos) != DM_ONGOING) return -1;
	DmMove ms[DM_MAX_MOVES];
	int n = dm_moves(&g->pos, ms, DM_MAX_MOVES);
	for (int i = 0; i < n; i++)
		if (same_move(&ms[i], m)) {
			dm_apply(&g->pos, &ms[i]);
			g->hist[g->n++] = ms[i];
			return 0;
		}
	return -1;
}

int dm_game_undo(DmGame *g, int plies)
{
	if (plies > g->n) plies = g->n;
	int n = g->n - plies;
	dm_init(&g->pos);
	for (int i = 0; i < n; i++) dm_apply(&g->pos, &g->hist[i]);
	g->n = n;
	return plies;
}

int dm_game_save(const DmGame *g, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "dama 1\nmode %d\nlevel %d\nhuman %d\nmoves", g->mode, g->level, g->human);
	for (int i = 0; i < g->n && o < cap; i++) {
		o += snprintf(buf + o, (size_t) (cap - o), " %d", g->hist[i].from);
		for (int k = 0; k < g->hist[i].n && o < cap; k++) o += snprintf(buf + o, (size_t) (cap - o), "-%d", g->hist[i].path[k]);
	}
	if (o < cap) o += snprintf(buf + o, (size_t) (cap - o), "\n");
	return o < cap ? o : -1;
}

int dm_game_load(DmGame *g, const char *buf)
{
	static DmGame t; /* large: keep it off the stack */
	int mode, level, human, off = 0;
	if (sscanf(buf, "dama 1\nmode %d\nlevel %d\nhuman %d\nmoves%n", &mode, &level, &human, &off) != 3 || !off) return -1;
	if (mode < 1 || mode > 2 || level < 1 || level > 3 || human < 0 || human > 1) return -1;
	dm_game_new(&t, mode, level, human);
	const char *p = buf + off;
	for (;;) {
		char *end;
		long v = strtol(p, &end, 10);
		if (end == p) break;
		DmMove m = { 0 };
		if (v < 0 || v > 63) return -1;
		m.from = (unsigned char) v;
		p = end;
		while (*p == '-' && m.n < DM_MAX_PATH) {
			v = strtol(p + 1, &end, 10);
			if (end == p + 1 || v < 0 || v > 63) return -1;
			m.path[m.n++] = (unsigned char) v;
			p = end;
		}
		if (!m.n || dm_game_play(&t, &m) != 0) return -1;
	}
	*g = t;
	return 0;
}

/* ---- lessons ---- */

void dm_sq_name(int sq, char out[3])
{
	out[0] = (char) ('a' + (sq & 7));
	out[1] = (char) ('0' + 8 - (sq >> 3));
	out[2] = '\0';
}

static int parse_sq(const char *s)
{
	if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
	return (8 - (s[1] - '0')) * 8 + (s[0] - 'a');
}

int dm_parse_move(const Dm *d, const char *s, DmMove *out)
{
	int sq[DM_MAX_PATH + 1], n = 0;
	for (const char *p = s;; p += 3) {
		if (n > DM_MAX_PATH || (sq[n++] = parse_sq(p)) < 0) return -1;
		if (!p[2]) break;
		if (p[2] != '-' && p[2] != 'x') return -1;
	}
	if (n < 2) return -1;
	static DmMove ms[DM_MAX_MOVES];
	int k = dm_moves(d, ms, DM_MAX_MOVES);
	for (int i = 0; i < k; i++) {
		if (ms[i].from != sq[0] || ms[i].n != n - 1) continue;
		int same = 1;
		for (int j = 0; j < ms[i].n; j++)
			if (ms[i].path[j] != sq[j + 1]) same = 0;
		if (same) {
			*out = ms[i];
			return 0;
		}
	}
	return -1;
}

void dm_from_rows(Dm *d, const char *const *rows, int side)
{
	dm_clear(d, side);
	for (int r = 0; r < 8; r++)
		for (int c = 0; c < 8 && rows[r] && rows[r][c]; c++) {
			const char *k = strchr("wWbB", rows[r][c]);
			if (k && *k) d->b[r * 8 + c] = (unsigned char) (DM_WMAN + (k - "wWbB"));
		}
}

const MasaPage dm_pages[] = {
	{ N_("Amaç"),
	  N_("Her oyuncunun 16 taşı vardır. Beyaz aşağıdadır ve ilk hamleyi yapar. Rakibin bütün taşlarını alan ya da onu "
	     "hiç hamle yapamaz hâle getiren kazanır."),
	  0,
	  { "........", "bbbbbbbb", "bbbbbbbb", "........", "........", "wwwwwwww", "wwwwwwww", "........" },
	  { NULL } },
	{ N_("Taş nasıl oynanır?"),
	  N_("Önce oynatacağınız taşa dokunun: kalın çerçeveyle seçilir, gidebileceği kareler noktayla gösterilir. Sonra "
	     "noktalı kareye dokunun. Taş bir kare ileri ya da yana gider; geri (×) ve çapraz gidemez."),
	  0,
	  { "........", "..bb.b..", ".b...b..", "........", "...w....", "..w..w..", "........", "........" },
	  { "........", "........", "........", "...o....", "..o*o...", "...x....", "........", "........" } },
	{ N_("Taş alma"),
	  N_("Rakip taş hemen önünüzde ya da yanınızdaysa ve arkasındaki kare boşsa, üzerinden atlayıp onu alırsınız. "
	     "Atladıktan sonra yine alabiliyorsanız devam edersiniz: e4 önce e5'i, sonra f6'yı alıp g6'ya konuyor."),
	  0,
	  { "........", "........", ".b...b..", "....b...", "....w...", ".w....w.", "........", "........" },
	  { "........", "........", "....vxo.", "....x...", "....*...", "........", "........", "........" } },
	{ N_("Almak zorunlu"),
	  N_("Alabiliyorsanız almak zorunludur ve en çok taşı alan yol seçilmelidir. Uygulama oynayabilecek taşları "
	     "çerçeveyle gösterir: c3 tek taş alabilirdi, f3 ise iki taş alıyor; bu yüzden yalnızca f3 oynayabilir."),
	  0,
	  { "........", ".b....b.", "........", "......b.", "..b..b..", "..w..w..", "........", "........" },
	  { "........", "........", "........", ".....v.o", "........", "..x..#..", "........", "........" } },
	{ N_("Dama olmak"),
	  N_("Son sıraya ulaşan taş dama olur (♛). Dama, düz çizgilerde istediği kadar kare gider. Uzaktaki bir taşı, "
	     "arası boşsa atlayıp arkasındaki herhangi bir boş kareye konarak alır: b2'deki dama b6'yı alabilir."),
	  0,
	  { "........", "........", ".b......", "........", "....b...", "........", ".W......", "........" },
	  { ".o......", ".o......", ".x......", "........", "........", "........", ".*......", "........" } },
	{ N_("Oyunun sonu"),
	  N_("Taşı kalmayan ya da hiç hamle yapamayan kaybeder. Burada Siyah'ın a2'deki taşı hiçbir yere gidemez: Beyaz "
	     "kazandı. İki tarafın da yalnızca birer taşı kalırsa oyun berabere biter."),
	  1,
	  { "........", "........", "........", "........", "........", "........", "bww.....", "W......." },
	  { "........", "........", "........", "........", "........", "........", "x.......", "........" } },
};
const int dm_npages = (int) (sizeof dm_pages / sizeof dm_pages[0]);

const MasaStep dm_demo[] = {
	{ "h3-h4", N_("Beyaz h3'teki taşını bir kare ileri, h4'e sürdü. Beyaz yukarı, Siyah aşağı doğru ilerler.") },
	{ "d6-d5", N_("Siyah d6'dan d5'e ilerledi.") },
	{ "e3-e4", N_("Beyaz e3'ten e4'e çıktı.") },
	{ "d5-d4", N_("Siyah d4'e indi. Önünde d3'teki beyaz taş var ve arkası (d5) boş: alınabilir!") },
	{ "d3xd5", N_("Beyaz d3'ten d4'ün üzerinden d5'e atladı ve siyah taşı aldı. Almak zorunludur.") },
	{ "e6-e5", N_("Siyah e5'e oynadı. Bu bir hata: e4'teki beyaz taş şimdi art arda iki taş alabilir.") },
	{ "e4xe6xe8", N_("Beyaz e5'i ve e7'yi art arda aldı ve son sıraya, e8'e ulaşıp dama oldu (♛). d5'ten tek taş almak "
			 "yasaktı: en çok alan yol zorunludur.") },
	{ "c6-c5", N_("Siyah c5'e oynadı ve d5'teki beyaz taşa yandan saldırdı.") },
	{ "d5xb5", N_("Beyaz'ın başka seçeneği yok, almak zorunlu: d5'teki taş yana atlayıp c5'i aldı ve b5'e kondu.") },
	{ "b6xb4", N_("Siyah b6'dan b4'e atlayıp b5'i geri aldı. Durum: Beyaz 15 taş (biri dama), Siyah 12 taş.") },
};
const int dm_ndemo = (int) (sizeof dm_demo / sizeof dm_demo[0]);

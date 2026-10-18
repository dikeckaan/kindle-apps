#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chess_game.h"

void game_new(Game *g, int mode, int level, int human_side)
{
	memset(g, 0, sizeof *g);
	snprintf(g->start_fen, sizeof g->start_fen, "%s", CHESS_START_FEN);
	chess_from_fen(&g->pos, CHESS_START_FEN);
	g->mode = mode;
	g->level = level;
	g->human_side = human_side;
}

int game_play(Game *g, Move m)
{
	if (g->n >= GAME_MAX_PLIES) return -1;
	Move ms[CHESS_MAX_MOVES];
	int n = chess_legal_moves(&g->pos, ms);
	for (int i = 0; i < n; i++) {
		if (ms[i].from == m.from && ms[i].to == m.to && ms[i].promo == m.promo) {
			chess_make(&g->pos, ms[i]);
			g->hist[g->n++] = ms[i];
			return 0;
		}
	}
	return -1;
}

static int parse_sq(const char *s)
{
	if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
	return (s[0] - 'a') + 8 * (s[1] - '1');
}

int game_play_uci(Game *g, const char *uci)
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
	return game_play(g, (Move){ (unsigned char) from, (unsigned char) to, (unsigned char) promo, 0 });
}

void move_to_uci(Move m, char out[6])
{
	out[0] = (char) ('a' + (m.from & 7));
	out[1] = (char) ('1' + (m.from >> 3));
	out[2] = (char) ('a' + (m.to & 7));
	out[3] = (char) ('1' + (m.to >> 3));
	out[4] = m.promo ? "nbrq"[m.promo - WN] : '\0';
	out[5] = '\0';
}

/* rebuilds pos from start_fen + the first n moves of hist */
static void replay(Game *g, int n)
{
	chess_from_fen(&g->pos, g->start_fen);
	for (int i = 0; i < n; i++) chess_make(&g->pos, g->hist[i]);
	g->n = n;
}

int game_undo(Game *g, int plies)
{
	if (plies > g->n) plies = g->n;
	if (plies <= 0) return 0;
	replay(g, g->n - plies);
	return plies;
}

/* FEN without the move counters: identifies a position for repetition */
static void position_key(const Board *b, char *out, int cap)
{
	chess_to_fen(b, out, cap);
	char *p = out;
	for (int spaces = 0; *p; p++)
		if (*p == ' ' && ++spaces == 4) {
			*p = '\0';
			break;
		}
}

static int insufficient(const Board *b)
{
	int minors[2] = { 0, 0 }, bishop_color[2] = { -1, -1 };
	for (int s = 0; s < 64; s++) {
		int p = b->sq[s];
		if (p == EMPTY || p == WK || p == BK) continue;
		int c = chess_piece_color(p), t = c ? p - 6 : p;
		if (t != WN && t != WB) return 0; /* pawn, rook or queen can still mate */
		minors[c]++;
		if (t == WB) bishop_color[c] = ((s & 7) + (s >> 3)) & 1;
	}
	if (minors[0] + minors[1] <= 1) return 1;
	/* K+B v K+B with bishops on the same colour */
	return minors[0] == 1 && minors[1] == 1 && bishop_color[0] >= 0 && bishop_color[0] == bishop_color[1];
}

Result game_result(const Game *g)
{
	Move ms[CHESS_MAX_MOVES];
	if (chess_legal_moves(&g->pos, ms) == 0) return chess_in_check(&g->pos, g->pos.side) ? RES_MATE : RES_STALEMATE;
	if (g->pos.halfmove >= 100) return RES_FIFTY;
	if (insufficient(&g->pos)) return RES_MATERIAL;
	char now[100], key[100];
	position_key(&g->pos, now, sizeof now);
	Board b;
	chess_from_fen(&b, g->start_fen);
	int seen = 0;
	for (int i = 0;; i++) {
		position_key(&b, key, sizeof key);
		if (strcmp(key, now) == 0) seen++;
		if (i == g->n) break;
		chess_make(&b, g->hist[i]);
	}
	return seen >= 3 ? RES_REPETITION : RES_ONGOING;
}

void game_uci_position(const Game *g, char *out, int cap)
{
	int o = snprintf(out, (size_t) cap, "position fen %s", g->start_fen);
	if (g->n && o < cap) o += snprintf(out + o, (size_t) (cap - o), " moves");
	for (int i = 0; i < g->n && o < cap; i++) {
		char m[6];
		move_to_uci(g->hist[i], m);
		o += snprintf(out + o, (size_t) (cap - o), " %s", m);
	}
}

int game_save(const Game *g, char *buf, int cap)
{
	int o = snprintf(buf, (size_t) cap, "v1\n%d %d %d %d\n%s\n", g->mode, g->level, g->human_side, g->flip, g->start_fen);
	for (int i = 0; i < g->n && o < cap; i++) {
		char m[6];
		move_to_uci(g->hist[i], m);
		o += snprintf(buf + o, (size_t) (cap - o), i ? " %s" : "%s", m);
	}
	if (o < cap) o += snprintf(buf + o, (size_t) (cap - o), "\n");
	return o < cap ? o : -1;
}

int game_load(Game *out, const char *buf)
{
	if (strncmp(buf, "v1\n", 3) != 0) return -1;
	const char *p = buf + 3;
	int mode, level, side, flip, used = 0;
	if (sscanf(p, "%d %d %d %d%n", &mode, &level, &side, &flip, &used) != 4) return -1;
	if ((mode != 1 && mode != 2) || level < 1 || level > 6 || (side != 0 && side != 1) || (flip != 0 && flip != 1)) return -1;
	p += used;
	if (*p++ != '\n') return -1;
	const char *nl = strchr(p, '\n');
	if (!nl || nl - p >= 100) return -1;
	static Game g; /* large: keep off the small device stack */
	memset(&g, 0, sizeof g);
	memcpy(g.start_fen, p, (size_t) (nl - p));
	g.start_fen[nl - p] = '\0';
	if (chess_from_fen(&g.pos, g.start_fen) != 0) return -1;
	g.mode = mode;
	g.level = level;
	g.human_side = side;
	g.flip = flip;
	p = nl + 1;
	nl = strchr(p, '\n');
	if (!nl) return -1;
	while (p < nl) {
		char m[8];
		int k = 0;
		while (p < nl && *p == ' ') p++;
		while (p < nl && *p != ' ' && k < 7) m[k++] = *p++;
		m[k] = '\0';
		if (k && game_play_uci(&g, m) != 0) return -1;
	}
	*out = g;
	return 0;
}

const char *result_text(Result r, int loser_side)
{
	switch (r) {
	case RES_MATE: return loser_side == 0 ? "Şah mat — Siyah kazandı" : "Şah mat — Beyaz kazandı";
	case RES_STALEMATE: return "Pat — berabere";
	case RES_FIFTY: return "50 hamle kuralı — berabere";
	case RES_REPETITION: return "Üçlü tekrar — berabere";
	case RES_MATERIAL: return "Yetersiz taş — berabere";
	default: return "";
	}
}

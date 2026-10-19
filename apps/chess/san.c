#include <ctype.h>
#include <string.h>
#include "san.h"

static const char piece_letters[] = "PNBRQK"; /* index + WP is the white piece type */

static int type_of(int p) { return p >= BP ? p - 6 : p; }

static int is_mate(const Board *b)
{
	Move ms[CHESS_MAX_MOVES];
	return chess_legal_moves(b, ms) == 0 && chess_in_check(b, b->side);
}

int san_parse(const Board *b, const char *san, Move *out)
{
	char s[16];
	int n = 0;
	/* copy without check marks and annotations (they may only trail) */
	for (const char *p = san; *p; p++) {
		if (strchr("+#!?", *p)) {
			for (const char *q = p; *q; q++)
				if (!strchr("+#!?", *q)) return -1;
			break;
		}
		if (n >= (int) sizeof s - 1) return -1;
		s[n++] = *p;
	}
	s[n] = '\0';
	if (!n) return -1;
	Move ms[CHESS_MAX_MOVES];
	int nm = chess_legal_moves(b, ms), found = -1;
	if (!strcmp(s, "O-O") || !strcmp(s, "0-0") || !strcmp(s, "O-O-O") || !strcmp(s, "0-0-0")) {
		int lng = n == 5;
		for (int i = 0; i < nm; i++)
			if ((ms[i].flags & MV_CASTLE) && (ms[i].to < ms[i].from) == lng) found = i;
		if (found < 0) return -1;
		*out = ms[found];
		return 0;
	}
	int type = WP, i = 0;
	if (strchr("NBRQK", s[0])) {
		type = WP + (int) (strchr(piece_letters, s[0]) - piece_letters);
		i = 1;
	}
	/* promotion at the end: "=Q" or "Q" */
	int promo = 0;
	if (type == WP && n >= 3 && strchr("NBRQ", s[n - 1])) {
		promo = WN + (int) (strchr("NBRQ", s[n - 1]) - "NBRQ");
		n -= s[n - 2] == '=' ? 2 : 1;
		s[n] = '\0';
	}
	/* destination: the last two characters */
	if (n - i < 2) return -1;
	char tf = s[n - 2], tr = s[n - 1];
	if (tf < 'a' || tf > 'h' || tr < '1' || tr > '8') return -1;
	int to = (tf - 'a') + 8 * (tr - '1');
	/* in between: from-file and/or from-rank, then an optional 'x' */
	int dfile = -1, drank = -1, j = i, end = n - 2;
	if (j < end && s[j] >= 'a' && s[j] <= 'h') dfile = s[j++] - 'a';
	if (j < end && s[j] >= '1' && s[j] <= '8') drank = s[j++] - '1';
	if (j < end && s[j] == 'x') j++;
	if (j != end) return -1;
	if (type == WP && end > i && dfile < 0) return -1; /* "xd5" */
	if (type == WP && drank >= 0) return -1;
	for (int k = 0; k < nm; k++) {
		Move m = ms[k];
		if (m.to != to || type_of(b->sq[m.from]) != type || m.promo != promo) continue;
		if (dfile >= 0 && (m.from & 7) != dfile) continue;
		if (drank >= 0 && (m.from >> 3) != drank) continue;
		if (type == WP && dfile < 0 && (m.from & 7) != (to & 7)) continue; /* a push stays on its file */
		if (found >= 0) return -1;                                          /* ambiguous */
		found = k;
	}
	if (found < 0) return -1;
	*out = ms[found];
	return 0;
}

void san_format(const Board *b, Move m, char *out, int cap)
{
	char s[16];
	int n = 0, type = type_of(b->sq[m.from]);
	int capture = b->sq[m.to] != EMPTY || (m.flags & MV_EP);
	if (m.flags & MV_CASTLE) {
		strcpy(s, m.to > m.from ? "O-O" : "O-O-O");
		n = (int) strlen(s);
	} else {
		if (type == WP) {
			if (capture) s[n++] = (char) ('a' + (m.from & 7));
		} else {
			s[n++] = piece_letters[type - WP];
			Move ms[CHESS_MAX_MOVES];
			int nm = chess_legal_moves(b, ms), others = 0, same_file = 0, same_rank = 0;
			for (int i = 0; i < nm; i++) {
				if (ms[i].to != m.to || ms[i].from == m.from || b->sq[ms[i].from] != b->sq[m.from]) continue;
				others++;
				if ((ms[i].from & 7) == (m.from & 7)) same_file = 1;
				if ((ms[i].from >> 3) == (m.from >> 3)) same_rank = 1;
			}
			if (others && !same_file) {
				s[n++] = (char) ('a' + (m.from & 7));
			} else if (others && !same_rank) {
				s[n++] = (char) ('1' + (m.from >> 3));
			} else if (others) {
				s[n++] = (char) ('a' + (m.from & 7));
				s[n++] = (char) ('1' + (m.from >> 3));
			}
		}
		if (capture) s[n++] = 'x';
		s[n++] = (char) ('a' + (m.to & 7));
		s[n++] = (char) ('1' + (m.to >> 3));
		if (m.promo) {
			s[n++] = '=';
			s[n++] = piece_letters[m.promo - WP];
		}
	}
	Board after = *b;
	chess_make(&after, m);
	if (chess_in_check(&after, after.side)) s[n++] = is_mate(&after) ? '#' : '+';
	s[n] = '\0';
	if (cap > 0) {
		strncpy(out, s, (size_t) cap - 1);
		out[cap - 1] = '\0';
	}
}

int san_replay(Board *b, const char *pgn, int max_plies, Move *last)
{
	Board w = *b;
	int plies = 0;
	const char *p = pgn;
	while (*p && plies < max_plies) {
		if (isspace((unsigned char) *p)) {
			p++;
			continue;
		}
		if (*p == '{') {
			const char *e = strchr(p, '}');
			if (!e) break;
			p = e + 1;
			continue;
		}
		char tok[32];
		int k = 0;
		while (*p && !isspace((unsigned char) *p) && *p != '{') {
			if (k < (int) sizeof tok - 1) tok[k++] = *p;
			p++;
		}
		tok[k] = '\0';
		if (tok[0] == '$') continue;
		if (!strcmp(tok, "1-0") || !strcmp(tok, "0-1") || !strcmp(tok, "1/2-1/2") || !strcmp(tok, "*")) break;
		/* "12." or "12..." alone, or glued to the move ("12.e4") */
		char *t = tok;
		if (isdigit((unsigned char) *t) && strchr(t, '.')) {
			while (isdigit((unsigned char) *t)) t++;
			while (*t == '.') t++;
			if (!*t) continue;
		}
		Move m;
		if (san_parse(&w, t, &m) != 0) return -1;
		chess_make(&w, m);
		if (last) *last = m;
		plies++;
	}
	*b = w;
	return plies;
}

#include <stdio.h>
#include <string.h>
#include "history.h"
#include "san.h"

int history_sans(const Game *g, char (*out)[HIST_SAN_MAX])
{
	Board b;
	if (chess_from_fen(&b, g->start_fen) != 0) return 0;
	for (int i = 0; i < g->n; i++) {
		san_format(&b, g->hist[i], out[i], HIST_SAN_MAX);
		chess_make(&b, g->hist[i]);
	}
	return g->n;
}

void san_figurine(const char *san, char *out, int cap)
{
	static const char *fig[] = { "♞", "♝", "♜", "♛", "♚" }; /* N B R Q K */
	int o = 0;
	if (cap <= 0) return;
	for (const char *p = san; *p; p++) {
		const char *l = strchr("NBRQK", *p);
		const char *put = l && *p ? fig[l - "NBRQK"] : NULL;
		char one[2] = { *p, '\0' };
		if (!put) put = one;
		int len = (int) strlen(put);
		if (o + len >= cap) break;
		memcpy(out + o, put, (size_t) len);
		o += len;
	}
	out[o] = '\0';
}

static void start_numbers(const Game *g, int *fullmove, int *side)
{
	Board b;
	if (chess_from_fen(&b, g->start_fen) != 0) chess_from_fen(&b, CHESS_START_FEN);
	*fullmove = b.fullmove > 0 ? b.fullmove : 1;
	*side = b.side;
}

int history_move_no(const Game *g, int ply)
{
	int fm, side;
	start_numbers(g, &fm, &side);
	return fm + (ply + side) / 2;
}

int history_is_black(const Game *g, int ply)
{
	int fm, side;
	start_numbers(g, &fm, &side);
	return (ply + side) & 1;
}

const char *history_result_tag(const Game *g)
{
	Result r = game_result(g);
	if (r == RES_ONGOING) return "*";
	if (r == RES_MATE) return g->pos.side == 0 ? "0-1" : "1-0";
	return "1/2-1/2";
}

/* appends s to buf, a newline first when the line would pass 79 columns */
static int put_token(char *buf, int cap, int o, int *col, const char *s)
{
	int len = (int) strlen(s);
	if (o < 0) return -1;
	const char *sep = *col == 0 ? "" : (*col + 1 + len > 79 ? "\n" : " ");
	if (*sep == '\n') *col = 0;
	int n = snprintf(buf + o, (size_t) (cap - o), "%s%s", sep, s);
	if (n < 0 || o + n >= cap) return -1;
	*col += (*sep == ' ' ? 1 : 0) + len;
	return o + n;
}

int history_pgn(const Game *g, const char *date, const char *white, const char *black, char *buf, int cap)
{
	static char sans[GAME_MAX_PLIES][HIST_SAN_MAX];
	const char *res = history_result_tag(g);
	int o = snprintf(buf, (size_t) cap,
			 "[Event \"Kindle Satranç\"]\n[Site \"Kindle\"]\n[Date \"%s\"]\n[Round \"-\"]\n[White \"%s\"]\n"
			 "[Black \"%s\"]\n[Result \"%s\"]\n",
			 date, white, black, res);
	if (o < 0 || o >= cap) return -1;
	if (strcmp(g->start_fen, CHESS_START_FEN) != 0) {
		int n = snprintf(buf + o, (size_t) (cap - o), "[SetUp \"1\"]\n[FEN \"%s\"]\n", g->start_fen);
		if (n < 0 || o + n >= cap) return -1;
		o += n;
	}
	if (o + 1 >= cap) return -1;
	buf[o++] = '\n';
	buf[o] = '\0';
	int n = history_sans(g, sans), col = 0;
	for (int i = 0; i < n; i++) {
		char num[16];
		if (!history_is_black(g, i)) {
			snprintf(num, sizeof num, "%d.", history_move_no(g, i));
			o = put_token(buf, cap, o, &col, num);
		} else if (i == 0) {
			snprintf(num, sizeof num, "%d...", history_move_no(g, i));
			o = put_token(buf, cap, o, &col, num);
		}
		o = put_token(buf, cap, o, &col, sans[i]);
	}
	o = put_token(buf, cap, o, &col, res);
	if (o < 0 || o + 1 >= cap) return -1;
	buf[o++] = '\n';
	buf[o] = '\0';
	return o;
}

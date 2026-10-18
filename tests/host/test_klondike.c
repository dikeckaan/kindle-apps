#include <stdlib.h>
#include "solitaire/klondike.h"
#include "t.h"

enum { S = 0, H = 1, D = 2, C = 3 };
static int card(int rank, int suit) { return suit * 13 + rank - 1; }

static void put(KlState *s, int pile, int c) { s->c[pile][s->n[pile]++] = (unsigned char) c; }

static void blank(Klondike *k, int draw)
{
	kl_init(k);
	memset(&k->s, 0, sizeof k->s);
	k->draw = draw;
}

static int top(const Klondike *k, int p) { return k->s.n[p] ? k->s.c[p][k->s.n[p] - 1] : -1; }

static int all_distinct(const KlState *s)
{
	int seen[52] = { 0 }, total = 0;
	for (int p = 0; p < KL_PILES; p++)
		for (int i = 0; i < s->n[p]; i++) {
			int c = s->c[p][i];
			if (c < 0 || c > 51 || seen[c]++) return 0;
			total++;
		}
	return total == 52;
}

static void test_deal(void)
{
	Klondike k;
	kl_init(&k);
	kl_new(&k, 42, 1);
	T_ASSERT(all_distinct(&k.s));
	int tab = 0;
	for (int t = 0; t < 7; t++) {
		T_EQ_INT(k.s.n[KL_T0 + t], t + 1);
		T_EQ_INT(k.s.down[t], t); /* only the top card is face up */
		tab += k.s.n[KL_T0 + t];
	}
	T_EQ_INT(tab, 28);
	T_EQ_INT(k.s.n[KL_STOCK], 24);
	T_EQ_INT(k.s.n[KL_WASTE], 0);
	for (int f = 0; f < 4; f++) T_EQ_INT(k.s.n[KL_F0 + f], 0);
	T_EQ_INT(k.nhist, 0);
	T_EQ_INT(k.draw, 1);

	Klondike k2, k3;
	kl_init(&k2);
	kl_init(&k3);
	kl_new(&k2, 42, 3);
	kl_new(&k3, 43, 3);
	T_ASSERT(memcmp(&k.s, &k2.s, sizeof k.s) == 0); /* same seed, same deal */
	T_ASSERT(memcmp(&k.s, &k3.s, sizeof k.s) != 0);
	T_EQ_INT(k2.draw, 3);
	for (unsigned seed = 0; seed < 200; seed++) {
		kl_new(&k3, seed, 1);
		T_ASSERT(all_distinct(&k3.s));
	}
	kl_free(&k);
	kl_free(&k2);
	kl_free(&k3);
}

static void test_tableau_moves(void)
{
	Klondike k;
	blank(&k, 1);
	put(&k.s, KL_T0 + 0, card(9, H));
	put(&k.s, KL_T0 + 0, card(8, S));             /* col 0: 9♥ 8♠ */
	put(&k.s, KL_T0 + 1, card(9, C));             /* col 1: 9♣ */
	put(&k.s, KL_T0 + 2, card(5, C));
	put(&k.s, KL_T0 + 2, card(10, D));            /* col 2: [5♣] 10♦ */
	k.s.down[2] = 1;
	put(&k.s, KL_T0 + 3, card(13, S));            /* col 3: K♠ */
	put(&k.s, KL_T0 + 4, card(2, H));
	put(&k.s, KL_T0 + 4, card(13, H));
	put(&k.s, KL_T0 + 4, card(12, C));            /* col 4: [2♥] K♥ Q♣ */
	k.s.down[4] = 1;
	/* col 5 empty */

	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 1, KL_T0 + 1)); /* 8♠ on 9♣: same colour */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 1, 0, KL_T0 + 0)); /* 9♣ on 8♠: wrong rank */
	T_ASSERT(kl_can_move(&k, KL_T0 + 1, 0, KL_T0 + 2));  /* 9♣ on 10♦ */
	T_ASSERT(kl_can_move(&k, KL_T0 + 0, 0, KL_T0 + 2) == 0); /* 9♥ on 10♦: same colour */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 1, 0, KL_T0 + 5)); /* only a K onto an empty column */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 0, KL_T0 + 5));
	T_ASSERT(kl_can_move(&k, KL_T0 + 4, 1, KL_T0 + 5));  /* K♥ Q♣ run onto empty */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 4, 0, KL_T0 + 5)); /* face-down card can't move */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 3, 0, KL_T0 + 3)); /* onto itself */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 0, KL_F0));     /* a run can't go to a foundation */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 2, KL_T0 + 2)); /* index past the top */

	T_EQ_INT(kl_move(&k, KL_T0 + 1, 0, KL_T0 + 2), 1);   /* 9♣ onto 10♦ */
	T_EQ_INT(k.s.n[KL_T0 + 1], 0);
	T_EQ_INT(top(&k, KL_T0 + 2), card(9, C));
	T_EQ_INT(k.nhist, 1);
	T_EQ_INT(kl_move(&k, KL_T0 + 0, 0, KL_T0 + 1), 0);   /* illegal: nothing recorded */
	T_EQ_INT(k.nhist, 1);
	T_EQ_INT(kl_move(&k, KL_T0 + 4, 1, KL_T0 + 5), 1);   /* K♥ Q♣ to empty: 2♥ is exposed and flipped */
	T_EQ_INT(k.s.n[KL_T0 + 4], 1);
	T_EQ_INT(k.s.down[4], 0);
	T_EQ_INT(k.s.n[KL_T0 + 5], 2);
	T_EQ_INT(k.s.c[KL_T0 + 5][0], card(13, H));
	T_EQ_INT(k.s.down[5], 0);
	T_EQ_INT(kl_move(&k, KL_T0 + 0, 0, KL_T0 + 5), 0);   /* 9♥ 8♠ on Q♣: wrong rank */
	kl_free(&k);
}

static void test_foundation_and_waste_moves(void)
{
	Klondike k;
	blank(&k, 1);
	put(&k.s, KL_WASTE, card(1, D));
	put(&k.s, KL_T0 + 0, card(3, S));
	put(&k.s, KL_T0 + 0, card(2, D));
	put(&k.s, KL_T0 + 1, card(2, C));
	put(&k.s, KL_T0 + 2, card(4, H));
	T_ASSERT(!kl_can_move(&k, KL_T0 + 1, 0, KL_F0)); /* 2 onto an empty foundation */
	T_ASSERT(kl_can_move(&k, KL_WASTE, 0, KL_F0 + 2));
	T_ASSERT(!kl_can_move(&k, KL_WASTE, 0, KL_T0 + 0)); /* A♦ on 2♦: same colour */
	T_EQ_INT(kl_move(&k, KL_WASTE, 0, KL_F0 + 2), 1);
	T_ASSERT(!kl_can_move(&k, KL_T0 + 1, 0, KL_F0 + 2)); /* 2♣ on A♦: wrong suit */
	T_ASSERT(kl_can_move(&k, KL_T0 + 0, 1, KL_F0 + 2));  /* 2♦ on A♦ */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 0, KL_F0 + 2)); /* not a top card */
	T_EQ_INT(kl_move(&k, KL_T0 + 0, 1, KL_F0 + 2), 1);
	T_EQ_INT(k.s.n[KL_F0 + 2], 2);
	/* foundation top back to the tableau: 2♦ onto 3♠ */
	T_ASSERT(kl_can_move(&k, KL_F0 + 2, 1, KL_T0 + 0));
	T_ASSERT(!kl_can_move(&k, KL_F0 + 2, 0, KL_T0 + 0)); /* only the top */
	T_EQ_INT(kl_move(&k, KL_F0 + 2, 1, KL_T0 + 0), 1);
	T_EQ_INT(top(&k, KL_T0 + 0), card(2, D));
	T_ASSERT(!kl_can_move(&k, KL_STOCK, 0, KL_T0 + 0)); /* never from the stock */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 1, KL_WASTE)); /* never onto the waste */
	T_ASSERT(!kl_can_move(&k, KL_T0 + 0, 1, KL_STOCK));

	/* auto move: foundation first, otherwise a tableau column */
	T_EQ_INT(kl_auto_move(&k, KL_T0 + 0, 1), KL_F0 + 2);
	put(&k.s, KL_WASTE, card(3, C)); /* 3♣ onto 4♥ */
	T_EQ_INT(kl_best_target(&k, KL_WASTE, 0), KL_T0 + 2);
	T_EQ_INT(kl_auto_move(&k, KL_WASTE, 0), KL_T0 + 2);
	T_EQ_INT(kl_auto_move(&k, KL_T0 + 1, 0), -1); /* 2♣ fits nowhere */
	kl_free(&k);
}

static void test_auto_move_king(void)
{
	Klondike k;
	blank(&k, 1);
	put(&k.s, KL_T0 + 0, card(13, S)); /* a lone K at the bottom of a column must not hop to another empty column */
	T_EQ_INT(kl_best_target(&k, KL_T0 + 0, 0), -1);
	put(&k.s, KL_T0 + 1, card(5, H));
	put(&k.s, KL_T0 + 1, card(13, D));
	k.s.down[1] = 1;
	T_EQ_INT(kl_best_target(&k, KL_T0 + 1, 1), KL_T0 + 2); /* K over a face-down card goes to the first empty column */
	kl_free(&k);
}

static void test_draw_one_and_recycle(void)
{
	Klondike k;
	kl_init(&k);
	kl_new(&k, 7, 1);
	int first = top(&k, KL_STOCK);
	T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 23);
	T_EQ_INT(k.s.n[KL_WASTE], 1);
	T_EQ_INT(top(&k, KL_WASTE), first);
	for (int i = 0; i < 23; i++) T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 0);
	T_EQ_INT(k.s.n[KL_WASTE], 24);
	T_EQ_INT(kl_draw(&k), 1); /* recycle */
	T_EQ_INT(k.s.n[KL_STOCK], 24);
	T_EQ_INT(k.s.n[KL_WASTE], 0);
	T_EQ_INT(top(&k, KL_STOCK), first); /* same order again */
	for (int r = 0; r < 5; r++) /* unlimited redeals */
		for (int i = 0; i < 25; i++) T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 24);
	k.s.n[KL_STOCK] = 0;
	T_EQ_INT(kl_draw(&k), 0); /* nothing anywhere */
	kl_free(&k);
}

static void test_draw_three(void)
{
	Klondike k;
	kl_init(&k);
	kl_new(&k, 9, 3);
	int a = k.s.c[KL_STOCK][23], b = k.s.c[KL_STOCK][22], c = k.s.c[KL_STOCK][21];
	T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 21);
	T_EQ_INT(k.s.n[KL_WASTE], 3);
	T_EQ_INT(k.s.c[KL_WASTE][0], a);
	T_EQ_INT(k.s.c[KL_WASTE][1], b);
	T_EQ_INT(k.s.c[KL_WASTE][2], c); /* the third card drawn is playable */
	for (int i = 0; i < 7; i++) T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 0);
	T_EQ_INT(k.s.n[KL_WASTE], 24);
	T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 24);
	T_EQ_INT(top(&k, KL_STOCK), a);
	/* fewer than three left: draws what remains */
	k.s.n[KL_STOCK] = 2;
	k.s.n[KL_WASTE] = 0;
	T_EQ_INT(kl_draw(&k), 1);
	T_EQ_INT(k.s.n[KL_STOCK], 0);
	T_EQ_INT(k.s.n[KL_WASTE], 2);
	kl_free(&k);
}

/* random play: draws and auto moves; every position is recorded and must come back exactly on undo */
static void test_undo_chain(void)
{
	for (unsigned seed = 1; seed <= 20; seed++) {
		Klondike k;
		kl_init(&k);
		kl_new(&k, seed, seed % 2 ? 1 : 3);
		static KlState seen[2000];
		int n = 0, recycles = 0;
		unsigned r = seed;
		seen[n++] = k.s;
		for (int step = 0; step < 600 && n < 2000; step++) {
			r = r * 1103515245u + 12345u;
			int moved = 0;
			if ((r >> 16) % 3 == 0) {
				int was_empty = k.s.n[KL_STOCK] == 0;
				moved = kl_draw(&k);
				if (moved && was_empty) recycles++;
			} else {
				int p = KL_WASTE + (int) ((r >> 8) % 12);
				int idx = p >= KL_T0 ? k.s.down[p - KL_T0] : k.s.n[p] - 1;
				if (k.s.n[p] && idx < k.s.n[p]) moved = kl_auto_move(&k, p, idx) >= 0;
			}
			if (moved) {
				T_ASSERT(all_distinct(&k.s));
				seen[n++] = k.s;
			}
		}
		T_EQ_INT(k.nhist, n - 1);
		T_ASSERT(recycles > 0);
		for (int i = n - 2; i >= 0; i--) {
			T_EQ_INT(kl_undo(&k), 1);
			T_ASSERT(memcmp(&k.s, &seen[i], sizeof k.s) == 0);
		}
		T_EQ_INT(kl_undo(&k), 0);
		kl_free(&k);
	}
}

/* foundations hold A..lim-1 of each suit; the rest sits face up in alternating descending runs */
static void near_won(Klondike *k, int lim)
{
	blank(k, 1);
	for (int s = 0; s < 4; s++)
		for (int r = 1; r < lim; r++) put(&k->s, KL_F0 + s, card(r, s));
	/* column 0: K♠ Q♥ J♠ …, column 1: K♥ Q♠ …, column 2: K♦ Q♣ …, column 3: K♣ Q♦ … */
	static const int pair[4][2] = { { S, H }, { H, S }, { D, C }, { C, D } };
	for (int col = 0; col < 4; col++)
		for (int r = 13; r >= lim; r--) put(&k->s, KL_T0 + col, card(r, pair[col][(13 - r) % 2]));
}

static int foundations_ok(const KlState *s)
{
	for (int f = 0; f < 4; f++)
		for (int i = 0; i < s->n[KL_F0 + f]; i++) {
			int c = s->c[KL_F0 + f][i];
			if (kl_rank(c) != i + 1 || kl_suit(c) != kl_suit(s->c[KL_F0 + f][0])) return 0;
		}
	return 1;
}

static void test_autocomplete(void)
{
	Klondike k;
	near_won(&k, 6);
	T_ASSERT(all_distinct(&k.s));
	T_ASSERT(!kl_won(&k));
	T_ASSERT(kl_can_autocomplete(&k));
	int steps = 0;
	while (kl_autocomplete_step(&k)) {
		steps++;
		T_ASSERT(foundations_ok(&k.s));
		T_ASSERT(all_distinct(&k.s));
		T_ASSERT(steps <= 52);
	}
	T_EQ_INT(steps, 32);
	T_ASSERT(kl_won(&k));
	T_ASSERT(!kl_can_autocomplete(&k));
	T_EQ_INT(k.nhist, 32);
	for (int i = 0; i < 32; i++) T_EQ_INT(kl_undo(&k), 1); /* each step is undoable */
	T_ASSERT(kl_can_autocomplete(&k));
	kl_free(&k);

	near_won(&k, 6);
	k.s.down[0] = 1; /* a face-down card: not yet */
	T_ASSERT(!kl_can_autocomplete(&k));
	T_EQ_INT(kl_autocomplete_step(&k), 0);
	kl_free(&k);

	near_won(&k, 6);
	k.s.n[KL_T0 + 3]--; /* 6♣ ... move the top card to the waste: not yet */
	put(&k.s, KL_WASTE, k.s.c[KL_T0 + 3][k.s.n[KL_T0 + 3]]);
	T_ASSERT(!kl_can_autocomplete(&k));
	kl_free(&k);
}

static void test_win(void)
{
	Klondike k;
	near_won(&k, 13); /* only the four kings are left */
	T_ASSERT(!kl_won(&k));
	for (int col = 0; col < 3; col++) T_EQ_INT(kl_auto_move(&k, KL_T0 + col, 0), KL_F0 + col);
	T_ASSERT(!kl_won(&k));
	T_EQ_INT(kl_move(&k, KL_T0 + 3, 0, KL_F0 + 3), 1);
	T_ASSERT(kl_won(&k));
	kl_free(&k);
}

static void test_save_roundtrip(void)
{
	Klondike k, l;
	kl_init(&k);
	kl_new(&k, 1234, 3);
	for (int i = 0; i < 12; i++) kl_draw(&k);
	for (int p = KL_WASTE; p < KL_PILES; p++)
		if (k.s.n[p]) kl_auto_move(&k, p, p >= KL_T0 ? k.s.down[p - KL_T0] : k.s.n[p] - 1);
	T_ASSERT(k.nhist >= 12);
	static char buf[1 << 16];
	int n = kl_save(&k, buf, sizeof buf);
	T_ASSERT(n > 0);
	T_EQ_INT((int) strlen(buf), n);
	kl_init(&l);
	T_EQ_INT(kl_load(&l, buf), 0);
	T_ASSERT(memcmp(&k.s, &l.s, sizeof k.s) == 0);
	T_EQ_INT(l.draw, 3);
	T_EQ_INT(l.nhist, k.nhist);
	for (int i = 0; i < k.nhist; i++) T_ASSERT(memcmp(&k.hist[i], &l.hist[i], sizeof(KlState)) == 0);
	while (kl_undo(&k)) {
		T_EQ_INT(kl_undo(&l), 1);
		T_ASSERT(memcmp(&k.s, &l.s, sizeof k.s) == 0);
	}

	/* a small buffer keeps the newest history only */
	kl_new(&k, 99, 1);
	for (int i = 0; i < 10; i++) kl_draw(&k);
	char small[700];
	n = kl_save(&k, small, sizeof small);
	T_ASSERT(n > 0 && n < (int) sizeof small);
	T_EQ_INT(kl_load(&l, small), 0);
	T_ASSERT(l.nhist > 0 && l.nhist < 10);
	T_ASSERT(memcmp(&k.s, &l.s, sizeof k.s) == 0);
	T_ASSERT(memcmp(&k.hist[k.nhist - 1], &l.hist[l.nhist - 1], sizeof(KlState)) == 0);
	T_EQ_INT(kl_save(&k, small, 20), -1);
	kl_free(&k);
	kl_free(&l);
}

static void test_load_rejects_garbage(void)
{
	Klondike k, l;
	kl_init(&k);
	kl_new(&k, 5, 1);
	kl_draw(&k);
	static char good[1 << 14], bad[1 << 14];
	T_ASSERT(kl_save(&k, good, sizeof good) > 0);
	kl_init(&l);
	kl_new(&l, 77, 3);
	KlState before = l.s;
	const char *garbage[] = { "", "hello", "klondike v2\ndraw 1\n", "klondike v1\n", "klondike v1\ndraw 2\n" };
	for (int i = 0; i < 5; i++) T_EQ_INT(kl_load(&l, garbage[i]), -1);
	/* duplicated card */
	strcpy(bad, good);
	char *p = strstr(bad, "\ns ") + 3;
	p[2] = p[0];
	p[3] = p[1];
	T_EQ_INT(kl_load(&l, bad), -1);
	/* truncated */
	strcpy(bad, good);
	bad[strlen(bad) - 10] = '\0';
	T_EQ_INT(kl_load(&l, bad), -1);
	/* bad rank character */
	strcpy(bad, good);
	p = strstr(bad, "\ns ") + 3;
	p[0] = 'Z';
	T_EQ_INT(kl_load(&l, bad), -1);
	T_ASSERT(memcmp(&l.s, &before, sizeof before) == 0);
	T_EQ_INT(l.draw, 3);
	T_EQ_INT(kl_load(&l, good), 0);
	kl_free(&k);
	kl_free(&l);
}

/* saves of positions no real game can reach (crafted files on /mnt/us) must be refused */
static void craft(Klondike *k)
{
	kl_init(k);
	k->draw = 1;
	memset(&k->s, 0, sizeof k->s);
}

static void test_load_rejects_impossible_positions(void)
{
	static char buf[65536];
	Klondike k, l;
	/* 24 stock + 24 waste + A-4 of spades: stock and waste can never hold 48 cards together */
	craft(&k);
	for (int c = 0; c < 4; c++) k.s.c[KL_F0][k.s.n[KL_F0]++] = (unsigned char) c;
	for (int c = 4; c < 28; c++) k.s.c[KL_STOCK][k.s.n[KL_STOCK]++] = (unsigned char) c;
	for (int c = 28; c < 52; c++) k.s.c[KL_WASTE][k.s.n[KL_WASTE]++] = (unsigned char) c;
	T_ASSERT(kl_save(&k, buf, sizeof buf) > 0);
	kl_init(&l);
	T_EQ_INT(kl_load(&l, buf), -1);
	kl_free(&l);
	kl_free(&k);
	/* column 0 with 23 face-down cards under a king: column t never has more than t face-down cards */
	craft(&k);
	int col = KL_T0, used = 0;
	for (int c = 0; c < 52 && k.s.n[col] < 23; c++)
		if (kl_rank(c) != 13) k.s.c[col][k.s.n[col]++] = (unsigned char) c;
	k.s.down[0] = 23;
	k.s.c[col][k.s.n[col]++] = 12; /* K of spades, face up */
	for (int c = 0; c < 52; c++) {
		int inpile = 0;
		for (int i = 0; i < k.s.n[col]; i++) inpile |= k.s.c[col][i] == c;
		if (inpile) continue;
		if (k.s.n[KL_STOCK] < 20) k.s.c[KL_STOCK][k.s.n[KL_STOCK]++] = (unsigned char) c;
		else k.s.c[KL_WASTE][k.s.n[KL_WASTE]++] = (unsigned char) c;
	}
	T_ASSERT(kl_save(&k, buf, sizeof buf) > 0);
	kl_init(&l);
	T_EQ_INT(kl_load(&l, buf), -1);
	kl_free(&l);
	kl_free(&k);
	(void) used;
	/* a valid column can still overflow when a run is dropped on it: column 6 with 6 down + K..2 (12 up) is fine,
	 * but 6 down + 13 up + more is impossible */
}

int main(void)
{
	T_RUN(test_load_rejects_impossible_positions);
	T_RUN(test_deal);
	T_RUN(test_tableau_moves);
	T_RUN(test_foundation_and_waste_moves);
	T_RUN(test_auto_move_king);
	T_RUN(test_draw_one_and_recycle);
	T_RUN(test_draw_three);
	T_RUN(test_undo_chain);
	T_RUN(test_autocomplete);
	T_RUN(test_win);
	T_RUN(test_save_roundtrip);
	T_RUN(test_load_rejects_garbage);
	return T_DONE();
}

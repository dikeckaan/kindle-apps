/* Klondike solitaire rules (pure, no I/O). Cards are 0..51: suit = c / 13 (0 ♠, 1 ♥, 2 ♦, 3 ♣), rank = c % 13 + 1. */
#ifndef KLONDIKE_H
#define KLONDIKE_H

enum { KL_STOCK = 0, KL_WASTE = 1, KL_F0 = 2, KL_T0 = 6, KL_PILES = 13 }; /* foundations 2..5, tableau 6..12 */
#define KL_MAXPILE 24

static inline int kl_rank(int c) { return c % 13 + 1; }
static inline int kl_suit(int c) { return c / 13; }
static inline int kl_red(int c) { return kl_suit(c) == 1 || kl_suit(c) == 2; }

/* One position. Index 0 is the bottom of a pile, n-1 its top. In tableau column t the first down[t] cards are face down;
 * the stock is all face down, waste and foundations are face up. */
typedef struct {
	unsigned char c[KL_PILES][KL_MAXPILE];
	unsigned char n[KL_PILES];
	unsigned char down[7];
} KlState;

typedef struct {
	KlState s;
	int draw;       /* 1 or 3 cards per stock tap */
	KlState *hist;  /* undo stack (positions before each action), oldest first */
	int nhist, cap;
} Klondike;

void kl_init(Klondike *k);                         /* empty game, no history (call before kl_new/kl_load) */
void kl_free(Klondike *k);                         /* releases the undo stack */
void kl_new(Klondike *k, unsigned seed, int draw); /* shuffles (Fisher-Yates, own PRNG) and deals; clears undo */

int kl_draw(Klondike *k); /* stock → waste (draw cards), or recycles the waste when the stock is empty; 1 if anything happened */
/* Moves the cards of pile `from` starting at `index` (must be the top card unless from is a tableau column) onto `to`.
 * Legal: waste/tableau/foundation → foundation (single card, same suit, next rank, A on empty);
 * waste/tableau run/foundation → tableau (alternating colour, one rank lower; only K-headed runs onto an empty column).
 * A newly exposed face-down tableau card is turned up. */
int kl_can_move(const Klondike *k, int from, int index, int to);
int kl_move(Klondike *k, int from, int index, int to);  /* 1 on success (undo recorded) */
int kl_best_target(const Klondike *k, int from, int index); /* where kl_auto_move would go, or -1 */
int kl_auto_move(Klondike *k, int from, int index);      /* moves to the best legal place; destination pile or -1 */
int kl_undo(Klondike *k);                                /* 1 on success; undoes moves, draws and recycles */

int kl_can_autocomplete(const Klondike *k); /* stock and waste empty, every tableau card face up, not yet won */
int kl_autocomplete_step(Klondike *k);      /* one legal foundation move; 1 if made */
int kl_won(const Klondike *k);

/* "klondike v1" text: draw mode, current position and the undo history (oldest entries dropped if cap is too small). */
int kl_save(const Klondike *k, char *buf, int cap); /* bytes written (NUL-terminated), -1 if not even the position fits */
int kl_load(Klondike *k, const char *buf);          /* 0 ok, -1 corrupt (k untouched) */
#endif

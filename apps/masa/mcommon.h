/* Masa Oyunları: small helpers shared by the pure game modules (clock, random numbers, search limits). */
#ifndef MASA_MCOMMON_H
#define MASA_MCOMMON_H
#include <stdint.h>
#include <time.h>

static inline long masa_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long) ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* xorshift32; state must not be 0 */
static inline uint32_t masa_rand(uint32_t *s)
{
	uint32_t x = *s ? *s : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *s = x;
}

/* what an AI may spend: time_ms 0 = no time limit; max_depth / max_playouts 0 = the level's default */
typedef struct {
	int level;        /* 1 Kolay, 2 Orta, 3 Zor */
	int time_ms;
	int max_depth;    /* alpha-beta games */
	int max_playouts; /* Go */
	uint32_t seed;
} MasaLimits;

typedef struct {
	long nodes;   /* positions searched (or playouts for Go) */
	int depth;    /* deepest completed iteration */
	long ms;      /* time spent */
	int score;    /* best score from the mover's point of view (game specific scale) */
} MasaStats;

#endif

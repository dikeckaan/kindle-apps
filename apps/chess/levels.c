#include "levels.h"

#ifndef N_
#define N_(s) (s) /* translatable; shown with T() */
#endif

/* Stockfish 11: UCI_Elo 1350..2850. On the Kindle (~130 kN/s) full strength at 4 s is still very strong. */
static const Level levels[LEVEL_COUNT] = {
	{ N_("Çok Kolay"), 0, 0, 0, 1, 100 },
	{ N_("Kolay"), 1, 1350, 20, 0, 500 },
	{ N_("Orta"), 1, 1600, 20, 0, 1000 },
	{ N_("İyi"), 1, 1900, 20, 0, 1500 },
	{ N_("Zor"), 1, 2200, 20, 0, 2500 },
	{ N_("Usta"), 0, 0, 20, 0, 4000 },
};

const Level *level_get(int level)
{
	if (level < 1 || level > LEVEL_COUNT) level = 3;
	return &levels[level - 1];
}

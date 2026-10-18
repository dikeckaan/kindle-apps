/* Difficulty levels → UCI engine settings (pure). */
#ifndef LEVELS_H
#define LEVELS_H
typedef struct {
	const char *name;
	int limit_strength; /* UCI_LimitStrength */
	int elo;            /* UCI_Elo when limit_strength */
	int skill;          /* Skill Level 0..20 */
	int depth;          /* go depth N (0 = use movetime) */
	int movetime_ms;
} Level;
#define LEVEL_COUNT 6
const Level *level_get(int level); /* 1..6; out of range → 3 */
#endif

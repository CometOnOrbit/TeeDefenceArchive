#ifndef GAME_SERVER_MMO_EXP_H
#define GAME_SERVER_MMO_EXP_H

// Exp required to reach a given level
static inline int ExpForLevel(int Level)
{
	if(Level <= 1) return 18;
	return Level * (Level - 1) * 24;
}

#endif // GAME_SERVER_MMO_EXP_H

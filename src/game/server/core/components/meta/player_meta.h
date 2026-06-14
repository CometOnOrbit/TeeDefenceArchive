#ifndef GAME_SERVER_CORE_COMPONENTS_META_PLAYER_META_H
#define GAME_SERVER_CORE_COMPONENTS_META_PLAYER_META_H

#include <base/system.h>

enum
{
	MAX_META_DUTIES = 8,
	MAX_META_ACHIEVEMENTS = 32,
	META_DATA_MAX = 4096,
};

struct SPlayerMetaData
{
	char m_aDay[16];
	int m_aDutyProgress[MAX_META_DUTIES];
	bool m_aDutyClaimed[MAX_META_DUTIES];
	bool m_aaAchievements[MAX_META_ACHIEVEMENTS];
	int m_aAchProgress[MAX_META_ACHIEVEMENTS];
	char m_aTraitId[32];
	bool m_TraitLocked;
};

void PlayerMeta_Reset(SPlayerMetaData *pMeta);
bool PlayerMeta_Parse(const char *pJson, SPlayerMetaData *pMeta);
void PlayerMeta_Serialize(const SPlayerMetaData *pMeta, char *pOut, int OutSize);
void PlayerMeta_EnsureDay(SPlayerMetaData *pMeta);

#endif

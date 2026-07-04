#ifndef GAME_SERVER_CORE_COMPONENTS_ACCOUNTS_PROFESSION_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_ACCOUNTS_PROFESSION_DATA_H

enum EProfession
{
	PROF_NONE = -1,
	PROF_WARRIOR = 0,
	PROF_MAGE,
	PROF_ARCHER,
	PROF_ASSASSIN,
	PROF_HEALER,
	PROF_FARMER,
	PROF_MINER,
	PROF_FISHERMAN,
	PROF_NUM
};

struct SProfessionDef
{
	EProfession m_Type;
	const char *m_pName;
	const char *m_pDescription;
	int m_BaseHP;
	float m_HPPerLevel;
	float m_AttackPerLevel;
	float m_DefensePerLevel;
	float m_ManaPerLevel;
	bool m_bCombatClass;
};

class CProfessionData
{
public:
	static const SProfessionDef *GetDef(EProfession Type);
	static const SProfessionDef *GetDef(int Type);
	static int NumProfessions() { return static_cast<int>(PROF_NUM); }
	static const char *GetName(EProfession Type);
	static bool IsCombat(EProfession Type);
	static EProfession FindByName(const char *pName);

	static SProfessionDef m_aDefs[static_cast<int>(PROF_NUM)];
	static void InitDefs();
};

#endif

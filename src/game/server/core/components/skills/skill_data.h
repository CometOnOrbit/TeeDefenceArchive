#ifndef GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_DATA_H

enum
{
	MAX_SKILLS = 16,
	SKILL_EMOTICON_NONE = -1,
	NUM_SKILL_EMOTICONS = 16,
};

struct SSkillDescription
{
	int m_Id;
	char m_aKey[32];
	bool m_Passive;
	bool m_AutoLearn;
	int m_ManaCostPct;
	int m_CooldownTicks;
	int m_LearnCostHearts;
	int m_Distance;
	int m_Radius;
	int m_Heal;
	int m_Repair;
};

struct SSkillInstance
{
	int m_SkillId;
	bool m_Learned;
	int m_EmoticonBind;
	int m_CooldownEnd;
};

const char *SkillEmoticonName(int EmoticonId);

#endif

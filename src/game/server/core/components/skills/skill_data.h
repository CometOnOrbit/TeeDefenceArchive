#ifndef GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_DATA_H

enum
{
	MAX_SKILLS = 52,
	SKILL_EMOTICON_NONE = -1,
	NUM_SKILL_EMOTICONS = 16,
	SKILL_DEFAULT_MAX_LEVEL = 5,
	SKILL_DEFAULT_USES_PER_LEVEL = 15,
	SKILL_PROFICIENCY_WINDOW_SEC = 60,
	SKILL_PROFICIENCY_MAX_PER_WINDOW = 4,
	SKILL_COMBAT_RECENCY_SEC = 45,
	SKILL_SAVE_DEBOUNCE_SEC = 30,
	SKILL_EFFECT_BONUS_BP = 5, // +5% effect per level above 1
};

enum ESkillCategory
{
	SKILL_CAT_ATTACK = 0,
	SKILL_CAT_CONTROL,
	SKILL_CAT_HEAL,
	SKILL_CAT_BUFF,
	SKILL_CAT_MOBILITY,
	SKILL_CAT_UTILITY,
	SKILL_CAT_NUM,
};

struct SSkillDescription
{
	int m_Id;
	char m_aKey[32];
	char m_aName[64];
	ESkillCategory m_Category = SKILL_CAT_UTILITY;
	bool m_Passive;
	bool m_AutoLearn;
	int m_ManaCostPct;
	int m_CooldownTicks;
	int m_LearnCostHearts;
	int m_Distance;
	int m_Radius;
	int m_Heal;
	int m_Damage;
	int m_Repair;
	int m_MaxLevel;
	int m_UsesPerLevel;
};

struct SSkillInstance
{
	int m_SkillId;
	bool m_Learned;
	int m_Level;
	int m_UseCount;
	int m_EmoticonBind;
	int m_CooldownEnd;
};

int SkillScaleInt(int Base, int Level);
float SkillLevelEffectMul(int Level);
float SkillLevelCooldownMul(int Level);
int SkillUsesRequiredForLevel(int Level, int UsesPerLevel);
int ScaleRewardByLevelGap(int PlayerLevel, int MobLevel, int Amount);

const char *SkillEmoticonName(int EmoticonId);
ESkillCategory SkillCategoryFromName(const char *pName);
const char *SkillCategoryLabel(ESkillCategory Cat);

#endif

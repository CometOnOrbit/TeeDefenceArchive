#ifndef GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_DEFS_H
#define GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_DEFS_H

#include <base/system.h>

enum ESkillEffectType
{
	SKILL_EFFECT_DAMAGE_BOOST,
	SKILL_EFFECT_DEFENSE_BOOST,
	SKILL_EFFECT_HEALTH_BOOST,
	SKILL_EFFECT_CRIT_CHANCE,
	SKILL_EFFECT_COUNT,
};

struct SMMOSkillDef
{
	int m_ID;
	const char *m_pName;
	const char *m_pDesc;
	int m_MaxLevel;
	int m_ReqPlayerLevel;
	ESkillEffectType m_Effect;
	float m_EffectPerLevel;
};

// Flat skill definitions
static constexpr int NUM_MMO_SKILLS = 8;

static inline const SMMOSkillDef g_aMMOSkillDefs[NUM_MMO_SKILLS] =
{
	{0, "Power Strike", "增加物理攻击力", 10, 1, SKILL_EFFECT_DAMAGE_BOOST, 2.0f},
	{1, "Iron Skin", "增加物理防御力", 10, 1, SKILL_EFFECT_DEFENSE_BOOST, 1.0f},
	{2, "Vitality", "增加最大生命值", 10, 2, SKILL_EFFECT_HEALTH_BOOST, 10.0f},
	{3, "Precision", "增加暴击率", 5, 3, SKILL_EFFECT_CRIT_CHANCE, 2.0f},
	{4, "Berserker", "大幅增加攻击 (低防御)", 5, 5, SKILL_EFFECT_DAMAGE_BOOST, 5.0f},
	{5, "Fortress", "大幅增加防御 (低攻击)", 5, 5, SKILL_EFFECT_DEFENSE_BOOST, 3.0f},
	{6, "Dragon Heart", "大量增加最大生命值", 5, 8, SKILL_EFFECT_HEALTH_BOOST, 25.0f},
	{7, "Assassin's Eye", "大幅增加暴击率", 3, 10, SKILL_EFFECT_CRIT_CHANCE, 5.0f},
};

// Player's learned skill state
struct SMMOSkillState
{
	int m_SkillID;
	int m_Level;
};

static constexpr int MAX_PLAYER_MMO_SKILLS = NUM_MMO_SKILLS;

#endif

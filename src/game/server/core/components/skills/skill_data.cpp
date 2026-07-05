#include "skill_data.h"

#include <base/math.h>
#include <base/system.h>

const char *SkillEmoticonName(int EmoticonId)
{
	if(EmoticonId < 0)
		return "none";
	static const char *const s_apNames[NUM_SKILL_EMOTICONS] = {
		"OOP", "!!!", "❤", "Drop", "...", "Music",
		"Sorry", "(0.0)", "Angry", ">:(", "#$$>:(", "#!!*#*!",
		"Zzz", "WTF", ":D", "???",
	};
	if(EmoticonId >= 0 && EmoticonId < NUM_SKILL_EMOTICONS)
		return s_apNames[EmoticonId];
	return "?";
}

ESkillCategory SkillCategoryFromName(const char *pName)
{
	if(!pName || !pName[0])
		return SKILL_CAT_UTILITY;
	if(str_comp_nocase(pName, "attack") == 0) return SKILL_CAT_ATTACK;
	if(str_comp_nocase(pName, "control") == 0) return SKILL_CAT_CONTROL;
	if(str_comp_nocase(pName, "heal") == 0) return SKILL_CAT_HEAL;
	if(str_comp_nocase(pName, "buff") == 0) return SKILL_CAT_BUFF;
	if(str_comp_nocase(pName, "mobility") == 0) return SKILL_CAT_MOBILITY;
	if(str_comp_nocase(pName, "utility") == 0) return SKILL_CAT_UTILITY;
	return SKILL_CAT_UTILITY;
}

float SkillLevelEffectMul(int Level)
{
	const int L = maximum(1, Level);
	return 1.f + (SKILL_EFFECT_BONUS_BP / 100.f) * (float)(L - 1);
}

float SkillLevelCooldownMul(int Level)
{
	const int L = maximum(1, Level);
	return 1.f / (1.f + 0.04f * (float)(L - 1));
}

int SkillScaleInt(int Base, int Level)
{
	if(Base <= 0 || Level <= 1)
		return Base;
	return maximum(1, (int)(Base * SkillLevelEffectMul(Level) + 0.5f));
}

int SkillUsesRequiredForLevel(int Level, int UsesPerLevel)
{
	const int PerLevel = maximum(1, UsesPerLevel);
	const int L = maximum(1, Level);
	return PerLevel * L * L;
}

int ScaleRewardByLevelGap(int PlayerLevel, int MobLevel, int Amount)
{
	if(Amount <= 0)
		return 0;
	const int Diff = PlayerLevel - MobLevel;
	if(Diff <= 2)
		return Amount;
	const int PenaltySteps = Diff - 2;
	const int MulPct = maximum(10, 100 - PenaltySteps * 20);
	return maximum(1, Amount * MulPct / 100);
}

const char *SkillCategoryLabel(ESkillCategory Cat)
{
	switch(Cat)
	{
	case SKILL_CAT_ATTACK: return "攻击魔法";
	case SKILL_CAT_CONTROL: return "控制魔法";
	case SKILL_CAT_HEAL: return "恢复魔法";
	case SKILL_CAT_BUFF: return "增益魔法";
	case SKILL_CAT_MOBILITY: return "位移魔法";
	case SKILL_CAT_UTILITY: return "辅助魔法";
	default: return "魔法";
	}
}

#ifndef GAME_SERVER_CORE_COMPONENTS_CONTENT_CONTENT_TYPES_H
#define GAME_SERVER_CORE_COMPONENTS_CONTENT_CONTENT_TYPES_H

#include <base/vmath.h>

class CCharacter;
class CPlayer;

enum EEffectTrigger
{
	TRIGGER_WEAPON_FIRE = 0,
	TRIGGER_RELOAD,
	TRIGGER_DEAL_DAMAGE,
	TRIGGER_TAKE_DAMAGE,
	TRIGGER_PROJECTILE_HIT,
	TRIGGER_LASER_HIT,
	TRIGGER_TURRET_FIRE,
	TRIGGER_MINE,
	TRIGGER_TICK,
	TRIGGER_SPAWN,
	TRIGGER_DEATH,
	NUM_EFFECT_TRIGGERS,
};

enum EEffectFlags
{
	EFFECT_FLAG_EXPLOSIVE = 1 << 0,
	EFFECT_FLAG_FUSION = 1 << 1,
	EFFECT_FLAG_ELECTRON = 1 << 2,
	EFFECT_FLAG_MANUAL_TURRET = 1 << 3,
};

enum
{
	MAX_CONTENT_EFFECTS = 48,
	MAX_CONTENT_ABILITIES = 16,
	MAX_CONTENT_TRAITS = 16,
	MAX_CONTENT_ENEMIES = 32,
	CONTENT_KEY_LEN = 32,
	MAX_CHARACTER_STATUSES = 8,
};

struct SEffectParams
{
	int m_ReloadPerStack;
	int m_MineCdPerStack;
	int m_TurretCdPerStack;
	int m_DamagePerStack;
	int m_RegenPerStack;
	int m_ForcePerStack;
	int m_RadiusBase;
	int m_RadiusPerStack;
	int m_RadiusPerFusionStack;
	int m_DurationPerStack;
	int m_DurationTicks;
	int m_TickInterval;
	int m_DotPerStack;
	int m_MoveDotPerStack;
	int m_AbsorbPerStack;
	int m_HealPerStack;
	int m_ArmorPerStack;
	int m_ChainsPerStack;
	int m_CritBonus;
	int m_SpreadDegPerStack;
	int m_AmmoBonusPerStack;
	int m_RangeBonusPerStack;
	int m_BurnStacks;
	float m_SlowMul;
};

struct SEffectDef
{
	char m_aId[CONTENT_KEY_LEN];
	int m_LegacyItem;
	bool m_IsStatus;
	bool m_aTriggers[NUM_EFFECT_TRIGGERS];
	SEffectParams m_Params;
};

struct SAbilityDef
{
	char m_aId[CONTENT_KEY_LEN];
	int m_CooldownTicks;
	int m_CostMp;
	int m_Distance;
	int m_Radius;
	int m_Heal;
	int m_Repair;
};

struct STraitDef
{
	char m_aId[CONTENT_KEY_LEN];
	float m_DamageMul;
	float m_ReloadMul;
	float m_SkillCdMul;
	int m_MiningLuckBonus;
	int m_MineCdBonus;
	int m_MaxHealthBonus;
	int m_ShieldBonus;
	int m_LifestealBonus;
};

enum
{
	MAX_ENEMY_LOOT = 6,
};

struct SEnemyLootEntry
{
	int m_ItemId;
	int m_MinNum;
	int m_MaxNum;
	int m_Weight;
};

struct SEnemyDef
{
	char m_aId[CONTENT_KEY_LEN];
	int m_LegacyZombId;
	float m_HpMul;
	int m_WaveMin;
	int m_SpawnWeight;
	bool m_aTags[8];
	char m_aaTagNames[8][24];
	int m_NumTags;
	SEnemyLootEntry m_aLoot[MAX_ENEMY_LOOT];
	int m_NumLoot;
	int m_BonusHearts;
};

struct CEffectContext
{
	CCharacter *m_pAttacker;
	CCharacter *m_pVictim;
	CPlayer *m_pPlayer;
	const char *m_pExtraJson;
	int m_Weapon;
	int m_InDamage;
	int m_OutDamage;
	int m_OutReloadDelta;
	int m_OutMineCd;
	int m_OutTurretCd;
	float m_OutForceMul;
	int m_Flags;
	int m_ElectronStacks;
	int m_FusionStacks;
	int m_ExplosionStacks;
	int m_TurretSpreadDeg;
	int m_TurretRangeBonus;
	int m_AmmoRegenTime;
	int m_MiningCritBonus;
	int m_ChainLightningStacks;
	vec2 m_Source;
	vec2 m_Force;
	bool m_ManualTurret;
};

EEffectTrigger EffectTriggerFromString(const char *pStr);

#endif

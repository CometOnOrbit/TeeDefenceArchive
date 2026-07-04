#ifndef GAME_SERVER_CORE_ATTRIBUTE_TYPES_H
#define GAME_SERVER_CORE_ATTRIBUTE_TYPES_H

enum class AttributeIdentifier : int
{
	Unknown = -1,
	DMG = 1,                     // Attribute identifier for damage
	AttackSPD = 2,               // Attribute identifier for attack speed
	CritDMG = 3,                 // Attribute identifier for critical damage
	Crit = 4,                    // Attribute identifier for critical chance
	HP = 5,                      // Attribute identifier for health points
	Lucky = 6,                   // Attribute identifier for luck
	MP = 7,                      // Attribute identifier for mana points
	Vampirism = 8,               // Attribute identifier for vampirism
	AmmoRegen = 9,               // Attribute identifier for ammo regeneration
	Ammo = 10,                   // Attribute identifier for ammo
	Efficiency = 11,             // Attribute identifier for efficiency
	Extraction = 12,             // Attribute identifier for extraction
	HammerDMG = 13,              // Attribute identifier for hammer damage
	GunDMG = 14,                 // Attribute identifier for gun damage
	ShotgunDMG = 15,             // Attribute identifier for shotgun damage
	GrenadeDMG = 16,             // Attribute identifier for grenade damage
	RifleDMG = 17,               // Attribute identifier for rifle damage
	LuckyDropItem = 18,          // Attribute identifier for lucky drop item
	EidolonPWR = 19,             // Attribute identifier for eidolon power
	GoldCapacity = 20,           // Attribute identifier for gold capacity
	Patience = 21,               // Attribute identifier for fisherman
	ProductCapacity = 22,        // Attribute identifier for loader
	ATTRIBUTES_NUM,              // The number of total attributes
	// TDA-specific progression attributes (custom values, don't overlap MRPG's)
	Level = 100,                 // Player level (was m_MMOLevel)
	Experience = 101,            // Player experience (was m_MMOExp)
	Gold = 102,                  // Player gold (was m_MMOGold)
	SkillPoints = 103,           // Skill points (was m_MMOSkillPoints)
	Reputation = 104,            // Reputation/prestige (was m_MMOReputation)
	// TRPG六维属性 (replaced Attack/Defense 2026-06-30)
	// Note: Attack=105/Defense=106 removed, use these for all combat calculations
	STR = 105,                   // 力量: 近战伤害
	DEX = 106,                   // 敏捷: 远程伤害 + 移动速度
	CON = 107,                   // 体质: 最大HP + 自然回血
	INT = 108,                   // 智力: MP上限 + 技能强度
	WIS = 109,                   // 智慧: 治疗量 + Buff持续时间
	CHA = 110,                   // 魅力: 声望 + NPC折扣 + 组队
	// Item attribute names (kept for backward compat with item JSONs)
	// Items can use "attack" and "defense" as generic combat attribute slots
	Attack = 111,                // 攻击 (item attribute, not player stat)
	Defense = 112,               // 防御 (item attribute, not player stat)
	MAX_ATTRIBUTES,              // Total count including TDA-specific
};

inline bool IsDamageAttributeIdentifier(AttributeIdentifier ID)
{
	switch(ID)
	{
		case AttributeIdentifier::DMG:
		case AttributeIdentifier::HammerDMG:
		case AttributeIdentifier::GunDMG:
		case AttributeIdentifier::ShotgunDMG:
		case AttributeIdentifier::GrenadeDMG:
		case AttributeIdentifier::RifleDMG:
			return true;
		default:
			return false;
	}
}

#endif

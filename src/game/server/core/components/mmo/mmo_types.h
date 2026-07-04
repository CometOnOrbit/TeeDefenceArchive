#ifndef GAME_SERVER_CORE_COMPONENTS_MMO_MMO_TYPES_H
#define GAME_SERVER_CORE_COMPONENTS_MMO_MMO_TYPES_H

#include <base/system.h>
#include <base/vmath.h>
#include <engine/shared/protocol.h>
#include <game/server/core/mmo_context.h>
#include <game/server/core/attribute_types.h>
#include <vector>

#include <game/server/data_center.h>

// Alias MRPG types from mmo_context.h
using MMOItemGroup = ItemGroup;
using MMOItemType = ItemType;

// Rarity constants (MRPG uses a simple int scale)
constexpr int MMORarity_Unknown = -1;
constexpr int MMORarity_Common = 0;
constexpr int MMORarity_Uncommon = 1;
constexpr int MMORarity_Rare = 2;
constexpr int MMORarity_Epic = 3;
constexpr int MMORarity_Legendary = 4;
using MMORarity = int;

using MMOItemID = int;
static constexpr int MMO_INVALID_ITEM_ID = -1;

// Flags
enum
{
	MMO_ITEMFLAG_CANT_DROP  = 1 << 0,
	MMO_ITEMFLAG_CANT_TRADE = 1 << 1,
};

// ─── Rarity ──────────────────────────────────────────────────────────

static inline MMORarity MMORarityFromName(const char *pName)
{
	if(!pName) return MMORarity_Unknown;
	if(str_comp_nocase(pName, "common") == 0) return MMORarity_Common;
	if(str_comp_nocase(pName, "uncommon") == 0) return MMORarity_Uncommon;
	if(str_comp_nocase(pName, "rare") == 0) return MMORarity_Rare;
	if(str_comp_nocase(pName, "epic") == 0) return MMORarity_Epic;
	if(str_comp_nocase(pName, "legendary") == 0) return MMORarity_Legendary;
	return MMORarity_Unknown;
}

static inline const char *MMORarityToStars(MMORarity R)
{
	switch(R)
	{
	case MMORarity_Uncommon:  return "★★";
	case MMORarity_Rare:      return "★★★";
	case MMORarity_Epic:      return "★★★★";
	case MMORarity_Legendary: return "★★★★★";
	default:                  return "★";
	}
}

static inline int MMORarityMultiplier(MMORarity R)
{
	switch(R)
	{
	case MMORarity_Uncommon:  return 120; // 1.2x (as integer %)
	case MMORarity_Rare:      return 150; // 1.5x
	case MMORarity_Epic:      return 200; // 2x
	case MMORarity_Legendary: return 300; // 3x
	default:                  return 100;
	}
}

// ─── String → enum helpers (for JSON loading) ───────────────────────

static inline MMOItemGroup MMOItemGroupFromName(const char *pName)
{
	if(!pName) return ItemGroup::Unknown;
	if(str_comp_nocase(pName, "quest") == 0) return ItemGroup::Quest;
	if(str_comp_nocase(pName, "currency") == 0) return ItemGroup::Currency;
	if(str_comp_nocase(pName, "usable") == 0) return ItemGroup::Usable;
	if(str_comp_nocase(pName, "resource") == 0) return ItemGroup::Resource;
	if(str_comp_nocase(pName, "other") == 0) return ItemGroup::Other;
	if(str_comp_nocase(pName, "settings") == 0) return ItemGroup::Settings;
	if(str_comp_nocase(pName, "equipment") == 0) return ItemGroup::Equipment;
	if(str_comp_nocase(pName, "decoration") == 0) return ItemGroup::Decoration;
	if(str_comp_nocase(pName, "potion") == 0) return ItemGroup::Potion;
	return ItemGroup::Unknown;
}

static inline MMOItemType MMOItemTypeFromName(const char *pName)
{
	if(!pName) return ItemType::Unknown;
	if(str_comp_nocase(pName, "default") == 0) return ItemType::Default;
	if(str_comp_nocase(pName, "equipgun") == 0) return ItemType::EquipGun;
	if(str_comp_nocase(pName, "equiphammer") == 0) return ItemType::EquipHammer;
	if(str_comp_nocase(pName, "equipshotgun") == 0) return ItemType::EquipShotgun;
	if(str_comp_nocase(pName, "equipgrenade") == 0) return ItemType::EquipGrenade;
	if(str_comp_nocase(pName, "equiplaser") == 0) return ItemType::EquipLaser;
	if(str_comp_nocase(pName, "equippickaxe") == 0) return ItemType::EquipPickaxe;
	if(str_comp_nocase(pName, "equiprake") == 0) return ItemType::EquipRake;
	if(str_comp_nocase(pName, "equipfishrod") == 0) return ItemType::EquipFishrod;
	if(str_comp_nocase(pName, "equipgloves") == 0) return ItemType::EquipGloves;
	if(str_comp_nocase(pName, "equiphelmettank") == 0) return ItemType::EquipHelmetTank;
	if(str_comp_nocase(pName, "equiphelmetdps") == 0) return ItemType::EquipHelmetDPS;
	if(str_comp_nocase(pName, "equiphelmethealer") == 0) return ItemType::EquipHelmetHealer;
	if(str_comp_nocase(pName, "equiparmortank") == 0) return ItemType::EquipArmorTank;
	if(str_comp_nocase(pName, "equiparmordps") == 0) return ItemType::EquipArmorDPS;
	if(str_comp_nocase(pName, "equiparmorhealer") == 0) return ItemType::EquipArmorHealer;
	if(str_comp_nocase(pName, "equipeidolon") == 0) return ItemType::EquipEidolon;
	if(str_comp_nocase(pName, "equipotionheal") == 0) return ItemType::EquipPotionHeal;
	if(str_comp_nocase(pName, "equipotionmana") == 0) return ItemType::EquipPotionMana;
	if(str_comp_nocase(pName, "equiptitle") == 0) return ItemType::EquipTitle;
	if(str_comp_nocase(pName, "usesingle") == 0) return ItemType::UseSingle;
	if(str_comp_nocase(pName, "usemultiple") == 0) return ItemType::UseMultiple;
	if(str_comp_nocase(pName, "resourceharvestable") == 0) return ItemType::ResourceHarvestable;
	if(str_comp_nocase(pName, "resourcemineable") == 0) return ItemType::ResourceMineable;
	if(str_comp_nocase(pName, "resourcefishes") == 0) return ItemType::ResourceFishes;
	return ItemType::Unknown;
}

// ─── MMOItemType → WEAPON_* mapping ──────────────────────────────────
// Returns -1 if the item type is not a weapon.
static inline int MMOItemTypeToWeapon(MMOItemType Type)
{
	switch(Type)
	{
	case ItemType::EquipHammer:   return 0; // WEAPON_HAMMER
	case ItemType::EquipGun:      return 1; // WEAPON_GUN
	case ItemType::EquipShotgun:  return 2; // WEAPON_SHOTGUN
	case ItemType::EquipGrenade:  return 3; // WEAPON_GRENADE
	case ItemType::EquipLaser:    return 4; // WEAPON_LASER
	default:                      return -1;
	}
}

// ─── NPC Function Types (MRPG-compatible) ───────────────────────────
enum class EMMONpcFunction
{
	None = -1,
	Default = 0,         // Wandering neutral
	Guardian,             // Defends territory
	Nurse,                // Heals players
	GiveQuest,            // Quest giver
	Shop,                 // Shop NPC
};

// ─── NPC Bot Info (simplified TDA version) ──────────────────────────
struct SMMONpcInfo
{
	EMMONpcFunction m_Function = EMMONpcFunction::Default;
	bool m_Static = false;      // Does not move
	int m_GuardRadius = 800;    // Guardian aggro radius
	char m_aName[32] = {0};     // Display name
};

// ─── Mob Behavior Flags (MRPG-style bitmask) ───────────────────────
enum
{
	MOBFLAG_BEHAVIOR_POISONOUS = 1 << 0,
	MOBFLAG_BEHAVIOR_NEUTRAL = 1 << 1,
	MOBFLAG_BEHAVIOR_SLEEPY = 1 << 2,
	MOBFLAG_BEHAVIOR_SLOWER = 1 << 3,
	MOBFLAG_BEHAVIOR_AMBIENT_CHAT = 1 << 4,
};

static inline int MobBehaviorFlagsFromName(const char *pName)
{
	if(!pName) return 0;
	if(str_comp_nocase(pName, "POISONOUS") == 0) return MOBFLAG_BEHAVIOR_POISONOUS;
	if(str_comp_nocase(pName, "NEUTRAL") == 0) return MOBFLAG_BEHAVIOR_NEUTRAL;
	if(str_comp_nocase(pName, "SLEEPY") == 0) return MOBFLAG_BEHAVIOR_SLEEPY;
	if(str_comp_nocase(pName, "SLOWER") == 0) return MOBFLAG_BEHAVIOR_SLOWER;
	if(str_comp_nocase(pName, "AMBIENT_CHAT") == 0) return MOBFLAG_BEHAVIOR_AMBIENT_CHAT;
	return 0;
}

// ─── Drop Entry ──────────────────────────────────────────────────────
// ─── Drop Entry ──────────────────────────────────────────────────────
struct SMMODropEntry
{
	int m_ItemID = -1;
	int m_Chance = 0;  // 0–100 (percentage)
	int m_MinCount = 1;
	int m_MaxCount = 1;
};

// ─── MMO Mob Runtime Data ────────────────────────────────────────────
// Attached to CPlayer as m_pMMOBotData for bot/dummy players.
struct SMMOBotData
{
	int m_DefID = -1;
	int m_HP = 100;
	int m_MaxHP = 100;
	int m_Attack = 3;
	int m_Defense = 1;
	int m_Level = 1;
	int m_ExpReward = 10;
	int m_GoldMin = 0;
	int m_GoldMax = 0;
	int m_RespawnTicks = 300;  // delay in ticks
	int m_RespawnAtTick = 0;   // absolute tick to respawn at
	int m_DeathTick = 0;
	int m_LastAttackTick = 0;
	vec2 m_SpawnPos = vec2(0,0);
	bool m_IsBoss = false;
	bool m_Respawning = false;
	bool m_IsQuestMob = false;
	bool m_IsNPC = false;
	EMMONpcFunction m_NpcFunction = EMMONpcFunction::None;
	int m_NumActiveForClients = 0;
	int m_aActiveForClients[MAX_CLIENTS]{};
	std::vector<SMMODropEntry> m_vDrops;

	SMMOBotData() = default;
	bool IsAlive() const { return !m_Respawning && m_HP > 0; }
	float GetHPPct() const { return m_MaxHP > 0 ? (float)m_HP / (float)m_MaxHP : 0.f; }
};

// ─── Static Mob Definition ──────────────────────────────────────────
struct SMMOMobDef
{
	int m_ID = -1;
	char m_aName[64] = {0};
	char m_aSkinName[24] = {0};
	int m_SkinColorBody = 0;
	int m_SkinColorFeet = 0;
	int m_Level = 1;
	int m_MaxHP = 50;
	int m_Attack = 3;
	int m_Defense = 1;
	int m_ExpReward = 10;
	int m_GoldMin = 0;
	int m_GoldMax = 0;
	int m_RespawnTicks = 300;
	float m_ActiveRadius = 800.f;
	int m_BehaviorFlags = 0; // bitmask
	bool m_IsBoss = false;
	std::vector<SMMODropEntry> m_vDrops;

		static inline const SMMOMobDef *Get(int ID) { return CDataCenter::FindMobDef(ID); }

	bool IsValid() const { return m_ID >= 0; }
};


// ─── Quest Mob Info ─────────────────────────────────────────────────
struct SMMOQuestMobInfo
{
	int m_QuestItemID = -1;
	bool m_ActiveForClient[MAX_CLIENTS]{};
	void ActivateForClient(int ClientID, bool Active = true)
	{
		if(ClientID >= 0 && ClientID < MAX_CLIENTS)
			m_ActiveForClient[ClientID] = Active;
	}
	void DeactivateAll()
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			m_ActiveForClient[i] = false;
	}
	bool IsActiveForAny() const
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(m_ActiveForClient[i]) return true;
		return false;
	}
};

// ─── Quest NPC Info ─────────────────────────────────────────────────
struct SMMOQuestNpcInfo
{
	bool m_HasAction = false;
	char m_aDialogueStart[128] = {0};
	int m_QuestID = -1;
};

// ─── Zone Def ────────────────────────────────────────────────────────
struct SMMOZoneDef
{
	int m_WorldID = 0;
	char m_aName[64] = {};
	int m_X1 = 0, m_Y1 = 0, m_X2 = 0, m_Y2 = 0; // bounds
	
	struct SZoneMob
	{
		int m_DefID;
		int m_Count;
	};
	
	std::vector<SZoneMob> m_vMobs;
	
	static inline const SMMOZoneDef *Find(int WorldID, const char *pName) { return CDataCenter::FindZone(WorldID, pName); }
	static inline const SMMOZoneDef *FindByPosition(int WorldID, vec2 Pos) { return CDataCenter::FindZoneByPos(WorldID, Pos); }
};

#endif

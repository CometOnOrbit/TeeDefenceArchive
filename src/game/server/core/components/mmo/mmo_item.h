#ifndef GAME_SERVER_CORE_COMPONENTS_MMO_MMO_ITEM_H
#define GAME_SERVER_CORE_COMPONENTS_MMO_MMO_ITEM_H

#include "mmo_types.h"

#include <game/server/data_center.h>
#include <base/system.h>
#include <engine/shared/protocol.h>
#include <deque>
#include <map>
#include <ctime>

// ─── Constants ───────────────────────────────────────────────────────

static constexpr int MMO_INVENTORY_MAX_STACK = 9999;
static constexpr int MMO_SELL_PRICE_DIVISOR = 4;      // 25% of buy/base price (was 50%)
static constexpr int MMO_SELL_TAX_PERCENT = 10;     // tax on gross proceeds
static constexpr int MMO_SELL_DAILY_GOLD_CAP = 2500;  // max gold/day from recycle
static constexpr int MMO_SELL_DAILY_COUNT_CAP = 20;   // max recycle transactions/day
static constexpr int MMO_SELL_MIN_UNIT_PRICE = 1;     // floor per unit after lookup
static constexpr int MMO_ENCHANT_MAX_LEVEL = 15;
static constexpr int MMO_DURABILITY_MAX = 100;

enum class EMMOFireStyle
{
	Default = 0,
	MagneticPulse,
	WallPusher,
	HomingGrenade,
	HammerBlast,
	TeslaChain,
	TrackedPlasma,
	HammerLamp,
};

inline EMMOFireStyle ParseFireStyle(const char *pStr)
{
	if(!pStr || !pStr[0])
		return EMMOFireStyle::Default;
	if(str_comp_nocase(pStr, "magnetic_pulse") == 0)
		return EMMOFireStyle::MagneticPulse;
	if(str_comp_nocase(pStr, "wall_pusher") == 0)
		return EMMOFireStyle::WallPusher;
	if(str_comp_nocase(pStr, "homing_grenade") == 0)
		return EMMOFireStyle::HomingGrenade;
	if(str_comp_nocase(pStr, "hammer_blast") == 0)
		return EMMOFireStyle::HammerBlast;
	if(str_comp_nocase(pStr, "tesla_chain") == 0)
		return EMMOFireStyle::TeslaChain;
	if(str_comp_nocase(pStr, "tracked_plasma") == 0)
		return EMMOFireStyle::TrackedPlasma;
	if(str_comp_nocase(pStr, "hammer_lamp") == 0)
		return EMMOFireStyle::HammerLamp;
	return EMMOFireStyle::Default;
}

inline const char *FireStyleLabelZh(EMMOFireStyle Style)
{
	switch(Style)
	{
	case EMMOFireStyle::MagneticPulse: return "磁脉冲";
	case EMMOFireStyle::WallPusher: return "激光推墙";
	case EMMOFireStyle::HomingGrenade: return "追踪榴弹";
	case EMMOFireStyle::HammerBlast: return "冲击波锤";
	case EMMOFireStyle::TeslaChain: return "特斯拉链";
	case EMMOFireStyle::TrackedPlasma: return "追踪等离子";
	case EMMOFireStyle::HammerLamp: return "飞锤灯";
	default: return nullptr;
	}
}

// Per-item weapon tuning (percent = 100 is vanilla tuning / default reload)
struct SMMOWeaponProfile
{
	bool m_Active = false;
	EMMOFireStyle m_FireStyle = EMMOFireStyle::Default;
	int m_MagneticRadius = 128;
	int m_WallPusherLifeTicks = 0;
	int m_HammerBlastRadius = 128;
	int m_HomingGrenadeSpeed = 18;
	int m_TeslaChainRange = 400;
	int m_TeslaChainTargets = 3;
	int m_TeslaDamageFalloff = 50;
	int m_TrackedPlasmaSpeedMin = 5;
	int m_TrackedPlasmaSpeedMax = 20;
	int m_HammerLampRadius = 380;
	int m_ProjSpeedPercent = 100;
	int m_ProjRangePercent = 100;
	int m_SpreadDegrees = 0;
	int m_ReloadPercent = 100;
	int m_ForcePercent = 100;
	int m_DamageMulPercent = 100;
	int m_CritChance = 0;
	int m_LifestealPercent = 0;
	int m_Multishot = 0;
	int m_FanShots = 0;
	int m_FanSpreadRad = 10;
	int m_Pierce = 0;
	int m_ShotgunPelletsAdd = 0;
	int m_ShotgunPelletCount = 0;
	int m_LaserReachPercent = 100;
	int m_GrenadeSalvoLifetimePercent = 0;
	int m_RecoilPercent = 100;
	bool m_Explosive = false;
	bool m_Pulse = false;
	int m_PulseReach = 400;
};

// ─── Item Definition (static data, loaded from JSON) ──────────────

class CMMOItemDescription
{
public:
	int m_ID = MMO_INVALID_ITEM_ID;
	char m_aName[64] = {0};
	char m_aDescription[128] = {0};
	char m_aNameKey[64] = {0};
	char m_aDescriptionKey[128] = {0};
	ItemGroup m_Group = ItemGroup::Unknown;
	ItemType m_Type = ItemType::Unknown;
	int m_Rarity = MMORarity_Common;
	int m_Flags = 0;
	int m_BasePrice = 0;
	int m_LevelReq = 1;
	int m_Attack = 0;
	int m_Defense = 0;
	int m_PerEnchantAttack = 0;
	int m_PerEnchantDefense = 0;
	std::map<AttributeIdentifier, int> m_vAttributes;
	std::map<AttributeIdentifier, int> m_vPerEnchantAttributes;
	SMMOWeaponProfile m_WeaponProfile;
	bool m_Stackable = true;
	bool m_Enchantable = false;
	bool m_CanDrop = true;
	bool m_CanTrade = true;

	CMMOItemDescription() = default;
	bool IsValid() const { return m_ID > 0; }

	int GetAttributeValue(AttributeIdentifier ID, int Enchant = 0) const
	{
		int Base = 0;
		auto it = m_vAttributes.find(ID);
		if(it != m_vAttributes.end()) Base = it->second;
		int PerEnch = 0;
		auto itPer = m_vPerEnchantAttributes.find(ID);
		if(itPer != m_vPerEnchantAttributes.end()) PerEnch = itPer->second;
		return Base + Enchant * PerEnch;
	}

	// Compatibility getters for mmo_manager.cpp
	int GetID() const { return m_ID; }
	const char* GetName() const { return m_aName; }
	const char* GetDescription() const { return m_aDescription; }
	const char* GetNameKey() const { return m_aNameKey[0] ? m_aNameKey : m_aName; }
	const char* GetDescriptionKey() const { return m_aDescriptionKey[0] ? m_aDescriptionKey : m_aDescription; }
	ItemGroup GetGroup() const { return m_Group; }
	ItemType GetType() const { return m_Type; }
	int GetInitialPrice() const { return m_BasePrice; }
	int GetLevelReq() const { return m_LevelReq; }
	int GetRarity() const { return m_Rarity; }
	int GetFlags() const { return m_Flags; }
	bool IsStackable() const { return m_Stackable; }
	bool IsEnchantable() const { return m_Enchantable; }
	bool CanDrop() const { return m_CanDrop; }
	bool CanTrade() const { return m_CanTrade; }
	bool IsEquipmentSlot() const
	{
		return (m_Group == ItemGroup::Equipment || m_Group == ItemGroup::Potion) && m_Type != ItemType::Default;
	}
	bool HasWeaponProfile() const { return m_WeaponProfile.m_Active; }
	const SMMOWeaponProfile &GetWeaponProfile() const { return m_WeaponProfile; }
	int GetWeaponAttackBonus(int Enchant = 0) const
	{
		int Bonus = GetAttributeValue(AttributeIdentifier::Attack, Enchant);
		if(Bonus <= 0 && m_Attack > 0)
			Bonus = m_Attack + Enchant * m_PerEnchantAttack;
		return Bonus > 0 ? Bonus : 0;
	}
	int GetEngineWeaponDamageBonus(int EngineWeapon, int Enchant = 0) const;
	int GetAttackSpeedPercent(int Enchant = 0) const;
	int GetSettings() const { return m_Flags; }
	void SetID(int ID) { m_ID = ID; }

	// Global registry — now lives in CDataCenter
	static inline CMMOItemDescription *Get(int ItemID) { return const_cast<CMMOItemDescription*>(CDataCenter::FindItemDef(ItemID)); }
	static inline std::map<int, CMMOItemDescription>& Data() { return const_cast<std::map<int, CMMOItemDescription>&>(CDataCenter::GetItemDefs()); }
	static inline CMMOItemDescription *GetDesc(int ID) { return Get(ID); }
};

// ─── Runtime Item Instance ───────────────────────────────────────────

class CMMOItem
{
public:
	int m_ItemID = MMO_INVALID_ITEM_ID;
	int m_Count = 0;
	int m_Enchant = 0;
	int m_Durability = MMO_DURABILITY_MAX;
	int m_Flags = 0;
	time_t m_ExpiresAt = 0;

	CMMOItem() = default;

	CMMOItem(int ItemID, int Count = 1, int Enchant = 0, int Durability = MMO_DURABILITY_MAX, time_t ExpiresAt = 0)
		: m_ItemID(ItemID), m_Count(Count), m_Enchant(Enchant), m_Durability(Durability), m_ExpiresAt(ExpiresAt)
	{
		ClampDurability();
	}

	bool IsValid() const { return m_ItemID > 0 && m_Count > 0; }
	void ClampDurability() { if(m_Durability > MMO_DURABILITY_MAX) m_Durability = MMO_DURABILITY_MAX; }
	bool IsExpired() const { return m_ExpiresAt > 0 && time(nullptr) >= m_ExpiresAt; }
	bool IsEnchantable() const
	{
		const auto* pDef = CMMOItemDescription::Get(m_ItemID);
		return pDef && pDef->m_Enchantable && m_Enchant < MMO_ENCHANT_MAX_LEVEL;
	}
	CMMOItemDescription* GetInfo() const { return CMMOItemDescription::Get(m_ItemID); }

	// Method-style API for compatibility with sub-agent changes
	int GetID() const { return m_ItemID; }
	int GetValue() const { return m_Count; }
	int GetEnchant() const { return m_Enchant; }
	int GetDurability() const { return m_Durability; }
	int GetSettings() const { return m_Flags; }
	time_t GetExpiresAt() const { return m_ExpiresAt; }

	void SetID(int ID) { m_ItemID = ID; }
	void SetValue(int Val) { m_Count = Val; }
	void SetEnchant(int Ench) { m_Enchant = Ench; }
	void SetDurability(int Dur) { m_Durability = Dur; ClampDurability(); }
	void SetSettings(int Flags) { m_Flags = Flags; }
	void SetExpiresAt(time_t T) { m_ExpiresAt = T; }

	// Stackability: same ID + same enchant + same flags = stackable
	bool CanStackWith(const CMMOItem& Other) const
	{
		if(m_ItemID != Other.m_ItemID) return false;
		if(m_Enchant != Other.m_Enchant) return false;
		if(m_Flags != Other.m_Flags) return false;
		return true;
	}
};

// ─── Player Inventory (deque-based) ──────────────────────────────────

class CMMOInventory
{
public:
	std::deque<CMMOItem> m_aItems;

	CMMOInventory() = default;

	int Count() const { return (int)m_aItems.size(); }
	bool IsEmpty() const { return m_aItems.empty(); }
	std::deque<CMMOItem>::iterator begin() { return m_aItems.begin(); }
	std::deque<CMMOItem>::const_iterator begin() const { return m_aItems.begin(); }
	std::deque<CMMOItem>::iterator end() { return m_aItems.end(); }
	std::deque<CMMOItem>::const_iterator end() const { return m_aItems.end(); }
	size_t size() const { return m_aItems.size(); }

	bool Add(const CMMOItem& Item);
	bool Add(int ItemID, int Count = 1, int Enchant = 0, int Durability = MMO_DURABILITY_MAX, time_t ExpiresAt = 0);

	bool RemoveAt(int Index, int Count = 1);
	bool RemoveByID(int ItemID, int Count = 1);

	CMMOItem* Get(int Index) { return (Index >= 0 && Index < (int)m_aItems.size()) ? &m_aItems[Index] : nullptr; }
	const CMMOItem* Get(int Index) const { return (Index >= 0 && Index < (int)m_aItems.size()) ? &m_aItems[Index] : nullptr; }

	// deque-style operator[] for compatibility
	CMMOItem& operator[](size_t Index) { return m_aItems[Index]; }
	const CMMOItem& operator[](size_t Index) const { return m_aItems[Index]; }

	int FindByID(int ItemID) const;
	int CountByID(int ItemID) const;


	void RemoveExpired();

	bool ToDBRow(int Slot, int& ItemID, int& Count, int& Enchant, int& Durability, time_t& ExpiresAt) const;
	int DBSize() const { return (int)m_aItems.size(); }
	void clear() { m_aItems.clear(); }
};

// ─── MRPG Compatibility Aliases ──────────────────────────────────────
using CItemsContainer = CMMOInventory;
using CItem = CMMOItem;
using CItemDescription = CMMOItemDescription;
using ItemIdentifier = int;

/// Static helpers following MRPG's CInventoryManager pattern
class CInventoryManager {
public:
	static bool AddItem(CMMOInventory& Inv, int ItemID, int Count = 1, int Enchant = 0, int Durability = MMO_DURABILITY_MAX, time_t ExpiresAt = 0) { return Inv.Add(ItemID, Count, Enchant, Durability, ExpiresAt); }
	static bool RemoveItemByID(CMMOInventory& Inv, int ItemID, int Count = 1) { return Inv.RemoveByID(ItemID, Count); }
	static bool RemoveItemAt(CMMOInventory& Inv, int Index, int Count = 1) { return Inv.RemoveAt(Index, Count); }
};
#endif
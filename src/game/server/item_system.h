/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ITEM_SYSTEM_H
#define GAME_SERVER_ITEM_SYSTEM_H

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <game/server/alloc.h>

class CGameContext;
class IStorage;

enum
{
	ITEM_LOG = 0,
	ITEM_COAL,
	ITEM_COPPER,
	ITEM_IRON,
	ITEM_GOLD,
	ITEM_DIAMOND,
	ITEM_ENEGRY,
	ITEM_ZOMBIEHEART,

	ITEM_SWORD_LOG = 8,
	ITEM_AXE_LOG,
	ITEM_PICKAXE_LOG,

	ITEM_SWORD_IRON = 11,
	ITEM_AXE_COPPER,
	ITEM_PICKAXE_COPPER,

	ITEM_AXE_IRON = 14,
	ITEM_PICKAXE_IRON,

	ITEM_SWORD_GOLDEN = 16,
	ITEM_AXE_GOLDEN,
	ITEM_PICKAXE_GOLDEN,

	ITEM_SWORD_DIAMOND = 19,
	ITEM_AXE_DIAMOND,
	ITEM_PICKAXE_DIAMOND,

	ITEM_SWORD_ENEGRY = 22,
	ITEM_PICKAXE_ENEGRY,

	ITEM_TURRET_BEGINNER = 24,
	ITEM_TURRET_INTERMEDIATE,
	ITEM_TURRET_ADVANCED,

	ITEM_CARD_QUICKLY_FIRE = 27,
	ITEM_CARD_QUICKLY_LOADING,
	ITEM_CARD_DAMAGE,
	ITEM_CARD_EXPLOSION,
	ITEM_CARD_ELECTRON,
	ITEM_CARD_FUSION,
	ITEM_CARD_FORCE,
	ITEM_CARD_MANUAL,

	ITEM_PART_BARREL = 35,
	ITEM_PART_COOLING = 36,
	ITEM_PART_MAGAZINE = 37,
	ITEM_PART_STABILIZER = 38,
	ITEM_MAT_TITANIUM = 39,
	ITEM_MAT_VOID_SHARD = 40,
	ITEM_CARD_LIFESTEAL = 41,
	ITEM_CARD_ARMOR_SHRED = 42,
	ITEM_CARD_CHAIN_LIGHTNING = 43,
	ITEM_CARD_MINING_LUCK = 44,
	ITEM_CARD_HASTE = 45,
	ITEM_TURRET_SNIPER = 46,
	ITEM_TURRET_SPLASH = 47,
	ITEM_SWORD_FLAME = 48,
	ITEM_PICKAXE_FORTUNE = 49,
	ITEM_CARD_MAX_HEALTH = 50,
	ITEM_HELMET_LOG = 51,
	ITEM_HELMET_IRON,
	ITEM_HELMET_COPPER,
	ITEM_HELMET_TITANIUM,
	ITEM_HELMET_DIAMOND,
	ITEM_HELMET_ENEGRY,
	ITEM_HELMET_VOID,
	ITEM_CHEST_LOG = 58,
	ITEM_CHEST_IRON,
	ITEM_CHEST_COPPER,
	ITEM_CHEST_TITANIUM,
	ITEM_CHEST_DIAMOND,
	ITEM_CHEST_ENEGRY,
	ITEM_CHEST_VOID,
	ITEM_LEGS_LOG = 65,
	ITEM_LEGS_IRON,
	ITEM_LEGS_COPPER,
	ITEM_LEGS_TITANIUM,
	ITEM_LEGS_DIAMOND,
	ITEM_LEGS_ENEGRY,
	ITEM_LEGS_VOID,

	ITEM_CARD_THORNS = 72,
	ITEM_CARD_REGENERATION,
	ITEM_CARD_FORTIFICATION,
	ITEM_CARD_ABSORPTION_SHIELD,
	ITEM_CARD_FORTUNE,
	ITEM_CARD_SWIFTNESS,
	ITEM_CARD_RETRIBUTION,
	ITEM_CARD_RESILIENCE,
	ITEM_CARD_SELF_HARM_IMMUNITY,
	NUM_ITEM,
};

enum
{
	MAX_ITEM_EFFECT_KEYS = 4,

	ITYPE_PICKAXE = 0,
	ITYPE_AXE,
	ITYPE_SWORD,
	ITYPE_TURRET,
	ITYPE_MATERIAL,
	ITYPE_CARD,
	ITYPE_HELMET,
	ITYPE_CHEST,
	ITYPE_LEGS,
	NUM_ITYPE,
};

enum
{
	ITEM_CARD_QUICKLY_FIRE_ID = ITEM_CARD_QUICKLY_FIRE,
	ITEM_CARD_QUICKLY_LOADING_ID = ITEM_CARD_QUICKLY_LOADING,
	ITEM_CARD_DAMAGE_ID = ITEM_CARD_DAMAGE,
	ITEM_CARD_EXPLOSION_ID = ITEM_CARD_EXPLOSION,
	ITEM_CARD_ELECTRON_ID = ITEM_CARD_ELECTRON,
	ITEM_CARD_FUSION_ID = ITEM_CARD_FUSION,
	ITEM_CARD_FORCE_ID = ITEM_CARD_FORCE,
	ITEM_CARD_MANUAL_ID = ITEM_CARD_MANUAL,
};

struct SPlayerItemData
{
	int m_Num;
	int m_Capacity;
	char m_aExtra[640];
};

struct SAccSyncData
{
	char m_aUsername[64];
	char m_aPassword[128];
	char m_aLanguage[64];
	int m_Holding[NUM_ITYPE];
	int m_ItemCount[NUM_ITYPE];
	SPlayerItemData m_aItems[NUM_ITEM];
};

struct SToolStat
{
	int m_Damage;
	int m_Capacity;
	int m_Defense;
};

class CItemHelper
{
	CGameContext *m_pGameServer;
	SToolStat m_aToolDmg[NUM_ITEM];
	int m_aMatHealth[NUM_ITEM];
	char m_aaItemName[NUM_ITEM][64];
	char m_aaItemDesc[NUM_ITEM][128];
	int m_aItemType[NUM_ITEM];
	int m_aItemMaxStack[NUM_ITEM];
	int m_aFormula[NUM_ITEM][NUM_ITEM];
	bool m_aHasFormula[NUM_ITEM];
	int m_aProba[NUM_ITEM];
	int m_aMaxPlace[NUM_ITEM];
	bool m_aaPlaceable[NUM_ITEM][NUM_ITYPE];
	char m_aaItemEffects[NUM_ITEM][MAX_ITEM_EFFECT_KEYS][32];
	int m_aNumItemEffects[NUM_ITEM];

public:
	explicit CItemHelper(CGameContext *pGameServer);
	CGameContext *GameServer() const { return m_pGameServer; }

	void ResetStats();
	void LoadDefinitions(IStorage *pStorage);

	bool CheckItemValid(int ID) const { return ID >= 0 && ID < NUM_ITEM; }

	int FindItemByName(const char *pName) const;
	int GetDmg(int ID) const;
	int GetDefense(int ID) const;
	int GetMaxHealth(int MatID) const;
	int GetMax(int ID) const;
	int GetMaxCapacity(int ID) const;
	int GetProba(int ID) const;
	/**  max_place 0 in JSON -> effective limit 999. **/
	int GetMaxPlace(int CardOrPartId) const;
	bool IsPlaceableOnItemType(int CardOrPartId, int HostItemType) const;
	bool IsPartItem(int ID) const;
	int GetType(int ID) const;
	bool HasFormula(int ID) const { return CheckItemValid(ID) && m_aHasFormula[ID]; }
	int GetFormulaNeed(int CraftId, int MatId) const;
	bool ItemExtraBlocksCraftConsume(const char *pExtraJson) const;

	const char *GetItemName(int ID, bool IncludeZero = true) const;
	const char *GetItemDesc(int ID) const;
	bool HasItemDefinition(int ID) const { return CheckItemValid(ID) && m_aaItemName[ID][0] != 0; }
	void FormatItemLocKey(int ID, char *pBuf, int BufSize) const;

	/** Reads Extra.Cards[] (id/num). **/
	int GetCard(const char *pExtraJson, int CardID) const;
	/** Reads Extra.Parts[] (id/num). **/
	int GetPart(const char *pExtraJson, int PartItemId) const;
	/** Sum of GetMaxCapacity(cardId)*num over Extra.Cards (was GetCapacity). **/
	int GetCapacityFromExtra(const char *pExtraJson) const;
	int GetNumItemEffects(int ItemId) const;
	const char *GetItemEffectKey(int ItemId, int EffectIdx) const;
	int GetEffectStacksFromExtra(const char *pExtraJson, int ItemId, const char *pEffectKey) const;
	int SumArmorEffectStacks(class CPlayer *pP, int CardItemId, const char *pEffectKey) const;
	int CountArmorWithCard(class CPlayer *pP, int CardItemId) const;

private:
	int GetExtraSlotNum(const char *pExtraJson, const char *pArrayName, int ItemId) const;
	void ConsumeStatPass(const json_value &Entry, int ParentType);
	void ConsumeFormulaPass(const json_value &Entry);
	void LoadItemFilePass(int Pass, const json_value *pRoot);
};

#endif

#ifndef GAME_SERVER_DATA_CENTER_H
#define GAME_SERVER_DATA_CENTER_H

#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include <base/vmath.h>

class CMMOItemDescription;
struct SMMOMobDef;
struct SMMOZoneDef;
class IStorage;

class CDataCenter
{
	// ─── State ─────────────────────────────────────────────────────
	static bool ms_Initialized;

	// ─── Item Definitions ──────────────────────────────────────────
	static std::map<int, CMMOItemDescription> ms_aItemDefs;
public:
	static const std::map<int, CMMOItemDescription>& GetItemDefs() { return ms_aItemDefs; }
	static const CMMOItemDescription *FindItemDef(int ItemID);

	// ─── Mob Definitions ───────────────────────────────────────────
private:
	static std::vector<SMMOMobDef> ms_aMobDefs;
public:
	static const std::vector<SMMOMobDef>& GetMobDefs() { return ms_aMobDefs; }
	static const SMMOMobDef *FindMobDef(int ID);

	// ─── Zone Definitions ──────────────────────────────────────────
private:
	static std::vector<SMMOZoneDef> ms_aZoneDefs;
public:
	static const std::vector<SMMOZoneDef>& GetZoneDefs() { return ms_aZoneDefs; }
	static const SMMOZoneDef *FindZone(int WorldID, const char *pName);
	static const SMMOZoneDef *FindZoneByPos(int WorldID, vec2 Pos);

	// ─── Shop Definitions ──────────────────────────────────────────
private:
	struct SShopItemEntry
	{
		int m_ItemID = 0;
		int m_Price = 0;
		int m_Stack = 1;
	};
	struct SShopDef
	{
		char m_aId[64]{};
		char m_aName[128]{};
		std::vector<SShopItemEntry> m_vItems;
	};
	static std::unordered_map<std::string, SShopDef> ms_aShops;
public:
	static const SShopDef *FindShop(const char *pId);
	static const SShopDef *FindShopByName(const char *pName);

	// ─── Lifecycle ─────────────────────────────────────────────────
public:
	static void Init(IStorage *pStorage);  // Call once at server startup
	static bool IsInitialized() { return ms_Initialized; }
	static void Shutdown();

private:
	static void LoadItemDefinitions(IStorage *pStorage);
	static void LoadMobDefinitions(IStorage *pStorage);
	static void LoadShopDefinitions(IStorage *pStorage);
	static void LoadZoneDefs(IStorage *pStorage);
};

#endif // GAME_SERVER_DATA_CENTER_H

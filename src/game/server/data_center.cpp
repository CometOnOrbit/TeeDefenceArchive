#include "data_center.h"
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <engine/storage.h>

// ─── Static member definitions ──────────────────────────────────────

bool CDataCenter::ms_Initialized = false;
std::map<int, CMMOItemDescription> CDataCenter::ms_aItemDefs;
std::vector<SMMOMobDef> CDataCenter::ms_aMobDefs;
std::vector<SMMOZoneDef> CDataCenter::ms_aZoneDefs;
std::unordered_map<std::string, CDataCenter::SShopDef> CDataCenter::ms_aShops;

// ─── FindItemDef ─────────────────────────────────────────────────────

const CMMOItemDescription *CDataCenter::FindItemDef(int ItemID)
{
	auto it = ms_aItemDefs.find(ItemID);
	return it != ms_aItemDefs.end() ? &it->second : nullptr;
}

// ─── FindMobDef ──────────────────────────────────────────────────────

const SMMOMobDef *CDataCenter::FindMobDef(int ID)
{
	for(const auto &Def : ms_aMobDefs)
		if(Def.m_ID == ID) return &Def;
	return nullptr;
}

// ─── FindZone / FindZoneByPos ────────────────────────────────────────

const SMMOZoneDef *CDataCenter::FindZone(int WorldID, const char *pName)
{
	for(const auto &Zone : ms_aZoneDefs)
	{
		if(Zone.m_WorldID == WorldID && str_comp(Zone.m_aName, pName) == 0)
			return &Zone;
	}
	return nullptr;
}

const SMMOZoneDef *CDataCenter::FindZoneByPos(int WorldID, vec2 Pos)
{
	for(const auto &Zone : ms_aZoneDefs)
	{
		if(Zone.m_WorldID != WorldID)
			continue;
		if(Pos.x >= Zone.m_X1 && Pos.x <= Zone.m_X2 &&
			Pos.y >= Zone.m_Y1 && Pos.y <= Zone.m_Y2)
			return &Zone;
	}
	return nullptr;
}

// ─── FindShop / FindShopByName ───────────────────────────────────────

const CDataCenter::SShopDef *CDataCenter::FindShop(const char *pId)
{
	auto it = ms_aShops.find(pId);
	return it != ms_aShops.end() ? &it->second : nullptr;
}

const CDataCenter::SShopDef *CDataCenter::FindShopByName(const char *pName)
{
	for(const auto &Pair : ms_aShops)
	{
		if(str_comp(Pair.second.m_aName, pName) == 0)
			return &Pair.second;
	}
	return nullptr;
}

// ─── Init ────────────────────────────────────────────────────────────

void CDataCenter::Init(IStorage *pStorage)
{
	if(ms_Initialized)
		return;  // Already initialized
	ms_Initialized = true;

	dbg_msg("datacenter", "CDataCenter initializing...");

	LoadItemDefinitions(pStorage);
	LoadMobDefinitions(pStorage);
	LoadZoneDefs(pStorage);
	LoadShopDefinitions(pStorage);

	dbg_msg("datacenter", "CDataCenter initialized:"
		" items=%zu mobs=%zu zones=%zu shops=%zu",
		ms_aItemDefs.size(), ms_aMobDefs.size(),
		ms_aZoneDefs.size(), ms_aShops.size());
}

// ─── Shutdown ─────────────────────────────────────────────────────────

void CDataCenter::Shutdown()
{
	ms_aItemDefs.clear();
	ms_aMobDefs.clear();
	ms_aZoneDefs.clear();
	ms_aShops.clear();
	ms_Initialized = false;
	dbg_msg("datacenter", "CDataCenter shut down");
}

// ─── LoadItemDefinitions ─────────────────────────────────────────────

void CDataCenter::LoadItemDefinitions(IStorage *pStorage)
{
	ms_aItemDefs.clear();

	if(!pStorage)
	{
		dbg_msg("mmo", "No storage available, using fallback item defs");
		goto fallback;
	}

	{
		CJsonParser Parser;
		json_value *pRoot = Parser.ParseFile("server_content/mmo/mmo_items.json", pStorage);
		if(!pRoot)
		{
			dbg_msg("mmo", "mmo_items.json: %s", Parser.Error());
			goto fallback;
		}

		const json_value &Arr = (*pRoot)["items"];
		if(Arr.type != json_array)
		{
			dbg_msg("mmo", "mmo_items.json: missing 'items' array");
			goto fallback;
		}

		int Loaded = 0;
		for(unsigned i = 0; i < Arr.u.array.length; i++)
		{
			const json_value &S = Arr[(int)i];
			if(S.type != json_object) continue;
			if(S["id"].type != json_integer) continue;

			const int ID = (int)S["id"].u.integer;
			CMMOItemDescription Def;
			Def.m_ID = ID;

			if(S["name"].type == json_string)
				str_copy(Def.m_aName, S["name"].u.string.ptr, sizeof(Def.m_aName));
			if(S["description"].type == json_string)
				str_copy(Def.m_aDescription, S["description"].u.string.ptr, sizeof(Def.m_aDescription));
			if(S["name_key"].type == json_string)
				str_copy(Def.m_aNameKey, S["name_key"].u.string.ptr, sizeof(Def.m_aNameKey));
			if(S["description_key"].type == json_string)
				str_copy(Def.m_aDescriptionKey, S["description_key"].u.string.ptr, sizeof(Def.m_aDescriptionKey));

			if(S["group"].type == json_string)
				Def.m_Group = MMOItemGroupFromName(S["group"].u.string.ptr);
			if(S["type"].type == json_string)
				Def.m_Type = MMOItemTypeFromName(S["type"].u.string.ptr);
			if(S["rarity"].type == json_string)
				Def.m_Rarity = MMORarityFromName(S["rarity"].u.string.ptr);

			Def.m_BasePrice = S["base_price"].type == json_integer ? (int)S["base_price"].u.integer : 0;
			Def.m_LevelReq = S["level_required"].type == json_integer ? (int)S["level_required"].u.integer : 1;

			if(S["stackable"].type == json_boolean) Def.m_Stackable = S["stackable"].u.boolean != 0;
			if(S["enchantable"].type == json_boolean) Def.m_Enchantable = S["enchantable"].u.boolean != 0;
			if(S["can_trade"].type == json_boolean) Def.m_CanTrade = S["can_trade"].u.boolean != 0;
			if(S["can_drop"].type == json_boolean) Def.m_CanDrop = S["can_drop"].u.boolean != 0;

			if(!Def.m_CanDrop) Def.m_Flags |= MMO_ITEMFLAG_CANT_DROP;
			if(!Def.m_CanTrade) Def.m_Flags |= MMO_ITEMFLAG_CANT_TRADE;

			// Load attack/defense (legacy)
			if(S["attack"].type == json_integer) Def.m_Attack = (int)S["attack"].u.integer;
			if(S["defense"].type == json_integer) Def.m_Defense = (int)S["defense"].u.integer;
			if(S["per_enchant_attack"].type == json_integer) Def.m_PerEnchantAttack = (int)S["per_enchant_attack"].u.integer;
			if(S["per_enchant_defense"].type == json_integer) Def.m_PerEnchantDefense = (int)S["per_enchant_defense"].u.integer;

			// Load MRPG-style attributes
			if(S["attributes"].type == json_array)
			{
				for(unsigned a = 0; a < S["attributes"].u.array.length; a++)
				{
					const json_value &Attr = S["attributes"][(int)a];
					if(Attr.type != json_object) continue;

					const char *pAttrType = nullptr;
					if(Attr["type"].type == json_string)
						pAttrType = Attr["type"].u.string.ptr;
					else if(Attr["attribute"].type == json_string)
						pAttrType = Attr["attribute"].u.string.ptr;

					if(!pAttrType) continue;

					int BaseVal = 0;
					if(Attr["base_value"].type == json_integer)
						BaseVal = (int)Attr["base_value"].u.integer;
					else if(Attr["value"].type == json_integer)
						BaseVal = (int)Attr["value"].u.integer;

					int PerEnchVal = Attr["per_enchant"].type == json_integer ? (int)Attr["per_enchant"].u.integer : 0;

					AttributeIdentifier attrID = AttributeIdentifier::Unknown;
					if(str_comp_nocase(pAttrType, "attack") == 0) attrID = AttributeIdentifier::Attack;
					else if(str_comp_nocase(pAttrType, "defense") == 0) attrID = AttributeIdentifier::Defense;
					else if(str_comp_nocase(pAttrType, "health") == 0 || str_comp_nocase(pAttrType, "hp") == 0) attrID = AttributeIdentifier::HP;
					else if(str_comp_nocase(pAttrType, "mana") == 0 || str_comp_nocase(pAttrType, "mp") == 0) attrID = AttributeIdentifier::MP;
					else if(str_comp_nocase(pAttrType, "damage") == 0) attrID = AttributeIdentifier::DMG;
					else if(str_comp_nocase(pAttrType, "crit_chance") == 0) attrID = AttributeIdentifier::Crit;
					else if(str_comp_nocase(pAttrType, "attack_spd") == 0 || str_comp_nocase(pAttrType, "attack_speed") == 0) attrID = AttributeIdentifier::AttackSPD;
					else if(str_comp_nocase(pAttrType, "gun_dmg") == 0) attrID = AttributeIdentifier::GunDMG;
					else if(str_comp_nocase(pAttrType, "hammer_dmg") == 0) attrID = AttributeIdentifier::HammerDMG;
					else if(str_comp_nocase(pAttrType, "shotgun_dmg") == 0) attrID = AttributeIdentifier::ShotgunDMG;
					else if(str_comp_nocase(pAttrType, "grenade_dmg") == 0) attrID = AttributeIdentifier::GrenadeDMG;
					else if(str_comp_nocase(pAttrType, "rifle_dmg") == 0 || str_comp_nocase(pAttrType, "laser_dmg") == 0) attrID = AttributeIdentifier::RifleDMG;
					else if(str_comp_nocase(pAttrType, "ammo") == 0) attrID = AttributeIdentifier::Ammo;
					else if(str_comp_nocase(pAttrType, "ammo_regen") == 0) attrID = AttributeIdentifier::AmmoRegen;

					if(attrID != AttributeIdentifier::Unknown)
					{
						if(BaseVal > 0) Def.m_vAttributes[attrID] = BaseVal;
						if(PerEnchVal > 0) Def.m_vPerEnchantAttributes[attrID] = PerEnchVal;
					}
				}
			}

			if(S["weapon_profile"].type == json_object)
			{
				const json_value &WP = S["weapon_profile"];
				Def.m_WeaponProfile.m_Active = true;
				if(WP["proj_speed"].type == json_integer)
					Def.m_WeaponProfile.m_ProjSpeedPercent = clamp((int)WP["proj_speed"].u.integer, 40, 200);
				if(WP["spread"].type == json_integer)
					Def.m_WeaponProfile.m_SpreadDegrees = clamp((int)WP["spread"].u.integer, 0, 25);
				if(WP["reload_speed"].type == json_integer)
					Def.m_WeaponProfile.m_ReloadPercent = clamp((int)WP["reload_speed"].u.integer, 40, 160);
				if(WP["force"].type == json_integer)
					Def.m_WeaponProfile.m_ForcePercent = clamp((int)WP["force"].u.integer, 50, 200);
				if(WP["proj_range"].type == json_integer)
					Def.m_WeaponProfile.m_ProjRangePercent = clamp((int)WP["proj_range"].u.integer, 50, 200);
				if(WP["damage_mul"].type == json_integer)
					Def.m_WeaponProfile.m_DamageMulPercent = clamp((int)WP["damage_mul"].u.integer, 50, 200);
				if(WP["crit_chance"].type == json_integer)
					Def.m_WeaponProfile.m_CritChance = clamp((int)WP["crit_chance"].u.integer, 0, 100);
				if(WP["lifesteal"].type == json_integer)
					Def.m_WeaponProfile.m_LifestealPercent = clamp((int)WP["lifesteal"].u.integer, 0, 100);
				if(WP["multishot"].type == json_integer)
					Def.m_WeaponProfile.m_Multishot = clamp((int)WP["multishot"].u.integer, 0, 3);
				if(WP["fan_shots"].type == json_integer)
					Def.m_WeaponProfile.m_FanShots = clamp((int)WP["fan_shots"].u.integer, 0, 7);
				if(WP["fan_spread"].type == json_double)
					Def.m_WeaponProfile.m_FanSpreadRad = clamp((int)(WP["fan_spread"].u.dbl * 100.f + 0.5f), 1, 50);
				else if(WP["fan_spread"].type == json_integer)
					Def.m_WeaponProfile.m_FanSpreadRad = clamp((int)WP["fan_spread"].u.integer, 1, 50);
				if(WP["pierce"].type == json_integer)
					Def.m_WeaponProfile.m_Pierce = clamp((int)WP["pierce"].u.integer, 0, 3);
				if(WP["shotgun_pellets"].type == json_integer)
				{
					const int Pellets = (int)WP["shotgun_pellets"].u.integer;
					if(Pellets >= 5)
						Def.m_WeaponProfile.m_ShotgunPelletCount = clamp(Pellets, 5, 15);
					else if(Pellets != 0)
						Def.m_WeaponProfile.m_ShotgunPelletsAdd = clamp(Pellets, -2, 3);
				}
				if(WP["laser_reach"].type == json_integer)
					Def.m_WeaponProfile.m_LaserReachPercent = clamp((int)WP["laser_reach"].u.integer, 25, 200);
				if(WP["grenade_salvo_lifetime_percent"].type == json_integer)
					Def.m_WeaponProfile.m_GrenadeSalvoLifetimePercent = clamp((int)WP["grenade_salvo_lifetime_percent"].u.integer, 50, 100);
				if(WP["recoil"].type == json_integer)
					Def.m_WeaponProfile.m_RecoilPercent = clamp((int)WP["recoil"].u.integer, 0, 200);
				if(WP["explosive"].type == json_boolean)
					Def.m_WeaponProfile.m_Explosive = WP["explosive"].u.boolean != 0;
				if(WP["pulse"].type == json_boolean)
					Def.m_WeaponProfile.m_Pulse = WP["pulse"].u.boolean != 0;
				if(WP["pulse_reach"].type == json_integer)
					Def.m_WeaponProfile.m_PulseReach = clamp((int)WP["pulse_reach"].u.integer, 100, 800);
				if(WP["fire_style"].type == json_string)
					Def.m_WeaponProfile.m_FireStyle = ParseFireStyle(WP["fire_style"]);
				if(WP["magnetic_radius"].type == json_integer)
					Def.m_WeaponProfile.m_MagneticRadius = clamp((int)WP["magnetic_radius"].u.integer, 32, 256);
				if(WP["wall_pusher_life_ticks"].type == json_integer)
					Def.m_WeaponProfile.m_WallPusherLifeTicks = clamp((int)WP["wall_pusher_life_ticks"].u.integer, 1, 1000);
				if(WP["hammer_blast_radius"].type == json_integer)
					Def.m_WeaponProfile.m_HammerBlastRadius = clamp((int)WP["hammer_blast_radius"].u.integer, 32, 256);
				if(WP["homing_grenade_speed"].type == json_integer)
					Def.m_WeaponProfile.m_HomingGrenadeSpeed = clamp((int)WP["homing_grenade_speed"].u.integer, 8, 32);
				if(WP["tesla_chain_range"].type == json_integer)
					Def.m_WeaponProfile.m_TeslaChainRange = clamp((int)WP["tesla_chain_range"].u.integer, 100, 800);
				if(WP["tesla_chain_targets"].type == json_integer)
					Def.m_WeaponProfile.m_TeslaChainTargets = clamp((int)WP["tesla_chain_targets"].u.integer, 1, 8);
				if(WP["tesla_damage_falloff"].type == json_integer)
					Def.m_WeaponProfile.m_TeslaDamageFalloff = clamp((int)WP["tesla_damage_falloff"].u.integer, 10, 100);
				if(WP["tracked_plasma_speed_min"].type == json_integer)
					Def.m_WeaponProfile.m_TrackedPlasmaSpeedMin = clamp((int)WP["tracked_plasma_speed_min"].u.integer, 1, 30);
				if(WP["tracked_plasma_speed_max"].type == json_integer)
					Def.m_WeaponProfile.m_TrackedPlasmaSpeedMax = clamp((int)WP["tracked_plasma_speed_max"].u.integer, 5, 40);
				if(WP["hammer_lamp_radius"].type == json_integer)
					Def.m_WeaponProfile.m_HammerLampRadius = clamp((int)WP["hammer_lamp_radius"].u.integer, 128, 600);
			}

			if(Def.m_Group == ItemGroup::Equipment && !MMOIsEquipmentItemType(Def.m_Type))
			{
				dbg_msg("mmo", "item %d: group=equipment requires a valid equip type (got type id %d)", ID, (int)Def.m_Type);
			}

			ms_aItemDefs[Def.m_ID] = Def;
			Loaded++;
		}

		dbg_msg("mmo", "Loaded %d item definitions from mmo_items.json", Loaded);
		return;
	}

fallback:
	dbg_msg("mmo", "Using hardcoded fallback item definitions");
	{
		CMMOItemDescription Coin;
		Coin.m_ID = 1;
		str_copy(Coin.m_aName, "金币", sizeof(Coin.m_aName));
		str_copy(Coin.m_aDescription, "TeeFun世界的标准货币", sizeof(Coin.m_aDescription));
		Coin.m_Group = ItemGroup::Currency;
		Coin.m_BasePrice = 1;
		Coin.m_Stackable = true;
		Coin.m_CanTrade = true;
		Coin.m_CanDrop = true;
		ms_aItemDefs[Coin.m_ID] = Coin;

		CMMOItemDescription HPPot;
		HPPot.m_ID = 2;
		str_copy(HPPot.m_aName, "灵质碎片", sizeof(HPPot.m_aName));
		str_copy(HPPot.m_aDescription, "浓缩灵能碎片，可用于特殊商店", sizeof(HPPot.m_aDescription));
		HPPot.m_Group = ItemGroup::Potion;
		HPPot.m_BasePrice = 50;
		HPPot.m_Stackable = true;
		HPPot.m_CanTrade = true;
		HPPot.m_CanDrop = true;
		ms_aItemDefs[HPPot.m_ID] = HPPot;

		CMMOItemDescription Sword;
		Sword.m_ID = 10;
		str_copy(Sword.m_aName, "铁剑", sizeof(Sword.m_aName));
		str_copy(Sword.m_aDescription, "一把基础铁剑", sizeof(Sword.m_aDescription));
		Sword.m_Group = ItemGroup::Equipment;
		Sword.m_BasePrice = 200;
		Sword.m_LevelReq = 5;
		Sword.m_Enchantable = true;
		Sword.m_CanTrade = false;
		Sword.m_CanDrop = true;
		Sword.m_Attack = 3;
		ms_aItemDefs[Sword.m_ID] = Sword;

		dbg_msg("mmo", "Registered 3 fallback item definitions");
	}
}

// ─── LoadMobDefinitions ──────────────────────────────────────────────

void CDataCenter::LoadMobDefinitions(IStorage *pStorage)
{
	ms_aMobDefs.clear();

	if(!pStorage)
	{
		dbg_msg("mmo_mob", "No storage, using fallback mob defs");
		goto fallback;
	}

	{
		CJsonParser Parser;
		json_value *pRoot = Parser.ParseFile("server_content/mmo/mmo_mobs.json", pStorage);
		if(!pRoot)
		{
			dbg_msg("mmo_mob", "mmo_mobs.json: %s (using fallback)", Parser.Error());
			goto fallback;
		}

		const json_value &Arr = (*pRoot)["mobs"];
		if(Arr.type != json_array)
		{
			dbg_msg("mmo_mob", "missing 'mobs' array");
			goto fallback;
		}

		int Loaded = 0;
		for(unsigned i = 0; i < Arr.u.array.length; i++)
		{
			const json_value &S = Arr[(int)i];
			if(S.type != json_object) continue;
			if(S["id"].type != json_integer) continue;

			SMMOMobDef Def;
			Def.m_ID = (int)S["id"].u.integer;

			if(S["name"].type == json_string)
				str_copy(Def.m_aName, S["name"].u.string.ptr, sizeof(Def.m_aName));
			if(S["skin"].type == json_string)
				str_copy(Def.m_aSkinName, S["skin"].u.string.ptr, sizeof(Def.m_aSkinName));
			if(S["color_body"].type == json_integer)
				Def.m_SkinColorBody = (int)S["color_body"].u.integer;
			if(S["color_feet"].type == json_integer)
				Def.m_SkinColorFeet = (int)S["color_feet"].u.integer;

			if(S["level"].type == json_integer) Def.m_Level = (int)S["level"].u.integer;
			if(S["hp"].type == json_integer) Def.m_MaxHP = (int)S["hp"].u.integer;
			if(S["attack"].type == json_integer) Def.m_Attack = (int)S["attack"].u.integer;
			if(S["defense"].type == json_integer) Def.m_Defense = (int)S["defense"].u.integer;
			if(S["exp_reward"].type == json_integer) Def.m_ExpReward = (int)S["exp_reward"].u.integer;
			if(S["respawn_ticks"].type == json_integer) Def.m_RespawnTicks = (int)S["respawn_ticks"].u.integer;
			if(S["is_boss"].type == json_boolean) Def.m_IsBoss = S["is_boss"].u.boolean != 0;

			if(S["gold_reward"].type == json_object)
			{
				const json_value &Gold = S["gold_reward"];
				if(Gold["min"].type == json_integer) Def.m_GoldMin = (int)Gold["min"].u.integer;
				if(Gold["max"].type == json_integer) Def.m_GoldMax = (int)Gold["max"].u.integer;
			}

			if(S["drops"].type == json_array)
			{
				for(unsigned d = 0; d < S["drops"].u.array.length; d++)
				{
					const json_value &Drop = S["drops"][(int)d];
					if(Drop.type != json_object) continue;
					SMMODropEntry Entry;
					Entry.m_ItemID = Drop["item_id"].type == json_integer ? (int)Drop["item_id"].u.integer : 0;
					Entry.m_Chance = Drop["chance"].type == json_integer ? (int)Drop["chance"].u.integer : 0;
					Entry.m_MinCount = Drop["min_count"].type == json_integer ? (int)Drop["min_count"].u.integer : 1;
					Entry.m_MaxCount = Drop["max_count"].type == json_integer ? (int)Drop["max_count"].u.integer : 1;
					if(Entry.m_ItemID > 0) Def.m_vDrops.push_back(Entry);
				}
			}

			// Parse behaviors array
			if(S["behaviors"].type == json_array)
			{
				Def.m_BehaviorFlags = 0;
				for(unsigned b = 0; b < S["behaviors"].u.array.length; b++)
				{
					const json_value &Beh = S["behaviors"][(int)b];
					if(Beh.type == json_string)
						Def.m_BehaviorFlags |= MobBehaviorFlagsFromName(Beh.u.string.ptr);
				}
			}

			if(S["active_radius"].type == json_integer)
				Def.m_ActiveRadius = (float)S["active_radius"].u.integer;
			else if(S["active_radius"].type == json_double)
				Def.m_ActiveRadius = (float)S["active_radius"].u.dbl;

			const json_value &Combat = S["combat"];
			if(Combat.type == json_object)
			{
				if(Combat["archetype"].type == json_string)
					Def.m_Archetype = MobArchetypeFromName(Combat["archetype"].u.string.ptr);
				if(Combat["preferred_range"].type == json_integer)
					Def.m_PreferredRange = (float)Combat["preferred_range"].u.integer;
				else if(Combat["preferred_range"].type == json_double)
					Def.m_PreferredRange = (float)Combat["preferred_range"].u.dbl;
				if(Combat["weapon_item_id"].type == json_integer)
					Def.m_WeaponItemId = (int)Combat["weapon_item_id"].u.integer;
				if(Combat["active_radius"].type == json_integer)
					Def.m_ActiveRadius = (float)Combat["active_radius"].u.integer;

				const json_value &Weapons = Combat["weapons"];
				if(Weapons.type == json_array)
				{
					for(unsigned w = 0; w < Weapons.u.array.length; w++)
					{
						const json_value &W = Weapons[(int)w];
						if(W.type != json_object)
							continue;
						SMMOMobWeaponEntry Entry;
						if(W["id"].type == json_integer)
							Entry.m_WeaponId = (int)W["id"].u.integer;
						if(W["ammo"].type == json_integer)
							Entry.m_Ammo = (int)W["ammo"].u.integer;
						if(W["primary"].type == json_boolean)
							Entry.m_Primary = W["primary"].u.boolean != 0;
						Def.m_vWeapons.push_back(Entry);
					}
				}
			}

			const json_value &Abilities = S["abilities"];
			if(Abilities.type == json_array)
			{
				for(unsigned a = 0; a < Abilities.u.array.length; a++)
				{
					const json_value &A = Abilities[(int)a];
					if(A.type != json_object || A["key"].type != json_string)
						continue;
					SMMOMobAbilityDef Ability;
					str_copy(Ability.m_aKey, A["key"].u.string.ptr, sizeof(Ability.m_aKey));
					if(A["trigger"].type == json_string)
						Ability.m_Trigger = MobAbilityTriggerFromName(A["trigger"].u.string.ptr);
					if(A["range"].type == json_integer)
						Ability.m_Range = (float)A["range"].u.integer;
					else if(A["range"].type == json_double)
						Ability.m_Range = (float)A["range"].u.dbl;
					if(A["cooldown_ticks"].type == json_integer)
						Ability.m_CooldownTicks = (int)A["cooldown_ticks"].u.integer;
					if(A["cast_ticks"].type == json_integer)
						Ability.m_CastTicks = (int)A["cast_ticks"].u.integer;
					if(A["scale_attack"].type == json_double)
						Ability.m_ScaleAttack = (float)A["scale_attack"].u.dbl;
					else if(A["scale_attack"].type == json_integer)
						Ability.m_ScaleAttack = (float)A["scale_attack"].u.integer;
					if(A["threshold_pct"].type == json_integer)
						Ability.m_ThresholdPct = (float)A["threshold_pct"].u.integer;
					else if(A["threshold_pct"].type == json_double)
						Ability.m_ThresholdPct = (float)A["threshold_pct"].u.dbl;
					if(Ability.m_ScaleAttack <= 0.f)
						Ability.m_ScaleAttack = 1.f;
					Def.m_vAbilities.push_back(Ability);
				}
			}

			ms_aMobDefs.push_back(Def);
			Loaded++;
		}

		dbg_msg("mmo_mob", "Loaded %d mob definitions", Loaded);
		return;
	}

fallback:
	{
		SMMOMobDef Slime;
		Slime.m_ID = 1;
		str_copy(Slime.m_aName, "Slime", sizeof(Slime.m_aName));
		str_copy(Slime.m_aSkinName, "bluestripe", sizeof(Slime.m_aSkinName));
		Slime.m_MaxHP = 100; Slime.m_Attack = 3; Slime.m_Defense = 1;
		Slime.m_ExpReward = 10; Slime.m_RespawnTicks = 300;
		ms_aMobDefs.push_back(Slime);

		dbg_msg("mmo_mob", "Registered %zu fallback mob defs", ms_aMobDefs.size());
	}
}

// ─── LoadShopDefinitions ─────────────────────────────────────────────

void CDataCenter::LoadShopDefinitions(IStorage *pStorage)
{
	ms_aShops.clear();

	if(!pStorage)
	{
		dbg_msg("shop", "No storage, cannot load shop_items.json");
		return;
	}

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/shop_items.json", pStorage);
	if(!pRoot)
	{
		dbg_msg("shop", "shop_items.json: %s", Parser.Error());
		return;
	}

	const json_value &Shops = (*pRoot)["shops"];
	if(Shops.type != json_array)
	{
		dbg_msg("shop", "missing 'shops' array");
		return;
	}

	int Loaded = 0;
	for(unsigned i = 0; i < Shops.u.array.length; i++)
	{
		const json_value &S = Shops[(int)i];
		if(S.type != json_object) continue;

		SShopDef Def;
		Def.m_aId[0] = '\0';
		Def.m_aName[0] = '\0';

		if(S["id"].type == json_string)
			str_copy(Def.m_aId, S["id"].u.string.ptr, sizeof(Def.m_aId));
		if(S["name"].type == json_string)
			str_copy(Def.m_aName, S["name"].u.string.ptr, sizeof(Def.m_aName));

		if(Def.m_aId[0] == '\0') continue;

		const json_value &Items = S["items"];
		if(Items.type == json_array)
		{
			for(unsigned j = 0; j < Items.u.array.length; j++)
			{
				const json_value &I = Items[(int)j];
				if(I.type != json_object) continue;

				SShopItemEntry Entry;
				Entry.m_ItemID = I["item_id"].type == json_integer ? (int)I["item_id"].u.integer : 0;
				Entry.m_Price = I["price"].type == json_integer ? (int)I["price"].u.integer : 0;
				Entry.m_Stack = I["stack"].type == json_integer ? (int)I["stack"].u.integer : 1;

				if(Entry.m_ItemID > 0)
					Def.m_vItems.push_back(Entry);
			}
		}

		if(!Def.m_vItems.empty())
		{
			ms_aShops[Def.m_aId] = Def;
			Loaded++;
		}
	}

	dbg_msg("shop", "Loaded %d shop definitions", Loaded);
}

// ─── LoadZoneDefs ────────────────────────────────────────────────────

void CDataCenter::LoadZoneDefs(IStorage *pStorage)
{
	ms_aZoneDefs.clear();

	if(!pStorage)
	{
		dbg_msg("mmo_zone", "No storage, skipping zones.json");
		return;
	}

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/mmo/zones.json", pStorage);
	if(!pRoot)
	{
		dbg_msg("mmo_zone", "zones.json: %s", Parser.Error());
		return;
	}

	const json_value &Zones = (*pRoot)["zones"];
	if(Zones.type != json_array)
	{
		dbg_msg("mmo_zone", "missing 'zones' array");
		return;
	}

	int Loaded = 0;
	for(unsigned i = 0; i < Zones.u.array.length; i++)
	{
		const json_value &Z = Zones[(int)i];
		if(Z.type != json_object) continue;

		SMMOZoneDef ZoneDef;

		// World ID
		if(Z["world"].type == json_integer)
			ZoneDef.m_WorldID = (int)Z["world"].u.integer;

		// Name
		if(Z["name"].type == json_string)
			str_copy(ZoneDef.m_aName, Z["name"].u.string.ptr, sizeof(ZoneDef.m_aName));

		// Bounds
		const json_value &Bounds = Z["bounds"];
		if(Bounds.type == json_object)
		{
			if(Bounds["x1"].type == json_integer) ZoneDef.m_X1 = (int)Bounds["x1"].u.integer;
			if(Bounds["y1"].type == json_integer) ZoneDef.m_Y1 = (int)Bounds["y1"].u.integer;
			if(Bounds["x2"].type == json_integer) ZoneDef.m_X2 = (int)Bounds["x2"].u.integer;
			if(Bounds["y2"].type == json_integer) ZoneDef.m_Y2 = (int)Bounds["y2"].u.integer;
		}

		// Mobs
		const json_value &Mobs = Z["mobs"];
		if(Mobs.type == json_array)
		{
			for(unsigned m = 0; m < Mobs.u.array.length; m++)
			{
				const json_value &M = Mobs[(int)m];
				if(M.type != json_object) continue;

				SMMOZoneDef::SZoneMob MobEntry;
				MobEntry.m_DefID = (M["def_id"].type == json_integer) ? (int)M["def_id"].u.integer : 0;
				MobEntry.m_Count = (M["count"].type == json_integer) ? (int)M["count"].u.integer : 1;

				if(MobEntry.m_DefID > 0)
					ZoneDef.m_vMobs.push_back(MobEntry);
			}
		}

		if(ZoneDef.m_aName[0])
		{
			ms_aZoneDefs.push_back(ZoneDef);
			Loaded++;
		}
	}

	dbg_msg("mmo_zone", "Loaded %d zone definitions from zones.json", Loaded);
}

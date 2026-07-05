#ifndef GAME_SERVER_CORE_MMO_CONTEXT_H
#define GAME_SERVER_CORE_MMO_CONTEXT_H

// ─── Item Groups ──────────────────────────────────────────────────────
enum class ItemGroup
{
	Unknown,
	Quest,
	Currency,
	Usable,
	Material,
	Other,
	Settings,
	Equipment,
	Decoration,
	Potion,
};

// ─── Item Types ──────────────────────────────────────────────────────
enum ItemType
{
	Unknown = 0,
	Default = 0,
	General = 0,

	// Equipment types (slots)
	EquipGun,
	EquipHammer,
	EquipShotgun,
	EquipGrenade,
	EquipLaser,
	EquipPickaxe,
	EquipRake,
	EquipFishrod,
	EquipGloves,
	EquipHelmetTank,
	EquipHelmetDPS,
	EquipHelmetHealer,
	EquipArmorTank,
	EquipArmorDPS,
	EquipArmorHealer,
	EquipEidolon,
	EquipPotionHeal,
	EquipPotionMana,
	EquipTitle,

	// Use types
	UseSingle,
	UseMultiple,

	// Resource types
	ResourceHarvestable,
	ResourceMineable,
	ResourceFishes,

	// Count
	NUM_ITEM_TYPES,
};

// ─── ItemGroup→Display Name ──────────────────────────────────────────
static inline const char *ItemGroupToName(ItemGroup G)
{
	switch(G)
	{
	case ItemGroup::Usable:      return "消耗品";
	case ItemGroup::Material:    return "材料";
	case ItemGroup::Equipment:   return "装备";
	case ItemGroup::Potion:      return "药水";
	case ItemGroup::Quest:       return "任务";
	case ItemGroup::Currency:    return "货币";
	case ItemGroup::Decoration:  return "装饰";
	case ItemGroup::Settings:    return "设置";
	case ItemGroup::Other:       return "杂项";
	default:                     return "其他";
	}
}

// ─── ItemType→Display Name ──────────────────────────────────────────
static inline const char *ItemTypeToName(ItemType T)
{
	switch(T)
	{
	case EquipGun:        return "枪械";
	case EquipHammer:     return "锤子";
	case EquipShotgun:    return "霰弹枪";
	case EquipGrenade:    return "榴弹";
	case EquipLaser:      return "激光";
	case EquipPickaxe:    return "镐";
	case EquipRake:       return "耙";
	case EquipFishrod:    return "鱼竿";
	case EquipGloves:     return "手套";
	case EquipHelmetTank: return "头盔(坦克)";
	case EquipHelmetDPS:  return "头盔(输出)";
	case EquipHelmetHealer: return "头盔(治疗)";
	case EquipArmorTank:  return "胸甲(坦克)";
	case EquipArmorDPS:   return "胸甲(输出)";
	case EquipArmorHealer: return "胸甲(治疗)";
	case EquipEidolon:    return "护符";
	case EquipTitle:      return "称号";
	case EquipPotionHeal: return "治疗药水";
	case EquipPotionMana: return "法力药水";
	default:              return "";
	}
}

// ─── ItemType slot group ─────────────────────────────────────────────
static inline const char *ItemTypeToSlotGroup(ItemType T)
{
	if(T >= EquipGun && T <= EquipLaser) return "武器";
	if(T == EquipPickaxe || T == EquipRake || T == EquipFishrod) return "工具";
	if(T == EquipGloves) return "手套";
	if(T >= EquipHelmetTank && T <= EquipHelmetHealer) return "头盔";
	if(T >= EquipArmorTank && T <= EquipArmorHealer) return "胸甲";
	if(T == EquipEidolon) return "护符";
	if(T == EquipTitle) return "称号";
	return "";
}

#endif // GAME_SERVER_CORE_MMO_CONTEXT_H

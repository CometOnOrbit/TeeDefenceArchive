#ifndef GAME_SERVER_CORE_COMPONENTS_ECONOMY_SHOP_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_ECONOMY_SHOP_DATA_H

#include <base/system.h>
#include <engine/shared/protocol.h>

// Simple shop item definition
struct SShopItem
{
	int m_ItemID;
	const char *m_pName;
	int m_Price; // buy price; sell = price/2
	bool m_Stackable;
};

// Shop entry — a collection of items sold by a specific NPC type
struct SShopEntry
{
	const char *m_pNpcID;
	const char *m_pName;
	SShopItem m_Items[16];
	int m_NumItems;
};

// Predefined shop data
// Item IDs match mmo_items.json definitions
static inline const SShopEntry g_aShopData[] =
{
	// ═══ General Store (shopkeeper) ═══
	// Basic supplies, food, common resources
	{
		"shopkeeper", "General Store",
		{
			{24, "Bread", 10, true},         // heal 10HP
			{10, "Small HP Potion", 25, true}, // heal 30HP
			{14, "Small MP Potion", 20, true}, // restore 30MP
			{40, "Healing Herb", 8, true},    // crafting mat
			{30, "Copper Ore", 6, true},      // crafting mat
			{31, "Iron Ore", 12, true},       // crafting mat
			{38, "Wood Plank", 4, true},      // crafting mat
			{42, "Cloth Scrap", 5, true},     // crafting mat
			{45, "Leather Strip", 8, true},   // crafting mat
			{208, "Fishing Bait", 5, true},   // fishing
			{100, "Town Scroll", 50, true},   // teleport
		},
		11
	},

	// ═══ Blacksmith (blacksmith) ═══
	// Ores, ingots, upgrade materials
	{
		"blacksmith", "Forge & Materials",
		{
			{31, "Iron Ore", 10, true},
			{32, "Silver Ore", 25, true},
			{33, "Gold Ore", 50, true},
			{36, "Titanium Ingot", 150, true},
			{95, "Upgrade Stone", 200, true},
			{96, "High Upgrade Stone", 800, true},
			{97, "Reforging Hammer", 500, false},
			{98, "Gem·Ruby", 300, true},
			{99, "Gem·Sapphire", 300, true},
			{93, "Enchant Scroll", 400, true},
			{94, "Protect Scroll", 600, true},
		},
		11
	},

	// ═══ Healing Supplies (healer) ═══
	// Potions, antidotes, herbs
	{
		"healer", "Healing Supplies",
		{
			{10, "Small HP Potion", 20, true},
			{11, "HP Potion", 60, true},
			{12, "Large HP Potion", 200, true},
			{14, "Small MP Potion", 15, true},
			{15, "MP Potion", 45, true},
			{18, "Antidote", 60, true},
			{19, "Panacea", 150, true},
			{20, "Speed Potion", 40, true},
			{23, "Regen Essence", 50, true},
			{40, "Healing Herb", 6, true},
			{41, "Magic Herb", 15, true},
		},
		11
	},

	// ═══ Adventurer's Gear (quest_master) ═══
	// Equipment — weapons, armor, accessories
	{
		"quest_master", "Adventurer's Gear",
		{
			{110, "Wooden Mallet", 200, false},   // lv1, ATK+2
			{116, "Old Pistol", 250, false},       // lv1
			{140, "Leather Cap", 150, false},      // lv1, DEF+1
			{145, "Leather Vest", 300, false},     // lv1, DEF+2
			{150, "Cloth Pants", 100, false},      // lv1, DEF+1
			{155, "Leather Boots", 120, false},    // lv1, DEF+1
			{160, "Cloth Gloves", 80, false},      // lv1, DEF+1
			{111, "Iron Hammer", 600, false},      // lv5, ATK+6
			{146, "Iron Chestplate", 800, false},  // lv5, DEF+4
			{141, "Iron Helmet", 400, false},      // lv5, DEF+2
			{156, "Iron Boots", 350, false},       // lv5, DEF+2
		},
		11
	},

	// ═══ Tea House (tea_house) ═══
	// Specialty foods, decorations, rare goods
	{
		"tea_house", "Tea House Goods",
		{
			{25, "Cooked Meat", 20, true},       // heal 40HP
			{26, "Fruit Salad", 35, true},        // heal 20HP+15MP
			{27, "Royal Feast", 500, true},       // full heal + buff
			{24, "Bread", 8, true},               // staple food
			{198, "Sunglasses", 200, false},      // cosmetic
			{192, "Halo Crown", 2000, false},     // cosmetic
			{196, "Top Hat", 500, false},         // cosmetic
			{109, "TeeFun Badge", 500, false},    // cosmetic
			{190, "Angel Wings", 3000, false},    // legendary cosmetic
			{191, "Demon Horns", 800, false},     // cosmetic
		},
		10
	},
};

static constexpr int NUM_SHOPS = (int)(sizeof(g_aShopData) / sizeof(g_aShopData[0]));

// Find a shop by NPC ID
static inline const SShopEntry *FindShopByNpcID(const char *pNpcID)
{
	if(!pNpcID) return 0;
	for(int i = 0; i < NUM_SHOPS; i++)
	{
		if(str_comp(g_aShopData[i].m_pNpcID, pNpcID) == 0)
			return &g_aShopData[i];
	}
	return 0;
}

#endif

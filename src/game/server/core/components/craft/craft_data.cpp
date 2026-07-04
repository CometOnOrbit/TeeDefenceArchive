#include "craft_data.h"
#include <base/system.h>
#include <cstring>

// TDA item IDs (from src/game/server/item_system.h) — used for fallback
static const int ITEM_SWORD_IRON = 11;
static const int ITEM_SWORD_GOLDEN = 16;
static const int ITEM_SWORD_DIAMOND = 19;
static const int ITEM_SWORD_ENEGRY = 22;
static const int ITEM_PICKAXE_IRON = 15;
static const int ITEM_PICKAXE_ENEGRY = 23;
static const int ITEM_HELMET_IRON = 52;
static const int ITEM_CHEST_IRON = 59;
static const int ITEM_HELMET_DIAMOND = 55;
static const int ITEM_MAT_TITANIUM = 39;
static const int ITEM_MAT_VOID_SHARD = 40;
static const int ITEM_IRON = 3;
static const int ITEM_LOG = 0;
static const int ITEM_GOLD = 4;
static const int ITEM_DIAMOND = 5;
static const int ITEM_ENEGRY = 6;
static const int ITEM_COAL = 1;
static const int ITEM_COPPER = 2;
static const int ITEM_ZOMBIEHEART = 7;

#define ING(id, cnt) { id, cnt }

array<SCraftRecipe> CCraftRecipeDB::ms_aRecipes;

// ─── Init: load from JSON, fallback to hardcoded ──────────────────

void CCraftRecipeDB::Init(IStorage *pStorage)
{
	ms_aRecipes.clear();

	if(!pStorage)
	{
		dbg_msg("craft", "No storage, using fallback recipes");
		LoadFallback();
		return;
	}

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/craft_recipes.json", pStorage);
	if(!pRoot)
	{
		dbg_msg("craft", "craft_recipes.json: %s (using fallback)", Parser.Error());
		LoadFallback();
		return;
	}

	const json_value &Arr = (*pRoot)["recipes"];
	if(Arr.type != json_array)
	{
		dbg_msg("craft", "craft_recipes.json: missing 'recipes' array");
		LoadFallback();
		return;
	}

	int Loaded = 0;
	for(unsigned i = 0; i < Arr.u.array.length; i++)
	{
		const json_value &S = Arr[(int)i];
		if(S.type != json_object) continue;
		if(S["id"].type != json_integer) continue;

		SCraftRecipe R;
		mem_zero(&R, sizeof(R));
		R.m_ID = (int)S["id"].u.integer;
		R.m_ResultItemID = S["result_item_id"].type == json_integer ? (int)S["result_item_id"].u.integer : 0;
		R.m_ResultCount = S["result_count"].type == json_integer ? (int)S["result_count"].u.integer : 1;

		if(S["name"].type == json_string)
			str_copy(R.m_aName, S["name"].u.string.ptr, sizeof(R.m_aName));
		if(S["name_key"].type == json_string)
			str_copy(R.m_aNameKey, S["name_key"].u.string.ptr, sizeof(R.m_aNameKey));
		if(S["description"].type == json_string)
			str_copy(R.m_aDescription, S["description"].u.string.ptr, sizeof(R.m_aDescription));
		if(S["description_key"].type == json_string)
			str_copy(R.m_aDescriptionKey, S["description_key"].u.string.ptr, sizeof(R.m_aDescriptionKey));
		if(S["category"].type == json_string)
			R.m_Category = ParseCategory(S["category"].u.string.ptr);
		if(S["level"].type == json_integer)
			R.m_RequiredLevel = (int)S["level"].u.integer;
		if(S["success_rate"].type == json_double)
			R.m_SuccessRate = (float)S["success_rate"].u.dbl;
		else if(S["success_rate"].type == json_integer)
			R.m_SuccessRate = (float)S["success_rate"].u.integer;
		if(S["gold_cost"].type == json_integer)
			R.m_GoldCost = (int)S["gold_cost"].u.integer;
		if(S["craft_time"].type == json_integer)
			R.m_CraftTime = (int)S["craft_time"].u.integer;

		// Parse ingredients
		R.m_NumIngredients = 0;
		if(S["ingredients"].type == json_array)
		{
			const json_value &Ings = S["ingredients"];
			for(unsigned j = 0; j < Ings.u.array.length && j < MAX_CRAFT_INGREDIENTS; j++)
			{
				const json_value &Ing = Ings[(int)j];
				if(Ing.type != json_object) continue;
				if(Ing["item_id"].type == json_integer)
				{
					R.m_Ingredients[R.m_NumIngredients].m_ItemID = (int)Ing["item_id"].u.integer;
					R.m_Ingredients[R.m_NumIngredients].m_Count = Ing["count"].type == json_integer ? (int)Ing["count"].u.integer : 1;
					R.m_NumIngredients++;
				}
			}
		}

		ms_aRecipes.add(R);
		Loaded++;
	}

	dbg_msg("craft", "Loaded %d recipes from craft_recipes.json", Loaded);
}

// ─── Fallback: hardcoded recipes (original TDA items) ─────────────

void CCraftRecipeDB::LoadFallback()
{
	ms_aRecipes.clear();

	auto Add = [](SCraftRecipe R) { ms_aRecipes.add(R); };

	Add({
		1, ITEM_SWORD_IRON, 1, "Iron Sword", "Iron Sword", "A basic iron blade.", "A basic iron blade.",
		CATEGORY_WEAPONS, 1, 0.85f, 50,
		{ING(ITEM_IRON, 3), ING(ITEM_LOG, 2)}, 2, 90
	});
	Add({
		2, ITEM_SWORD_GOLDEN, 1, "Golden Sword", "Golden Sword", "A shiny golden sword.", "A shiny golden sword.",
		CATEGORY_WEAPONS, 3, 0.75f, 200,
		{ING(ITEM_GOLD, 4), ING(ITEM_LOG, 2)}, 2, 120
	});
	Add({
		3, ITEM_SWORD_DIAMOND, 1, "Diamond Sword", "Diamond Sword", "Superior diamond blade.", "Superior diamond blade.",
		CATEGORY_WEAPONS, 5, 0.65f, 500,
		{ING(ITEM_DIAMOND, 3), ING(ITEM_IRON, 4)}, 2, 180
	});
	Add({
		4, ITEM_SWORD_ENEGRY, 1, "Energy Sword", "Energy Sword", "Advanced energy blade.", "Advanced energy blade.",
		CATEGORY_WEAPONS, 8, 0.50f, 1000,
		{ING(ITEM_ENEGRY, 3), ING(ITEM_DIAMOND, 2), ING(ITEM_MAT_VOID_SHARD, 1)}, 3, 240
	});
	Add({
		5, ITEM_PICKAXE_IRON, 1, "Iron Pickaxe", "Iron Pickaxe", "Mines faster than wood.", "Mines faster than wood.",
		CATEGORY_TOOLS, 2, 0.90f, 80,
		{ING(ITEM_IRON, 3), ING(ITEM_LOG, 2)}, 2, 90
	});
	Add({
		6, ITEM_PICKAXE_ENEGRY, 1, "Energy Pickaxe", "Energy Pickaxe", "Top mining tool.", "Top mining tool.",
		CATEGORY_TOOLS, 7, 0.55f, 800,
		{ING(ITEM_ENEGRY, 3), ING(ITEM_DIAMOND, 2), ING(ITEM_MAT_TITANIUM, 2)}, 3, 240
	});
	Add({
		7, ITEM_HELMET_IRON, 1, "Iron Helmet", "Iron Helmet", "Basic iron helmet.", "Basic iron helmet.",
		CATEGORY_ARMOR, 2, 0.85f, 100,
		{ING(ITEM_IRON, 3)}, 1, 90
	});
	Add({
		8, ITEM_CHEST_IRON, 1, "Iron Chestplate", "Iron Chestplate", "Solid defense.", "Solid defense.",
		CATEGORY_ARMOR, 3, 0.80f, 150,
		{ING(ITEM_IRON, 5)}, 1, 120
	});
	// ... more fallback recipes can be added here

	dbg_msg("craft", "Loaded %d fallback recipes", ms_aRecipes.size());
}

// ─── Helpers ──────────────────────────────────────────────────────

ECraftCategory CCraftRecipeDB::ParseCategory(const char *pName)
{
	if(!pName) return CATEGORY_WEAPONS;
	if(str_comp_nocase(pName, "weapons") == 0) return CATEGORY_WEAPONS;
	if(str_comp_nocase(pName, "armor") == 0) return CATEGORY_ARMOR;
	if(str_comp_nocase(pName, "potions") == 0) return CATEGORY_POTIONS;
	if(str_comp_nocase(pName, "materials") == 0) return CATEGORY_MATERIALS;
	if(str_comp_nocase(pName, "tools") == 0) return CATEGORY_TOOLS;
	return CATEGORY_WEAPONS;
}

// ─── Query API ────────────────────────────────────────────────────

const SCraftRecipe *CCraftRecipeDB::FindByID(int ID)
{
	for(int i = 0; i < ms_aRecipes.size(); i++)
		if(ms_aRecipes[i].m_ID == ID)
			return &ms_aRecipes[i];
	return 0;
}

const SCraftRecipe *CCraftRecipeDB::FindByResult(int ResultItemID)
{
	for(int i = 0; i < ms_aRecipes.size(); i++)
		if(ms_aRecipes[i].m_ResultItemID == ResultItemID)
			return &ms_aRecipes[i];
	return 0;
}

int CCraftRecipeDB::GetRecipeCount()
{
	return ms_aRecipes.size();
}

const SCraftRecipe *CCraftRecipeDB::GetRecipe(int Index)
{
	if(Index < 0 || Index >= ms_aRecipes.size())
		return 0;
	return &ms_aRecipes[Index];
}

const SCraftRecipe *CCraftRecipeDB::GetRecipesByCategory(ECraftCategory Cat, int &OutCount)
{
	static const SCraftRecipe *s_pResults[32];
	int Count = 0;
	for(int i = 0; i < ms_aRecipes.size() && Count < 32; i++)
	{
		if(ms_aRecipes[i].m_Category == Cat)
			s_pResults[Count++] = &ms_aRecipes[i];
	}
	OutCount = Count;
	return Count > 0 ? s_pResults[0] : 0;
}

const char *CCraftRecipeDB::CategoryName(ECraftCategory Cat)
{
	switch(Cat)
	{
	case CATEGORY_WEAPONS: return "武器";
	case CATEGORY_ARMOR: return "护甲";
	case CATEGORY_POTIONS: return "药水";
	case CATEGORY_MATERIALS: return "材料";
	case CATEGORY_TOOLS: return "工具";
	default: return "未知";
	}
}

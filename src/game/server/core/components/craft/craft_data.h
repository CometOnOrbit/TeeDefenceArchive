#ifndef GAME_SERVER_CORE_COMPONENTS_CRAFT_CRAFT_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_CRAFT_CRAFT_DATA_H

// A recipe: combine items to produce an item
#include <base/tl/array.h>
#include <engine/shared/jsonparser.h>
#include <engine/storage.h>

struct SCraftIngredient
{
	int m_ItemID;
	int m_Count;
};

// Max ingredients per recipe
static const int MAX_CRAFT_INGREDIENTS = 5;

// Recipe category/group
enum ECraftCategory
{
	CATEGORY_WEAPONS = 0,
	CATEGORY_ARMOR,
	CATEGORY_POTIONS,
	CATEGORY_MATERIALS,
	CATEGORY_TOOLS,
	CATEGORY_NUM
};

struct SCraftRecipe
{
	int m_ID;                // Unique recipe ID
	int m_ResultItemID;
	int m_ResultCount;
	char m_aName[64];        // Recipe display name (default language, Chinese)
	char m_aNameKey[64];        // MRPG-style localization key for recipe name
	char m_aDescription[128];   // fallback description
	char m_aDescriptionKey[128]; // MRPG-style localization key for description
	ECraftCategory m_Category;
	int m_RequiredLevel;
	float m_SuccessRate;     // 0.0 - 1.0
	int m_GoldCost;
	SCraftIngredient m_Ingredients[MAX_CRAFT_INGREDIENTS];
	int m_NumIngredients;
	int m_CraftTime;         // ticks
};

// Recipe database — loads from JSON, falls back to hardcoded
class CCraftRecipeDB
{
public:
	static void Init(IStorage *pStorage);

	static const SCraftRecipe *FindByID(int ID);
	static const SCraftRecipe *FindByResult(int ResultItemID);
	static int GetRecipeCount();
	static const SCraftRecipe *GetRecipe(int Index);
	static const SCraftRecipe *GetRecipesByCategory(ECraftCategory Cat, int &OutCount);
	static const char *CategoryName(ECraftCategory Cat);

private:
	static void LoadFallback();
	static ECraftCategory ParseCategory(const char *pName);

	static array<SCraftRecipe> ms_aRecipes;
};

#endif

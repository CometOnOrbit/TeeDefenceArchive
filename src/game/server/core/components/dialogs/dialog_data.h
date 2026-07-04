#ifndef GAME_SERVER_CORE_COMPONENTS_DIALOGS_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_DIALOGS_DATA_H

#include <base/tl/array.h>

#include <engine/shared/protocol.h>

#include <game/server/core/components/npcs/npc_manager.h>

// ── Condition types ──────────────────────────────────────────
enum class EDialogCondType
{
	NONE,
	FLAG,        // player flag is true
	NOT_FLAG,    // player flag is false
	HAS_ITEM,    // player has item
	NOT_ITEM,    // player doesn't have item
	LEVEL_GE,    // level >= N
	LEVEL_EQ,    // level == N
	REPUTATION_GE,
	REPUTATION_LT,
	GOLD_GE,
	QUEST_DONE,  // quest completed
	QUEST_ACTIVE,// quest active (accepted but not completed)
	CHANCE,      // random chance (0.0 - 1.0)
	CUSTOM,      // extensible: runs a named condition function
};

// ── Action types ─────────────────────────────────────────────
enum class EDialogActionType
{
	NONE,
	SET_FLAG,
	CLEAR_FLAG,
	ADD_REPUTATION,
	ADD_GOLD,
	REMOVE_GOLD,
	ADD_ITEM,
	REMOVE_ITEM,
	COMPLETE_QUEST,
	START_QUEST,
	HEAL,
	TELEPORT,
	OPEN_SHOP,
	OPEN_SKILLS,
	OPEN_CRAFT,
	OPEN_QUESTS,
	MESSAGE,
	SET_LEVEL,
	SET_ATTRIBUTE,
	CUSTOM, // extensible: runs a named action function
};

// ── Single condition node ────────────────────────────────────
struct CDialogCondition
{
	EDialogCondType m_Type = EDialogCondType::NONE;
	char m_aKey[64]{};   // flag name / item id / quest id
	int m_Value = 0;     // numeric value for GE/EQ/CHANCE etc.
	float m_fValue = 0.f;

	bool Evaluate(class CPlayer *pPlayer, class CGameContext *pGS) const;
};

// ── Single action node ───────────────────────────────────────
struct CDialogAction
{
	EDialogActionType m_Type = EDialogActionType::NONE;
	char m_aKey[64]{};   // flag name / item id / quest id / map name
	int m_Value = 0;     // numeric amount / level / value
	float m_fValue = 0.f;
	char m_aStrValue[256]{}; // string payload (message text, etc.)

	void Execute(class CPlayer *pPlayer, class CGameContext *pGS) const;
};

// ── One dialog step ──────────────────────────────────────────
struct CDialogStep
{
	char m_aId[32]{};               // step identifier (e.g. "greeting", "story_1")
	// multiple condition sets (OR of ANDs)
	array<array<CDialogCondition>> m_Conditions;
	array<CDialogAction> m_Actions;

	// dialog text lines (sent one by one)
	char m_aaTextLines[8][256]{};   // up to 8 lines per step (default language, Chinese)
	char m_aTextKey[64]{};      // MRPG-style localization key for dialog text
	int m_NumLines = 0;

	// branching
	char m_aNextStep[32]{};         // "" means end dialog
	char m_aFailTextKey[64]{};      // MRPG-style localization key for fail text
	char m_aFailText[256]{};        // fallback text shown when conditions fail (optional)
};

// ── Full NPC dialog definition ───────────────────────────────
struct SNpcDialogDef
{
	char m_aNpcId[NPC_KEY_LEN]{};
	array<CDialogStep> m_Steps;

	const CDialogStep *FindFirstMatching(class CPlayer *pPlayer, class CGameContext *pGS, const char *pStartStep = nullptr) const;
};

#endif // GAME_SERVER_CORE_COMPONENTS_DIALOGS_DATA_H

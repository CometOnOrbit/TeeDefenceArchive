#ifndef GAME_SERVER_CORE_COMPONENTS_QUESTS_QUEST_BOARD_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_QUESTS_QUEST_BOARD_DATA_H

#include <base/tl/array.h>
#include <base/vmath.h>

class CQuestDescription;

constexpr auto TW_QUEST_BOARDS_TABLE = "tw_quest_boards";
constexpr auto TW_QUESTS_DAILY_BOARD_LIST = "tw_quests_board_list";

class CQuestsBoard
{
	int m_ID {};
	char m_aName[64] {};
	vec2 m_Pos {};
	int m_WorldID {};
	array<CQuestDescription*> m_vQuestList {};

public:
	CQuestsBoard() = default;

	void Init(int ID, const char* pName, vec2 Pos, int WorldID)
	{
		m_ID = ID;
		str_copy(m_aName, pName, sizeof(m_aName));
		m_Pos = Pos;
		m_WorldID = WorldID;
	}

	int GetID() const { return m_ID; }
	const char* GetName() const { return m_aName; }
	vec2 GetPos() const { return m_Pos; }
	int GetWorldID() const { return m_WorldID; }
	array<CQuestDescription*>& GetQuestList() { return m_vQuestList; }
};

class CEntityQuestBoard;

#endif
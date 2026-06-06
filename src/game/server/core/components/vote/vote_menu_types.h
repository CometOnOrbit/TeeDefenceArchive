#ifndef GAME_SERVER_COMPONENT_VOTE_MENU_TYPES_H
#define GAME_SERVER_COMPONENT_VOTE_MENU_TYPES_H

#include <base/tl/array.h>

#include <game/voting.h>

enum EVoteMenuPage
{
	PAGE_MENU = 0,
	PAGE_INVENTORY,
	PAGE_CHECK_ITEM,
	PAGE_CRAFT,
	PAGE_CRAFT_SELECTED,
	PAGE_EQUIPMENT,
	PAGE_TURRET,
	PAGE_TURRET_AMMO,
	PAGE_WORLDS,
	PAGE_COMMUNITY,
	PAGE_DIFFICULTY,
};

struct SPlayerVote
{
	enum EVoteSelect
	{
		ITEMLIST = 0,
		ITEM,
		EQUIPMENT,
		NUM_SELECT,
	};

	struct SVoteOptions
	{
		char m_aDescription[VOTE_DESC_LENGTH];
		char m_aCommand[VOTE_CMD_LENGTH];
	};

	array<SVoteOptions> m_aVoteOptions;
	int m_LastPage;
	int m_Page;
	int m_Select[NUM_SELECT];
	bool m_Confirm;
	char m_aExtraText[VOTE_DESC_LENGTH];

	void Reset()
	{
		m_aVoteOptions.clear();
		m_LastPage = 0;
		m_Page = 0;
		m_Confirm = false;
		for(int i = 0; i < NUM_SELECT; i++)
			m_Select[i] = 0;
		m_aExtraText[0] = 0;
	}
};

#endif

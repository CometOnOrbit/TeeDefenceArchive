#ifndef GAME_SERVER_CORE_COMPONENTS_PROFESSION_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_PROFESSION_MANAGER_H

#include <game/server/core/tworld_component.h>
#include <game/server/core/components/accounts/profession_data.h>

class CCommandManager;

class CProfessionManager : public TWorldComponent
{
public:
	void RegisterChatCommands(CCommandManager *pManager);

	// Select profession for a player
	bool SetProfession(int ClientID, EProfession Prof);

	// Get XP required for next MMO level
	static int GetExpForLevel(int Level);

private:
	// Chat command handlers
	static void ConProfessionList(void *pUser);
	static void ConProfessionSet(void *pUser);
};

#endif

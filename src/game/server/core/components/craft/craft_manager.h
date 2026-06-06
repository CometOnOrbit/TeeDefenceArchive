#ifndef GAME_SERVER_COMPONENT_CRAFT_MANAGER_H
#define GAME_SERVER_COMPONENT_CRAFT_MANAGER_H

#include <game/server/core/tworld_component.h>

class CCommandManager;
class IConsole;

class CCraftManager : public TWorldComponent
{
protected:
	void OnConsoleInit() override;

public:
	bool TryCraftOneItem(int ClientID, int Item, char *pErr, int ErrSize);

	void RegisterChatCommands(CCommandManager *pManager);
	void RegisterVoteCommands(CCommandManager *pManager);
};

#endif

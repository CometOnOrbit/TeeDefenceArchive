#ifndef GAME_SERVER_COMPONENT_ACCOUNT_MANAGER_H
#define GAME_SERVER_COMPONENT_ACCOUNT_MANAGER_H

#include <game/server/account.h>
#include <game/server/core/tworld_component.h>

class CCommandManager;

class CAccountManager : public TWorldComponent
{
	CAccountSystem m_Accounts;

protected:
	void OnPostInit() override;
	void OnConsoleInit() override;
	void OnTick() override;
	void OnShutdown() override;

public:
	CAccountSystem *System() { return &m_Accounts; }
	const CAccountSystem *System() const { return &m_Accounts; }

	void RegisterChatCommands(CCommandManager *pManager);
};

#endif

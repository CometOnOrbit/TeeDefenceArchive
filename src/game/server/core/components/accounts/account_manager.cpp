#include <engine/engine.h>

#include <game/server/core/components/accounts/account_manager.h>
#include <game/server/gamecontext.h>

void CAccountManager::OnPostInit()
{
	if(!GS() || !Engine())
		return;
	if(!m_Accounts.Init(GS(), Engine(), Console(), GS()->Config()))
		dbg_msg("server", "account subsystem failed (see sv_mysql_* / MySQL client install)");
}

void CAccountManager::OnConsoleInit()
{
	if(GS())
		m_Accounts.RegisterConsoleCommands(Console(), GS());
}

void CAccountManager::OnTick()
{
	m_Accounts.OnGameTick();
}

void CAccountManager::OnShutdown()
{
	m_Accounts.Shutdown();
}

void CAccountManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(pManager && GS())
		m_Accounts.RegisterChatCommands(pManager, GS());
}

#include <engine/shared/config.h>

#include <game/server/account.h>
#include <game/server/botengine.h>
#include <game/server/core/components/accounts/account_manager.h>
#include <game/server/core/components/bots/defence_bot_manager.h>
#include <game/server/core/components/craft/craft_manager.h>
#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include "tworld_controller.h"

TWorldController::TWorldController(CGameContext *pGameServer)
	: m_pGameServer(pGameServer),
	m_pLocalizationManager(nullptr),
	m_pAccountManager(nullptr),
	m_pVoteMenuManager(nullptr),
	m_pCraftManager(nullptr),
	m_pWorldManager(nullptr),
	m_pDefenceBotManager(nullptr)
{
	m_System.Add(m_pLocalizationManager = new CLocalizationManager);
	m_System.Add(m_pAccountManager = new CAccountManager);
	m_System.Add(m_pVoteMenuManager = new CVoteMenuManager);
	m_System.Add(m_pCraftManager = new CCraftManager);
	m_System.Add(m_pWorldManager = new CWorldManager);
	m_System.Add(m_pDefenceBotManager = new CDefenceBotManager);
}

void TWorldController::OnInit(IServer *pServer, IConsole *pConsole, IStorage *pStorage, IEngine *pEngine)
{
	const char *pMap = "";
	if(m_pGameServer && m_pGameServer->Config() && m_pGameServer->Config()->m_SvMap[0])
		pMap = m_pGameServer->Config()->m_SvMap;

	for(int i = 0; i < m_System.m_apComponents.size(); i++)
	{
		TWorldComponent *pComponent = m_System.m_apComponents[i];
		pComponent->m_Core = this;
		pComponent->m_GameServer = m_pGameServer;
		pComponent->m_pServer = pServer;
		pComponent->m_pConsole = pConsole;
		pComponent->m_pStorage = pStorage;
		pComponent->m_pEngine = pEngine;

		pComponent->OnPreInit();
		pComponent->OnInitWorld(pMap);
		pComponent->OnPostInit();
	}
}

void TWorldController::OnConsoleInit(IConsole *pConsole) const
{
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
	{
		TWorldComponent *pComponent = m_System.m_apComponents[i];
		pComponent->m_pConsole = pConsole;
		pComponent->OnConsoleInit();
	}
}

void TWorldController::OnTick() const
{
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
		m_System.m_apComponents[i]->OnTick();
}

void TWorldController::OnShutdown()
{
	for(int i = m_System.m_apComponents.size() - 1; i >= 0; i--)
		m_System.m_apComponents[i]->OnShutdown();
}

void TWorldController::OnResetClientData(int ClientID) const
{
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
		m_System.m_apComponents[i]->OnClientReset(ClientID);
}

void TWorldController::OnCharacterSpawn(CPlayer *pPlayer) const
{
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
		m_System.m_apComponents[i]->OnCharacterSpawn(pPlayer);
}

IServer *TWorldController::Server() const
{
	return m_pGameServer ? m_pGameServer->Server() : nullptr;
}

CAccountSystem *TWorldController::Account() const
{
	return m_pAccountManager ? m_pAccountManager->System() : nullptr;
}

CItemHelper *TWorldController::Items() const
{
	return m_pGameServer ? m_pGameServer->ItemHelper() : nullptr;
}

CBotEngine *TWorldController::BotEngine() const
{
	return m_pGameServer ? m_pGameServer->BotEngine() : nullptr;
}

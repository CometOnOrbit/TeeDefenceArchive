#ifndef GAME_SERVER_CORE_TWORLD_CONTROLLER_H
#define GAME_SERVER_CORE_TWORLD_CONTROLLER_H

#include "tworld_component.h"

class CAccountManager;
class CAccountSystem;
class CBotEngine;
class CCraftManager;
class CGameContext;
class CItemHelper;
class CLocalizationManager;
class CTravelManager;
class CDefenceBotManager;
class CVoteMenuManager;
class CPlayer;
class IConsole;
class IEngine;
class IStorage;
class IServer;

/** Central controller — mirrors kurosio/MRPG CMmoController. */
class TWorldController
{
	CGameContext *m_pGameServer;
	TWorldComponent::CStack m_System;

	CLocalizationManager *m_pLocalizationManager;
	CAccountManager *m_pAccountManager;
	CVoteMenuManager *m_pVoteMenuManager;
	CCraftManager *m_pCraftManager;
	CTravelManager *m_pTravelManager;
	CDefenceBotManager *m_pDefenceBotManager;

public:
	explicit TWorldController(CGameContext *pGameServer);
	~TWorldController() = default;

	void OnInit(IServer *pServer, IConsole *pConsole, IStorage *pStorage, IEngine *pEngine);
	void OnConsoleInit(IConsole *pConsole) const;
	void OnTick() const;
	void OnShutdown();
	void OnResetClientData(int ClientID) const;
	void OnCharacterSpawn(CPlayer *pPlayer) const;

	CGameContext *GS() const { return m_pGameServer; }
	IServer *Server() const;
	CAccountSystem *Account() const;
	CItemHelper *Items() const;
	CBotEngine *BotEngine() const;

	CLocalizationManager *LocalizationManager() const { return m_pLocalizationManager; }
	CAccountManager *AccountManager() const { return m_pAccountManager; }
	CVoteMenuManager *VoteMenuManager() const { return m_pVoteMenuManager; }
	CCraftManager *CraftManager() const { return m_pCraftManager; }
	CTravelManager *TravelManager() const { return m_pTravelManager; }
	CDefenceBotManager *DefenceBotManager() const { return m_pDefenceBotManager; }

	CLocalizationManager &Loc() const { return *m_pLocalizationManager; }
	CTravelManager &Travel() const { return *m_pTravelManager; }
	CDefenceBotManager &DefenceBots() const { return *m_pDefenceBotManager; }
};

#endif

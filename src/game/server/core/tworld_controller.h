#ifndef GAME_SERVER_CORE_TWORLD_CONTROLLER_H
#define GAME_SERVER_CORE_TWORLD_CONTROLLER_H

#include "tworld_component.h"

#include <game/server/core/tools/event_listener.h>
#include <game/server/entity_manager.h>

class CAccountManager;
class CAccountSystem;
class CBotEngine;
class CCraftManager;
class CGameContext;
class CItemHelper;
class CLocalizationManager;
class CWorldManager;
class CPortalManager;
class CNpcManager;
class CQuestManager;
class CSkillManager;
class CDefenceBotManager;
class CEffectRegistry;
class CEnemyRegistry;
class CStatusManager;
class CTraitManager;
class CVoteMenuManager;
class CMetaManager;
class CAchievementManager;
class CDutiesManager;
class CMiniEventsManager;
class CDurabilityManager;
class CGuildManager;
class CMMOManager;
class CProfessionManager;
class CDungeonManager;
class CWorldBossManager;
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
	CEventListenerHub m_Events;
	CEntityManager m_EntityManager;

	CLocalizationManager *m_pLocalizationManager;
	CAccountManager *m_pAccountManager;
	CVoteMenuManager *m_pVoteMenuManager;
	CCraftManager *m_pCraftManager;
	CWorldManager *m_pWorldManager;
	CPortalManager *m_pPortalManager;
	CNpcManager *m_pNpcManager;
	class CDialogManager *m_pDialogManager;
	CQuestManager *m_pQuestManager;
	CDefenceBotManager *m_pDefenceBotManager;
	CEffectRegistry *m_pEffectRegistry;
	CStatusManager *m_pStatusManager;
	CSkillManager *m_pSkillManager;
	CTraitManager *m_pTraitManager;
	CEnemyRegistry *m_pEnemyRegistry;
	CMetaManager *m_pMetaManager;
	CAchievementManager *m_pAchievementManager;
	CDutiesManager *m_pDutiesManager;
	CMiniEventsManager *m_pMiniEventsManager;
	CDurabilityManager *m_pDurabilityManager;
	CGuildManager *m_pGuildManager;
	CMMOManager *m_pMMOManager;
	CProfessionManager *m_pProfessionManager;
	CDungeonManager *m_pDungeonManager;
	CWorldBossManager *m_pWorldBossManager;

public:
	explicit TWorldController(CGameContext *pGameServer);
	~TWorldController();

	void OnInit(IServer *pServer, IConsole *pConsole, IStorage *pStorage, IEngine *pEngine);
	void OnConsoleInit(IConsole *pConsole) const;
	void OnTick() const;
	void OnShutdown();
	void OnResetClientData(int ClientID) const;
	void OnCharacterSpawn(CPlayer *pPlayer);
	void OnPlayerLogin(CPlayer *pPlayer);
	bool DispatchPlayerVoteCommand(int ClientID, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason) const;

	int ComponentCount() const { return m_System.m_apComponents.size(); }
	TWorldComponent *GetComponent(int idx) const
	{
		if(idx < 0 || idx >= m_System.m_apComponents.size())
			return nullptr;
		return m_System.m_apComponents[idx];
	}

	CGameContext *GS() const { return m_pGameServer; }
	IServer *Server() const;
	CAccountSystem *Account() const;
	CItemHelper *Items() const;
	CBotEngine *BotEngine() const;

	CEventListenerHub &Events() { return m_Events; }
	const CEntityManager *EntityManager() const { return &m_EntityManager; }
	CEntityManager *EntityManager() { return &m_EntityManager; }

	CLocalizationManager *LocalizationManager() const { return m_pLocalizationManager; }
	CAccountManager *AccountManager() const { return m_pAccountManager; }
	CVoteMenuManager *VoteMenuManager() const { return m_pVoteMenuManager; }
	CCraftManager *CraftManager() const { return m_pCraftManager; }
	CWorldManager *WorldManager() const { return m_pWorldManager; }
	CPortalManager *PortalManager() const { return m_pPortalManager; }
	CNpcManager *NpcManager() const { return m_pNpcManager; }
	class CDialogManager *DialogManager() const { return m_pDialogManager; }
	CQuestManager *QuestManager() const { return m_pQuestManager; }
	CDefenceBotManager *DefenceBotManager() const { return m_pDefenceBotManager; }
	CEffectRegistry *EffectRegistry() const { return m_pEffectRegistry; }
	CStatusManager *StatusManager() const { return m_pStatusManager; }
	CSkillManager *SkillManager() const { return m_pSkillManager; }
	CTraitManager *TraitManager() const { return m_pTraitManager; }
	CEnemyRegistry *EnemyRegistry() const { return m_pEnemyRegistry; }
	CMetaManager *MetaManager() const { return m_pMetaManager; }
	CAchievementManager *AchievementManager() const { return m_pAchievementManager; }
	CDutiesManager *DutiesManager() const { return m_pDutiesManager; }
	CMiniEventsManager *MiniEventsManager() const { return m_pMiniEventsManager; }
	CDurabilityManager *DurabilityManager() const { return m_pDurabilityManager; }
	CGuildManager *GuildManager() const { return m_pGuildManager; }
	CMMOManager *GetMMOManager() const { return m_pMMOManager; }
	CProfessionManager *ProfessionManager() const { return m_pProfessionManager; }
	CDungeonManager *GetDungeonManager() const { return m_pDungeonManager; }
	CWorldBossManager *GetWorldBossManager() const { return m_pWorldBossManager; }

	CLocalizationManager &Loc() const { return *m_pLocalizationManager; }
	CWorldManager &Worlds() const { return *m_pWorldManager; }
	CDefenceBotManager &DefenceBots() const { return *m_pDefenceBotManager; }
};

#endif

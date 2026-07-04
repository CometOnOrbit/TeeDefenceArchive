#include <engine/shared/config.h>
#include <engine/shared/world_detail.h>

#include <game/server/account.h>
#include <game/server/botengine.h>
#include <game/server/core/components/accounts/account_manager.h>
#include <game/server/core/components/bots/defence_bot_manager.h>
#include <game/server/core/components/craft/craft_manager.h>
#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/components/meta/achievement_manager.h>
#include <game/server/core/components/meta/duties_manager.h>
#include <game/server/core/components/meta/durability_manager.h>
#include <game/server/core/components/meta/meta_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/dialogs/dialog_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/worlds/portal_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/components/skills/skill_manager.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/enemy_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/guilds/guild_manager.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/mmo/mmo_world_boss.h>
#include <game/server/core/components/profession/profession_manager.h>
#include <game/server/core/components/dungeon/dungeon_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include "tworld_controller.h"

// Determine world type from server detail (available at construction time)
static WorldType GetWorldType(CGameContext *pGS)
{
	if(!pGS || !pGS->Server()) return WorldType::Defence;
	const CWorldDetail *pDetail = pGS->Server()->GetWorldDetail(pGS->GetWorldID());
	return pDetail ? pDetail->GetType() : WorldType::Defence;
}

TWorldController::TWorldController(CGameContext *pGameServer)
	: m_pGameServer(pGameServer),
	m_EntityManager(pGameServer),
	m_pLocalizationManager(nullptr),
	m_pAccountManager(nullptr),
	m_pVoteMenuManager(nullptr),
	m_pCraftManager(nullptr),
	m_pWorldManager(nullptr),
	m_pPortalManager(nullptr),
	m_pNpcManager(nullptr),
	m_pDialogManager(nullptr),
	m_pQuestManager(nullptr),
	m_pDefenceBotManager(nullptr),
	m_pEffectRegistry(nullptr),
	m_pStatusManager(nullptr),
	m_pSkillManager(nullptr),
	m_pTraitManager(nullptr),
	m_pEnemyRegistry(nullptr),
	m_pMetaManager(nullptr),
	m_pAchievementManager(nullptr),
	m_pDutiesManager(nullptr),
	m_pMiniEventsManager(nullptr),
	m_pDurabilityManager(nullptr),
	m_pGuildManager(nullptr),
	m_pMMOManager(nullptr),
	m_pProfessionManager(nullptr),
	m_pDungeonManager(nullptr),
	m_pWorldBossManager(nullptr)
{
	const WorldType WT = GetWorldType(pGameServer);

	// Universal components — every world type needs these
	m_System.Add(m_pLocalizationManager = new CLocalizationManager);
	m_System.Add(m_pAccountManager = new CAccountManager);
	m_System.Add(m_pMetaManager = new CMetaManager);
	m_System.Add(m_pAchievementManager = new CAchievementManager);
	m_System.Add(m_pDutiesManager = new CDutiesManager);
	m_System.Add(m_pMiniEventsManager = new CMiniEventsManager);
	m_System.Add(m_pDurabilityManager = new CDurabilityManager);
	m_System.Add(m_pVoteMenuManager = new CVoteMenuManager);
	m_System.Add(m_pWorldManager = new CWorldManager);
	m_System.Add(m_pGuildManager = new CGuildManager);

	// Combat worlds (Defence, PvP, RPG, Story) — combat effects, status, enemy registry
	if(WT != WorldType::Hub)
	{
		m_System.Add(m_pEffectRegistry = new CEffectRegistry);
		m_System.Add(m_pStatusManager = new CStatusManager);
		m_System.Add(m_pEnemyRegistry = new CEnemyRegistry);
	}

	// NPC interaction worlds (Hub, RPG, Story)
	if(WT == WorldType::Hub || WT == WorldType::RPG || WT == WorldType::Story)
	{
		m_System.Add(m_pNpcManager = new CNpcManager);
		m_System.Add(m_pDialogManager = new CDialogManager);
	}

	// Portal worlds (RPG, Hub) — world-to-world travel
	if(WT == WorldType::RPG || WT == WorldType::Hub)
	{
		m_System.Add(m_pPortalManager = new CPortalManager);
	}

	// Full MMO worlds (RPG, Story)
	if(WT == WorldType::RPG || WT == WorldType::Story)
	{
		m_System.Add(m_pCraftManager = new CCraftManager);
		m_System.Add(m_pSkillManager = new CSkillManager);
		m_System.Add(m_pTraitManager = new CTraitManager);
		m_System.Add(m_pQuestManager = new CQuestManager);
		m_System.Add(m_pMMOManager = new CMMOManager);
		m_System.Add(m_pProfessionManager = new CProfessionManager);
		m_pDungeonManager = new CDungeonManager;
		m_System.Add(m_pWorldBossManager = new CWorldBossManager);
	}

	// Defence world — zombie defense bot manager
	if(WT == WorldType::Defence)
	{
		m_System.Add(m_pDefenceBotManager = new CDefenceBotManager);
	}
}

void TWorldController::OnInit(IServer *pServer, IConsole *pConsole, IStorage *pStorage, IEngine *pEngine)
{
	const char *pMap = "";
	if(m_pGameServer && m_pGameServer->Server())
		pMap = m_pGameServer->Server()->GetWorldMapPath(m_pGameServer->GetWorldID());

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

	if(m_pDungeonManager)
		m_pDungeonManager->Init(m_pGameServer);
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

	if(m_pDungeonManager)
		m_pDungeonManager->Tick();

	// m_pWorldBossManager->OnTick() called via component loop above
}

TWorldController::~TWorldController()
{
	delete m_pDungeonManager;
	m_pDungeonManager = nullptr;
	// m_pWorldBossManager deleted by CStack::~CStack()
	m_pWorldBossManager = nullptr;
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

void TWorldController::OnCharacterSpawn(CPlayer *pPlayer)
{
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
		m_System.m_apComponents[i]->OnCharacterSpawn(pPlayer);
	if(pPlayer && !pPlayer->IsDummy())
		m_Events.EmitCharacterSpawn(pPlayer);
}

void TWorldController::OnPlayerLogin(CPlayer *pPlayer)
{
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
		m_System.m_apComponents[i]->OnPlayerLogin(pPlayer);
	if(pPlayer && !pPlayer->IsDummy())
		m_Events.EmitPlayerLogin(pPlayer);
}

bool TWorldController::DispatchPlayerVoteCommand(int ClientID, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_pGameServer)
		return false;
	CPlayer *pP = m_pGameServer->m_apPlayers[ClientID];
	if(!pP)
		return false;
	for(int i = 0; i < m_System.m_apComponents.size(); i++)
	{
		if(m_System.m_apComponents[i]->OnPlayerVoteCommand(pP, pCmd, pArgs, ReasonNumber, pReason))
			return true;
	}
	return false;
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

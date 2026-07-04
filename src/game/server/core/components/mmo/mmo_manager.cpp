#include "mmo_manager.h"
#include "mmo_item.h"
#include "mmo_types.h"
#include "mmo_world_boss.h"

#include <game/server/data_center.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/server/entities/character_bot_ai.h>
#include <game/server/core/tworld_controller.h>

#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <base/system.h>

// ─── Lifecycle ───────────────────────────────────────────────────────

void CMMOManager::OnPreInit()
{
	// Static data is now loaded once by CDataCenter::Init()
	// in gamecontext.cpp before any world initialization.
}

void CMMOManager::OnShutdown()
{
}

void CMMOManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;

	const int WorldID = GS()->GetWorldID();
	LoadWorldSpawns(WorldID);
	LoadZoneDefs();
	SpawnZoneMobs(WorldID);
	LoadNPCBots();
}

void CMMOManager::OnConsoleInit()
{
	// Debug / management commands are registered in mmo_commands.cpp
	RegisterCheckinCommands();
	RegisterDebugCommands();
	RegisterEnchantCommands();
	RegisterRankingCommands();
	RegisterAuctionCommands();
	RegisterHouseCommands();
	RegisterMountCommands();
	RegisterAutoPathCommands();
	RegisterFashionCommands();
	RegisterPetCommands();
	RegisterMarriageCommands();

	if(CWorldBossManager *pWB = Core() ? Core()->GetWorldBossManager() : nullptr)
		pWB->RegisterBossCommands();
}

// ─── Tick ────────────────────────────────────────────────────────────

void CMMOManager::OnTick()
{
	const int Now = (int)time(nullptr);

	// Clean up expired trades
	for(int i = 0; i < m_NumTrades; i++)
	{
		auto &T = m_aTrades[i];
		if(T.m_Active && Now > T.m_ExpiresAt)
		{
			if(T.m_PartyA_CID >= 0)
				GS()->SendChatTo(T.m_PartyA_CID, "交易已超时取消。");
			if(T.m_PartyB_CID >= 0)
				GS()->SendChatTo(T.m_PartyB_CID, "交易已超时取消。");
			T.Clear();
		}
	}

	// Clean up empty groups from trailing end
	while(m_NumGroups > 0 && m_aGroups[m_NumGroups - 1].m_ID < 0)
		m_NumGroups--;

	// Tick per-map world mob spawning
	TickWorldSpawns();

	// Tick registered bots
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || !pP->IsDummy())
			continue;
		TickMMOBot(pP);
	}
}

// ─── Emote Item Use ──────────────────────────────────────────────────

void CMMOManager::UseItemByEmoticon(CPlayer *pPlayer, int Emoticon)
{
	if(!pPlayer) return;
	if(Emoticon != 36)
		return;

	GS()->SendChatTo(pPlayer->GetCID(), "emote 36 auto-use");
}


#include "mmo_manager.h"
#include <game/server/data_center.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/entities/character_bot_ai.h>
#include <game/server/entities/ai_core/mob_ai.h>
#include <game/server/entities/ai_core/npc_ai.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/entities/character.h>
#include <game/server/core/components/mmo/mmo_item.h>

// ─── SMMOZoneDef (forwarders — data now lives in CDataCenter) ──────
// No static members here; moved to CDataCenter.

void CMMOManager::LoadNPCBots()
{
	if(!Storage())
	{
		dbg_msg("mmo_npc", "No storage available");
		return;
	}

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/mmo/npc_spawns.json", Storage());
	if(!pRoot)
	{
		dbg_msg("mmo_npc", "npc_spawns.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["spawns"];
	if(Arr.type != json_array)
	{
		dbg_msg("mmo_npc", "missing 'spawns' array");
		return;
	}

	int Spawned = 0;
	for(unsigned i = 0; i < Arr.u.array.length; i++)
	{
		const json_value &S = Arr[(int)i];
		if(S.type != json_object) continue;

		int World = S["world"].type == json_integer ? (int)S["world"].u.integer : 0;
		if(World != GS()->GetWorldID())
			continue;

		vec2 Pos;
		if(S["x"].type == json_integer && S["y"].type == json_integer)
			Pos = vec2((float)S["x"].u.integer, (float)S["y"].u.integer);
		else
			continue;

		char aName[64];
		if(S["name"].type == json_string)
			str_copy(aName, S["name"].u.string.ptr, sizeof(aName));
		else
			str_format(aName, sizeof(aName), "Bot_%d", i);

		// Find a free bot slot (same pattern as SpawnMob)
		const int BotSlotStart = MAX_HUMAN_CLIENTS;
		int CID = -1;
		for(int c = BotSlotStart; c < MAX_CLIENTS; c++)
		{
			if(!GS()->m_apPlayers[c])
			{
				CID = c;
				break;
			}
		}
		if(CID < 0)
		{
			dbg_msg("mmo_npc", "No free bot slot for NPC '%s'", aName);
			continue;
		}

		// Clean up existing state on this slot
		if(GS()->m_apPlayers[CID])
		{
			delete GS()->m_apPlayers[CID]->m_pMMOBotData;
			GS()->m_apPlayers[CID]->m_pMMOBotData = 0;
			GS()->Server()->DummyRemove(CID);
			delete GS()->m_apPlayers[CID];
			GS()->m_apPlayers[CID] = 0;
		}

		// Create player + register as bot
		GS()->Server()->DummyJoin(CID, aName, GS()->GetWorldID());
		CPlayer *pPlayer = GS()->m_apPlayers[CID];
		if(!pPlayer)
		{
			dbg_msg("mmo_npc", "Failed to create player for NPC '%s'", aName);
			continue;
		}

		pPlayer->SetTeam(TEAM_BLUE);

		// Apply skin
		if(S["skin"].type == json_string)
		{
			str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[0], S["skin"].u.string.ptr,
				sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[0]));
		}
		if(S["color_body"].type == json_integer)
		{
			pPlayer->m_TeeInfos.m_aUseCustomColors[0] = 1;
			pPlayer->m_TeeInfos.m_aSkinPartColors[0] = (int)S["color_body"].u.integer;
		}
		if(S["color_feet"].type == json_integer)
		{
			pPlayer->m_TeeInfos.m_aSkinPartColors[2] = (int)S["color_feet"].u.integer;
		}

		GS()->BroadcastClientInfo(CID, false);

		// Allocate and attach SMMOBotData so other systems recognise this as a bot
		SMMOBotData *pData = new SMMOBotData();
		pData->m_Level = 1;
		pData->m_MaxHP = 99999; // NPCs are effectively invulnerable
		pData->m_HP = 99999;
		pData->m_SpawnPos = Pos;
		pData->m_IsNPC = true;
		pPlayer->m_pMMOBotData = pData;

		// Spawn as CCharacterBotAI
		CCharacterBotAI *pChr = new(CID) CCharacterBotAI(&GS()->m_World);
		if(!pChr || !pChr->Spawn(pPlayer, Pos))
		{
			dbg_msg("mmo_npc", "Failed to spawn NPC '%s'", aName);
			if(pChr) delete pChr;
			continue;
		}

		// Attach NPC AI
		{
			SMMONpcInfo NpcInfo;
			NpcInfo.m_Static = S["static"].type == json_boolean ? (S["static"].u.boolean != 0) : true;
			NpcInfo.m_GuardRadius = S["radius"].type == json_integer ? (int)S["radius"].u.integer : 80;
			str_copy(NpcInfo.m_aName, aName, sizeof(NpcInfo.m_aName));
			pChr->SetAI(std::make_unique<CNpcAI>(pChr, NpcInfo));
		}

		// Give at least hammer so the bot has a weapon
		pChr->GiveWeapon(0, -1);

		Spawned++;
	}

	if(Spawned > 0)
		dbg_msg("mmo_npc", "Spawned %d NPC bots in world %d", Spawned, GS()->GetWorldID());
}



int CMMOManager::SpawnMob(int DefID, vec2 Pos, int PreferredSlot)
{
	const SMMOMobDef *pDef = SMMOMobDef::Get(DefID);
	if(!pDef)
	{
		dbg_msg("mmo_bot", "Unknown mob def %d", DefID);
		return -1;
	}

	CGameContext *pGS = GS();
	if(!pGS) { dbg_msg("mmo_bot", "SpawnMob: no game context"); return -1; }

	int CID = -1;
	const int BotSlotStart = MAX_HUMAN_CLIENTS;

	auto SlotUsable = [&](int Slot) {
		if(Slot < BotSlotStart || Slot >= MAX_CLIENTS)
			return false;
		if(pGS->m_apPlayers[Slot] && !pGS->m_apPlayers[Slot]->IsDummy())
			return false;
		if(pGS->m_apPlayers[Slot] && pGS->m_apPlayers[Slot]->m_pMMOBotData && pGS->m_apPlayers[Slot]->m_pMMOBotData->IsAlive())
			return false;
		return CCharacterBotAI::PoolSlotFree(Slot);
	};

	if(PreferredSlot >= 0 && SlotUsable(PreferredSlot))
		CID = PreferredSlot;

	if(CID < 0)
	{
		for(int i = BotSlotStart; i < MAX_CLIENTS; i++)
		{
			if(!pGS->m_apPlayers[i] || !pGS->m_apPlayers[i]->m_pMMOBotData)
			{
				if(CCharacterBotAI::PoolSlotFree(i))
				{
					CID = i;
					break;
				}
			}
		}
	}
	if(CID < 0)
	{
		dbg_msg("mmo_bot", "SpawnMob: No free bot slots!");
		return -1;
	}

	// Clean up existing state on this slot
	if(pGS->m_apPlayers[CID])
	{
		// Clear bot data first (before DummyRemove) so slot scans skip this CID
		delete pGS->m_apPlayers[CID]->m_pMMOBotData;
		pGS->m_apPlayers[CID]->m_pMMOBotData = 0;

		// DummyRemove takes the normal disconnect path which properly handles
		// KillCharacter → delete m_pCharacter (no double-free this way)
		pGS->Server()->DummyRemove(CID);
		delete pGS->m_apPlayers[CID];
		pGS->m_apPlayers[CID] = 0;
	}

	// Register with engine: sets state→INGAME, name, broadcasts to all clients
	pGS->Server()->DummyJoin(CID, pDef->m_aName, pGS->GetWorldID());

	CPlayer *pPlayer = pGS->m_apPlayers[CID];
	if(!pPlayer) { dbg_msg("mmo_bot", "SpawnMob: pPlayer null after DummyJoin"); return -1; }

	// Allocate and attach bot data
	SMMOBotData *pData = new SMMOBotData();
	if(!pData) { dbg_msg("mmo_bot", "SpawnMob: pData null"); return -1; }

	pData->m_DefID = DefID;
	pData->m_MaxHP = pDef->m_MaxHP;
	pData->m_HP = pDef->m_MaxHP;
	pData->m_Attack = pDef->m_Attack;
	pData->m_Defense = pDef->m_Defense;
	pData->m_Level = pDef->m_Level;
	pData->m_ExpReward = pDef->m_ExpReward;
	pData->m_GoldMin = pDef->m_GoldMin;
	pData->m_GoldMax = pDef->m_GoldMax;
	pData->m_RespawnTicks = pDef->m_RespawnTicks;
	pData->m_SpawnPos = Pos;
	pData->m_IsBoss = pDef->m_IsBoss;
	pData->m_Respawning = false;
	pData->m_vDrops = pDef->m_vDrops;

	pPlayer->m_pMMOBotData = pData;

	// Set up player
	pPlayer->SetTeam(TEAM_BLUE);
	pGS->Server()->SetClientName(CID, pDef->m_aName);
	pGS->Server()->SetClientClan(CID, "");

	// Apply skin from mob definition
	if(pDef->m_aSkinName[0])
	{
		str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[0], pDef->m_aSkinName, sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[0]));
		pPlayer->m_TeeInfos.m_aUseCustomColors[0] = (pDef->m_SkinColorBody != 0 || pDef->m_SkinColorFeet != 0) ? 1 : 0;
		pPlayer->m_TeeInfos.m_aSkinPartColors[0] = pDef->m_SkinColorBody;
		pPlayer->m_TeeInfos.m_aSkinPartColors[2] = pDef->m_SkinColorFeet;
	}

	// Re-broadcast client info with updated skin
	pGS->BroadcastClientInfo(CID, false);

	// Spawn character (use CCharacterBotAI for MRPG-style AI)
	CCharacterBotAI *pChr = new(CID) CCharacterBotAI(&pGS->m_World);
	if(!pChr || !pChr->Spawn(pPlayer, Pos))
	{
		dbg_msg("mmo_bot", "SpawnMob: character spawn failed");
		if(pChr) delete pChr;
		pPlayer->m_pMMOBotData = nullptr;
		delete pData;
		return -1;
	}

	// Set the bot character's world ID from its player's world
	pChr->GetCore()->m_WorldID = pPlayer->GetCurrentWorldID();

	dbg_msg("mmo_bot", "Spawned '%s' Lv.%d at (%.0f,%.0f) on slot %d",
		pDef->m_aName, pDef->m_Level, Pos.x, Pos.y, CID);
	return CID;
}

int CMMOManager::SpawnQuestMob(int DefID, vec2 Pos, int QuestID, int StepPos, int TargetClientID)
{
	const SMMOMobDef *pDef = SMMOMobDef::Get(DefID);
	if(!pDef)
	{
		dbg_msg("mmo_quest", "Unknown mob def %d for quest mob", DefID);
		return -1;
	}

	CGameContext *pGS = GS();
	if(!pGS) return -1;

	// Find a free bot slot
	int CID = -1;
	const int BotSlotStart = MAX_HUMAN_CLIENTS;
	for(int i = BotSlotStart; i < MAX_CLIENTS; i++)
	{
		if(!pGS->m_apPlayers[i] || !pGS->m_apPlayers[i]->m_pMMOBotData)
		{
			if(CCharacterBotAI::PoolSlotFree(i))
			{
				CID = i;
				break;
			}
		}
	}
	if(CID < 0)
	{
		dbg_msg("mmo_quest", "SpawnQuestMob: No free bot slots!");
		return -1;
	}

	// Clean up existing state
	if(pGS->m_apPlayers[CID])
	{
		delete pGS->m_apPlayers[CID]->m_pMMOBotData;
		pGS->m_apPlayers[CID]->m_pMMOBotData = 0;
		delete pGS->m_apPlayers[CID]->m_pQuestMobInfo;
		pGS->m_apPlayers[CID]->m_pQuestMobInfo = 0;

		pGS->Server()->DummyRemove(CID);
		delete pGS->m_apPlayers[CID];
		pGS->m_apPlayers[CID] = 0;
	}

	pGS->Server()->DummyJoin(CID, pDef->m_aName, pGS->GetWorldID());

	CPlayer *pPlayer = pGS->m_apPlayers[CID];
	if(!pPlayer) return -1;

	// Allocate bot data
	SMMOBotData *pData = new SMMOBotData();
	pData->m_DefID = DefID;
	pData->m_MaxHP = pDef->m_MaxHP;
	pData->m_HP = pDef->m_MaxHP;
	pData->m_Attack = pDef->m_Attack;
	pData->m_Defense = pDef->m_Defense;
	pData->m_Level = pDef->m_Level;
	pData->m_ExpReward = pDef->m_ExpReward;
	pData->m_GoldMin = pDef->m_GoldMin;
	pData->m_GoldMax = pDef->m_GoldMax;
	pData->m_SpawnPos = Pos;
	pData->m_IsQuestMob = true;
	pData->m_Respawning = false;
	pData->m_vDrops = pDef->m_vDrops;

	pPlayer->m_pMMOBotData = pData;

	// Allocate quest mob info
	SMMOQuestMobInfo *pQuestInfo = new SMMOQuestMobInfo();
	pQuestInfo->m_QuestItemID = QuestID;
	if(TargetClientID >= 0 && TargetClientID < MAX_CLIENTS)
		pQuestInfo->ActivateForClient(TargetClientID, true);
	pPlayer->m_pQuestMobInfo = pQuestInfo;

	// Set up player
	pPlayer->SetTeam(TEAM_BLUE);
	pGS->Server()->SetClientName(CID, pDef->m_aName);
	pGS->Server()->SetClientClan(CID, "");

	if(pDef->m_aSkinName[0])
	{
		str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[0], pDef->m_aSkinName,
			sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[0]));
		pPlayer->m_TeeInfos.m_aUseCustomColors[0] = (pDef->m_SkinColorBody != 0 || pDef->m_SkinColorFeet != 0) ? 1 : 0;
		pPlayer->m_TeeInfos.m_aSkinPartColors[0] = pDef->m_SkinColorBody;
		pPlayer->m_TeeInfos.m_aSkinPartColors[2] = pDef->m_SkinColorFeet;
	}

	pGS->BroadcastClientInfo(CID, false);

	// Spawn character
	CCharacterBotAI *pChr = new(CID) CCharacterBotAI(&pGS->m_World);
	if(!pChr || !pChr->Spawn(pPlayer, Pos))
	{
		dbg_msg("mmo_quest", "SpawnQuestMob: character spawn failed");
		if(pChr) delete pChr;
		pPlayer->m_pMMOBotData = nullptr;
		pPlayer->m_pQuestMobInfo = nullptr;
		delete pData;
		delete pQuestInfo;
		return -1;
	}

	pChr->GiveWeapon(0, -1);
	pChr->GiveWeapon(1, 100);
	pChr->GetCore()->m_WorldID = pPlayer->GetCurrentWorldID();

	dbg_msg("mmo_quest", "Spawned quest mob '%s' for quest %d step %d client %d on CID %d",
		pDef->m_aName, QuestID, StepPos, TargetClientID, CID);
	return CID;
}

void CMMOManager::DespawnQuestMobs(int QuestID, int ClientID)
{
	CGameContext *pGS = GS();
	if(!pGS) return;

	for(int i = MAX_HUMAN_CLIENTS; i < MAX_CLIENTS; i++)
	{
		CPlayer *p = pGS->m_apPlayers[i];
		if(!p || !p->m_pMMOBotData || !p->m_pMMOBotData->m_IsQuestMob)
			continue;
		if(!p->m_pQuestMobInfo)
			continue;

		// If QuestID matches, or if this quest mob is only for this specific client
		if(p->m_pQuestMobInfo->m_QuestItemID == QuestID)
		{
			// Deactivate for this client
			p->m_pQuestMobInfo->ActivateForClient(ClientID, false);

			// If no clients need this mob anymore, remove it
			if(!p->m_pQuestMobInfo->IsActiveForAny())
			{
				if(p->GetCharacter())
					p->GetCharacter()->Die(ClientID, WEAPON_WORLD);

				delete p->m_pMMOBotData;
				p->m_pMMOBotData = nullptr;
				delete p->m_pQuestMobInfo;
				p->m_pQuestMobInfo = nullptr;

				pGS->Server()->DummyRemove(i);
				delete p;
				pGS->m_apPlayers[i] = nullptr;
			}
		}
	}
}

void CMMOManager::TickMMOBot(CPlayer *pPlayer)
{
	SMMOBotData *pData = pPlayer->m_pMMOBotData;
	if(!pData) return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	CCharacter *pChr = pPlayer->GetCharacter();

	// If this mob uses CCharacterBotAI, TickWorldSpawns/Tick handle all respawn.
	// DO NOT fall through to the legacy respawn path — it would try to create a
	// second CCharacterBotAI on a pool slot still held by the dead entity (crash!).
	if(pChr && dynamic_cast<CCharacterBotAI*>(pChr))
	{
		// CCharacterBotAI::Tick handles HP bar & respawn
		return;
	}
	if(!pChr && pData->m_DefID >= 0)
	{
		// CCharacterBotAI is dead; respawn managed by TickWorldSpawns.
		// Skip legacy path entirely.
		return;
	}

	// ── Legacy path (no CCharacterBotAI) —
	// This only runs for bots not spawned via TickWorldSpawns (shouldn't happen normally). ──
	if(pData->m_Respawning)
	{
		if(pData->m_RespawnAtTick > 0 && pGS->Server()->Tick() >= pData->m_RespawnAtTick)
		{
			pData->m_HP = pData->m_MaxHP;
			pData->m_Respawning = false;
			pData->m_DeathTick = 0;

			if(!pChr)
				pChr = new(pPlayer->GetCID()) CCharacterBotAI(&pGS->m_World);
			if(pChr)
				pChr->Spawn(pPlayer, pData->m_SpawnPos);
		}
		return;
	}

	if(!pChr || !pChr->IsAlive()) return;

	// Update HP display
	{
		char aClan[64];
		int pct = (int)(pData->GetHPPct() * 100.0f);
		str_format(aClan, sizeof(aClan), "HP: %d/%d [%d%%]", pData->m_HP, pData->m_MaxHP, pct);
		pGS->Server()->SetClientClan(pPlayer->GetCID(), aClan);
	}
}

void CMMOManager::ConMMOSpawn(const char *pMobName, CPlayer *pPlayer)
{
	if(!pPlayer || !pPlayer->GetCharacter()) return;

	int MobID = atoi(pMobName);
	if(MobID <= 0)
	{
		for(const auto &Def : CDataCenter::GetMobDefs())
		{
			if(str_comp_nocase(Def.m_aName, pMobName) == 0)
			{
				MobID = Def.m_ID;
				break;
			}
		}
		if(MobID <= 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Unknown mob: %s", pMobName);
			GS()->SendChat(pPlayer->GetCID(), CHAT_ALL, -1, aBuf);
			return;
		}
	}

	vec2 Pos = pPlayer->GetCharacter()->GetPos() + vec2(100, 0);
	int CID = SpawnMob(MobID, Pos);
	if(CID >= 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Spawned mob '%s' (ClientID=%d)", GS()->Server()->ClientName(CID), CID);
		GS()->SendChat(pPlayer->GetCID(), CHAT_ALL, -1, aBuf);
	}
	else
	{
		GS()->SendChat(pPlayer->GetCID(), CHAT_ALL, -1, "Failed to spawn mob (no free slots?)");
	}
}

void CMMOManager::LoadZoneDefs()
{
	// Data is loaded once by CDataCenter::Init().
	// Just repopulate m_WorldZones from CDataCenter.
	m_WorldZones.clear();
	const auto &ZoneDefs = CDataCenter::GetZoneDefs();
	for(const auto &Zone : ZoneDefs)
	{
		m_WorldZones[Zone.m_WorldID].push_back(Zone);
	}
	dbg_msg("mmo_zone", "Populated per-world zones from CDataCenter (%zu entries)", ZoneDefs.size());
}

void CMMOManager::SpawnZoneMobs(int WorldID)
{
	auto it = m_WorldZones.find(WorldID);
	if(it == m_WorldZones.end())
	{
		dbg_msg("mmo_zone", "No zones found for world %d", WorldID);
		return;
	}

	const std::vector<SMMOZoneDef> &Zones = it->second;
	int NumRegistered = 0;

	for(const auto &Zone : Zones)
	{
		for(const auto &MobDef : Zone.m_vMobs)
		{
			const SMMOMobDef *pDef = SMMOMobDef::Get(MobDef.m_DefID);
			if(!pDef)
			{
				dbg_msg("mmo_zone", "Zone '%s': unknown mob def %d", Zone.m_aName, MobDef.m_DefID);
				continue;
			}

			for(int i = 0; i < MobDef.m_Count; i++)
			{
				// Calculate random position within zone bounds
				int ZoneW = maximum(1, Zone.m_X2 - Zone.m_X1);
				int ZoneH = maximum(1, Zone.m_Y2 - Zone.m_Y1);
				vec2 SpawnPos = vec2(
					(float)(Zone.m_X1 + (random_int() % ZoneW)),
					(float)(Zone.m_Y1 + (random_int() % ZoneH))
				);

				// Register as a world spawn
				RegisterWorldSpawn(WorldID, MobDef.m_DefID, SpawnPos);

				// Set zone name on the entry
				if(!m_WorldSpawns[WorldID].empty())
				{
					str_copy(m_WorldSpawns[WorldID].back().m_ZoneName,
						Zone.m_aName,
						sizeof(m_WorldSpawns[WorldID].back().m_ZoneName));
				}

				NumRegistered++;
			}
		}
	}

	dbg_msg("mmo_zone", "Registered %d zone mob spawns for world %d", NumRegistered, WorldID);
}

void CMMOManager::RegisterWorldSpawn(int WorldID, int DefID, vec2 Pos, int RespawnDelay)
{
	SWorldSpawnEntry Entry;
	Entry.m_DefID = DefID;
	Entry.m_Pos = Pos;
	Entry.m_RespawnDelay = RespawnDelay > 0 ? RespawnDelay : Server()->TickSpeed() * 30; // default 30s
	Entry.m_RespawnTick = 0;
	Entry.m_Active = true;
	Entry.m_SpawnedCID = -1;
	m_WorldSpawns[WorldID].push_back(Entry);
}

void CMMOManager::TickWorldSpawns()
{
	const int WorldID = GS()->GetWorldID();
	auto it = m_WorldSpawns.find(WorldID);
	if(it == m_WorldSpawns.end())
		return;

	std::vector<SWorldSpawnEntry> &Spawns = it->second;

	// Compute dynamic difficulty based on players in this world
	int NumPlayersInWorld = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *p = GS()->m_apPlayers[i];
		if(p && !p->IsDummy() && p->GetTeam() != TEAM_SPECTATORS && p->GetCurrentWorldID() == WorldID)
			NumPlayersInWorld++;
	}
	float DifficultyMod = 1.0f + (NumPlayersInWorld - 1) * 0.25f;
	if(DifficultyMod < 1.0f) DifficultyMod = 1.0f;
	if(DifficultyMod > 3.0f) DifficultyMod = 3.0f;

	for(auto &Spawn : Spawns)
	{
		if(!Spawn.m_Active)
			continue;

		if(Spawn.m_SpawnedCID >= 0)
		{
			// Check if the spawned mob is still alive
			CPlayer *pPlayer = GS()->m_apPlayers[Spawn.m_SpawnedCID];
			if(pPlayer && pPlayer->m_pMMOBotData && pPlayer->m_pMMOBotData->IsAlive())
				continue; // still alive, skip

			// Mob is dead — clean up player + entity so the slot + pool can be reused
			{
				int OldCID = Spawn.m_SpawnedCID;
				CPlayer *pOld = GS()->m_apPlayers[OldCID];
				if(pOld)
				{
					// Must free m_pMMOBotData BEFORE DummyRemove:
					//   DummyRemove → OnClientDrop → RemovePlayerFromWorld → delete pPlayer
					// After DummyRemove, pOld is freed memory — accessing it is use-after-free!
					// SpawnMob() uses the same cleanup order pattern.
					delete pOld->m_pMMOBotData;
					pOld->m_pMMOBotData = 0;

					// DummyRemove takes the normal disconnect path:
					//   OnClientDrop → RemovePlayerFromWorld → delete m_pCharacter (frees pool slot)
					//   → delete pPlayer → m_apPlayers[OldCID] = nullptr
					GS()->Server()->DummyRemove(OldCID);
				}
				Spawn.m_SpawnedCID = -1;
				Spawn.m_RespawnTick = Server()->Tick() + Spawn.m_RespawnDelay;
			}
		}
		else
		{
			// Check if respawn timer has elapsed
			if(Server()->Tick() >= Spawn.m_RespawnTick)
			{
				// Validate spawn position: don't spawn too close to players
				bool bTooClose = false;
				if(Spawn.m_MinDistFromPlayer > 0)
				{
					for(int i = 0; i < MAX_CLIENTS && !bTooClose; i++)
					{
						CPlayer *p = GS()->m_apPlayers[i];
						if(!p || p->IsDummy() || p->GetTeam() == TEAM_SPECTATORS || p->GetCurrentWorldID() != WorldID)
							continue;
						CCharacter *pChr = p->GetCharacter();
						if(!pChr)
							continue;
						float Dist = distance(Spawn.m_Pos, pChr->GetPos());
						if(Dist < Spawn.m_MinDistFromPlayer)
							bTooClose = true;
					}
				}

				if(bTooClose)
				{
					// Player nearby, retry in 3 seconds
					Spawn.m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 3;
					continue;
				}

				int CID = SpawnMob(Spawn.m_DefID, Spawn.m_Pos);
				if(CID >= 0)
				{
					Spawn.m_SpawnedCID = CID;

					// Apply dynamic difficulty scaling to the spawned mob
					if(CPlayer *pMob = GS()->m_apPlayers[CID])
					{
						if(pMob->m_pMMOBotData)
						{
							int ScaledHP = (int)(pMob->m_pMMOBotData->m_MaxHP * DifficultyMod);
							if(ScaledHP < 1) ScaledHP = 1;
							pMob->m_pMMOBotData->m_MaxHP = ScaledHP;
							pMob->m_pMMOBotData->m_HP = ScaledHP;
						}

						// Pass zone info to the spawned mob's character
						if(CCharacterBotAI *pBotChr = dynamic_cast<CCharacterBotAI*>(pMob->GetCharacter()))
						{
							if(Spawn.m_ZoneName[0])
							{
								str_copy(pBotChr->m_ZoneName, Spawn.m_ZoneName, sizeof(pBotChr->m_ZoneName));

								// Also set zone on the mob AI for patrol behavior
								if(CMobAI *pMobAI = dynamic_cast<CMobAI*>(pBotChr->AI()))
								{
									const SMMOZoneDef *pZone = SMMOZoneDef::Find(WorldID, Spawn.m_ZoneName);
									if(pZone)
									{
										pMobAI->SetZone(pZone->m_aName,
											vec2((float)pZone->m_X1, (float)pZone->m_Y1),
											vec2((float)pZone->m_X2, (float)pZone->m_Y2));
									}
								}
							}
						}
					}

					dbg_msg("mmo_mob", "World spawn: mob def=%d at (%.0f,%.0f) CID=%d diff=%.2f%s%s",
						Spawn.m_DefID, Spawn.m_Pos.x, Spawn.m_Pos.y, CID, DifficultyMod,
						Spawn.m_ZoneName[0] ? " zone=" : "",
						Spawn.m_ZoneName[0] ? Spawn.m_ZoneName : "");
				}
				else
				{
					// Failed to spawn (no free slots), retry in 5s
					Spawn.m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 5;
				}
			}
		}
	}
}

void CMMOManager::LoadWorldSpawns(int WorldID)
{
	// Clear existing spawn entries for this world
	m_WorldSpawns[WorldID].clear();

	// Try loading from JSON config first
	if(Storage() && LoadWorldSpawnsFromConfig(WorldID))
	{
		dbg_msg("mmo_mob", "Loaded %d world spawns for WorldID=%d from config",
			(int)m_WorldSpawns[WorldID].size(), WorldID);
		return;
	}

	// Fallback: define hardcoded spawn positions per world
	int NumSpawns = 0;
	if(WorldID == 0)
	{
		// Starting world: basic slimes
		const vec2 aSpawnPoints[] = {
			vec2(400.f, 400.f),
			vec2(800.f, 400.f),
			vec2(600.f, 800.f),
			vec2(400.f, 1200.f),
			vec2(800.f, 1200.f),
		};
		for(const auto &Pos : aSpawnPoints)
		{
			RegisterWorldSpawn(WorldID, 1, Pos); // DefID 1 = 史莱姆
			NumSpawns++;
		}
	}
	else
	{
		// Generic world fallback
		const vec2 aSpawnPoints[] = {
			vec2(600.f, 600.f), vec2(800.f, 600.f),
			vec2(600.f, 800.f), vec2(800.f, 800.f),
		};
		for(const auto &Pos : aSpawnPoints)
		{
			RegisterWorldSpawn(WorldID, 1, Pos);
			NumSpawns++;
		}
	}

	dbg_msg("mmo_mob", "Loaded %d world spawns for WorldID=%d (hardcoded)",
		NumSpawns, WorldID);
}

bool CMMOManager::LoadWorldSpawnsFromConfig(int WorldID)
{
	if(!Storage())
		return false;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/mmo/world_spawns.json", Storage());
	if(!pRoot)
		return false;

	const json_value &Worlds = (*pRoot)["worlds"];
	if(Worlds.type != json_array)
		return false;

	bool bFound = false;
	for(unsigned i = 0; i < Worlds.u.array.length; i++)
	{
		const json_value &W = Worlds[(int)i];
		if(W.type != json_object) continue;
		if(W["id"].type != json_integer) continue;
		if((int)W["id"].u.integer != WorldID) continue;

		const json_value &Spawns = W["spawns"];
		if(Spawns.type != json_array)
			return false;

		for(unsigned s = 0; s < Spawns.u.array.length; s++)
		{
			const json_value &S = Spawns[(int)s];
			if(S.type != json_object) continue;

			int DefID = 1;
			vec2 Pos(0, 0);
			int RespawnDelay = Server()->TickSpeed() * 30;
			int MinDist = 200;

			if(S["def_id"].type == json_integer) DefID = (int)S["def_id"].u.integer;
			if(S["x"].type == json_integer) Pos.x = (float)(int)S["x"].u.integer;
			if(S["y"].type == json_integer) Pos.y = (float)(int)S["y"].u.integer;
			if(S["respawn_delay"].type == json_integer) RespawnDelay = (int)S["respawn_delay"].u.integer;
			if(S["min_dist"].type == json_integer) MinDist = (int)S["min_dist"].u.integer;

			if(DefID >= 0 && (Pos.x != 0 || Pos.y != 0))
			{
				RegisterWorldSpawn(WorldID, DefID, Pos, RespawnDelay);
				if(!m_WorldSpawns[WorldID].empty())
					m_WorldSpawns[WorldID].back().m_MinDistFromPlayer = MinDist;
				bFound = true;
			}
		}
		break;
	}

	return bFound;
}

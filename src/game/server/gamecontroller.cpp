/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/version.h>
#include <generated/server_data.h>

#include <base/math.h>

#include "entities/character.h"
#include "entities/electro.h"
#include "entities/laser.h"
#include "entities/lightning.h"
#include "entities/pickup.h"
#include "entities/projectile.h"
#include "account.h"
#include "core/components/accounts/account_manager.h"
#include "core/components/bots/defence_bot_manager.h"
#include "core/components/craft/craft_manager.h"
#include "core/components/vote/vote_menu_manager.h"
#include "core/tworld_controller.h"
#include "entities/tower-main.h"
#include "entities/CKs.h"
#include "item_system.h"
#include "gamecontext.h"
#include "botengine.h"
#include "gamecontroller.h"
#include "player.h"
#include "zombie_bot.h"

static int TdZombieFirstSlot(const CConfig *pCfg)
{
	return maximum((int)MAX_HUMAN_CLIENTS, pCfg->m_SvMaxClients);
}

static bool IsZombiePlayer(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->IsDummy() && pPlayer->GetZomb() != ZOMB_NONE;
}

CGameController::CGameController(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();

	m_GameStartTick = Server()->Tick();

	m_TdWarmup = 0;
	m_TdGameOverTick = -1;
	m_TdZombStart = 0;
	m_TdWave = 0;
	mem_zero(m_TdZombie, sizeof(m_TdZombie));
	m_TdZombLeft = 0;
	m_pTower = nullptr;
	mem_zero(m_apZombieBots, sizeof(m_apZombieBots));
	m_TdDummyRemoveLen = 0;
	mem_zero(m_aTdDummyRemove, sizeof(m_aTdDummyRemove));
	m_TdPendingZomb = ZOMB_ZABY;
	m_RealPlayerNum = 0;
}

CGameController::~CGameController()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		delete m_apZombieBots[i];
		m_apZombieBots[i] = nullptr;
	}
}

void CGameController::PreTick()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		if(pP->GetZomb() != ZOMB_NONE)
			TdRunZombieBrain(pP);
		else if(TWorldController *pCore = GameServer()->Core())
			pCore->DefenceBotManager()->TickPlayer(pP);
	}
}

vec2 CGameController::TdGetZombieMarchGoal() const
{
	vec2 Goal = m_pTower ? m_pTower->GetPos() : TdGetZombieRallyPos();
	CBotEngine *pBE = GameServer()->BotEngine();
	if(pBE)
	{
		CGraph *pGraph = pBE->GetGraph();
		if(pGraph && pGraph->m_NumVertices > 0 && pGraph->m_pVertices)
		{
			const int V = pBE->GetClosestVertex(Goal);
			if(V >= 0 && V < pGraph->m_NumVertices)
				Goal = pGraph->m_pVertices[V].m_Pos;
		}
	}
	return Goal;
}

vec2 CGameController::TdGetZombieRallyPos() const
{
	for(int Slot = 1; Slot >= 0; Slot--)
	{
		if(m_alSpawnPoints[Slot].size() == 0)
			continue;
		vec2 Sum(0.0f, 0.0f);
		for(int i = 0; i < m_alSpawnPoints[Slot].size(); i++)
			Sum += m_alSpawnPoints[Slot][i];
		return Sum / (float)m_alSpawnPoints[Slot].size();
	}
	return vec2(0.0f, 0.0f);
}

int CGameController::GetDummyTeam() const
{
	return TEAM_BLUE;
}

void CGameController::OnBotPlayerCreated(CPlayer *pPlayer)
{
	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		pPlayer->m_TeeInfos.m_aUseCustomColors[p] = 0;
		pPlayer->m_TeeInfos.m_aSkinPartColors[p] = 0xFF000000;
	}
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[0], "standard", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[2], "uniban", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[3], "standard", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[4], "standard", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[5], "standard", MAX_SKIN_ARRAY_SIZE);

	const int CID = pPlayer->GetCID();
	delete m_apZombieBots[CID];
	m_apZombieBots[CID] = nullptr;

	pPlayer->InitZombie(m_TdPendingZomb);
	const char *pBodySkin = "saddo";
	switch(m_TdPendingZomb)
	{
	case ZOMB_ZOOMER:
		pBodySkin = "redstripe";
		break;
	case ZOMB_ZOOKER:
		pBodySkin = "bluekitty";
		break;
	case ZOMB_ZAMER:
		pBodySkin = "twinbop";
		break;
	case ZOMB_ZUNNER:
		pBodySkin = "cammostripes";
		break;
	case ZOMB_ZASTER:
		pBodySkin = "coala";
		break;
	case ZOMB_ZOTTER:
		pBodySkin = "cammo";
		break;
	case ZOMB_ZENADE:
		pBodySkin = "twintri";
		break;
	case ZOMB_FLOMBIE:
		pBodySkin = "toptri";
		break;
	case ZOMB_ZINJA:
		pBodySkin = "default";
		break;
	case ZOMB_ZELE:
		pBodySkin = "redbopp";
		break;
	case ZOMB_ZINVIS:
		pBodySkin = "pinky";
		break;
	case ZOMB_ZEATER:
		pBodySkin = "warpaint";
		break;
	case ZOMB_ZABY:
	default:
		pBodySkin = "saddo";
		break;
	}
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[1], pBodySkin, MAX_SKIN_ARRAY_SIZE);

	if(pPlayer->GetZomb() != ZOMB_NONE && GameServer()->BotEngine())
		m_apZombieBots[CID] = new CZombieBot(GameServer()->BotEngine(), pPlayer, this);
}

void CGameController::TdRunZombieBrain(CPlayer *pP)
{
	const int CID = pP->GetCID();
	if(!m_apZombieBots[CID] && GameServer()->BotEngine())
		m_apZombieBots[CID] = new CZombieBot(GameServer()->BotEngine(), pP, this);
	CZombieBot *pBot = m_apZombieBots[CID];
	if(!pBot)
		return;

	pBot->Tick();
	CNetObj_PlayerInput Inp = pBot->Input();
	pP->OnPredictedInput(&Inp);
	pP->OnDirectInput(&Inp);
}

// activity
void CGameController::DoActivityCheck()
{
	if(Config()->m_SvInactiveKickTime == 0)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && !GameServer()->m_apPlayers[i]->IsDummy() && (GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS || Config()->m_SvInactiveKick > 0) &&
			!Server()->IsAuthed(i) && (GameServer()->m_apPlayers[i]->m_InactivityTickCounter > Config()->m_SvInactiveKickTime * Server()->TickSpeed() * 60))
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
			{
				if(Config()->m_SvInactiveKickSpec)
				{
					char aReason[128];
					str_copy(aReason, GameServer()->Loc(i, "kick.inactivity", "Kicked for inactivity"), sizeof(aReason));
					Server()->Kick(i, aReason);
				}
			}
			else
			{
				switch(Config()->m_SvInactiveKick)
				{
					case 1:
					{
						// move player to spectator
						DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
					}
					break;
					case 2:
					{
						// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
						int Spectators = 0;
						for(int j = 0; j < MAX_CLIENTS; ++j)
							if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
								++Spectators;
						if(Spectators >= Config()->m_SvMaxClients - GameServer()->GetMaxPlayerSlots())
						{
							char aReason[128];
							str_copy(aReason, GameServer()->Loc(i, "kick.inactivity", "Kicked for inactivity"), sizeof(aReason));
							Server()->Kick(i, aReason);
						}
						else
							DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
					}
					break;
					case 3:
					{
						// kick the player
						{
							char aReason[128];
							str_copy(aReason, GameServer()->Loc(i, "kick.inactivity", "Kicked for inactivity"), sizeof(aReason));
							Server()->Kick(i, aReason);
						}
					}
				}
			}
		}
	}
}

bool CGameController::GetPlayersReadyState(int WithoutID)
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == WithoutID)
			continue; // skip
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !GameServer()->m_apPlayers[i]->m_IsReadyToPlay)
			return false;
	}

	return true;
}

void CGameController::SetPlayersReadyState(bool ReadyState)
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && (ReadyState || !GameServer()->m_apPlayers[i]->m_DeadSpecMode))
			GameServer()->m_apPlayers[i]->m_IsReadyToPlay = ReadyState;
	}
}

bool CGameController::CanCharacterPickup(CCharacter *pChr) const
{
	if(!pChr || !pChr->GetPlayer())
		return false;
	if(IsZombiePlayer(pChr->GetPlayer()))
		return false;
	return true;
}

void CGameController::TdClearZombieBot(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	delete m_apZombieBots[ClientID];
	m_apZombieBots[ClientID] = nullptr;
}

// event
int CGameController::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	CPlayer *pVictimPlayer = pVictim->GetPlayer();
	if(IsZombiePlayer(pVictimPlayer))
	{
		if(pVictimPlayer->IsEliminated())
			return 0;

		if(pKiller && !pKiller->IsDummy() && Weapon != WEAPON_GAME)
		{
			int Reward = ITEM_LOG;
			const int Rando = rand() % 100 + 1;
			if(Rando <= 50)
				Reward = ITEM_LOG;
			else if(Rando <= 75)
				Reward = ITEM_COPPER;
			else
				Reward = ITEM_GOLD;

			pKiller->m_AccData.m_aItems[Reward].m_Num++;
			pKiller->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num++;
			pKiller->m_Score++;

			GameServer()->SendChatLocF(pKiller->GetCID(), "game.loot_drop", u8"掉落：%s（僵尸心+1）", GameServer()->LocItemName(pKiller->GetCID(), Reward));

			if(GameServer()->Accounts()->IsEnabled() && pKiller->GetAccountId() >= 0)
				GameServer()->Accounts()->RequestSaveItems(pKiller->GetCID());
		}

		pVictimPlayer->ForbidRespawn();
		TdClearZombieBot(pVictimPlayer->GetCID());

		if(m_TdZombLeft > 0)
			m_TdZombLeft--;
		TdDoZombMessage(m_TdZombLeft);

		if(m_TdDummyRemoveLen < TD_REMOVE_QUEUE)
			m_aTdDummyRemove[m_TdDummyRemoveLen++] = pVictimPlayer->GetCID();

		return 0;
	}

	// update spectator modes for dead players in survival
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_DeadSpecMode)
			GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
	if(pKiller == pVictim->GetPlayer())
		pVictim->GetPlayer()->m_Score--; // suicide or world
	else
		pKiller->m_Score++; // normal kill
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 3.0f;

	return 0;
}

void CGameController::OnCharacterSpawn(CCharacter *pChr)
{
	// vanilla baseline
	pChr->IncreaseHealth(10);
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_GUN, 10);

	if(pChr->GetPlayer()->IsDummy())
	{
		const int Z = pChr->GetPlayer()->GetZomb();
		int Health = maximum(1, m_TdWave);
		if(Z == ZOMB_ZASTER)
			Health = maximum(40, m_TdWave * 10);

		pChr->SetHealthDirect(Health);
		pChr->GiveWeapon(WEAPON_HAMMER, -1);
		pChr->GiveWeapon(WEAPON_GUN, -1);
		pChr->GiveWeapon(WEAPON_SHOTGUN, -1);
		pChr->GiveWeapon(WEAPON_GRENADE, -1);
		pChr->GiveWeapon(WEAPON_LASER, -1);

		switch(Z)
		{
		case ZOMB_ZUNNER:
		case ZOMB_FLOMBIE:
			pChr->SetWeapon(WEAPON_GUN);
			break;
		case ZOMB_ZOOKER:
			pChr->SetWeapon(WEAPON_HAMMER);
			break;
		case ZOMB_ZOTTER:
			pChr->SetWeapon(WEAPON_SHOTGUN);
			break;
		case ZOMB_ZENADE:
			pChr->SetWeapon(WEAPON_GRENADE);
			break;
		case ZOMB_ZOOMER:
			pChr->SetWeapon(WEAPON_LASER);
			break;
		case ZOMB_ZAMER:
		case ZOMB_ZABY:
		case ZOMB_ZASTER:
		case ZOMB_ZINJA:
		case ZOMB_ZELE:
		case ZOMB_ZINVIS:
		case ZOMB_ZEATER:
		default:
			pChr->SetWeapon(WEAPON_HAMMER);
			break;
		}
	}
	else
		pChr->SetHealthDirect(Config()->m_SvPlayerMaxHealth);

	if(IsZombiePlayer(pChr->GetPlayer()))
	{
		const int Idx = pChr->GetPlayer()->GetZomb() - ZOMB_ZABY;
		if(Idx >= 0 && Idx < NUM_TD_ZOMB && m_TdZombie[Idx] > 0)
			m_TdZombie[Idx]--;
	}
}

bool CGameController::OnEntity(int Index, vec2 Pos)
{
	if(Index == ENTITY_MAIN_TOWER)
	{
		m_pTower = new CTowerMain(&GameServer()->m_World, Pos);
		return true;
	}

	int Type = -1;
	int CkItem = -1;

	switch(Index)
	{
	case ENTITY_LOG:
		CkItem = ITEM_LOG;
		break;
	case ENTITY_COAL:
		CkItem = ITEM_COAL;
		break;
	case ENTITY_COPPER:
		CkItem = ITEM_COPPER;
		break;
	case ENTITY_IRON:
		CkItem = ITEM_IRON;
		break;
	case ENTITY_GOLD:
		CkItem = ITEM_GOLD;
		break;
	case ENTITY_DIAMOND:
		CkItem = ITEM_DIAMOND;
		break;
	case ENTITY_ENERGY:
		CkItem = ITEM_ENEGRY;
		break;
	default:
		break;
	}

	if(CkItem != -1)
	{
		new CKs(&GameServer()->m_World, CkItem, Pos);
		return true;
	}

	switch(Index)
	{
		case ENTITY_SPAWN:
			m_alSpawnPoints[0].add(Pos);
			break;
		case ENTITY_SPAWN_RED:
			m_alSpawnPoints[1].add(Pos);
			break;
		case ENTITY_SPAWN_BLUE:
			m_alSpawnPoints[2].add(Pos);
			break;
		case ENTITY_ARMOR_1:
			Type = PICKUP_ARMOR;
			break;
		case ENTITY_HEALTH_1:
			Type = PICKUP_HEALTH;
			break;
		case ENTITY_WEAPON_SHOTGUN:
			Type = PICKUP_SHOTGUN;
			break;
		case ENTITY_WEAPON_GRENADE:
			Type = PICKUP_GRENADE;
			break;
		case ENTITY_WEAPON_LASER:
			Type = PICKUP_LASER;
			break;
		case ENTITY_POWERUP_NINJA:
			Type = PICKUP_NINJA;
	}

	if(Type != -1)
	{
		new CPickup(&GameServer()->m_World, Type, Pos);
		return true;
	}

	return false;
}

bool CGameController::OnExtraTile(int Index, vec2 Pos)
{
	/*
		Example: hook game-layer tile indices between TILE_UNHOOKABLE and ENTITY_OFFSET.

		int Flag = -1;
		switch(Index)
		{
		case TILE_START: Flag = COLFLAG_START; break;
		case TILE_FINISH: Flag = COLFLAG_FINISH; break;
		}
		if(Flag == -1)
			return false;
		GameServer()->Collision()->SetFlagFor(Pos, Flag);
		return true;
	*/

	(void)Index;
	(void)Pos;
	return false;
}

void CGameController::HandleCharacterTiles(class CCharacter *pChr, vec2 LastPos, vec2 NewPos)
{
	(void)pChr;
	(void)LastPos;
	(void)NewPos;
}

void CGameController::OnPlayerConnect(CPlayer *pPlayer)
{
	if(!pPlayer->IsDummy())
	{
		const int CID = pPlayer->GetCID();
		GameServer()->SendChatLoc(CID, "welcome", "Welcome to TeeDefense Archive");
		GameServer()->SendChatLoc(CID, "login.hint", "Use /register or /login");
		GameServer()->SendCommunityInfo(CID);
		GameServer()->SendChatAllLocF("game.join", "%s joined TeeDefense — defend the tower!", Server()->ClientName(CID));
	}

	int ClientID = pPlayer->GetCID();
	pPlayer->Respawn();

	if(!IsZombiePlayer(pPlayer))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	SendGameInfo(ClientID);
}

void CGameController::OnPlayerDisconnect(CPlayer *pPlayer)
{
	if(IsZombiePlayer(pPlayer))
		TdClearZombieBot(pPlayer->GetCID());

	pPlayer->OnDisconnect();

	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID) && !IsZombiePlayer(pPlayer))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}

	if(!pPlayer->IsDummy() && pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		--m_RealPlayerNum;
	}
}

void CGameController::OnPlayerInfoChange(CPlayer *pPlayer)
{
}

void CGameController::OnPlayerReadyChange(CPlayer *pPlayer)
{
	// change players ready state
	pPlayer->m_IsReadyToPlay ^= 1;
}

// general
void CGameController::Snap(int SnappingClient)
{
	CNetObj_GameData *pGameData = static_cast<CNetObj_GameData *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData)));
	if(!pGameData)
		return;

	pGameData->m_GameStartTick = m_GameStartTick;
	pGameData->m_GameStateFlags = 0;
	pGameData->m_GameStateEndTick = 0; // no timer/infinite = 0, on end = GameEndTick, otherwise = GameStateEndTick
	if(m_TdWarmup > 0)
	{
		pGameData->m_GameStateFlags |= GAMESTATEFLAG_WARMUP;
		pGameData->m_GameStateEndTick = Server()->Tick() + m_TdWarmup;
	}

	CNetObj_GameDataPrediction *pGameDataPrediction = static_cast<CNetObj_GameDataPrediction *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATAPREDICTION, 0, sizeof(CNetObj_GameDataPrediction)));
	if(!pGameDataPrediction)
		return;

	pGameDataPrediction->m_PredictionFlags = GAMEPREDICTIONFLAG_EVENT | GAMEPREDICTIONFLAG_INPUT;
	// demo recording
	if(SnappingClient == -1)
	{
		CNetObj_De_GameInfo *pGameInfo = static_cast<CNetObj_De_GameInfo *>(Server()->SnapNewItem(NETOBJTYPE_DE_GAMEINFO, 0, sizeof(CNetObj_De_GameInfo)));
		if(!pGameInfo)
			return;

		pGameInfo->m_GameFlags = 0;
		pGameInfo->m_TimeLimit = 0;
		if(m_pTower)
		{
			pGameInfo->m_ScoreLimit = m_pTower->GetHealth();
			pGameInfo->m_MatchNum = m_TdZombStart;
			pGameInfo->m_MatchCurrent = m_TdZombLeft;
		}
		else
		{
			pGameInfo->m_ScoreLimit = 0;
			pGameInfo->m_MatchNum = 0;
			pGameInfo->m_MatchCurrent = 1;
		}
	}
}

void CGameController::Tick()
{
	DoActivityCheck();

	for(int i = 0; i < m_TdDummyRemoveLen; i++)
		Server()->DummyRemove(m_aTdDummyRemove[i]);
	TdResetPendingRemoves();

	if(m_TdGameOverTick != -1)
	{
		if(Server()->Tick() > m_TdGameOverTick + Server()->TickSpeed() * 5)
			Server()->ChangeMap(Config()->m_SvMap);
		return;
	}

	int Players = 0;
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i])
			continue;
		if(GameServer()->m_apPlayers[i]->GetTeam() != TEAM_RED)
			continue;
		if(!GameServer()->GetPlayerChar(i))
			continue;
		Players++;
	}

	if(Players >= 1 && m_TdWave == 0)
		TdStartRound();

	if(m_TdWave == 0)
		return;

	if(m_TdWarmup > 0)
	{
		if(m_TdWarmup % Server()->TickSpeed() == 0)
		{
			const int SecLeft = (m_TdWarmup + Server()->TickSpeed() - 1) / Server()->TickSpeed();
			GameServer()->SendBroadcastLocF(-1, "game.wave_countdown", "Next wave %d in %d s", m_TdWave + 1, SecLeft);
			TdBroadcastGameInfo();
		}
		m_TdWarmup--;
		if(m_TdWarmup == 0)
			TdStartRound();
		return;
	}

	TdCheckZombie();
	TdDoWincheck();
}

bool CGameController::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	(void)Damage;
	if(ClientID1 == ClientID2)
		return false;

	CPlayer *p1 = GameServer()->m_apPlayers[ClientID1];
	CPlayer *p2 = GameServer()->m_apPlayers[ClientID2];
	if(!p1 || !p2)
		return false;

	return p1->GetTeam() == p2->GetTeam();
}

bool CGameController::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	return Team1 == Team2;
}

int CGameController::GetPlayerCheckTeam(CPlayer *pPlayer) const
{
	if(!pPlayer)
		return TEAM_RED;
	return pPlayer->GetTeam();
}

void CGameController::SendGameInfo(int ClientID)
{
	/* Bot slots have no net connection; SendPackMsg would assert in CNetServer::Send. */
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
	{
		CPlayer *pP = GameServer()->m_apPlayers[ClientID];
		if(pP && pP->IsDummy())
			return;
	}

	CNetMsg_Sv_GameInfo GameInfoMsg;
	GameInfoMsg.m_GameFlags = 0;
	GameInfoMsg.m_TimeLimit = 0;
	if(m_pTower)
	{
		GameInfoMsg.m_ScoreLimit = m_pTower->GetHealth();
		if(m_TdWarmup > 0)
		{
			GameInfoMsg.m_MatchNum = m_TdWave + 1;
			GameInfoMsg.m_MatchCurrent = (m_TdWarmup + Server()->TickSpeed() - 1) / Server()->TickSpeed();
		}
		else
		{
			GameInfoMsg.m_MatchNum = m_TdZombStart;
			GameInfoMsg.m_MatchCurrent = m_TdZombLeft;
		}
	}
	else
	{
		GameInfoMsg.m_ScoreLimit = 0;
		GameInfoMsg.m_MatchNum = 0;
		GameInfoMsg.m_MatchCurrent = 1;
	}
	Server()->SendPackMsg(&GameInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
}

void CGameController::TdBroadcastGameInfo()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i] || GameServer()->m_apPlayers[i]->IsDummy())
			continue;
		if(Server()->ClientIngame(i))
			SendGameInfo(i);
	}
}

// spawn
bool CGameController::CanSpawn(int Team, vec2 *pOutPos) const
{
	if(Team == TEAM_SPECTATORS)
		return false;

	CSpawnEval Eval;
	mem_zero(&Eval, sizeof(Eval));
	if(Team == TEAM_RED)
		EvaluateSpawnType(&Eval, 1);
	else
	{
		Eval.m_RandomSpawn = true;
		EvaluateSpawnType(&Eval, 0);
		EvaluateSpawnType(&Eval, 2);
	}
	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

float CGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos) const
{
	float Score = 0.0f;

	for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = static_cast<CCharacter *>(r.front());
		if(!pChr || !pChr->GetPlayer())
			continue;
		// team mates are not as dangerous as enemies
		float Scoremod = 1.0f;
		if(pEval->m_FriendlyTeam != -1 && pChr->GetPlayer()->GetTeam() == pEval->m_FriendlyTeam)
			Scoremod = 0.5f;

		float d = distance(Pos, pChr->GetPos());
		Score += Scoremod * (d == 0 ? 1000000000.0f : 1.0f / d);
	}

	return Score;
}

void CGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type) const
{
	// get spawn point
	for(int i = 0; i < m_alSpawnPoints[Type].size(); i++)
	{
		// check if the position is occupado
		array<CEntity *> lpEnts;
		lpEnts.hint_size(8);
		int Num = GameServer()->m_World.FindEntities(m_alSpawnPoints[Type][i], 64, lpEnts, CGameWorld::ENTTYPE_CHARACTER);
		vec2 Positions[13] = {
			vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(0.0f, 32.0f),
			vec2(-64.0f, 0.0f), vec2(64.0f, 0.0f), vec2(-32.0f, -32.0f), vec2(32.0f, -32.0f),
			vec2(-32.0f, 32.0f), vec2(32.0f, 32.0f), vec2(-96.0f, 0.0f), vec2(96.0f, 0.0f)};
		int Result = -1;
		for(int Index = 0; Index < 13 && Result == -1; ++Index)
		{
			Result = Index;
			for(int c = 0; c < Num; ++c)
				if(GameServer()->Collision()->CheckPoint(m_alSpawnPoints[Type][i] + Positions[Index]) ||
					distance(lpEnts[c]->GetPos(), m_alSpawnPoints[Type][i] + Positions[Index]) <= lpEnts[c]->GetProximityRadius())
				{
					Result = -1;
					break;
				}
		}
		if(Result == -1)
			continue; // try next spawn point

		vec2 P = m_alSpawnPoints[Type][i] + Positions[Result];
		float S = pEval->m_RandomSpawn ? (Result + random_float()) : EvaluateSpawnPos(pEval, P);
		if(!pEval->m_Got || pEval->m_Score > S)
		{
			pEval->m_Got = true;
			pEval->m_Score = S;
			pEval->m_Pos = P;
		}
	}
}

bool CGameController::GetStartRespawnState() const
{
	return false;
}

// team
bool CGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam) const
{
	if(!pPlayer->IsDummy() && JoinTeam == TEAM_BLUE)
		return false;
	return true;
}

bool CGameController::CanJoinTeam(int Team, int NotThisID) const
{
	return true;
}

int CGameController::ClampTeam(int Team) const
{
	if(Team < TEAM_RED)
		return TEAM_SPECTATORS;
	return TEAM_RED;
}

void CGameController::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	int OldTeam = pPlayer->GetTeam();
	pPlayer->SetTeam(Team);

	int ClientID = pPlayer->GetCID();

	// notify clients
	CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Team = Team;
	Msg.m_Silent = DoChatMsg ? 0 : 1;
	Msg.m_CooldownTick = pPlayer->m_TeamChangeTick;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	if(!IsZombiePlayer(pPlayer))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d->%d", ClientID, Server()->ClientName(ClientID), OldTeam, Team);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	// update effected game settings
	if(OldTeam != TEAM_SPECTATORS)
	{
		--m_RealPlayerNum;
	}
	if(Team != TEAM_SPECTATORS)
	{
		++m_RealPlayerNum;
	}
	OnPlayerInfoChange(pPlayer);
	GameServer()->OnClientTeamChange(ClientID);

	// reset inactivity counter when joining the game
	if(OldTeam == TEAM_SPECTATORS)
		pPlayer->m_InactivityTickCounter = 0;
}

int CGameController::GetStartTeam()
{
	return TEAM_RED;
}

void CGameController::Com_About(IConsole::IResult *pResult, void *pContext)
{
	CCommandManager::SCommandContext *pCmdContext = static_cast<CCommandManager::SCommandContext *>(pContext);
	CGameController *pSelf = static_cast<CGameController *>(pCmdContext->m_pContext);
	int ClientID = pCmdContext->m_ClientID;
	char aBuf[128];
	pSelf->GameServer()->LocFormat(aBuf, sizeof(aBuf), ClientID, "cmd.about.message", "%s v%s by CometOnOrbit", MOD_NAME, MOD_VERSION);
	pSelf->GameServer()->SendChatTo(ClientID, aBuf);
}

void CGameController::Com_Community(IConsole::IResult *pResult, void *pContext)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCmdContext = static_cast<CCommandManager::SCommandContext *>(pContext);
	CGameController *pSelf = static_cast<CGameController *>(pCmdContext->m_pContext);
	pSelf->GameServer()->SendCommunityInfo(pCmdContext->m_ClientID);
}

void CGameController::RegisterChatCommands(CCommandManager *pManager)
{
	pManager->AddCommand("about", "cmd.about.help", "", Com_About, this);
	pManager->AddCommand("info", "cmd.info.help", "", Com_About, this);
	pManager->AddCommand("community", "cmd.community.help", "", Com_Community, this);
	pManager->AddCommand("qq", "cmd.community.help", "", Com_Community, this);

	if(TWorldController *pCore = GameServer()->Core())
	{
		if(pCore->AccountManager())
			pCore->AccountManager()->RegisterChatCommands(pManager);
		if(pCore->VoteMenuManager())
			pCore->VoteMenuManager()->RegisterChatCommands(pManager);
		if(pCore->CraftManager())
		{
			pCore->CraftManager()->RegisterChatCommands(pManager);
			pCore->CraftManager()->RegisterVoteCommands(pManager);
		}
	}
}

bool CGameController::CanCharacterWeaponFullAuto(CCharacter *pChr, int Weapon)
{
	return Weapon == WEAPON_GRENADE || Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_LASER;
}

int CGameController::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	if(!pChr)
		return 0;

	int ClientID = pChr->GetCID();
	vec2 ChrPos = pChr->GetPos();
	vec2 ProjStartPos = ChrPos + Direction * pChr->GetProximityRadius() * 0.75f;

	CPlayer *pPl = pChr->GetPlayer();
	CItemHelper *pH = GameServer()->ItemHelper();
	const char *pSx = (pPl && pH) ? pPl->GetExtraForItem(pPl->GetHolding(ITYPE_SWORD)) : nullptr;
	const int ExtraDmg = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_DAMAGE_ID) * 2 : 0;
	const float MoreForce = (pH && pSx) ? 1.f + (float)pH->GetCard(pSx, ITEM_CARD_FORCE_ID) * 2.f : 1.f;
	const int Electron = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_ELECTRON_ID) : 0;
	const int ExplosionStacks = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_EXPLOSION_ID) : 0;

	int ReloadTimer = 0;
	switch(Weapon)
	{
		case WEAPON_HAMMER:
		{
			GameServer()->m_World.CreateSound(ChrPos, SOUND_HAMMER_FIRE);

			if(Electron > 0)
			{
				for(int i = 0; i < 5; i++)
				{
					const float Spreading[] = {-0.185f, -0.130f, -0.050f, 0.050f, 0.130f, 0.185f};
					float a = angle(Direction);
					a += Spreading[i + 1];
					new CLightning(&GameServer()->m_World, ChrPos, vec2(cosf(a), sinf(a)), 200.f, 100.f, ClientID, ExtraDmg);
				}
			}

			array<CEntity *> lpEnts;
			lpEnts.hint_size(8);
			int Hits = 0;
			const float HammerQueryR = pChr->GetProximityRadius() * 0.5f + (float)Config()->m_SvTdTowerHitRadius;
			const int Num = GameServer()->m_World.FindFlagEntities(ProjStartPos, HammerQueryR, lpEnts, CGameWorld::ENTFLAG_HITABLE);
			for(int i = 0; i < Num; ++i)
			{
				CEntity *pHitEnt = lpEnts[i];
				if(pHitEnt->ObjType() == CGameWorld::ENTTYPE_TOWERMAIN)
				{
					if(!pChr->GetPlayer()->IsDummy() && pChr->GetPlayer()->GetTeam() != TEAM_BLUE)
						continue;

					CTowerMain *pTower = static_cast<CTowerMain *>(pHitEnt);
					if(GameServer()->Collision()->IntersectLine(ProjStartPos, pTower->GetPos(), NULL, NULL))
						continue;

					if(distance(pTower->GetPos(), ProjStartPos) > 0.0f)
						GameServer()->m_World.CreateHammerHit(pTower->GetPos() - normalize(pTower->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
					else
						GameServer()->m_World.CreateHammerHit(ProjStartPos);

					vec2 Dir;
					if(length(pTower->GetPos() - ChrPos) > 0.0f)
						Dir = normalize(pTower->GetPos() - ChrPos);
					else
						Dir = vec2(0.f, -1.f);

					const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
					pTower->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f * MoreForce, Dir * -1, HamVanilla + ExtraDmg,
						pChr, Weapon);
					Hits++;
					continue;
				}

				CCharacter *pTarget = static_cast<CCharacter *>(pHitEnt);

				if((pTarget == pChr) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), NULL, NULL))
					continue;

				// set his velocity to fast upward (for now)
				if(length(pTarget->GetPos() - ProjStartPos) > 0.0f)
					GameServer()->m_World.CreateHammerHit(pTarget->GetPos() - normalize(pTarget->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
				else
					GameServer()->m_World.CreateHammerHit(ProjStartPos);

				vec2 Dir;
				if(length(pTarget->GetPos() - ChrPos) > 0.0f)
					Dir = normalize(pTarget->GetPos() - ChrPos);
				else
					Dir = vec2(0.f, -1.f);

				const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
				const int HamPvp = HamVanilla + ExtraDmg;

				pTarget->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f * MoreForce, Dir * -1, HamPvp,
					pChr, Weapon);
				for(int e = 0; e < ExplosionStacks; e++)
				{
					const int OffX = (random_int() % 401) - 200;
					const int OffY = (random_int() % 401) - 200;
					GameServer()->m_World.CreateExplosion(vec2(ChrPos.x + OffX, ChrPos.y + OffY), pChr, WEAPON_HAMMER, maximum(1, ExtraDmg));
				}
				Hits++;
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
				ReloadTimer = Server()->TickSpeed() / 3;
		}
		break;

		case WEAPON_GUN:
		{
			new CProjectile(&GameServer()->m_World, WEAPON_GUN,
				ClientID,
				ProjStartPos,
				Direction,
				(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime),
				ExtraDmg, false, MoreForce, -1, WEAPON_GUN);

			GameServer()->m_World.CreateSound(ChrPos, SOUND_GUN_FIRE);
		}
		break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 2;

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = angle(Direction);
				a += Spreading[i + 2];
				float v = 1 - (absolute(i) / (float) ShotSpread);
				float Speed = mix((float) GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				new CProjectile(&GameServer()->m_World, WEAPON_SHOTGUN,
					ClientID,
					ProjStartPos,
					vec2(cosf(a), sinf(a)) * Speed,
					(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime),
					ExtraDmg, false, MoreForce, -1, WEAPON_SHOTGUN);
			}

			GameServer()->m_World.CreateSound(ChrPos, SOUND_SHOTGUN_FIRE);
		}
		break;

		case WEAPON_GRENADE:
		{
			new CProjectile(&GameServer()->m_World, WEAPON_GRENADE,
				ClientID,
				ProjStartPos,
				Direction,
				(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime),
				ExtraDmg, true, MoreForce, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			GameServer()->m_World.CreateSound(ChrPos, SOUND_GRENADE_FIRE);
		}
		break;

		case WEAPON_LASER:
		{
			if(Electron > 0)
			{
				vec2 Start = ChrPos + Direction * 50.f;
				float a = angle(Direction);
				vec2 To = ChrPos + vec2(cosf(a), sinf(a)) * 400.f;
				GameServer()->Collision()->IntersectLine(Start, To, 0x0, &To);
				vec2 At;
				CCharacter *pHit = GameServer()->m_World.IntersectCharacter(Start, To, 70.f, At, pChr);
				if(pHit)
				{
					To = pHit->GetPos();
					pHit->TakeHit(Direction, Direction * -1, 1 + ExtraDmg, pChr, WEAPON_LASER);
				}
				int Segments = distance(Start, To) / 100;
				Segments = clamp(Segments, 2, 4);
				new CElectro(&GameServer()->m_World, Start, To, vec2(cosf(a * 1.2f), sinf(a * 1.2f)) * 40.f, Segments);
			}
			const int HostLaser = pPl ? pPl->GetHolding(ITYPE_SWORD) : -1;
			new CLaser(&GameServer()->m_World, ChrPos, Direction, GameServer()->Tuning()->m_LaserReach, ClientID, ExtraDmg, false, MoreForce, 0, HostLaser);
			GameServer()->m_World.CreateSound(ChrPos, SOUND_LASER_FIRE);
		}
		break;

		case WEAPON_NINJA:
		{
			pChr->DoNinjaFire(Direction, g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000);
			GameServer()->m_World.CreateSound(ChrPos, SOUND_NINJA_FIRE);
		}
		break;
	}
	const int LessReload = (pH && pSx) ? 10 * pH->GetCard(pSx, ITEM_CARD_QUICKLY_FIRE_ID) : 0;
	if(!ReloadTimer)
		ReloadTimer = maximum(0, g_pData->m_Weapons.m_aId[Weapon].m_Firedelay * Server()->TickSpeed() / 1000 - LessReload);

	return ReloadTimer;
}

void CGameController::NotifyPlayerConnected(CPlayer *pPlayer)
{
	if(!pPlayer->IsDummy() && pPlayer->GetTeam() != TEAM_SPECTATORS)
		++m_RealPlayerNum;
}

void CGameController::TdResetPendingRemoves()
{
	m_TdDummyRemoveLen = 0;
	mem_zero(m_aTdDummyRemove, sizeof(m_aTdDummyRemove));
}

void CGameController::TdDoWarmup(int Seconds)
{
	m_TdWarmup = Seconds * Server()->TickSpeed();
}

void CGameController::TdStartRound()
{
	m_TdGameOverTick = -1;

	const int Zombie0 = TdZombieFirstSlot(Config());
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->IsDummy())
			Server()->DummyRemove(i);
	}

	m_TdWave++;
	TdStartWave(m_TdWave);
}

void CGameController::TdEndRound()
{
	m_TdGameOverTick = Server()->Tick();
	m_TdWave = 0;
	mem_zero(m_TdZombie, sizeof(m_TdZombie));

	const int Zombie0 = TdZombieFirstSlot(Config());
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->IsDummy())
			Server()->DummyRemove(i);
	}

	GameServer()->SendChatAllLoc("game.tower_destroyed", "The tower was destroyed! Game over — map will reload.");
	TdBroadcastGameInfo();
}

void CGameController::TdDoWincheck()
{
	if(!m_pTower)
		return;
	if(m_TdGameOverTick != -1)
		return;
	if(m_TdWarmup)
		return;
	if(m_pTower->GetHealth() <= 0)
		TdEndRound();
}

void CGameController::TdStartWave(int Wave)
{
	if(!Wave)
		return;

	mem_zero(m_TdZombie, sizeof(m_TdZombie));
	if(Wave == 1)
		m_TdZombie[0] = 10;
	else if(Wave == 2)
		m_TdZombie[0] = 25;
	else
		TdSetWaveAlg(Wave % 3, Wave / 3);

	m_TdZombLeft = 0;
	for(unsigned i = 0; i < sizeof(m_TdZombie) / sizeof(m_TdZombie[0]); i++)
		m_TdZombLeft += m_TdZombie[i];

	TdDoZombMessage(0);
	TdBroadcastGameInfo();
}

void CGameController::TdCheckZombie()
{
	if(m_TdWarmup || !m_TdWave || m_TdGameOverTick != -1)
		return;

	if(TdEndWave())
		return;

	if(TdCountZombiePopulation() >= m_TdZombStart)
		return;

	vec2 SpawnPos;
	if(!CanSpawn(GetDummyTeam(), &SpawnPos))
		return;

	const int Zombie0 = TdZombieFirstSlot(Config());

	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
			continue;
		if(!Server()->IsClientSlotEmpty(i))
			continue;

		const int Random = TdRandZomb();
		if(Random < 0)
			break;

		m_TdPendingZomb = Random + 1;

		char aName[16];
		str_format(aName, sizeof(aName), "z%d", i);
		Server()->DummyJoin(i, aName);
		break;
	}
}

int CGameController::TdCountZombiePopulation() const
{
	int Count = 0;
	const int Zombie0 = TdZombieFirstSlot(Config());
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(IsZombiePlayer(GameServer()->m_apPlayers[i]))
			Count++;
	}
	return Count;
}

int CGameController::TdRandZomb()
{
	const int size = (int)(sizeof(m_TdZombie) / sizeof(m_TdZombie[0]));
	int Rand = rand() % size;
	int Attempts = Config()->m_SvMaxZombieSpawn;
	while(!m_TdZombie[Rand])
	{
		Rand = rand() % size;
		Attempts--;
		if(!Attempts)
			return -1;
	}
	return Rand;
}

bool CGameController::TdIsWaveCleared() const
{
	int PlayerCount = 0;
	for(int k = 0; k < MAX_HUMAN_CLIENTS; k++)
	{
		if(GameServer()->m_apPlayers[k])
		{
			PlayerCount++;
			break;
		}
	}

	if(!PlayerCount)
		return true;

	for(unsigned j = 0; j < sizeof(m_TdZombie) / sizeof(m_TdZombie[0]); j++)
	{
		if(m_TdZombie[j])
			return false;
	}

	const int Zombie0 = TdZombieFirstSlot(Config());
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!IsZombiePlayer(pP))
			continue;
		if(!pP->IsEliminated())
			return false;
	}

	return true;
}

bool CGameController::TdEndWave()
{
	if(!TdIsWaveCleared())
		return false;

	int PlayerCount = 0;
	for(int k = 0; k < MAX_HUMAN_CLIENTS; k++)
	{
		if(GameServer()->m_apPlayers[k])
		{
			PlayerCount++;
			break;
		}
	}

	if(!PlayerCount)
	{
		const int Zombie0 = TdZombieFirstSlot(Config());
		for(int i = Zombie0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->IsDummy())
				Server()->DummyRemove(i);
		}
		m_TdWave = 0;
		return true;
	}

	const int NextBreak = Config()->m_SvZombWarmup + 5 * m_TdWave;
	GameServer()->SendChatAllLocF("game.wave_cleared", "Wave %d cleared! Break: %d s — prepare for wave %d.", m_TdWave, NextBreak, m_TdWave + 1);

	if(Config()->m_SvTdWaveScoreBonus)
	{
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			CPlayer *pP = GameServer()->m_apPlayers[i];
			if(!pP || pP->GetTeam() != TEAM_RED || pP->IsDummy())
				continue;
			pP->m_Score += m_TdWave;
		}
		GameServer()->SendChatAllLocF("game.wave_score_bonus", "Defenders gain %d score for clearing the wave.", m_TdWave);
	}

	if(m_TdWave % 5 == 0)
	{
		GameServer()->SendBroadcastLocF(-1, "community.broadcast",
			u8"加群 %d · 赞助 QQ %d — 输入 /community 查看",
			Config()->m_SvTdQQGroup, Config()->m_SvTdQQSponsor);
	}

	TdDoWarmup(NextBreak);
	TdBroadcastGameInfo();
	return true;
}

void CGameController::TdDoZombMessage(int Which)
{
	if(!Which)
	{
		m_TdZombStart = m_TdZombLeft;
		GameServer()->SendChatAllLocF("game.wave_start", "Wave %d started — %d zombies!", m_TdWave, m_TdZombLeft);
		return;
	}

	const int Left = Which;
	if(Left > 1 && (Left <= 5 || !(Left % 10)))
		GameServer()->SendChatAllLocF("game.wave_left", "Wave %d: %d zombies left", m_TdWave, Left);
	else if(Left == 1)
		GameServer()->SendChatAllLocF("game.wave_one_left", "Wave %d: 1 zombie left", m_TdWave);
}

void CGameController::TdSetWaveAlg(int Modulus, int WaveThird)
{
	if(WaveThird > 11)
	{
		for(int i = 0; i < NUM_TD_ZOMB; i++)
			m_TdZombie[i] = m_TdWave + 10;
		return;
	}

	if(!Modulus)
	{
		m_TdZombie[TdGetZombieOrder(WaveThird)] = 10;
	}
	else if(Modulus == 1)
	{
		m_TdZombie[TdGetZombieOrder(WaveThird)] = 20;
	}
	else if(Modulus == 2)
	{
		for(int i = 0; i <= WaveThird; i++)
			m_TdZombie[TdGetZombieOrder(i)] = m_TdWave;
	}
}

int CGameController::TdGetZombieOrder(int WaveThird)
{
	return WaveThird % NUM_TD_ZOMB;
}

void CGameController::TdSetWave(int Wave)
{
	m_TdWave = maximum(1, Wave);
}

void CGameController::TdSetTowerHealth(int Health)
{
	if(!m_pTower)
		return;
	m_pTower->SetHealth(clamp(Health, 1, Config()->m_SvMaxTowerHealth));
}

void CGameController::ConTdSetWave(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pGameServer = static_cast<CGameContext *>(pUser);
	static_cast<CGameController *>(pGameServer->m_pController)->TdSetWave(pResult->GetInteger(0));
}

void CGameController::ConTdSetTowerHealth(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pGameServer = static_cast<CGameContext *>(pUser);
	static_cast<CGameController *>(pGameServer->m_pController)->TdSetTowerHealth(pResult->GetInteger(0));
}

void CGameController::RegisterTeeDefenseConsoleCommands(CGameContext *pCtx)
{
	pCtx->Console()->Register("td_set_wave", "i[wave]", CFGFLAG_SERVER, ConTdSetWave, pCtx, "Set next TeeDefense wave number");
	pCtx->Console()->Register("td_set_tower_health", "i[health]", CFGFLAG_SERVER, ConTdSetTowerHealth, pCtx, "Set main tower current health");
}

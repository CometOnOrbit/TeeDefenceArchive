/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/version.h>
#include <generated/server_data.h>

#include "entities/character.h"
#include "entities/laser.h"
#include "entities/pickup.h"
#include "entities/projectile.h"
#include "entities/tower-main.h"
#include "entities/CKs.h"
#include "crafting.h"
#include "item_system.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"

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
	m_TdDummyRemoveLen = 0;
	mem_zero(m_aTdDummyRemove, sizeof(m_aTdDummyRemove));
	m_TdPendingZomb = ZOMB_ZABY;
	m_RealPlayerNum = 0;
}

void CGameController::PreTick()
{
	const int Zombie0 = maximum(1, Config()->m_SvTdZombieFirstSlot);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		if(i < Zombie0)
			continue;

		TdRunZombieBrain(pP);
	}
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

	pPlayer->InitZombie(m_TdPendingZomb);
	switch(m_TdPendingZomb)
	{
	case ZOMB_ZOOKER:
		str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[1], "bluekitty", MAX_SKIN_ARRAY_SIZE);
		break;
	case ZOMB_ZABER:
		str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[1], "twinbop", MAX_SKIN_ARRAY_SIZE);
		break;
	case ZOMB_ZABY:
	default:
		str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[1], "bear", MAX_SKIN_ARRAY_SIZE);
		break;
	}
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
					Server()->Kick(i, "Kicked for inactivity");
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
							Server()->Kick(i, "Kicked for inactivity");
						else
							DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
					}
					break;
					case 3:
					{
						// kick the player
						Server()->Kick(i, "Kicked for inactivity");
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

// event
int CGameController::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	if(Weapon != WEAPON_GAME && pVictim->GetPlayer()->IsDummy())
	{
		if(pKiller && !pKiller->IsDummy())
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

			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), "掉落：%s（僵尸心+1）", GameServer()->ItemHelper()->GetItemName(Reward));
			GameServer()->SendChat(pKiller->GetCID(), CHAT_ALL, -1, aBuf);

			if(GameServer()->Accounts()->IsEnabled() && pKiller->GetAccountId() >= 0)
				GameServer()->Accounts()->RequestSaveItems(pKiller->GetCID());
		}

		if(m_TdZombLeft > 0)
			m_TdZombLeft--;
		TdDoZombMessage(m_TdZombLeft);

		if(m_TdDummyRemoveLen < TD_REMOVE_QUEUE)
			m_aTdDummyRemove[m_TdDummyRemoveLen++] = pVictim->GetPlayer()->GetCID();

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
		pChr->SetHealthDirect(maximum(1, m_TdWave));
		pChr->GiveWeapon(WEAPON_HAMMER, -1);
		pChr->GiveWeapon(WEAPON_GUN, 0);
		switch(pChr->GetPlayer()->GetZomb())
		{
		case ZOMB_ZOOKER:
			pChr->GiveWeapon(WEAPON_GUN, 20);
			pChr->SetWeapon(WEAPON_GUN);
			break;
		case ZOMB_ZABER:
		case ZOMB_ZABY:
		default:
			pChr->SetWeapon(WEAPON_HAMMER);
			break;
		}
	}
	else
		pChr->SetHealthDirect(Config()->m_SvPlayerMaxHealth);
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
		char aWelcome[256];
		str_format(aWelcome, sizeof(aWelcome), "%s joined TeeDefense — defend the tower!", Server()->ClientName(pPlayer->GetCID()));
		GameServer()->SendChat(-1, CHAT_ALL, -1, aWelcome);
	}

	int ClientID = pPlayer->GetCID();
	pPlayer->Respawn();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	SendGameInfo(ClientID);
}

void CGameController::OnPlayerDisconnect(CPlayer *pPlayer)
{
	pPlayer->OnDisconnect();

	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID))
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
	const int HumanMax = maximum(1, Config()->m_SvTdZombieFirstSlot);
	for(int i = 0; i < HumanMax; i++)
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
		GameInfoMsg.m_MatchNum = m_TdZombStart;
		GameInfoMsg.m_MatchCurrent = m_TdZombLeft;
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
		vec2 Positions[5] = {vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f)}; // start, left, up, right, down
		int Result = -1;
		for(int Index = 0; Index < 5 && Result == -1; ++Index)
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

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d->%d", ClientID, Server()->ClientName(ClientID), OldTeam, Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

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
	pSelf->SendSystemChat(ClientID, MOD_NAME " v" MOD_VERSION " by CometOnOrbit");
}

void CGameController::RegisterChatCommands(CCommandManager *pManager)
{
	pManager->AddCommand("about", "About the mod", "", Com_About, this);
	pManager->AddCommand("info", "About the mod", "", Com_About, this);

	GameServer()->Accounts()->RegisterChatCommands(pManager, GameServer());
	RegisterCraftingChatCommands(pManager, GameServer());
	RegisterVoteMenuCommands(pManager, GameServer());
}

bool CGameController::CanCharacterWeaponFullAuto(CCharacter *pChr, int Weapon)
{
	return Weapon == WEAPON_GRENADE || Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_LASER;
}

void CGameController::SendSystemChat(int TargetID, const char *pMsg)
{
	GameServer()->SendChat(-1, CHAT_ALL, TargetID, pMsg);
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
	const int DmgMul = (pH && pSx) ? maximum(1, pH->GetCard(pSx, ITEM_CARD_DAMAGE_ID)) : 1;
	const bool ExpCard = (pH && pSx) && pH->GetCard(pSx, ITEM_CARD_EXPLOSION_ID) > 0;
	const float ProjForce = (pH && pSx) ? (float)(pH->GetCard(pSx, ITEM_CARD_FORCE_ID)) * 2.5f : 0.0f;
	const int Electron = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_ELECTRON_ID) : 0;

	int ReloadTimer = 0;
	switch(Weapon)
	{
		case WEAPON_HAMMER:
		{
			GameServer()->m_World.CreateSound(ChrPos, SOUND_HAMMER_FIRE);

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

					pTower->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Dir * -1, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
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
				const int HamPvp = (pPl && pH && !pPl->IsDummy()) ? maximum(1, HamVanilla * DmgMul) : HamVanilla;

				pTarget->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Dir * -1, HamPvp,
					pChr, Weapon);
				Hits++;
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
				ReloadTimer = Server()->TickSpeed() / 3;
		}
		break;

		case WEAPON_GUN:
		{
			const int Base = g_pData->m_Weapons.m_Gun.m_pBase->m_Damage;
			const int GDmg = maximum(1, Base * DmgMul);
			new CProjectile(&GameServer()->m_World, WEAPON_GUN,
				ClientID,
				ProjStartPos,
				Direction,
				(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime),
				GDmg, ExpCard, ProjForce, -1, WEAPON_GUN);

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
				const int Base = g_pData->m_Weapons.m_Shotgun.m_pBase->m_Damage;
				const int SDmg = maximum(1, Base * DmgMul);
				new CProjectile(&GameServer()->m_World, WEAPON_SHOTGUN,
					ClientID,
					ProjStartPos,
					vec2(cosf(a), sinf(a)) * Speed,
					(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime),
					SDmg, ExpCard, ProjForce, -1, WEAPON_SHOTGUN);
			}

			GameServer()->m_World.CreateSound(ChrPos, SOUND_SHOTGUN_FIRE);
		}
		break;

		case WEAPON_GRENADE:
		{
			const int Base = g_pData->m_Weapons.m_Grenade.m_pBase->m_Damage;
			const int Bonus = (ExpCard ? 8 : 0);
			const int GrDmg = maximum(1, Base * DmgMul + Bonus);
			new CProjectile(&GameServer()->m_World, WEAPON_GRENADE,
				ClientID,
				ProjStartPos,
				Direction,
				(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime),
				GrDmg, true, ProjForce, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			GameServer()->m_World.CreateSound(ChrPos, SOUND_GRENADE_FIRE);
		}
		break;

		case WEAPON_LASER:
		{
			const int Base = g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage;
			const int LDmg = maximum(1, Base * DmgMul);
			int LEl = Electron;
			const int LFrc = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_FORCE_ID) : 0;
			if(LEl > 0 && LFrc > 0)
				LEl += minimum(LEl, LFrc);
			const int HostLaser = pPl ? pPl->GetHolding(ITYPE_SWORD) : -1;
			new CLaser(&GameServer()->m_World, ChrPos, Direction, GameServer()->Tuning()->m_LaserReach, ClientID, LDmg, ExpCard, ProjForce, LEl, HostLaser);
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
	if(!ReloadTimer)
		ReloadTimer = g_pData->m_Weapons.m_aId[Weapon].m_Firedelay * Server()->TickSpeed() / 1000;

	if(pH && pSx)
	{
		const int QF = pH->GetCard(pSx, ITEM_CARD_QUICKLY_FIRE_ID);
		const int QL = pH->GetCard(pSx, ITEM_CARD_QUICKLY_LOADING_ID);
		if(QF > 0)
			ReloadTimer = maximum(1, ReloadTimer - (QF * Server()->TickSpeed()) / 25);
		if(QF > 0 && QL > 0)
			ReloadTimer = maximum(1, ReloadTimer - Server()->TickSpeed() / 35);
	}

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

	const int Zombie0 = maximum(1, Config()->m_SvTdZombieFirstSlot);
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

	const int Zombie0 = maximum(1, Config()->m_SvTdZombieFirstSlot);
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->IsDummy())
			Server()->DummyRemove(i);
	}

	GameServer()->SendChat(-1, CHAT_ALL, -1, "The tower was destroyed! Game over — map will reload.");
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
	if(m_TdWarmup || !m_TdWave || TdEndWave() || m_TdGameOverTick != -1)
		return;

	const int Zombie0 = maximum(1, Config()->m_SvTdZombieFirstSlot);

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
		m_TdZombie[Random]--;
	}
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

bool CGameController::TdEndWave()
{
	int PlayerCount = 0;
	const int HumanMax = maximum(1, Config()->m_SvTdZombieFirstSlot);
	for(int k = 0; k < HumanMax; k++)
	{
		if(GameServer()->m_apPlayers[k])
		{
			PlayerCount++;
			break;
		}
	}

	if(!PlayerCount)
	{
		const int Zombie0 = maximum(1, Config()->m_SvTdZombieFirstSlot);
		for(int i = Zombie0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->IsDummy())
				Server()->DummyRemove(i);
		}
		m_TdWave = 0;
		return true;
	}

	for(unsigned j = 0; j < sizeof(m_TdZombie) / sizeof(m_TdZombie[0]); j++)
	{
		if(m_TdZombie[j])
			return false;
	}

	const int Zombie0 = maximum(1, Config()->m_SvTdZombieFirstSlot);
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetCharacter() && GameServer()->m_apPlayers[i]->GetCharacter()->IsAlive())
			return false;
	}

	char aClear[256];
	const int NextBreak = Config()->m_SvZombWarmup + 5 * m_TdWave;
	str_format(aClear, sizeof(aClear), "Wave %d cleared! Break: %d s — prepare for wave %d.", m_TdWave, NextBreak, m_TdWave + 1);
	GameServer()->SendChat(-1, CHAT_ALL, -1, aClear);

	if(Config()->m_SvTdWaveScoreBonus)
	{
		for(int i = 0; i < HumanMax; i++)
		{
			CPlayer *pP = GameServer()->m_apPlayers[i];
			if(!pP || pP->GetTeam() != TEAM_RED || pP->IsDummy())
				continue;
			pP->m_Score += m_TdWave;
		}
		char aScore[128];
		str_format(aScore, sizeof(aScore), "Defenders gain %d score for clearing the wave.", m_TdWave);
		GameServer()->SendChat(-1, CHAT_ALL, -1, aScore);
	}

	TdDoWarmup(NextBreak);
	TdBroadcastGameInfo();
	return true;
}

void CGameController::TdDoZombMessage(int Which)
{
	char aBuf[256];
	if(!Which)
	{
		m_TdZombStart = m_TdZombLeft;
		str_format(aBuf, sizeof(aBuf), "Wave %d started — %d zombies!", m_TdWave, m_TdZombLeft);
		GameServer()->SendChat(-1, CHAT_ALL, -1, aBuf);
		return;
	}

	const int Left = Which;
	if(Left > 1 && (Left <= 5 || !(Left % 10)))
	{
		str_format(aBuf, sizeof(aBuf), "Wave %d: %d zombies left", m_TdWave, Left);
		GameServer()->SendChat(-1, CHAT_ALL, -1, aBuf);
	}
	else if(Left == 1)
	{
		str_format(aBuf, sizeof(aBuf), "Wave %d: 1 zombie left", m_TdWave);
		GameServer()->SendChat(-1, CHAT_ALL, -1, aBuf);
	}
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
	if(!WaveThird)
		return 0;
	if(WaveThird < NUM_TD_ZOMB)
		return WaveThird + 1;
	return 1;
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

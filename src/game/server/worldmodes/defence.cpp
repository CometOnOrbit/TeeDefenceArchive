#include "defence.h"

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/extra_hud.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>
#include <game/server/core/components/skills/skill_data.h>
#include <game/server/entities/character.h>
#include <game/server/entities/tower-main.h>
#include <game/server/entities/spider_boss.h>
#include <game/server/entities/ai_core/zombie_ai.h>

#include <game/server/core/components/content/enemy_registry.h>

#include <base/math.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/account.h>
#include <game/server/botengine.h>
#include <game/server/zombie_nav.h>

static int TdZombieFirstSlot(const CConfig *pCfg)
{
	return minimum((int)MAX_HUMAN_CLIENTS, pCfg->m_SvMaxClients);
}

static bool IsZombiePlayer(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->IsDummy() && pPlayer->GetZomb() != ZOMB_NONE;
}

static bool IsHumanDefenderInWorld(CGameContext *pGame, int ClientID, int WorldID)
{
	if(!pGame || ClientID < 0 || ClientID >= MAX_HUMAN_CLIENTS)
		return false;
	if(pGame->Server()->GetClientWorldID(ClientID) != WorldID)
		return false;
	CPlayer *pP = pGame->m_apPlayers[ClientID];
	return pP && pP->GetTeam() == TEAM_RED && pP->GetCharacter();
}

CGameControllerDefence::CGameControllerDefence(CGameContext *pGameServer)
	: CGameController(pGameServer)
{
	m_TdWarmup = 0;
	m_TdGameOverTick = -1;
	m_TdZombStart = 0;
	m_TdWave = 0;
	m_TdZombLeft = 0;
	mem_zero(m_TdZombie, sizeof(m_TdZombie));
	m_pTower = nullptr;
	mem_zero(m_apZombieAIs, sizeof(m_apZombieAIs));
	m_pSpiderBoss = nullptr;
	m_TdBossWave = false;
	m_TdSpiderBossPending = false;
	m_TdDummyRemoveLen = 0;
	mem_zero(m_aTdDummyRemove, sizeof(m_aTdDummyRemove));
	m_TdPendingZomb = 0;
	m_TdDifficulty = clamp(Config()->m_SvTdDifficulty, 0, 2);
	m_TdMMOActive = false;
}

CGameControllerDefence::~CGameControllerDefence()
{
	TdDestroySpiderBoss();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		delete m_apZombieAIs[i];
		m_apZombieAIs[i] = nullptr;
	}
}

float CGameControllerDefence::TdDifficultyZombieMul() const
{
	static const float s_aMul[NUM_TD_DIFF] = {0.65f, 0.85f, 1.15f};
	return s_aMul[m_TdDifficulty];
}

float CGameControllerDefence::TdDifficultyHealthMul() const
{
	static const float s_aMul[NUM_TD_DIFF] = {0.75f, 0.88f, 1.10f};
	return s_aMul[m_TdDifficulty];
}

float CGameControllerDefence::TdDifficultyAiMul() const
{
	static const float s_aMul[NUM_TD_DIFF] = {0.85f, 1.0f, 1.15f};
	return s_aMul[m_TdDifficulty];
}

float CGameControllerDefence::TdDifficultyTowerMul() const
{
	static const float s_aMul[NUM_TD_DIFF] = {1.25f, 1.0f, 0.75f};
	return s_aMul[m_TdDifficulty];
}

int CGameControllerDefence::TdGetDifficultyTowerMaxHealth() const
{
	return maximum(1, (int)(Config()->m_SvMaxTowerHealth * TdDifficultyTowerMul() + 0.5f));
}

bool CGameControllerDefence::TdCanChangeDifficulty() const
{
	return m_TdWave == 0 && m_TdGameOverTick == -1;
}

bool CGameControllerDefence::TdSetDifficulty(int Difficulty)
{
	if(!TdCanChangeDifficulty())
		return false;
	m_TdDifficulty = clamp(Difficulty, 0, 2);
	Config()->m_SvTdDifficulty = m_TdDifficulty;
	TdRefreshTowerMaxHealth();
	return true;
}

void CGameControllerDefence::TdRefreshTowerMaxHealth()
{
	if(!m_pTower)
		return;
	const int MaxHp = TdGetDifficultyTowerMaxHealth();
	if(m_pTower->GetHealth() > MaxHp)
		m_pTower->SetHealth(MaxHp);
	else if(m_TdWave == 0)
		m_pTower->SetHealth(MaxHp);
}

void CGameControllerDefence::TdApplyDifficultyToZombieCounts()
{
	const float Mul = TdDifficultyZombieMul();
	if(fabs(Mul - 1.0f) < 0.001f)
		return;
	for(unsigned i = 0; i < sizeof(m_TdZombie) / sizeof(m_TdZombie[0]); i++)
	{
		if(m_TdZombie[i] > 0)
			m_TdZombie[i] = maximum(1, (int)(m_TdZombie[i] * Mul + 0.5f));
	}
}

void CGameControllerDefence::TdResetPendingRemoves()
{
	m_TdDummyRemoveLen = 0;
	mem_zero(m_aTdDummyRemove, sizeof(m_aTdDummyRemove));
}

void CGameControllerDefence::TdDoWarmup(int Seconds)
{
	m_TdWarmup = Seconds * Server()->TickSpeed();
}

bool CGameControllerDefence::TdSkipWarmup()
{
	if(m_TdWarmup <= 0)
		return false;

	m_TdWarmup = 0;
	if(m_TdWave > 0 && m_TdGameOverTick == -1)
		TdStartRound();
	TdBroadcastGameInfo();
	return true;
}

void CGameControllerDefence::TdPurgeZombieDummies()
{
	const int Zombie0 = TdZombieFirstSlot(Config());
	const int WorldID = GameServer()->GetWorldID();
	for(int i = MAX_CLIENTS - 1; i >= Zombie0; i--)
	{
		if(Server()->GetClientWorldID(i) != WorldID)
			continue;
		if(Server()->IsClientSlotEmpty(i))
			continue;
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && !pP->IsDummy())
			continue;
		if(pP && pP->GetZomb() == ZOMB_NONE)
			continue;
		Server()->DummyRemove(i);
	}
}

void CGameControllerDefence::PreTick()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		if(pP->GetZomb() == ZOMB_SPIDER_BOSS)
			continue;
		if(pP->GetZomb() != ZOMB_NONE)
			TdRunZombieBrain(pP);
	}
}

void CGameControllerDefence::OnBotPlayerCreated(CPlayer *pPlayer)
{
	CGameController::OnBotPlayerCreated(pPlayer);

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
	delete m_apZombieAIs[CID];
	m_apZombieAIs[CID] = nullptr;

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
	case ZOMB_ZSHIELD:
		pBodySkin = "brownkitty";
		break;
	case ZOMB_ZHEALER:
		pBodySkin = "limekitty";
		break;
	case ZOMB_ZSPLITTER:
		pBodySkin = "bluestripe";
		break;
	case ZOMB_SPIDER_BOSS:
		pBodySkin = "pinky";
		break;
	case ZOMB_ZABY:
	default:
		pBodySkin = "saddo";
		break;
	}
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[1], pBodySkin, MAX_SKIN_ARRAY_SIZE);

	if(pPlayer->GetZomb() == ZOMB_SPIDER_BOSS)
	{
		pPlayer->TryRespawn();
		if(!pPlayer->GetCharacter())
			m_TdSpiderBossPending = false;
		return;
	}

	if(pPlayer->GetZomb() != ZOMB_NONE && GameServer()->BotEngine())
		m_apZombieAIs[CID] = new CZombieAI(pPlayer, this, GameServer());
}

void CGameControllerDefence::TdRunZombieBrain(CPlayer *pP)
{
	const int CID = pP->GetCID();
	if(!m_apZombieAIs[CID] && GameServer()->BotEngine())
		m_apZombieAIs[CID] = new CZombieAI(pP, this, GameServer());
	CZombieAI *pBot = m_apZombieAIs[CID];
	if(!pBot)
		return;

	pBot->TickBrain();

	CNetObj_PlayerInput Inp = pBot->GetInput();
	pP->OnPredictedInput(&Inp);
	pP->OnDirectInput(&Inp);
}

void CGameControllerDefence::TdClearZombieAI(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	delete m_apZombieAIs[ClientID];
	m_apZombieAIs[ClientID] = nullptr;
}

void CGameControllerDefence::TdDestroySpiderBoss()
{
	if(m_pSpiderBoss)
	{
		delete m_pSpiderBoss;
		m_pSpiderBoss = nullptr;
	}
	m_TdSpiderBossPending = false;
	m_TdBossWave = false;
}

bool CGameControllerDefence::TdHasSpiderBossPlayer() const
{
	const int Zombie0 = TdZombieFirstSlot(Config());
	const int WorldID = GameServer()->GetWorldID();
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		const CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->IsDummy() && pP->GetZomb() == ZOMB_SPIDER_BOSS && Server()->GetClientWorldID(i) == WorldID)
			return true;
	}
	return false;
}

bool CGameControllerDefence::IsSpiderBossCore(CCharacter *pChr) const
{
	const CPlayer *pPlayer = pChr ? pChr->GetPlayer() : nullptr;
	if(!pPlayer || pPlayer->GetZomb() != ZOMB_SPIDER_BOSS)
		return false;
	return m_pSpiderBoss && m_pSpiderBoss->GetOwnerCid() == pPlayer->GetCID();
}

vec2 CGameControllerDefence::TdGetZombieMarchGoal() const
{
	vec2 Goal = m_pTower ? m_pTower->GetPos() : TdGetZombieRallyPos();
	Goal = ZombieNavResolveGoal(GameServer(), Goal);
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

vec2 CGameControllerDefence::TdGetZombieRallyPos() const
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

// event
int CGameControllerDefence::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	CPlayer *pVictimPlayer = pVictim->GetPlayer();
	if(IsZombiePlayer(pVictimPlayer))
	{
		if(pVictimPlayer->IsEliminated())
			return 0;

		if(pKiller && !pKiller->IsDummy() && Weapon != WEAPON_GAME)
		{
			if(TWorldController *pCore = GameServer()->Core())
			{
				if(pCore->EnemyRegistry())
					pCore->EnemyRegistry()->RollLoot(pKiller, pVictimPlayer->GetZomb());
				pCore->Events().EmitPlayerKill(pKiller, pVictimPlayer->GetZomb());
			}

			// Queue RPG experience reward for Defence kills
			int ZombieLevel = 1;
			switch(pVictimPlayer->GetZomb())
			{
			case ZOMB_ZABY: ZombieLevel = 1; break;
			case ZOMB_ZOOMER: case ZOMB_ZOOKER: ZombieLevel = 2; break;
			case ZOMB_ZAMER: case ZOMB_ZUNNER: ZombieLevel = 3; break;
			case ZOMB_ZASTER: case ZOMB_ZOTTER: case ZOMB_ZENADE: ZombieLevel = 4; break;
			case ZOMB_FLOMBIE: case ZOMB_ZINJA: ZombieLevel = 4; break;
			case ZOMB_ZELE: case ZOMB_ZINVIS: case ZOMB_ZEATER: ZombieLevel = 5; break;
			case ZOMB_ZSHIELD: case ZOMB_ZHEALER: case ZOMB_ZSPLITTER: ZombieLevel = 3; break;
			case ZOMB_SPIDER_BOSS: ZombieLevel = 8; break;
			default: ZombieLevel = 1; break;
			}
			int ExpReward = ZombieLevel * 5 + 3;
			const int PlayerLevel = maximum(1, pKiller->GetStat(AttributeIdentifier::Level));
			ExpReward = ScaleRewardByLevelGap(PlayerLevel, ZombieLevel, ExpReward);
			if(ExpReward > 0)
			{
				const int Cap = maximum(120, PlayerLevel * 60);
				const int Next = pKiller->m_DefencePendingExp + ExpReward;
				pKiller->m_DefencePendingExp = minimum(Cap, Next);
			}
		}

		if(pVictimPlayer->GetZomb() == ZOMB_SPIDER_BOSS)
			TdDestroySpiderBoss();
		else if(TWorldController *pCore = GameServer()->Core())
		{
			if(pCore->EnemyRegistry())
				pCore->EnemyRegistry()->OnZombieDeath(this, pVictimPlayer);
		}

		pVictimPlayer->ForbidRespawn();
		TdClearZombieAI(pVictimPlayer->GetCID());

		if(m_TdZombLeft > 0)
		{
			m_TdZombLeft--;
			if(m_TdZombLeft > 0)
				TdDoZombMessage(m_TdZombLeft);
		}

		if(m_TdDummyRemoveLen < TD_REMOVE_QUEUE)
			m_aTdDummyRemove[m_TdDummyRemoveLen++] = pVictimPlayer->GetCID();

		return 0;
	}

	// Use base handling for human deaths
	return CGameController::OnCharacterDeath(pVictim, pKiller, Weapon);
}

void CGameControllerDefence::OnCharacterSpawn(CCharacter *pChr)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	pChr->IncreaseHealth(10);
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_GUN, 10);

	if(pPlayer->IsDummy())
	{
		const int Z = pPlayer->GetZomb();
		int Health = TdZombieBaseHealth(m_TdWave);
		if(Z == ZOMB_ZASTER)
			Health = maximum(40, Health * 10);
		if(Z == ZOMB_SPIDER_BOSS)
			Health = maximum(100, m_TdWave * 20);
		Health = maximum(1, (int)(Health * TdDifficultyHealthMul() + 0.5f));
		if(TWorldController *pCore = GameServer()->Core())
		{
			if(pCore->EnemyRegistry())
				Health = maximum(1, (int)(Health * pCore->EnemyRegistry()->GetHpMul(Z) + 0.5f));
		}

		if(Z == ZOMB_SPIDER_BOSS)
			pChr->SetBossHealth(Health);
		else
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
		case ZOMB_ZSHIELD:
		case ZOMB_ZHEALER:
		case ZOMB_ZSPLITTER:
			pChr->SetWeapon(WEAPON_HAMMER);
			break;
		case ZOMB_SPIDER_BOSS:
			pChr->SetWeapon(WEAPON_GRENADE);
			pChr->SetHitRadius(112.0f);
			pChr->SyncSpiderBody(TdSnapSpawnToGround(pChr->GetPos(), 112.0f));
			break;
		default:
			pChr->SetWeapon(WEAPON_HAMMER);
			break;
		}

		if(Z == ZOMB_SPIDER_BOSS)
		{
			m_TdSpiderBossPending = false;
			if(m_pSpiderBoss)
			{
				if(m_TdDummyRemoveLen < TD_REMOVE_QUEUE)
					m_aTdDummyRemove[m_TdDummyRemoveLen++] = pChr->GetPlayer()->GetCID();
				return;
			}
			m_pSpiderBoss = new CSpiderBoss(&GameServer()->m_World, pChr, this, m_TdWave);
			TdBroadcastBossHealth();
		}
	}
	else
	{
		// Human: use base spawn behavior
		CGameController::OnCharacterSpawn(pChr);
	}
}

bool CGameControllerDefence::OnEntity(int Index, vec2 Pos)
{
	if(Index == ENTITY_MAIN_TOWER)
	{
		m_pTower = new CTowerMain(&GameServer()->m_World, Pos);
		TdRefreshTowerMaxHealth();
		return true;
	}

	return CGameController::OnEntity(Index, Pos);
}

void CGameControllerDefence::OnPlayerConnect(CPlayer *pPlayer)
{
	CGameController::OnPlayerConnect(pPlayer);

	if(!pPlayer->IsDummy() && pPlayer->GetAccountId() >= 0)
	{
		GameServer()->SendChatLoc(pPlayer->GetCID(), "welcome", "Welcome to TeeDefense Archive");
		GameServer()->SendChatAllLocF("game.join", "%s joined TeeDefense — defend the tower!", Server()->ClientName(pPlayer->GetCID()));
	}

	if(!IsZombiePlayer(pPlayer))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", pPlayer->GetCID(), Server()->ClientName(pPlayer->GetCID()), pPlayer->GetTeam());
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
}

void CGameControllerDefence::OnPlayerDisconnect(CPlayer *pPlayer)
{
	if(IsZombiePlayer(pPlayer))
		TdClearZombieAI(pPlayer->GetCID());

	CGameController::OnPlayerDisconnect(pPlayer);
}

void CGameControllerDefence::Snap(int SnappingClient)
{
	CNetObj_GameData *pGameData = static_cast<CNetObj_GameData *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData)));
	if(!pGameData)
		return;

	pGameData->m_GameStartTick = m_GameStartTick;
	pGameData->m_GameStateFlags = 0;
	pGameData->m_GameStateEndTick = 0;
	if(m_TdWarmup > 0)
	{
		pGameData->m_GameStateFlags |= GAMESTATEFLAG_WARMUP;
		pGameData->m_GameStateEndTick = Server()->Tick() + m_TdWarmup;
	}

	CNetObj_GameDataPrediction *pGameDataPrediction = static_cast<CNetObj_GameDataPrediction *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATAPREDICTION, 0, sizeof(CNetObj_GameDataPrediction)));
	if(!pGameDataPrediction)
		return;

	pGameDataPrediction->m_PredictionFlags = GAMEPREDICTIONFLAG_EVENT | GAMEPREDICTIONFLAG_INPUT;

	if(m_pSpiderBoss && m_pSpiderBoss->IsCoreAlive())
	{
		ExtraHudSnapProgress(Server(), EXTRAHUD_SLOT_PRIMARY,
			m_pSpiderBoss->GetCoreHealth(), m_pSpiderBoss->GetCoreMaxHealth(),
			m_pSpiderBoss->GetLegsAlive());
	}

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

void CGameControllerDefence::SendGameInfo(int ClientID)
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

void CGameControllerDefence::TdBroadcastGameInfo()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i] || GameServer()->m_apPlayers[i]->IsDummy())
			continue;
		if(Server()->ClientIngame(i))
			SendGameInfo(i);
	}
}

void CGameControllerDefence::TdBroadcastBossHealth()
{
	if(!m_pSpiderBoss || !m_pSpiderBoss->IsCoreAlive())
		return;

	GameServer()->SendBroadcastLocF(-1, "boss.spider.health_broadcast", "Boss 核心: %d / %d  腿: %d / 4",
		m_pSpiderBoss->GetCoreHealth(), m_pSpiderBoss->GetCoreMaxHealth(), m_pSpiderBoss->GetLegsAlive());
}

void CGameControllerDefence::Tick()
{
	CGameController::Tick();

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
	const int WorldID = GameServer()->GetWorldID();
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!IsHumanDefenderInWorld(GameServer(), i, WorldID))
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
			TdBroadcastGameInfo();
		m_TdWarmup--;
		if(m_TdWarmup == 0)
			TdStartRound();
		return;
	}

	if(m_pSpiderBoss && m_pSpiderBoss->IsCoreAlive() && Server()->Tick() % Server()->TickSpeed() == 0)
		TdBroadcastBossHealth();

	TdCheckZombie();
	TdDoWincheck();
}

bool CGameControllerDefence::TdIsWaveCleared() const
{
	if(m_TdZombLeft > 0)
		return false;
	const int CurrentPop = TdCountZombiePopulation();
	return CurrentPop <= 0;
}

bool CGameControllerDefence::TdEndWave()
{
	if(!TdIsWaveCleared())
		return false;

	if(m_TdWave % 5 == 0)
	{
		GameServer()->SendChatAllLocF("community.broadcast",
			"加群 %d · 赞助 QQ %d — 输入 /community 查看",
			Config()->m_SvTdQQGroup, Config()->m_SvTdQQSponsor);
	}

	TdDoZombMessage(0);
	TdBroadcastGameInfo();
	if(TWorldController *pCore = GameServer()->Core())
		pCore->Events().EmitWaveComplete(m_TdWave);

	if(m_TdWave >= 40)
		return true; // victory

	return false;
}

void CGameControllerDefence::TdDoZombMessage(int Left)
{
	if(Left > 0)
		GameServer()->SendBroadcastLocF(-1, "game.zombies_left", "剩余僵尸: %d", Left);
	else
		GameServer()->SendBroadcastLocF(-1, "game.wave_cleared", "波次清除！");
}

int CGameControllerDefence::TdZombieBaseHealth(int Wave)
{
	if(Wave <= 0) return 1;
	return maximum(1, 1 + (Wave - 1) / 5);
}

void CGameControllerDefence::TdSetWaveAlg(int Modulus, int WaveThird, int Wave)
{
	if(WaveThird > 11)
	{
		for(int i = 0; i < NUM_TD_ZOMB; i++)
			m_TdZombie[i] = Wave + 10;
		return;
	}

	if(!Modulus)
		m_TdZombie[TdGetZombieOrder(WaveThird)] = 10;
	else if(Modulus == 1)
		m_TdZombie[TdGetZombieOrder(WaveThird)] = 20;
	else if(Modulus == 2)
	{
		for(int i = 0; i <= WaveThird; i++)
			m_TdZombie[TdGetZombieOrder(i)] = Wave;
	}
}

int CGameControllerDefence::TdGetZombieOrder(int WaveThird)
{
	return WaveThird % NUM_TD_ZOMB;
}

void CGameControllerDefence::TdStartRound()
{
	m_TdGameOverTick = -1;
	TdDestroySpiderBoss();

	m_RealPlayerNum = 0;
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && !pP->IsDummy() && pP->GetTeam() == TEAM_RED)
			m_RealPlayerNum++;
	}

	m_TdWave++;
	TdStartWave(m_TdWave);
}

void CGameControllerDefence::TdEndRound()
{
	m_TdGameOverTick = Server()->Tick();
	TdDestroySpiderBoss();
	GameServer()->SendChatLoc(-1, "game.defeat", "塔被摧毁 — 人类失败！");
	TdBroadcastGameInfo();
}

void CGameControllerDefence::TdDoWincheck()
{
	if(!m_pTower)
		return;
	if(m_TdGameOverTick != -1)
		return;
	if(m_TdWarmup)
		return;

	if(m_pTower->GetHealth() <= 0)
	{
		TdEndRound();
		return;
	}

	if(TdIsWaveCleared())
	{
		if(TdEndWave())
		{
			GameServer()->SendChatLoc(-1, "game.victory", "所有波次被击败 — 胜利！");
			TdEndRound();
		}
		else if(m_TdWarmup <= 0)
			TdDoWarmup(10);
	}
}

void CGameControllerDefence::TdStartWave(int Wave)
{
	m_TdWave = Wave;
	m_TdWarmup = 0;
	m_TdZombStart = m_TdWave * 4 + m_RealPlayerNum * 2;
	if(m_TdWave % 6 == 0)
		m_TdZombStart += m_RealPlayerNum * 4;
	m_TdZombLeft = m_TdZombStart;
	mem_zero(m_TdZombie, sizeof(m_TdZombie));
	for(int i = 0; i < m_TdWave; i++)
	{
		int Type = TdRandZomb();
		m_TdZombie[Type] += maximum(1, (i / 10 + 1));
	}
	TdApplyDifficultyToZombieCounts();
	TdBroadcastGameInfo();
	GameServer()->SendChatLocF(-1, "game.wave_start", "===== 第 %d 波 ====", m_TdWave);

	if(m_TdWave >= 25 && m_TdWave % 5 == 0)
	{
		m_TdBossWave = true;
		TdTrySpawnSpiderBoss();
	}
}

void CGameControllerDefence::TdTrySpawnSpiderBoss()
{
	if(m_TdSpiderBossPending || m_TdWave % 5 != 0 || m_TdWave < 25)
		return;
	m_TdSpiderBossPending = true;
	if(m_pTower)
		m_pTower->SetHealth(minimum(m_pTower->GetHealth() + 100, TdGetDifficultyTowerMaxHealth()));
	// The actual CSpiderBoss entity will be created when the player gets a character
	const int Zombie0 = TdZombieFirstSlot(Config());
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(Server()->GetClientWorldID(i) != GameServer()->GetWorldID())
			continue;
		if(!Server()->ClientIngame(i))
			continue;
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || pP->IsEliminated() || !Server()->ClientIngame(i))
			continue;
		pP->InitZombie(ZOMB_SPIDER_BOSS);
	}
}

void CGameControllerDefence::TdCheckZombie()
{
	if(m_TdZombLeft <= 0)
		return;

	const int CurrentPop = TdCountZombiePopulation();
	const int ConcurrentCap = maximum(1, (int)TD_MAX_ACTIVE_ZOMBIES);
	if(CurrentPop >= ConcurrentCap)
		return;

	// pick a random type that still has remaining count
	int Type = -1;
	int PoolTotal = 0;
	for(int i = 0; i < NUM_TD_ZOMB; i++)
	{
		if(m_TdZombie[i] > 0)
		{
			PoolTotal += m_TdZombie[i];
			if(random_float() < (float)m_TdZombie[i] / PoolTotal)
				Type = i;
		}
	}
	if(Type < 0)
		return;

	// pick a slot
	const int Zombie0 = TdZombieFirstSlot(Config());
	const int WorldID = GameServer()->GetWorldID();
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(Server()->GetClientWorldID(i) != WorldID)
			continue;
		if(!Server()->ClientIngame(i))
			continue;
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || !pP->IsDummy() || !pP->IsEliminated() || pP->GetZomb() != ZOMB_NONE)
			continue;

		// Claim this slot
		m_TdPendingZomb = Type;
		m_TdZombie[Type]--;
		m_TdZombLeft--;
		pP->InitZombie(Type);
		pP->Respawn();
		break;
	}
}

int CGameControllerDefence::TdCountZombiePopulation() const
{
	const int Zombie0 = TdZombieFirstSlot(Config());
	const int WorldID = GameServer()->GetWorldID();
	int Count = 0;
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		if(!pP->IsDummy() || pP->GetZomb() == ZOMB_NONE)
			continue;
		if(Server()->GetClientWorldID(i) != WorldID)
			continue;
		Count++;
	}
	return Count;
}

void CGameControllerDefence::TdAddZombiePool(int ZombType, int Count)
{
	if(ZombType < 0 || ZombType >= NUM_TD_ZOMB)
		return;
	m_TdZombie[ZombType] += Count;
	m_TdZombLeft += Count;
	m_TdZombStart += Count;
}

int CGameControllerDefence::TdRandZomb()
{
	const float r = random_float();
	if(r < 0.20f) return ZOMB_ZABY;
	if(r < 0.35f) return ZOMB_ZOOMER;
	if(r < 0.50f) return ZOMB_ZOOKER;
	if(r < 0.60f) return ZOMB_ZAMER;
	if(r < 0.70f) return ZOMB_ZUNNER;
	if(r < 0.78f) return ZOMB_ZASTER;
	if(r < 0.85f) return ZOMB_ZOTTER;
	if(r < 0.90f) return ZOMB_ZENADE;
	if(r < 0.93f) return ZOMB_FLOMBIE;
	if(r < 0.96f) return ZOMB_ZINJA;
	if(r < 0.97f) return ZOMB_ZELE;
	if(r < 0.98f) return ZOMB_ZINVIS;
	if(r < 0.99f) return ZOMB_ZEATER;
	return ZOMB_ZSHIELD;
}

void CGameControllerDefence::TdSetWave(int Wave)
{
	if(m_TdGameOverTick != -1)
		return;
	m_TdWave = maximum(1, Wave);
	TdDoWarmup(3);
	TdBroadcastGameInfo();
}

void CGameControllerDefence::TdSetTowerHealth(int Health)
{
	if(m_pTower)
		m_pTower->SetHealth(Health);
}

void CGameControllerDefence::ConTdSetDifficulty(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pCtx = (CGameContext *)pUser;
	CGameControllerDefence *pCtrl = dynamic_cast<CGameControllerDefence *>(pCtx->m_pController);
	if(!pCtrl)
		return;
	const int D = pResult->GetInteger(0);
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "difficulty %s", pCtrl->TdSetDifficulty(D) ? "ok" : "failed (already started)");
	pCtx->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameControllerDefence::ConTdSetWave(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pCtx = (CGameContext *)pUser;
	auto *pCtrl = dynamic_cast<CGameControllerDefence *>(pCtx->m_pController);
	if(pCtrl)
		pCtrl->TdSetWave(pResult->GetInteger(0));
}

void CGameControllerDefence::ConTdSetTowerHealth(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pCtx = (CGameContext *)pUser;
	auto *pCtrl = dynamic_cast<CGameControllerDefence *>(pCtx->m_pController);
	if(pCtrl)
		pCtrl->TdSetTowerHealth(pResult->GetInteger(0));
}

void CGameControllerDefence::ConTdSkipWarmup(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pCtx = (CGameContext *)pUser;
	auto *pCtrl = dynamic_cast<CGameControllerDefence *>(pCtx->m_pController);
	if(pCtrl)
		pCtrl->TdSkipWarmup();
}

void CGameControllerDefence::RegisterTeeDefenseConsoleCommands(CGameContext *pCtx)
{
	pCtx->Console()->Register("td_set_wave", "i[wave]", CFGFLAG_SERVER, ConTdSetWave, pCtx, "Set next TeeDefense wave number");
	pCtx->Console()->Register("td_set_tower_health", "i[health]", CFGFLAG_SERVER, ConTdSetTowerHealth, pCtx, "Set main tower current health");
	pCtx->Console()->Register("td_set_difficulty", "i[0-2]", CFGFLAG_SERVER, ConTdSetDifficulty, pCtx, "Set difficulty: 0=easy 1=normal 2=hard (before wave 1)");
	pCtx->Console()->Register("td_skip_warmup", "", CFGFLAG_SERVER, ConTdSkipWarmup, pCtx, "Skip inter-wave warmup and start the next wave immediately");
}

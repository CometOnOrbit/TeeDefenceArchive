#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/server/ai/defence.h>
#include <game/server/botengine.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "defence_bot_manager.h"

static CCharacter *FindNearestZombie(CGameContext *pGame, CCharacter *pBot, int Zombie0)
{
	CCharacter *pBest = nullptr;
	float BestD2 = 1.0e12f;
	const vec2 BotPos = pBot->GetPos();

	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pZ = pGame->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE)
			continue;
		CCharacter *pChr = pZ->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;
		const vec2 Delta = pChr->GetPos() - BotPos;
		const float D2 = Delta.x * Delta.x + Delta.y * Delta.y;
		if(D2 < BestD2)
		{
			BestD2 = D2;
			pBest = pChr;
		}
	}
	return pBest;
}

void CDefenceBotManager::OnPreInit()
{
	mem_zero(m_apDefence, sizeof(m_apDefence));
	mem_zero(m_aLastInp, sizeof(m_aLastInp));
	mem_zero(m_aGoal, sizeof(m_aGoal));
	mem_zero(m_aGoalTick, sizeof(m_aGoalTick));
}

CDefenceBotManager::~CDefenceBotManager()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		ClearClient(i);
}

void CDefenceBotManager::ClearClient(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	delete m_apDefence[ClientID];
	m_apDefence[ClientID] = nullptr;
	mem_zero(&m_aLastInp[ClientID], sizeof(m_aLastInp[ClientID]));
	m_aGoal[ClientID] = vec2(0.0f, 0.0f);
	m_aGoalTick[ClientID] = 0;
}

void CDefenceBotManager::OnClientReset(int ClientID)
{
	ClearClient(ClientID);
}

void CDefenceBotManager::OnCharacterSpawn(CPlayer *pPlayer)
{
	if(!pPlayer || !pPlayer->IsDummy() || !GS())
		return;

	const int CID = pPlayer->GetCID();
	ClearClient(CID);
	CBotEngine *pBE = GS()->BotEngine();
	if(!pBE)
		return;

	CDefence *pDef = new CDefence(pBE);
	pDef->SetTeam(pPlayer->GetTeam());
	m_apDefence[CID] = pDef;
}

void CDefenceBotManager::TickPlayer(CPlayer *pPlayer)
{
	if(!GS() || !pPlayer || !pPlayer->IsDummy())
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return;

	const int CID = pPlayer->GetCID();
	CDefence *pDef = m_apDefence[CID];
	if(!pDef)
	{
		OnCharacterSpawn(pPlayer);
		pDef = m_apDefence[CID];
	}
	if(!pDef)
		return;

	const int Zombie0 = GS()->Config()->m_SvMaxClients;
	CCharacter *pEnemy = FindNearestZombie(GS(), pChr, Zombie0);

	vec2 TargetOff(0.0f, 0.0f);
	vec2 GoalPos = pDef->GetCenter();
	bool ShouldFire = false;

	if(pEnemy)
	{
		const vec2 ToEnemy = pEnemy->GetPos() - pChr->GetPos();
		const bool Follow = pDef->FollowPlayer(pChr, pEnemy);
		const bool Attack = pDef->AttackPlayer(pChr, pEnemy);

		if(Follow || Attack)
		{
			TargetOff = ToEnemy;
			GoalPos = pEnemy->GetPos();
		}
		else
			TargetOff = pDef->GetCenter() - pChr->GetPos();

		const bool HasLos = !GS()->Collision()->IntersectLine(pChr->GetPos(), pEnemy->GetPos(), nullptr, nullptr);
		if(Attack && HasLos && length(ToEnemy) < 640.0f)
			ShouldFire = true;
	}
	else
		TargetOff = pDef->GetCenter() - pChr->GetPos();

	CBotEngine *pBE = GS()->BotEngine();
	if(pBE && length(TargetOff) > 48.0f)
	{
		CBotEngine::CPath *pPath = &pBE->m_aPaths[CID];
		const int Now = Server()->Tick();
		if(distance(m_aGoal[CID], GoalPos) > 48.0f || Now - m_aGoalTick[CID] > Server()->TickSpeed() * 2)
		{
			pBE->GetPath(pChr->GetPos(), GoalPos, pPath);
			m_aGoal[CID] = GoalPos;
			m_aGoalTick[CID] = Now;
		}
		if(pPath->m_Size > 0)
		{
			vec2 Way;
			if(pBE->FarestPointOnEdge(pPath, pChr->GetPos(), &Way) >= 0)
				TargetOff = Way - pChr->GetPos();
			else
				TargetOff = pBE->NextPoint(pChr->GetPos(), GoalPos) - pChr->GetPos();
		}
	}

	CNetObj_PlayerInput Inp = m_aLastInp[CID];
	mem_zero(&Inp, sizeof(Inp));

	Inp.m_Direction = TargetOff.x > 28.f ? 1 : (TargetOff.x < -28.f ? -1 : 0);
	Inp.m_TargetX = (int)TargetOff.x;
	Inp.m_TargetY = (int)TargetOff.y;
	if(Inp.m_TargetX == 0 && Inp.m_TargetY == 0)
		Inp.m_TargetY = -1;

	if(ShouldFire)
	{
		if(pChr->WeaponAmmo(WEAPON_GUN) > 0)
		{
			Inp.m_WantedWeapon = WEAPON_GUN + 1;
			Inp.m_Fire = (Server()->Tick() % 14) < 7 ? 1 : 0;
		}
		else
		{
			Inp.m_WantedWeapon = WEAPON_HAMMER + 1;
			Inp.m_Fire = (Server()->Tick() % 18) < 9 ? 1 : 0;
		}
	}
	else if(pChr->WeaponAmmo(WEAPON_GUN) > 0)
		Inp.m_WantedWeapon = WEAPON_GUN + 1;

	if(pChr->IsGrounded())
	{
		const bool Stuck = absolute(pChr->GetVelocity().x) < 0.4f && absolute(TargetOff.x) > 40.0f;
		const bool NeedJump = TargetOff.y < -48.0f || Stuck;
		if(NeedJump)
			Inp.m_Jump = 1;
	}

	m_aLastInp[CID] = Inp;
	pPlayer->OnPredictedInput(&Inp);
	pPlayer->OnDirectInput(&Inp);
}

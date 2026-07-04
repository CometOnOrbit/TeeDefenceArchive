#include "base_ai.h"
#include <game/server/entities/character_bot_ai.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/tools/path_finder.h>
#include <game/collision.h>
#include <engine/server.h>
#include <cmath>

CBaseAI::CBaseAI(CCharacterBotAI *pCharacter)
	: m_pCharacter(pCharacter)
{
	m_ClientID = pCharacter->GetCID();
	m_SpawnPoint = pCharacter->GetPos();
	m_Target.Init(pCharacter);
}

CGameContext *CBaseAI::GS() const
{
	return m_pCharacter->GameServer();
}

IServer *CBaseAI::Server() const
{
	return m_pCharacter->Server();
}

CPathFinder *CBaseAI::PathFinder() const
{
	CGameContext *pGS = GS();
	if(!pGS || !pGS->TW() || !pGS->TW()->GetMMOManager())
		return nullptr;
	return pGS->TW()->GetMMOManager()->m_pPathFinder;
}

CPlayer *CBaseAI::GetPlayer(int ClientID, bool CheckCharacter, bool CheckAlive)
{
	CGameContext *pGS = GS();
	if(!pGS) return nullptr;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS) return nullptr;
	CPlayer *pPlayer = pGS->m_apPlayers[ClientID];
	if(!pPlayer) return nullptr;
	if(pPlayer->m_pMMOBotData) return nullptr;
	if(CheckCharacter && !pPlayer->GetCharacter()) return nullptr;
	if(CheckAlive && pPlayer->GetCharacter() && !pPlayer->GetCharacter()->IsAlive()) return nullptr;
	return pPlayer;
}

int CBaseAI::SearchNearestPlayer(float Radius, bool SkipBots)
{
	CGameContext *pGS = GS();
	if(!pGS) return -1;

	int NearestCID = -1;
	float NearestDist = Radius;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pTarget = pGS->m_apPlayers[i];
		if(!pTarget || !pTarget->GetCharacter()) continue;
		if(pTarget->GetCID() == m_ClientID) continue;
		if(SkipBots && pTarget->m_pMMOBotData) continue;
		if(pTarget->GetTeam() == TEAM_SPECTATORS) continue;
		if(!pTarget->GetCharacter()->IsAlive()) continue;

		float d = distance(m_pCharacter->GetPos(), pTarget->GetCharacter()->GetPos());
		if(d < NearestDist)
		{
			NearestDist = d;
			NearestCID = i;
		}
	}
	return NearestCID;
}

CPlayer *CBaseAI::SearchPlayerCondition(float Radius, const std::function<bool(CPlayer*)> &Condition)
{
	CGameContext *pGS = GS();
	if(!pGS || !Condition) return nullptr;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = pGS->m_apPlayers[i];
		if(!pPlayer || !pPlayer->GetCharacter()) continue;
		if(pPlayer->GetCID() == m_ClientID) continue;
		if(pPlayer->m_pMMOBotData) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		if(!pPlayer->GetCharacter()->IsAlive()) continue;

		float d = distance(m_pCharacter->GetPos(), pPlayer->GetCharacter()->GetPos());
		if(d > Radius) continue;

		if(Condition(pPlayer))
			return pPlayer;
	}
	return nullptr;
}

int CBaseAI::SearchPlayerByCondition(float Radius, bool (*Condition)(CPlayer *))
{
	CGameContext *pGS = GS();
	if(!pGS || !Condition) return -1;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = pGS->m_apPlayers[i];
		if(!pPlayer || !pPlayer->GetCharacter()) continue;
		if(pPlayer->GetCID() == m_ClientID) continue;
		if(pPlayer->m_pMMOBotData) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		if(!pPlayer->GetCharacter()->IsAlive()) continue;

		float d = distance(m_pCharacter->GetPos(), pPlayer->GetCharacter()->GetPos());
		if(d > Radius) continue;

		if(Condition(pPlayer))
			return i;
	}
	return -1;
}

void CBaseAI::MoveToward(vec2 TargetPos)
{
	vec2 MyPos = m_pCharacter->GetPos();
	vec2 Dir = TargetPos - MyPos;
	float Dist = length(Dir);
	if(Dist < 1.f) return;

	Dir /= Dist;

	CNetObj_PlayerInput Input;
	mem_zero(&Input, sizeof(Input));

	Input.m_Direction = Dir.x > 0 ? 1 : -1;
	Input.m_TargetX = (int)(Dir.x * 100.f);
	Input.m_TargetY = (int)(Dir.y * 100.f);

	if(Dir.y < -60.f && MyPos.y - TargetPos.y > 64.f)
		Input.m_Jump = 1;

	m_pCharacter->SetBotInput(Input);
}

void CBaseAI::FollowPath(const vec2 &TargetPos, float AttackDist, int *pOutFire)
{
	if(pOutFire) *pOutFire = 0;

	vec2 MyPos = m_pCharacter->GetPos();
	float Dist = distance(MyPos, TargetPos);

	// In attack range - just aim and fire
	if(Dist < AttackDist)
	{
		vec2 Dir = TargetPos - MyPos;
		if(Dist > 0.1f) Dir /= Dist;

		CNetObj_PlayerInput Input;
		mem_zero(&Input, sizeof(Input));
		Input.m_TargetX = (int)(Dir.x * 100.f);
		Input.m_TargetY = (int)(Dir.y * 100.f);

		if(pOutFire) *pOutFire = 1;

		m_pCharacter->SetBotInput(Input);
		return;
	}

	// Try to use pathfinding
	if(m_PathHandle.TryGetPath() && !m_PathHandle.vPath.empty())
	{
		// Follow the path
		vec2 NextNode = m_PathHandle.vPath.front();
		float NodeDist = distance(MyPos, NextNode);

		if(NodeDist < 32.f)
		{
			m_PathHandle.vPath.erase(m_PathHandle.vPath.begin());
			if(m_PathHandle.vPath.empty())
			{
				MoveToward(TargetPos);
				return;
			}
			NextNode = m_PathHandle.vPath.front();
		}

		MoveToward(NextNode);
	}
	else
	{
		// Request new path or move directly
		CPathFinder *pFinder = PathFinder();
		if(pFinder)
		{
			m_PathHandle.Reset();
			pFinder->RequestPath(m_PathHandle, MyPos, TargetPos);
		}
		MoveToward(TargetPos);
	}
}

void CBaseAI::SmartMove(vec2 TargetPos)
{
	vec2 MyPos = m_pCharacter->GetPos();
	vec2 Dir = TargetPos - MyPos;
	float Dist = length(Dir);
	if(Dist < 1.f) return;
	Dir /= Dist;

	CCollision *pColl = GS()->Collision();

	CNetObj_PlayerInput Input;
	mem_zero(&Input, sizeof(Input));
	Input.m_Direction = Dir.x > 0 ? 1 : -1;
	Input.m_TargetX = (int)(Dir.x * 100.f);
	Input.m_TargetY = (int)(Dir.y * 100.f);

	// Jump if there's a wall in front
	vec2 CheckPos = MyPos + vec2(Input.m_Direction * 48.f, 0.f);
	if(pColl && pColl->CheckPoint(CheckPos))
		Input.m_Jump = 1;

	// Jump toward target if target is above
	if(Dir.y < -60.f)
		Input.m_Jump = 1;

	// Use hook if far from target (vertical/horizontal)
	if(Dist > 400.f && abs(Dir.y) > 100.f)
		Input.m_Hook = 1;

	m_pCharacter->SetBotInput(Input);
}

void CBaseAI::SetWeaponIfAvailable(int Weapon)
{
	if(!m_pCharacter) return;
	if(Weapon < 0 || Weapon >= NUM_WEAPONS) return;
	// Just attempt to switch - CCharacter::SetWeapon handles validation
	m_pCharacter->SetWeapon(Weapon);
}

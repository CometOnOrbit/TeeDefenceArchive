#include "quest_mob_ai.h"
#include <game/server/entities/character_bot_ai.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CQuestMobAI::CQuestMobAI(CCharacterBotAI *pCharacter, SMMOQuestMobInfo *pQuestMobInfo)
	: CBaseAI(pCharacter), m_pQuestMobInfo(pQuestMobInfo) {}

bool CQuestMobAI::CanDamage(CPlayer *pFrom)
{
	if(!pFrom) return false;

	// Only active-for-client players can damage this mob
	if(!pFrom->m_pMMOBotData && m_pQuestMobInfo->m_ActiveForClient[pFrom->GetCID()])
		return true;

	return false;
}

void CQuestMobAI::OnSpawn()
{
	m_pCharacter->SetEmote(EMOTE_ANGRY, 5);
}

void CQuestMobAI::OnRewardPlayer(CPlayer *pPlayer) const
{
	if(!pPlayer) return;
	int ClientID = pPlayer->GetCID();

	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_pQuestMobInfo->m_ActiveForClient[ClientID] = false;
}

void CQuestMobAI::OnDie(int Killer, int Weapon)
{
	// Mark for cleanup if no active clients remain
	if(!m_pQuestMobInfo->IsActiveForAny())
	{
		// Mark for removal — handled by gamecontroller
	}
}

void CQuestMobAI::OnTargetRules(float Radius)
{
	CGameContext *pGS = GS();
	if(!pGS) return;

	int TargetCID = m_Target.GetCID();
	CPlayer *pTarget = (TargetCID >= 0) ? pGS->m_apPlayers[TargetCID] : nullptr;
	int TargetHP = (pTarget && pTarget->GetCharacter()) ? pTarget->GetCharacter()->GetHealth() : 99999;

	// Manual loop instead of SearchPlayerByCondition with lambda capture
	int BestCID = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pCandidate = pGS->m_apPlayers[i];
		if(!pCandidate || !pCandidate->GetCharacter() || !pCandidate->GetCharacter()->IsAlive())
			continue;
		if(pCandidate->GetCID() == m_ClientID)
			continue;
		if(!m_pQuestMobInfo->m_ActiveForClient[i])
			continue;

		float d = distance(m_pCharacter->GetPos(), pCandidate->GetCharacter()->GetPos());
		if(d > Radius)
			continue;

		// Prefer targets with lower HP
		int CandidateHP = pCandidate->GetCharacter()->GetHealth();
		if(CandidateHP < TargetHP)
		{
			BestCID = i;
			TargetHP = CandidateHP;
		}
	}

	if(BestCID >= 0)
		m_Target.Set(BestCID, 100);
}

void CQuestMobAI::Process()
{
	if(!m_pCharacter || !m_pCharacter->IsAlive()) return;

	CGameContext *pGS = GS();
	if(!pGS) return;

	float DistToSpawn = distance(m_SpawnPoint, m_pCharacter->GetPos());

	// Too far from spawn — return
	if(DistToSpawn > 800.0f)
	{
		MoveToward(m_SpawnPoint);
		return;
	}

	// Far from spawn and no target — return to spawn, reset target
	if(DistToSpawn > 400.0f && m_Target.IsEmpty())
	{
		MoveToward(m_SpawnPoint);
		return;
	}

	// Update target
	OnTargetRules(800.0f);

	int TargetCID = m_Target.GetCID();
	CPlayer *pTarget = (TargetCID >= 0) ? pGS->m_apPlayers[TargetCID] : nullptr;
	CCharacter *pTargetChr = pTarget ? pTarget->GetCharacter() : nullptr;

	if(pTargetChr && pTargetChr->IsAlive())
	{
		vec2 MyPos = m_pCharacter->GetPos();
		vec2 TargetPos = pTargetChr->GetPos();
		float Dist = distance(MyPos, TargetPos);

		if(Dist < 150.f)
		{
			// Attack
			vec2 Dir = TargetPos - MyPos;
			if(Dist > 0.1f) Dir /= Dist;

			CNetObj_PlayerInput Input;
			mem_zero(&Input, sizeof(Input));
			Input.m_TargetX = (int)(Dir.x * 100.f);
			Input.m_TargetY = (int)(Dir.y * 100.f);
			Input.m_Fire = 1;
			m_pCharacter->SetBotInput(Input);
		}
		else
		{
			FollowPath(TargetPos, 150.f);
		}
	}
	else
	{
		m_Target.Reset();

		// Return to spawn if idle
		if(DistToSpawn > 128.0f)
			MoveToward(m_SpawnPoint);
		else
		{
			CNetObj_PlayerInput Input;
			mem_zero(&Input, sizeof(Input));
			m_pCharacter->SetBotInput(Input);
		}
	}
}

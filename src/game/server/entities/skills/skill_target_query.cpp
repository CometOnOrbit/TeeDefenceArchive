#include "skill_target_query.h"

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

bool SkillTargetIsHostile(CGameContext *pGS, CPlayer *pCaster, CCharacter *pTarget)
{
	if(!pGS || !pCaster || !pTarget || !pTarget->IsAlive())
		return false;

	CPlayer *pTargetPl = pTarget->GetPlayer();
	if(!pTargetPl || pTargetPl == pCaster)
		return false;
	if(!pTargetPl->IsDummy() && pTargetPl->GetTeam() == pCaster->GetTeam())
		return false;
	return true;
}

bool SkillTargetIsAllyCandidate(CPlayer *pAlly, bool PlayersOnly)
{
	if(!pAlly || !pAlly->GetCharacter() || !pAlly->GetCharacter()->IsAlive())
		return false;
	if(PlayersOnly && pAlly->IsDummy())
		return false;
	return true;
}

bool MobTargetIsHostile(CPlayer *pMob, CCharacter *pTarget)
{
	if(!pMob || !pTarget || !pTarget->IsAlive())
		return false;

	CPlayer *pTargetPl = pTarget->GetPlayer();
	if(!pTargetPl || pTargetPl == pMob)
		return false;
	if(pTargetPl->m_pMMOBotData)
		return false;
	if(pTargetPl->GetTeam() == TEAM_SPECTATORS)
		return false;
	return true;
}

CCharacter *FindNearestSkillHostile(CGameContext *pGS, CPlayer *pCaster, vec2 From, float MaxDist,
	vec2 AimDir, float MinDot, CCollision *pCollForLos)
{
	if(!pGS || !pCaster || MaxDist <= 0.f)
		return nullptr;

	const float MaxDistSq = MaxDist * MaxDist;
	const bool UseCone = length(AimDir) > 0.01f && MinDot > -1.f;
	if(UseCone)
		AimDir = normalize(AimDir);

	CCharacter *pBest = nullptr;
	float BestDistSq = MaxDistSq;

	for(CGameWorld::TypeRange r = pGS->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!SkillTargetIsHostile(pGS, pCaster, pChar))
			continue;

		const vec2 TargetPos = pChar->GetPos();
		const vec2 Delta = TargetPos - From;
		const float DistSq = dot(Delta, Delta);
		if(DistSq > BestDistSq)
			continue;

		if(UseCone)
		{
			const vec2 ToTarget = normalize(Delta);
			if(dot(AimDir, ToTarget) <= MinDot)
				continue;
		}

		if(pCollForLos)
		{
			vec2 HitPos;
			vec2 ColPos;
			if(pCollForLos->FastIntersectLine(From, TargetPos, &HitPos, &ColPos))
				continue;
		}

		BestDistSq = DistSq;
		pBest = pChar;
	}

	return pBest;
}

CCharacter *FindNearestMobHostile(CGameContext *pGS, CPlayer *pMob, vec2 From, float MaxDist)
{
	if(!pGS || !pMob || MaxDist <= 0.f)
		return nullptr;

	const float MaxDistSq = MaxDist * MaxDist;
	CCharacter *pBest = nullptr;
	float BestDistSq = MaxDistSq;

	for(CGameWorld::TypeRange r = pGS->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MobTargetIsHostile(pMob, pChar))
			continue;

		const float DistSq = dot(pChar->GetPos() - From, pChar->GetPos() - From);
		if(DistSq > BestDistSq)
			continue;

		BestDistSq = DistSq;
		pBest = pChar;
	}

	return pBest;
}

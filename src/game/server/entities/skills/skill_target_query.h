#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_TARGET_QUERY_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_TARGET_QUERY_H

#include <base/vmath.h>
#include <game/server/entities/character.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

class CCollision;
class CGameContext;

// Same hostile filter as legacy skill_manager MAX_CLIENTS loops.
bool SkillTargetIsHostile(CGameContext *pGS, CPlayer *pCaster, CCharacter *pTarget);

// Mob AI: valid player target for a hostile mob (not other bots).
bool MobTargetIsHostile(CPlayer *pMob, CCharacter *pTarget);

// Ally in AoE: alive character; PlayersOnly skips dummy/mob clients.
bool SkillTargetIsAllyCandidate(CPlayer *pAlly, bool PlayersOnly);

template<typename Fn>
void ForEachCharacterInRadius(CGameWorld *pWorld, vec2 Pos, float Radius, Fn &&Callback)
{
	if(!pWorld || Radius <= 0.f)
		return;

	const float RadiusSq = Radius * Radius;
	for(CGameWorld::TypeRange r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!pChar || !pChar->IsAlive())
			continue;
		const vec2 Delta = pChar->GetPos() - Pos;
		if(dot(Delta, Delta) > RadiusSq)
			continue;
		if(!Callback(pChar))
			break;
	}
}

template<typename Fn>
void ForEachHostileInRadius(CGameWorld *pWorld, CGameContext *pGS, CPlayer *pCaster, vec2 Pos, float Radius, Fn &&Callback)
{
	if(!pWorld || !pGS || !pCaster)
		return;

	ForEachCharacterInRadius(pWorld, Pos, Radius, [&](CCharacter *pChar) {
		if(!SkillTargetIsHostile(pGS, pCaster, pChar))
			return true;
		return Callback(pChar);
	});
}

template<typename Fn>
void ForEachAllyInRadius(CGameWorld *pWorld, vec2 Pos, float Radius, bool PlayersOnly, Fn &&Callback)
{
	if(!pWorld)
		return;

	ForEachCharacterInRadius(pWorld, Pos, Radius, [&](CCharacter *pChar) {
		CPlayer *pAlly = pChar->GetPlayer();
		if(!SkillTargetIsAllyCandidate(pAlly, PlayersOnly))
			return true;
		return Callback(pChar, pAlly);
	});
}

template<typename Fn>
void ForEachMobHostileInRadius(CGameWorld *pWorld, CPlayer *pMob, vec2 Pos, float Radius, Fn &&Callback)
{
	if(!pWorld || !pMob)
		return;

	ForEachCharacterInRadius(pWorld, Pos, Radius, [&](CCharacter *pChar) {
		if(!MobTargetIsHostile(pMob, pChar))
			return true;
		return Callback(pChar);
	});
}

// Nearest hostile within MaxDist from From (optional aim cone + line-of-sight).
CCharacter *FindNearestSkillHostile(CGameContext *pGS, CPlayer *pCaster, vec2 From, float MaxDist,
	vec2 AimDir = vec2(0, 0), float MinDot = -1.f, CCollision *pCollForLos = nullptr);

CCharacter *FindNearestMobHostile(CGameContext *pGS, CPlayer *pMob, vec2 From, float MaxDist);

inline int SkillHostileCID(CCharacter *pTarget)
{
	return pTarget && pTarget->GetPlayer() ? pTarget->GetPlayer()->GetCID() : -1;
}

#endif

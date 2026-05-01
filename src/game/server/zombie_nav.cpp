// Comet: AI wrote this AI
#include <base/math.h>

#include <game/collision.h>
#include <game/gamecore.h>

#include "entities/character.h"
#include "gamecontext.h"
#include "player.h"
#include "botengine.h"

bool ZombieNavLineBlocked(CCollision *pCol, vec2 From, vec2 To)
{
	const vec2 Phys(CCharacterCore::PHYS_SIZE, CCharacterCore::PHYS_SIZE);
	const float Len = distance(From, To);
	if(Len < 20.0f)
		return false;
	int Samples = (int)(Len / 16.0f) + 2;
	Samples = clamp(Samples, 5, 64);
	for(int i = 1; i < Samples - 1; i++)
	{
		const float t = (float)i / (float)(Samples - 1);
		const vec2 P = mix(From, To, t);
		if(pCol->TestBox(P, Phys, CCollision::COLFLAG_SOLID))
			return true;
		if(pCol->TestBox(P, Phys, CCollision::COLFLAG_DEATH))
			return true;
	}
	return false;
}

void ZombieNavClear(CPlayer *pP)
{
	pP->m_ZombNavLen = 0;
	pP->m_ZombNavIndex = 0;
	pP->m_ZombNavNextRebuildTick = 0;
	pP->m_ZombNavCachedGoalTX = -1;
	pP->m_ZombNavCachedGoalTY = -1;
}

void ZombieNavUpdateWaypoint(CGameContext *pGame, CCollision *pCol, vec2 ZombPos, vec2 GoalWorld, int CurTick, int TickSpeed, CPlayer *pP, vec2 *pFollowWorld, vec2 *pAimHintWorld)
{
	(void)pCol;
	(void)CurTick;
	(void)TickSpeed;
	(void)pP;

	vec2 Follow = GoalWorld;
	if(pGame && pGame->BotEngine())
		Follow = pGame->BotEngine()->NextPoint(ZombPos, GoalWorld);

	*pFollowWorld = Follow;
	*pAimHintWorld = Follow;
}

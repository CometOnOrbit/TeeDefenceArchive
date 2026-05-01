// Comet: AI wrote this AI
/* TeeDefense — zombie steering; uses CBotEngine::NextPoint (Teeworlds-Alchemist style). */

#ifndef GAME_SERVER_ZOMBIE_NAV_H
#define GAME_SERVER_ZOMBIE_NAV_H

#include <base/vmath.h>

class CCollision;
class CGameContext;
class CPlayer;

enum
{
	ZOMB_NAV_PATH_CAP = 384,
};

bool ZombieNavLineBlocked(CCollision *pCol, vec2 From, vec2 To);
void ZombieNavClear(CPlayer *pP);
void ZombieNavUpdateWaypoint(CGameContext *pGame, CCollision *pCol, vec2 ZombPos, vec2 GoalWorld, int CurTick, int TickSpeed, CPlayer *pP, vec2 *pFollowWorld, vec2 *pAimHintWorld);

#endif

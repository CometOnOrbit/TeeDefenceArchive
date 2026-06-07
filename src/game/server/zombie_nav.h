// Comet: AI wrote this AI
/* TeeDefense — grid A* pathfinding for zombies marching to the tower. */

#ifndef GAME_SERVER_ZOMBIE_NAV_H
#define GAME_SERVER_ZOMBIE_NAV_H

#include <base/vmath.h>

class CCollision;
class CGameContext;
class CPlayer;

bool ZombieNavLineBlocked(CCollision *pCol, vec2 From, vec2 To);
vec2 ZombieNavResolveGoal(CGameContext *pGame, vec2 GoalWorld);
void ZombieNavClear(CPlayer *pP);
void ZombieNavFollow(CGameContext *pGame, CPlayer *pP, vec2 AgentPos, vec2 GoalWorld, int CurTick, int TickSpeed, vec2 *pFollowWorld, vec2 *pAimHintWorld);

#endif

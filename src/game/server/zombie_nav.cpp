// Comet: AI wrote this AI
#include <base/math.h>

#include <game/collision.h>
#include <game/gamecore.h>

#include "entities/character.h"
#include "gamecontext.h"
#include "player.h"

namespace
{
constexpr int ZOMB_ASTAR_MAX_NODES = 8192;

static int WorldToTileX(float Wx, int MapW)
{
	return clamp((int)(Wx / 32.0f), 0, MapW - 1);
}

static int WorldToTileY(float Wy, int MapH)
{
	return clamp((int)(Wy / 32.0f), 0, MapH - 1);
}

static vec2 TileCenter(int Tx, int Ty)
{
	return vec2((float)Tx * 32.0f + 16.0f, (float)Ty * 32.0f + 16.0f);
}

static bool ZombieNavTileWalkable(CCollision *pCol, int Tx, int Ty, int MapW, int MapH)
{
	if(Tx < 0 || Ty < 0 || Tx >= MapW || Ty >= MapH)
		return false;

	const float Cx = (float)Tx * 32.0f + 16.0f;
	const float Cy = (float)Ty * 32.0f + 16.0f;
	if(pCol->CheckPoint(Cx, Cy, CCollision::COLFLAG_DEATH))
		return false;
	if(pCol->CheckPoint(Cx, Cy))
		return false;

	for(int dy = 8; dy <= 40; dy += 4)
	{
		if(pCol->CheckPoint(Cx, Cy + (float)dy))
			return true;
	}
	return false;
}

static int ZombieNavHeuristic(int X0, int Y0, int X1, int Y1)
{
	return (absolute(X1 - X0) + absolute(Y1 - Y0)) * 10;
}

struct SAStarNode
{
	short m_X;
	short m_Y;
	int m_G;
	int m_F;
	short m_Parent;
	bool m_Open;
	bool m_Closed;
};

static int ZombieNavFindNearestWalkable(CCollision *pCol, int Tx, int Ty, int MapW, int MapH, int Radius, int *pOutX, int *pOutY)
{
	if(ZombieNavTileWalkable(pCol, Tx, Ty, MapW, MapH))
	{
		*pOutX = Tx;
		*pOutY = Ty;
		return 0;
	}
	int BestDist = 1 << 30;
	int BestX = Tx, BestY = Ty;
	bool Found = false;
	for(int r = 1; r <= Radius; r++)
	{
		for(int dy = -r; dy <= r; dy++)
		{
			for(int dx = -r; dx <= r; dx++)
			{
				if(absolute(dx) != r && absolute(dy) != r)
					continue;
				const int Nx = Tx + dx, Ny = Ty + dy;
				if(!ZombieNavTileWalkable(pCol, Nx, Ny, MapW, MapH))
					continue;
				const int D = absolute(dx) + absolute(dy);
				if(D < BestDist)
				{
					BestDist = D;
					BestX = Nx;
					BestY = Ny;
					Found = true;
				}
			}
		}
		if(Found)
			break;
	}
	if(!Found)
		return -1;
	*pOutX = BestX;
	*pOutY = BestY;
	return 0;
}
} // namespace

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

bool ZombieNavRebuild(CGameContext *pGame, CPlayer *pP, vec2 ZombPos, vec2 GoalWorld)
{
	if(!pGame || !pP)
		return false;
	CCollision *pCol = pGame->Collision();
	if(!pCol)
		return false;

	const int MapW = pCol->GetWidth();
	const int MapH = pCol->GetHeight();
	if(MapW <= 0 || MapH <= 0)
		return false;

	int Sx = WorldToTileX(ZombPos.x, MapW);
	int Sy = WorldToTileY(ZombPos.y, MapH);
	int Gx = WorldToTileX(GoalWorld.x, MapW);
	int Gy = WorldToTileY(GoalWorld.y, MapH);

	if(ZombieNavFindNearestWalkable(pCol, Sx, Sy, MapW, MapH, 6, &Sx, &Sy) < 0)
		return false;
	if(ZombieNavFindNearestWalkable(pCol, Gx, Gy, MapW, MapH, 10, &Gx, &Gy) < 0)
		return false;

	if(Sx == Gx && Sy == Gy)
	{
		pP->m_aZombNavTx[0] = (short)Gx;
		pP->m_aZombNavTy[0] = (short)Gy;
		pP->m_ZombNavLen = 1;
		pP->m_ZombNavIndex = 0;
		pP->m_ZombNavCachedGoalTX = (short)Gx;
		pP->m_ZombNavCachedGoalTY = (short)Gy;
		return true;
	}

	SAStarNode aNodes[ZOMB_ASTAR_MAX_NODES];
	mem_zero(aNodes, sizeof(aNodes));
	int NodeCount = 0;

	auto NodeIdx = [&](int X, int Y) -> int {
		for(int i = 0; i < NodeCount; i++)
			if(aNodes[i].m_X == X && aNodes[i].m_Y == Y)
				return i;
		return -1;
	};

	auto PushNode = [&](int X, int Y, int G, int F, int Parent) -> int {
		if(NodeCount >= ZOMB_ASTAR_MAX_NODES)
			return -1;
		const int Idx = NodeCount++;
		aNodes[Idx].m_X = (short)X;
		aNodes[Idx].m_Y = (short)Y;
		aNodes[Idx].m_G = G;
		aNodes[Idx].m_F = F;
		aNodes[Idx].m_Parent = (short)Parent;
		aNodes[Idx].m_Open = true;
		return Idx;
	};

	const int StartIdx = PushNode(Sx, Sy, 0, ZombieNavHeuristic(Sx, Sy, Gx, Gy), -1);
	if(StartIdx < 0)
		return false;

	static const int aDX[] = {1, -1, 0, 0};
	static const int aDY[] = {0, 0, 1, -1};

	int GoalNode = -1;
	while(true)
	{
		int Best = -1;
		int BestF = 1 << 30;
		for(int i = 0; i < NodeCount; i++)
		{
			if(!aNodes[i].m_Open || aNodes[i].m_Closed)
				continue;
			if(aNodes[i].m_F < BestF)
			{
				BestF = aNodes[i].m_F;
				Best = i;
			}
		}
		if(Best < 0)
			break;

		aNodes[Best].m_Open = false;
		aNodes[Best].m_Closed = true;

		if(aNodes[Best].m_X == Gx && aNodes[Best].m_Y == Gy)
		{
			GoalNode = Best;
			break;
		}

		for(int d = 0; d < 4; d++)
		{
			const int Nx = aNodes[Best].m_X + aDX[d];
			const int Ny = aNodes[Best].m_Y + aDY[d];
			if(!ZombieNavTileWalkable(pCol, Nx, Ny, MapW, MapH))
				continue;

			const int Ng = aNodes[Best].m_G + 10;
			const int Nf = Ng + ZombieNavHeuristic(Nx, Ny, Gx, Gy);
			int Exist = NodeIdx(Nx, Ny);
			if(Exist >= 0)
			{
				if(aNodes[Exist].m_Closed)
					continue;
				if(Ng < aNodes[Exist].m_G)
				{
					aNodes[Exist].m_G = Ng;
					aNodes[Exist].m_F = Nf;
					aNodes[Exist].m_Parent = (short)Best;
					aNodes[Exist].m_Open = true;
				}
			}
			else
			{
				if(PushNode(Nx, Ny, Ng, Nf, Best) < 0)
					return false;
			}
		}
	}

	if(GoalNode < 0)
		return false;

	short aRevX[ZOMB_NAV_PATH_CAP];
	short aRevY[ZOMB_NAV_PATH_CAP];
	int RevLen = 0;
	for(int Cur = GoalNode; Cur >= 0 && RevLen < ZOMB_NAV_PATH_CAP; Cur = aNodes[Cur].m_Parent)
	{
		aRevX[RevLen] = aNodes[Cur].m_X;
		aRevY[RevLen] = aNodes[Cur].m_Y;
		RevLen++;
	}

	pP->m_ZombNavLen = 0;
	for(int i = RevLen - 1; i >= 0 && pP->m_ZombNavLen < ZOMB_NAV_PATH_CAP; i--)
	{
		pP->m_aZombNavTx[pP->m_ZombNavLen] = aRevX[i];
		pP->m_aZombNavTy[pP->m_ZombNavLen] = aRevY[i];
		pP->m_ZombNavLen++;
	}

	pP->m_ZombNavIndex = 0;
	while(pP->m_ZombNavIndex < pP->m_ZombNavLen - 1)
	{
		const vec2 Way = TileCenter(pP->m_aZombNavTx[pP->m_ZombNavIndex], pP->m_aZombNavTy[pP->m_ZombNavIndex]);
		if(distance(ZombPos, Way) > 40.0f)
			break;
		pP->m_ZombNavIndex++;
	}

	pP->m_ZombNavCachedGoalTX = (short)Gx;
	pP->m_ZombNavCachedGoalTY = (short)Gy;
	return pP->m_ZombNavLen > 0;
}

static bool ZombieNavPathStale(CPlayer *pP, vec2 ZombPos, vec2 GoalWorld)
{
	if(pP->m_ZombNavLen <= 0)
		return true;

	const vec2 ToGoal = GoalWorld - ZombPos;
	if(length(ToGoal) < 48.0f)
		return false;

	pP->m_ZombNavIndex = clamp(pP->m_ZombNavIndex, 0, pP->m_ZombNavLen - 1);
	const vec2 Way = TileCenter(pP->m_aZombNavTx[pP->m_ZombNavIndex], pP->m_aZombNavTy[pP->m_ZombNavIndex]);
	const vec2 ToWay = Way - ZombPos;

	if(length(ToWay) < 8.0f)
		return false;
	if(dot(ToGoal, ToWay) < 0.0f)
		return true;
	if(distance(ZombPos, Way) > 160.0f)
		return true;

	float BestDist = 1.0e12f;
	for(int i = pP->m_ZombNavIndex; i < pP->m_ZombNavLen; i++)
	{
		const vec2 P = TileCenter(pP->m_aZombNavTx[i], pP->m_aZombNavTy[i]);
		BestDist = minimum(BestDist, distance(ZombPos, P));
	}
	return BestDist > 192.0f;
}

void ZombieNavUpdateWaypoint(CGameContext *pGame, CCollision *pCol, vec2 ZombPos, vec2 GoalWorld, int CurTick, int TickSpeed, CPlayer *pP, vec2 *pFollowWorld, vec2 *pAimHintWorld)
{
	(void)pCol;
	if(!pP || !pFollowWorld || !pAimHintWorld)
		return;

	const int GoalTx = WorldToTileX(GoalWorld.x, pGame->Collision()->GetWidth());
	const int GoalTy = WorldToTileY(GoalWorld.y, pGame->Collision()->GetHeight());

	const bool GoalMoved = pP->m_ZombNavCachedGoalTX != GoalTx || pP->m_ZombNavCachedGoalTY != GoalTy;
	const bool NeedRebuild = pP->m_ZombNavLen <= 0 || GoalMoved || ZombieNavPathStale(pP, ZombPos, GoalWorld) ||
		CurTick >= pP->m_ZombNavNextRebuildTick;

	if(NeedRebuild && pGame)
	{
		if(!ZombieNavRebuild(pGame, pP, ZombPos, GoalWorld))
			ZombieNavClear(pP);
		pP->m_ZombNavNextRebuildTick = CurTick + TickSpeed;
	}

	vec2 Follow = GoalWorld;
	if(pP->m_ZombNavLen > 0)
	{
		pP->m_ZombNavIndex = clamp(pP->m_ZombNavIndex, 0, pP->m_ZombNavLen - 1);
		while(pP->m_ZombNavIndex < pP->m_ZombNavLen - 1)
		{
			const vec2 Way = TileCenter(pP->m_aZombNavTx[pP->m_ZombNavIndex], pP->m_aZombNavTy[pP->m_ZombNavIndex]);
			if(distance(ZombPos, Way) > 40.0f)
				break;
			pP->m_ZombNavIndex++;
		}
		Follow = TileCenter(pP->m_aZombNavTx[pP->m_ZombNavIndex], pP->m_aZombNavTy[pP->m_ZombNavIndex]);
	}

	const vec2 ToGoal = GoalWorld - ZombPos;
	const vec2 ToFollow = Follow - ZombPos;
	if(length(ToGoal) > 1.0f && length(ToFollow) > 1.0f && dot(ToGoal, ToFollow) < 0.0f)
		Follow = GoalWorld;

	*pFollowWorld = Follow;
	*pAimHintWorld = GoalWorld;
}

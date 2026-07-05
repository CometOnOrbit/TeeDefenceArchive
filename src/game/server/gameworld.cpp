/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <generated/server_data.h>

#include <engine/shared/config.h>
#include <engine/shared/world_detail.h>
#include <generated/protocol.h>

#include "entities/character.h"
#include "entities/growingexplosion.h"
#include "entities/plasma.h"
#include "entities/skills/skill_vfx_common.h"
#include "entity.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "player.h"

#include <game/collision.h>

#include <base/math.h>
#include <algorithm>

//////////////////////////////////////////////////
// game world
//////////////////////////////////////////////////
CGameWorld::CGameWorld()
{
	m_pGameServer = 0x0;
	m_pConfig = 0x0;
	m_pServer = 0x0;
	m_NumMarkedBotsActive = 0;
	mem_zero(m_aBotsActive, sizeof(m_aBotsActive));
	mem_zero(m_aMarkedBotsActive, sizeof(m_aMarkedBotsActive));
	mem_zero(m_aLastMapViewPos, sizeof(m_aLastMapViewPos));
	mem_zero(m_aLastMapUpdateTick, sizeof(m_aLastMapUpdateTick));

	for(int i = 0; i < NUM_ENTTYPES; i++)
	{
		m_alpEntityLists[i].hint_size(16);
	}
	m_lpFlagEntityList.hint_size(8);
}

CGameWorld::~CGameWorld()
{
	if(m_pGameServer && m_pServer)
	{
		const int WorldId = m_pGameServer->GetWorldID();
		for(int i = 0; i < m_aLaserDots.size(); i++)
			m_pServer->SnapFreeID(m_aLaserDots[i].m_SnapId, WorldId);
		m_aLaserDots.clear();
	}

	// delete all entities
	for(int i = 0; i < NUM_ENTTYPES; i++)
		while(m_alpEntityLists[i].size())
			delete m_alpEntityLists[i][0];
}

void CGameWorld::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();
	m_Events.SetGameServer(pGameServer);
}

CGameWorld::TypeRange CGameWorld::DoTypeRange(int Type)
{
	dbg_assert(Type >= 0 && Type < NUM_ENTTYPES, "out of range");
	return m_alpEntityLists[Type].all();
}

CGameWorld::FlagRange CGameWorld::DoFlagRange(int Flag)
{
	return FlagRange(m_lpFlagEntityList.all(), CFlagCheck(Flag));
}

void CGameWorld::EnsureSpatialDimensions()
{
	if(m_SpatialCellsX > 0 && m_SpatialCellsY > 0)
		return;

	int MapW = 8192;
	int MapH = 8192;
	if(m_pGameServer && m_pGameServer->Collision())
	{
		MapW = maximum(32, m_pGameServer->Collision()->GetWidth() * 32);
		MapH = maximum(32, m_pGameServer->Collision()->GetHeight() * 32);
	}

	m_SpatialCellsX = maximum(1, (MapW + SPATIAL_CELL_SIZE - 1) / SPATIAL_CELL_SIZE);
	m_SpatialCellsY = maximum(1, (MapH + SPATIAL_CELL_SIZE - 1) / SPATIAL_CELL_SIZE);
	const int NumCells = m_SpatialCellsX * m_SpatialCellsY;

	m_aSpatialCharacter.clear();
	m_aSpatialCharacter.set_size(NumCells);
	m_aSpatialRpgCk.clear();
	m_aSpatialRpgCk.set_size(NumCells);
	m_aBotSpatial.clear();
	m_aBotSpatial.set_size(NumCells);
}

int CGameWorld::SpatialCellAt(vec2 Pos) const
{
	if(m_SpatialCellsX <= 0 || m_SpatialCellsY <= 0)
		return 0;

	int Cx = (int)(Pos.x / (float)SPATIAL_CELL_SIZE);
	int Cy = (int)(Pos.y / (float)SPATIAL_CELL_SIZE);
	Cx = clamp(Cx, 0, m_SpatialCellsX - 1);
	Cy = clamp(Cy, 0, m_SpatialCellsY - 1);
	return Cy * m_SpatialCellsX + Cx;
}

void CGameWorld::SpatialClear(array<SSpatialCell> &Grid)
{
	for(int i = 0; i < Grid.size(); i++)
		Grid[i].m_aEnts.clear();
}

void CGameWorld::SpatialAddEntity(CEntity *pEnt, array<SSpatialCell> &Grid)
{
	if(!pEnt || Grid.size() == 0)
		return;

	EnsureSpatialDimensions();
	const int Idx = SpatialCellAt(pEnt->m_Pos);
	Grid[Idx].m_aEnts.add(pEnt);
	pEnt->m_SpatialCellIdx = Idx;
}

void CGameWorld::SpatialRemoveEntity(CEntity *pEnt, array<SSpatialCell> &Grid)
{
	if(!pEnt || pEnt->m_SpatialCellIdx < 0 || pEnt->m_SpatialCellIdx >= Grid.size())
		return;

	array<CEntity *> &Cell = Grid[pEnt->m_SpatialCellIdx].m_aEnts;
	for(int i = 0; i < Cell.size(); i++)
	{
		if(Cell[i] == pEnt)
		{
			Cell.remove_index(i);
			break;
		}
	}
	pEnt->m_SpatialCellIdx = -1;
}

void CGameWorld::RebuildCharacterSpatial()
{
	EnsureSpatialDimensions();
	SpatialClear(m_aSpatialCharacter);
	for(int i = 0; i < m_alpEntityLists[ENTTYPE_CHARACTER].size(); i++)
	{
		CEntity *pEnt = m_alpEntityLists[ENTTYPE_CHARACTER][i];
		if(!pEnt)
			continue;
		const int Idx = SpatialCellAt(pEnt->m_Pos);
		m_aSpatialCharacter[Idx].m_aEnts.add(pEnt);
		pEnt->m_SpatialCellIdx = Idx;
	}
	m_SpatialCharacterRebuildTick = Server()->Tick();
}

int CGameWorld::FindEntitiesInGrid(const array<SSpatialCell> &Grid, vec2 Pos, float Radius, array<CEntity *> &lpEnts) const
{
	if(Grid.size() == 0 || m_SpatialCellsX <= 0)
		return 0;

	const int MinCx = maximum(0, (int)((Pos.x - Radius) / (float)SPATIAL_CELL_SIZE));
	const int MaxCx = minimum(m_SpatialCellsX - 1, (int)((Pos.x + Radius) / (float)SPATIAL_CELL_SIZE));
	const int MinCy = maximum(0, (int)((Pos.y - Radius) / (float)SPATIAL_CELL_SIZE));
	const int MaxCy = minimum(m_SpatialCellsY - 1, (int)((Pos.y + Radius) / (float)SPATIAL_CELL_SIZE));

	int Num = 0;
	for(int Cy = MinCy; Cy <= MaxCy; Cy++)
	{
		for(int Cx = MinCx; Cx <= MaxCx; Cx++)
		{
			const array<CEntity *> &Cell = Grid[Cy * m_SpatialCellsX + Cx].m_aEnts;
			for(int i = 0; i < Cell.size(); i++)
			{
				CEntity *pEnt = Cell[i];
				if(!pEnt)
					continue;
				if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
				{
					lpEnts.add(pEnt);
					Num++;
				}
			}
		}
	}
	return Num;
}

CEntity *CGameWorld::ClosestEntityInGrid(const array<SSpatialCell> &Grid, vec2 Pos, float Radius, CEntity *pNotThis) const
{
	if(Grid.size() == 0 || m_SpatialCellsX <= 0)
		return nullptr;

	float ClosestRange = Radius * 2.f;
	CEntity *pClosest = nullptr;

	const int MinCx = maximum(0, (int)((Pos.x - Radius) / (float)SPATIAL_CELL_SIZE));
	const int MaxCx = minimum(m_SpatialCellsX - 1, (int)((Pos.x + Radius) / (float)SPATIAL_CELL_SIZE));
	const int MinCy = maximum(0, (int)((Pos.y - Radius) / (float)SPATIAL_CELL_SIZE));
	const int MaxCy = minimum(m_SpatialCellsY - 1, (int)((Pos.y + Radius) / (float)SPATIAL_CELL_SIZE));

	for(int Cy = MinCy; Cy <= MaxCy; Cy++)
	{
		for(int Cx = MinCx; Cx <= MaxCx; Cx++)
		{
			const array<CEntity *> &Cell = Grid[Cy * m_SpatialCellsX + Cx].m_aEnts;
			for(int i = 0; i < Cell.size(); i++)
			{
				CEntity *pEnt = Cell[i];
				if(!pEnt || pEnt == pNotThis)
					continue;

				const float Len = distance(Pos, pEnt->m_Pos);
				if(Len < pEnt->m_ProximityRadius + Radius && Len < ClosestRange)
				{
					ClosestRange = Len;
					pClosest = pEnt;
				}
			}
		}
	}
	return pClosest;
}

void CGameWorld::BuildBotSpatialIndex()
{
	EnsureSpatialDimensions();
	for(int i = 0; i < m_aBotSpatial.size(); i++)
		m_aBotSpatial[i].m_aClientIds.clear();

	if(!m_pGameServer)
		return;

	const int WorldId = m_pGameServer->GetWorldID();
	for(int ClientID = MAX_HUMAN_CLIENTS; ClientID < MAX_CLIENTS; ClientID++)
	{
		if(!Server()->ClientIngame(ClientID) || Server()->GetClientWorldID(ClientID) != WorldId)
			continue;

		CPlayer *pBot = m_pGameServer->m_apPlayers[ClientID];
		if(!pBot || !pBot->GetCharacter())
			continue;

		const int Cell = SpatialCellAt(pBot->GetCharacter()->GetPos());
		m_aBotSpatial[Cell].m_aClientIds.add(ClientID);
	}
}

void CGameWorld::CollectBotsNearView(vec2 ViewPos, float Radius, array<int> &apBotIds) const
{
	apBotIds.clear();
	if(m_aBotSpatial.size() == 0 || m_SpatialCellsX <= 0)
		return;

	const int MinCx = maximum(0, (int)((ViewPos.x - Radius) / (float)SPATIAL_CELL_SIZE));
	const int MaxCx = minimum(m_SpatialCellsX - 1, (int)((ViewPos.x + Radius) / (float)SPATIAL_CELL_SIZE));
	const int MinCy = maximum(0, (int)((ViewPos.y - Radius) / (float)SPATIAL_CELL_SIZE));
	const int MaxCy = minimum(m_SpatialCellsY - 1, (int)((ViewPos.y + Radius) / (float)SPATIAL_CELL_SIZE));

	for(int Cy = MinCy; Cy <= MaxCy; Cy++)
	{
		for(int Cx = MinCx; Cx <= MaxCx; Cx++)
		{
			const array<int> &Cell = m_aBotSpatial[Cy * m_SpatialCellsX + Cx].m_aClientIds;
			for(int i = 0; i < Cell.size(); i++)
				apBotIds.add(Cell[i]);
		}
	}
}

int CGameWorld::FindEntities(vec2 Pos, float Radius, array<CEntity *> &lpEnts, int Type)
{
	if(Type < 0 || Type >= NUM_ENTTYPES)
		return 0;

	if(Type == ENTTYPE_CHARACTER && m_aSpatialCharacter.size() > 0 && m_SpatialCharacterRebuildTick >= 0)
		return FindEntitiesInGrid(m_aSpatialCharacter, Pos, Radius, lpEnts);

	if(Type == ENTTYPE_RPG_CK && m_aSpatialRpgCk.size() > 0)
		return FindEntitiesInGrid(m_aSpatialRpgCk, Pos, Radius, lpEnts);

	int Num = 0;
	for(auto &pEnt : m_alpEntityLists[Type])
	{
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			lpEnts.add(pEnt);
			Num++;
		}
	}

	return Num;
}

int CGameWorld::FindFlagEntities(vec2 Pos, float Radius, array<CEntity *> &lpEnts, int Flag)
{
	CFlagCheck Check(Flag);
	int Num = 0;
	for(auto &pEnt : m_lpFlagEntityList)
	{
		if(!Check(pEnt)) continue;
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			lpEnts.add(pEnt);
			Num++;
		}
	}

	return Num;
}

void CGameWorld::InsertEntity(CEntity *pEnt)
{
	m_alpEntityLists[pEnt->m_ObjType].add(pEnt);
	if(pEnt->ObjFlag() != 0)
		m_lpFlagEntityList.add(pEnt);

	if(pEnt->m_ObjType == ENTTYPE_RPG_CK)
		SpatialAddEntity(pEnt, m_aSpatialRpgCk);
}

void CGameWorld::DestroyEntity(CEntity *pEnt)
{
	pEnt->MarkForDestroy();
}

void CGameWorld::RemoveEntity(CEntity *pEnt)
{
	if(pEnt->m_ObjType == ENTTYPE_RPG_CK)
		SpatialRemoveEntity(pEnt, m_aSpatialRpgCk);

	m_alpEntityLists[pEnt->m_ObjType].remove_fast(pEnt);
	m_lpFlagEntityList.remove_fast(pEnt);

	if(m_alpEntityLists[pEnt->m_ObjType].size() > 32 && m_alpEntityLists[pEnt->m_ObjType].used_memory() < m_alpEntityLists[pEnt->m_ObjType].memusage() / 3) // lower than 1/3
	{
		m_alpEntityLists[pEnt->m_ObjType].optimize();
	}
	if(m_lpFlagEntityList.size() > 32 && m_lpFlagEntityList.used_memory() < m_lpFlagEntityList.memusage() / 3) // lower than 1/3
	{
		m_lpFlagEntityList.optimize();
	}
}

//
void CGameWorld::Snap(int SnappingClient)
{
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(auto &pEnt : m_alpEntityLists[i])
			if(pEnt)
				pEnt->Snap(SnappingClient);
	SnapLaserDots(SnappingClient);
	m_Events.Snap(SnappingClient);
}

void CGameWorld::PostSnap()
{
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(auto &pEnt : m_alpEntityLists[i])
			if(pEnt)
				pEnt->PostSnap();
	m_Events.Clear();
}

void CGameWorld::RemoveEntities()
{
	// destroy objects marked for destruction
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(int j = 0; j < m_alpEntityLists[i].size(); j++)
		{
			if(m_alpEntityLists[i][j]->IsMarkedForDestroy())
			{
				m_alpEntityLists[i][j]->Destroy();
				j--;
			}
		}
}

void CGameWorld::Tick()
{
	// update all objects (index loop: Tick() may InsertEntity into any list)
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(int j = 0; j < m_alpEntityLists[i].size(); j++)
			if(m_alpEntityLists[i][j])
				m_alpEntityLists[i][j]->Tick();

	RebuildCharacterSpatial();

	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(int j = 0; j < m_alpEntityLists[i].size(); j++)
			if(m_alpEntityLists[i][j])
				m_alpEntityLists[i][j]->TickDefered();

	TickLaserDots();
	RemoveEntities();
	UpdatePlayerMaps();
}

static bool distCompare(std::pair<float, int> a, std::pair<float, int> b)
{
	return a.first < b.first;
}

void CGameWorld::UpdatePlayerMaps(bool Force)
{
	if(!Force && (!m_pConfig || Server()->Tick() % m_pConfig->m_SvMapUpdateRate != 0))
		return;

	std::pair<float, int> Dist[MAX_CLIENTS];
	array<int> apNearbyBots;
	apNearbyBots.hint_size(64);

	const float MaxBotQueryRadius = m_pConfig ? (float)m_pConfig->m_SvMapDistanceActiveBot : 1000.f;

	int aUpdateClients[MAX_HUMAN_CLIENTS];
	int NumUpdateClients = 0;

	for(int ClientID = 0; ClientID < MAX_HUMAN_CLIENTS; ClientID++)
	{
		CPlayer *pPlayer = m_pGameServer->m_apPlayers[ClientID];
		if(!Server()->ClientIngame(ClientID) || Server()->GetClientWorldID(ClientID) != m_pGameServer->GetWorldID() || !pPlayer)
			continue;

		const vec2 ViewPos = pPlayer->m_ViewPos;
		const vec2 ViewDelta = ViewPos - m_aLastMapViewPos[ClientID];
		const float ViewMoveSq = dot(ViewDelta, ViewDelta);
		if(!Force && ViewMoveSq < 64.f * 64.f
			&& Server()->Tick() - m_aLastMapUpdateTick[ClientID] < m_pConfig->m_SvMapUpdateRate * 2)
			continue;

		aUpdateClients[NumUpdateClients++] = ClientID;
	}

	if(NumUpdateClients == 0)
		return;

	BuildBotSpatialIndex();

	for(int u = 0; u < NumUpdateClients; u++)
	{
		const int ClientID = aUpdateClients[u];
		CPlayer *pPlayer = m_pGameServer->m_apPlayers[ClientID];
		const vec2 ViewPos = pPlayer->m_ViewPos;
		m_aLastMapViewPos[ClientID] = ViewPos;
		m_aLastMapUpdateTick[ClientID] = Server()->Tick();

		int *pMap = Server()->GetIdMap(ClientID);

		for(int j = MAX_HUMAN_CLIENTS; j < MAX_CLIENTS; j++)
		{
			Dist[j].second = j;
			Dist[j].first = 1e10f;
		}

		CollectBotsNearView(ViewPos, MaxBotQueryRadius, apNearbyBots);
		for(int i = 0; i < apNearbyBots.size(); i++)
		{
			const int j = apNearbyBots[i];
			if(j < MAX_HUMAN_CLIENTS || j >= MAX_CLIENTS)
				continue;

			CPlayer *pBotPlayer = m_pGameServer->m_apPlayers[j];
			if(!pBotPlayer || !pBotPlayer->GetCharacter())
				continue;

			const float ActiveBotDistSq = pBotPlayer->GetActiveDistance() * pBotPlayer->GetActiveDistance();
			const vec2 BotPos = pBotPlayer->GetCharacter()->GetPos();
			const float BotDist = distance(ViewPos, BotPos);
			const float DistanceSq = BotDist * BotDist;
			if(DistanceSq > ActiveBotDistSq)
				continue;

			if(pBotPlayer->IsSnappingInactiveForClient(ClientID))
				continue;

			Dist[j].first = DistanceSq;
		}

		Dist[ClientID].first = 0.f;

		int aReverseMap[MAX_CLIENTS];
		memset(aReverseMap, -1, sizeof(int) * MAX_CLIENTS);
		for(int j = MAX_HUMAN_CLIENTS; j < VANILLA_MAX_CLIENTS; j++)
		{
			if(pMap[j] == -1)
				continue;

			if(Dist[pMap[j]].first > 5e9f)
				pMap[j] = -1;
			else
				aReverseMap[pMap[j]] = j;
		}

		std::nth_element(&Dist[MAX_HUMAN_CLIENTS], &Dist[VANILLA_MAX_CLIENTS - 1], &Dist[MAX_CLIENTS], distCompare);

		int Mapc = MAX_HUMAN_CLIENTS;
		int Demand = 0;
		for(int j = MAX_HUMAN_CLIENTS; j < VANILLA_MAX_CLIENTS - 1; j++)
		{
			const int k = Dist[j].second;
			if(aReverseMap[k] != -1 || Dist[j].first > 5e9f)
				continue;

			while(Mapc < VANILLA_MAX_CLIENTS && pMap[Mapc] != -1)
				Mapc++;

			if(Mapc < VANILLA_MAX_CLIENTS - 1)
			{
				pMap[Mapc] = k;
				m_pGameServer->SendClientInfo(ClientID, k, false, true);
			}
			else
				Demand++;
		}

		for(int j = MAX_CLIENTS - 1; j > VANILLA_MAX_CLIENTS - 2; j--)
		{
			const int k = Dist[j].second;
			if(aReverseMap[k] != -1 && Demand-- > 0)
				pMap[aReverseMap[k]] = -1;
		}

		for(int j = MAX_HUMAN_CLIENTS; j < VANILLA_MAX_CLIENTS - 1; j++)
		{
			if(pMap[j] >= 0 && m_NumMarkedBotsActive < MAX_CLIENTS)
				m_aMarkedBotsActive[m_NumMarkedBotsActive++] = pMap[j];
		}

		pMap[VANILLA_MAX_CLIENTS - 1] = -1;
	}

	mem_zero(m_aBotsActive, sizeof(m_aBotsActive));
	for(int i = 0; i < m_NumMarkedBotsActive; i++)
	{
		const int MarkedID = m_aMarkedBotsActive[i];
		if(MarkedID >= 0 && MarkedID < MAX_CLIENTS)
			m_aBotsActive[MarkedID] = true;
	}
	m_NumMarkedBotsActive = 0;
}

CEntity *CGameWorld::IntersectEntity(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, int Type, CEntity *pNotThis)
{
	// Find other entities
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_alpEntityLists[Type])
	{
		if(pEnt == pNotThis)
			continue;

		vec2 IntersectPos = closest_point_on_line(Pos0, Pos1, pEnt->m_Pos);
		float Len = distance(pEnt->m_Pos, IntersectPos);
		if(Len < pEnt->GetProximityRadius() + Radius)
		{
			Len = distance(Pos0, IntersectPos);
			if(Len < ClosestLen)
			{
				NewPos = IntersectPos;
				ClosestLen = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

CCharacter *CGameWorld::IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, CCharacter *pNotThis)
{
	vec2 At;
	CEntity *pHit = IntersectEntity(Pos0, Pos1, Radius, At, ENTTYPE_CHARACTER, pNotThis);
	if(!pHit)
		return nullptr;
	NewPos = At;
	return static_cast<CCharacter *>(pHit);
}

CEntity *CGameWorld::IntersectFlagEntity(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, int Flag, CEntity *pNotThis)
{
	CFlagCheck Check(Flag);
	// Find other entities
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_lpFlagEntityList)
	{
		if(pEnt == pNotThis || !Check(pEnt))
			continue;

		vec2 IntersectPos = closest_point_on_line(Pos0, Pos1, pEnt->m_Pos);
		float Len = distance(pEnt->m_Pos, IntersectPos);
		if(Len < pEnt->GetProximityRadius() + Radius)
		{
			Len = distance(Pos0, IntersectPos);
			if(Len < ClosestLen)
			{
				NewPos = IntersectPos;
				ClosestLen = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

CEntity *CGameWorld::IntersectFlagEntitySkippingTurrets(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, int Flag, CEntity *pNotThis)
{
	const float MaxDist = distance(Pos0, Pos1);
	if(MaxDist < 0.001f)
		return IntersectFlagEntity(Pos0, Pos1, Radius, NewPos, Flag, pNotThis);

	const vec2 Dir = normalize(Pos1 - Pos0);
	vec2 Start = Pos0;
	float Traveled = 0.f;

	while(Traveled < MaxDist)
	{
		vec2 At;
		CEntity *pHit = IntersectFlagEntity(Start, Pos1, Radius, At, Flag, pNotThis);
		if(!pHit)
			return nullptr;
		if(pHit->ObjType() != ENTTYPE_TURRET)
		{
			NewPos = At;
			return pHit;
		}

		const float Step = distance(Start, At) + pHit->GetProximityRadius() + Radius + 2.0f;
		if(Step < 0.5f)
			return nullptr;
		Traveled += Step;
		Start = Pos0 + Dir * minimum(Traveled, MaxDist);
	}
	return nullptr;
}

CEntity *CGameWorld::ClosestEntity(vec2 Pos, float Radius, int Type, CEntity *pNotThis)
{
	if(Type == ENTTYPE_CHARACTER && m_aSpatialCharacter.size() > 0 && m_SpatialCharacterRebuildTick >= 0)
		return ClosestEntityInGrid(m_aSpatialCharacter, Pos, Radius, pNotThis);

	if(Type == ENTTYPE_RPG_CK && m_aSpatialRpgCk.size() > 0)
		return ClosestEntityInGrid(m_aSpatialRpgCk, Pos, Radius, pNotThis);

	// Find other entities
	float ClosestRange = Radius * 2;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_alpEntityLists[Type])
	{
		if(pEnt == pNotThis)
			continue;

		float Len = distance(Pos, pEnt->m_Pos);
		if(Len < pEnt->m_ProximityRadius + Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

CEntity *CGameWorld::ClosestFlagEntity(vec2 Pos, float Radius, int Flag, CEntity *pNotThis)
{
	CFlagCheck Check(Flag);
	// Find other entities
	float ClosestRange = Radius * 2;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_lpFlagEntityList)
	{
		if(pEnt == pNotThis || !Check(pEnt))
			continue;

		float Len = distance(Pos, pEnt->m_Pos);
		if(Len < pEnt->m_ProximityRadius + Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

bool CGameWorld::CFlagCheck::operator()(CEntity *&pEntity) const { return pEntity->ObjFlag() & m_ConditionFlag; }

void CGameWorld::CreateDamage(vec2 Pos, int Id, vec2 Source, int HealthAmount, int ArmorAmount, bool Self)
{
	float f = angle(Source);
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *) m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_ClientID = Id;
		pEvent->m_Angle = (int) (f * 256.0f);
		pEvent->m_HealthAmount = HealthAmount;
		pEvent->m_ArmorAmount = ArmorAmount;
		pEvent->m_Self = Self;
	}
}

void CGameWorld::CreateFloatingAmount(vec2 Pos, int ClientID, int Amount, int64 Mask)
{
	if(Amount <= 0)
		return;

	int HealthAmount = 0;
	int ArmorAmount = 0;
	if(Amount < 10)
		HealthAmount = Amount;
	else
	{
		HealthAmount = minimum(9, Amount / 10);
		ArmorAmount = Amount % 10;
	}

	float f = angle(vec2(0.f, -1.f));
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *)m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
		pEvent->m_Angle = (int)(f * 256.0f);
		pEvent->m_HealthAmount = HealthAmount;
		pEvent->m_ArmorAmount = ArmorAmount;
		pEvent->m_Self = false;
	}
}

void CGameWorld::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *) m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}
}

bool CGameWorld::IsHumanDefenderOwner(int OwnerCid)
{
	if(OwnerCid < 0 || OwnerCid >= MAX_CLIENTS || !GameServer())
		return false;
	CPlayer *pP = GameServer()->m_apPlayers[OwnerCid];
	return pP && !pP->IsDummy() && pP->GetTeam() != TEAM_BLUE;
}

int CGameWorld::DamageOwnerFromEntity(CEntity *pFrom) const
{
	if(!pFrom)
		return -1;
	if(pFrom->ObjType() == ENTTYPE_CHARACTER)
		return static_cast<CCharacter *>(pFrom)->GetCID();
	if(pFrom->ObjType() == ENTTYPE_GROWINGEXPLOSION)
		return static_cast<CGrowingExplosion *>(pFrom)->GetOwner();
	if(pFrom->ObjType() == ENTTYPE_PLASMA)
		return static_cast<CPlasma *>(pFrom)->GetOwner();
	if(pFrom->ObjFlag() & ENTFLAG_CHILD)
		return static_cast<const CChildEntity *>(pFrom)->GetOwner();
	return -1;
}

void CGameWorld::CreateExplosion(vec2 Pos, CEntity *pOwner, int Weapon, int MaxDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *) m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}

	const int OwnerCid = DamageOwnerFromEntity(pOwner);
	const bool HumanDefender = IsHumanDefenderOwner(OwnerCid);

	// deal damage
	array<CEntity *> lpEnts;
	lpEnts.hint_size(8);
	float Radius = g_pData->m_Explosion.m_Radius;
	float InnerRadius = 48.0f;
	float MaxForce = g_pData->m_Explosion.m_MaxForce;
	const int Num = FindFlagEntities(Pos, Radius, lpEnts, CGameWorld::ENTFLAG_HITABLE);
	for(int i = 0; i < Num; i++)
	{
		CEntity *pEnt = lpEnts[i];
		if(!pEnt || pEnt->IsMarkedForDestroy())
			continue;
		const int Type = pEnt->ObjType();
		if(Type != ENTTYPE_CHARACTER && Type != ENTTYPE_TOWERMAIN && Type != ENTTYPE_TURRET && Type != ENTTYPE_SPIDERLEG)
			continue;
		if(HumanDefender && (Type == ENTTYPE_TOWERMAIN || Type == ENTTYPE_TURRET))
			continue;

		vec2 Diff = pEnt->GetPos() - Pos;
		vec2 Force(0, MaxForce);
		float l = length(Diff);
		if(l)
			Force = normalize(Diff) * MaxForce;
		float Factor = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		if((int) (Factor * MaxDamage))
			static_cast<CHitableEntity *>(pEnt)->TakeHit(Force * Factor, Diff * -1, (int) (Factor * MaxDamage), pOwner, Weapon);
	}
}

void CGameWorld::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *) m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int) Pos.x;
		ev->m_Y = (int) Pos.y;
	}
}

void CGameWorld::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *) m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameWorld::CreateSound(vec2 Pos, int Sound, int64 Mask)
{
	if(IsCustomSound(Sound))
	{
		const CWorldDetail *pDetail = GameServer()->Server()->GetWorldDetail(GameServer()->GetWorldID());
		if(pDetail && pDetail->HasFlag(WORLD_FLAG_NO_PREPARE_MAP))
			return;

		CNetEvent_MapSoundWorld *pEvent = (CNetEvent_MapSoundWorld *)m_Events.Create(NETEVENTTYPE_MAPSOUNDWORLD, sizeof(CNetEvent_MapSoundWorld), Mask);
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_SoundId = SpecialSoundToPreparedIndex(Sound);
		}
	}
	else if(Sound >= 0)
	{
		CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_SoundID = Sound;
		}
	}
}

void CGameWorld::CreatePlayerSound(int ClientID, int Sound)
{
	if(IsCustomSound(Sound))
	{
		const CWorldDetail *pDetail = GameServer()->Server()->GetWorldDetail(GameServer()->GetWorldID());
		if(pDetail && pDetail->HasFlag(WORLD_FLAG_NO_PREPARE_MAP))
			return;

		CNetMsg_Sv_MapSoundGlobal Msg;
		Msg.m_SoundId = SpecialSoundToPreparedIndex(Sound);
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
	else if(Sound >= 0)
	{
		CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), CmaskOne(ClientID));
		if(pEvent && GameServer()->m_apPlayers[ClientID])
		{
			pEvent->m_X = (int)GameServer()->m_apPlayers[ClientID]->m_ViewPos.x;
			pEvent->m_Y = (int)GameServer()->m_apPlayers[ClientID]->m_ViewPos.y;
			pEvent->m_SoundID = Sound;
		}
	}
}

void CGameWorld::CreateLaserDot(vec2 From, vec2 To, int LifeSpan)
{
	if(!m_pGameServer || LifeSpan <= 0)
		return;

	const int WorldId = m_pGameServer->GetWorldID();
	SLaserDotSegment Segment;
	Segment.m_From = From;
	Segment.m_To = To;
	Segment.m_LifeSpan = LifeSpan;
	Segment.m_StartTick = m_pServer->Tick();
	Segment.m_SnapId = m_pServer->SnapNewID(WorldId);
	m_aLaserDots.add(Segment);
}

void CGameWorld::TickLaserDots()
{
	if(!m_pGameServer)
		return;

	const int WorldId = m_pGameServer->GetWorldID();
	for(int i = 0; i < m_aLaserDots.size();)
	{
		m_aLaserDots[i].m_LifeSpan--;
		if(m_aLaserDots[i].m_LifeSpan <= 0)
		{
			m_pServer->SnapFreeID(m_aLaserDots[i].m_SnapId, WorldId);
			m_aLaserDots.remove_index(i);
		}
		else
			i++;
	}
}

void CGameWorld::SnapLaserDots(int SnappingClient)
{
	if(!m_pServer)
		return;

	for(int i = 0; i < m_aLaserDots.size(); i++)
	{
		const SLaserDotSegment &Dot = m_aLaserDots[i];
		if(SnappingClient >= 0 && m_pGameServer)
		{
			const vec2 CheckPos = (Dot.m_From + Dot.m_To) * 0.5f;
			CPlayer *pSnap = m_pGameServer->m_apPlayers[SnappingClient];
			if(pSnap)
			{
				if(absolute(pSnap->m_ViewPos.x - CheckPos.x) > 1000.0f || absolute(pSnap->m_ViewPos.y - CheckPos.y) > 800.0f)
					continue;
				if(distance(pSnap->m_ViewPos, CheckPos) > 1100.0f)
					continue;
			}
		}

		SnapLaserSegment(m_pServer, Dot.m_SnapId, Dot.m_From, Dot.m_To, Dot.m_StartTick);
	}
}
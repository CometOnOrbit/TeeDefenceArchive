/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "entity.h"
#include "gamecontext.h"
#include "player.h"

CEntity::CEntity(CGameWorld *pGameWorld, int ObjType, int ObjFlag, vec2 Pos, int ProximityRadius)
{
	m_pGameWorld = pGameWorld;

	m_pPrevTypeEntity = 0;
	m_pNextTypeEntity = 0;

	m_ID = Server()->SnapNewID(GameWorld()->GameServer()->GetWorldID());
	m_ObjType = ObjType;
	m_ObjFlag = ObjFlag;

	m_ProximityRadius = ProximityRadius;

	m_MarkedForDestroy = false;
	m_Pos = Pos;
}

CEntity::~CEntity()
{
	const int WorldId = GameWorld()->GameServer()->GetWorldID();
	Server()->SnapFreeID(m_ID, WorldId);
	for(int i = 0; i < m_aSnapIdGroups.size(); i++)
	{
		for(int j = 0; j < m_aSnapIdGroups[i].m_aIds.size(); j++)
			Server()->SnapFreeID(m_aSnapIdGroups[i].m_aIds[j], WorldId);
	}
	m_aSnapIdGroups.clear();
	GameWorld()->RemoveEntity(this);
}

void CEntity::AddSnappingGroupIds(int GroupId, int NumIds)
{
	if(NumIds <= 0)
		return;

	RemoveSnappingGroupIds(GroupId);

	SSnapIdGroup Group;
	Group.m_GroupId = GroupId;
	Group.m_aIds.set_size(NumIds);
	const int WorldId = GameWorld()->GameServer()->GetWorldID();
	for(int i = 0; i < NumIds; i++)
		Group.m_aIds[i] = Server()->SnapNewID(WorldId);
	m_aSnapIdGroups.add(Group);
}

void CEntity::RemoveSnappingGroupIds(int GroupId)
{
	const int WorldId = GameWorld()->GameServer()->GetWorldID();
	for(int i = 0; i < m_aSnapIdGroups.size(); i++)
	{
		if(m_aSnapIdGroups[i].m_GroupId != GroupId)
			continue;
		for(int j = 0; j < m_aSnapIdGroups[i].m_aIds.size(); j++)
			Server()->SnapFreeID(m_aSnapIdGroups[i].m_aIds[j], WorldId);
		m_aSnapIdGroups.remove_index(i);
		return;
	}
}

const array<int> *CEntity::FindSnappingGroupIds(int GroupId) const
{
	for(int i = 0; i < m_aSnapIdGroups.size(); i++)
		if(m_aSnapIdGroups[i].m_GroupId == GroupId)
			return &m_aSnapIdGroups[i].m_aIds;
	return nullptr;
}

int CEntity::SnapGroupId(int GroupId, int Index) const
{
	const array<int> *pIds = FindSnappingGroupIds(GroupId);
	if(!pIds || Index < 0 || Index >= pIds->size())
		return -1;
	return (*pIds)[Index];
}

int CEntity::NetworkClipped(int SnappingClient)
{
	return NetworkClipped(SnappingClient, m_Pos);
}

int CEntity::NetworkClipped(int SnappingClient, vec2 CheckPos)
{
	if(SnappingClient == -1)
		return 0;

	float dx = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	float dy = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return 1;

	if(distance(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, CheckPos) > 1100.0f)
		return 1;
	return 0;
}

int CEntity::NetworkClippedLine(int SnappingClient, vec2 From, vec2 To)
{
	if(SnappingClient == -1)
		return 0;
	vec2 CheckPos = closest_point_on_line(From, To, GameServer()->m_apPlayers[SnappingClient]->m_ViewPos);

	float dx = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	float dy = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return 1;

	if(distance(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, CheckPos) > 1100.0f)
		return 1;
	return 0;
}

bool CEntity::GameLayerClipped(vec2 CheckPos)
{
	int rx = round_to_int(CheckPos.x) / 32;
	int ry = round_to_int(CheckPos.y) / 32;
	return (rx < -200 || rx >= GameServer()->Collision()->GetWidth() + 200) || (ry < -200 || ry >= GameServer()->Collision()->GetHeight() + 200);
}

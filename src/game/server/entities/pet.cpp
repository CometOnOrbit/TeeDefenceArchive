#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include "pet.h"

CPet::CPet(CGameContext *pGameServer, int OwnerCID, int PetID)
: CEntity(&pGameServer->m_World, CGameWorld::ENTTYPE_PICKUP, 0, vec2(0,0), 0)
, m_OwnerCID(OwnerCID)
, m_PetID(PetID)
, m_SnapClientID(OwnerCID + MAX_CLIENTS)
{
	m_aName[0] = 0;
	GameWorld()->InsertEntity(this);
}

void CPet::Tick()
{
	CPlayer *pOwner = GameServer()->m_apPlayers[m_OwnerCID];
	if(!pOwner)
	{
		GameWorld()->RemoveEntity(this);
		MarkForDestroy();
		return;
	}

	CCharacter *pChar = pOwner->GetCharacter();
	if(!pChar)
	{
		GameWorld()->RemoveEntity(this);
		MarkForDestroy();
		return;
	}

	// 跟随玩家，在玩家身后一定距离处
	vec2 OwnerPos = pChar->GetCore()->m_Pos;
	vec2 FollowOffset;

	// 根据玩家朝向决定宠物位置（身后）
	if(pChar->GetCore()->m_Direction == -1)
		FollowOffset = vec2(64, -24);
	else
		FollowOffset = vec2(-64, -24);

	m_Pos = OwnerPos + FollowOffset;
}

void CPet::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pObj = (CNetObj_Pickup *)Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_SnapClientID, sizeof(CNetObj_Pickup));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_Type = 3; // 使用 NINJA 类型以示区别（客户端会显示忍者道具的视觉效果）
}

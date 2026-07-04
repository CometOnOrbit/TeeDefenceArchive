#include "dir_navigator.h"
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>

CEntityDirNavigator::CEntityDirNavigator(CGameContext *pGameServer, vec2 Start, vec2 End, int ClientID, int WorldID, int Type, int Subtype, float Clipped)
	: CEntity(&pGameServer->m_World, 0, 0, Start)
{
	m_ClientID = ClientID;
	m_Start = Start;
	m_Type = Type;
	m_Subtype = Subtype;
	m_Clipped = Clipped;
	m_PosTo = End;

	// Resolve cross-world position via WorldManager
	if(WorldID >= 0 && pGameServer->Core() && pGameServer->Core()->WorldManager())
	{
		m_PosTo = pGameServer->Core()->WorldManager()->FindPosition(WorldID, End);
	}

	GameWorld()->InsertEntity(this);
}

CEntityDirNavigator::~CEntityDirNavigator()
{
}

void CEntityDirNavigator::Tick()
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[m_ClientID];
	if(!pPlayer || !pPlayer->GetCharacter() || !pPlayer->GetCharacter()->IsAlive())
	{
		MarkForDestroy();
		return;
	}

	m_Pos = pPlayer->GetCharacter()->GetPos();
}

void CEntityDirNavigator::Snap(int SnappingClient)
{
	if(SnappingClient != m_ClientID)
		return;

	CPlayer *pPlayer = GameServer()->m_apPlayers[m_ClientID];
	if(!pPlayer || !pPlayer->GetCharacter())
		return;

	const vec2 PlayerPos = pPlayer->GetCharacter()->GetPos();

	// If player is close enough to the target, hide the navigator
	if(m_Clipped > 1.f && distance(m_PosTo, PlayerPos) < m_Clipped)
		return;

	vec2 EndPos = m_PosTo;
	vec2 StartPos = PlayerPos;

	// Clamp to max visible distance
	if(distance(StartPos, EndPos) > 2000.0f)
	{
		EndPos = StartPos + normalize(EndPos - StartPos) * 2000.0f;
	}

	// Snap as laser for directional indicator
	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = round_to_int(EndPos.x);
	pObj->m_Y = round_to_int(EndPos.y);
	pObj->m_FromX = round_to_int(StartPos.x);
	pObj->m_FromY = round_to_int(StartPos.y);
	pObj->m_StartTick = Server()->Tick();
}

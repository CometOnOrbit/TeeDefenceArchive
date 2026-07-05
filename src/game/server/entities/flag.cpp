#include "flag.h"

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>

#include <generated/protocol.h>

CFlag::CFlag(CGameWorld *pGameWorld, int Team, vec2 StandPos)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_FLAG, 0, StandPos, ms_PhysSize)
{
	m_Team = Team;
	m_StandPos = StandPos;

	GameWorld()->InsertEntity(this);

	Reset();
}

void CFlag::Reset()
{
	m_pCarrier = nullptr;
	m_AtStand = true;
	m_Pos = m_StandPos;
	m_Vel = vec2(0.f, 0.f);
	m_GrabTick = 0;
	m_DropTick = 0;
}

void CFlag::Grab(CCharacter *pChar)
{
	m_pCarrier = pChar;
	if(m_AtStand)
		m_GrabTick = Server()->Tick();
	m_AtStand = false;
}

void CFlag::Drop()
{
	m_pCarrier = nullptr;
	m_Vel = vec2(0.f, 0.f);
	m_DropTick = Server()->Tick();
}

void CFlag::TickDefered()
{
	if(m_pCarrier)
	{
		m_Pos = m_pCarrier->GetPos();
		return;
	}

	if((GameServer()->Collision()->GetCollisionAt(m_Pos.x, m_Pos.y) & CCollision::COLFLAG_DEATH) || GameLayerClipped(m_Pos))
	{
		Reset();
		GameServer()->m_pController->OnFlagReturn(this);
		return;
	}

	if(!m_AtStand)
	{
		if(Server()->Tick() > m_DropTick + Server()->TickSpeed() * 30)
		{
			Reset();
			GameServer()->m_pController->OnFlagReturn(this);
		}
		else
		{
			m_Vel.y += GameWorld()->m_Core.m_Tuning.m_Gravity;
			GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2((float)ms_PhysSize, (float)ms_PhysSize), 0.5f);
		}
	}
}

void CFlag::TickPaused()
{
	m_DropTick++;
	if(m_GrabTick)
		m_GrabTick++;
}

void CFlag::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Flag *pFlag = static_cast<CNetObj_Flag *>(Server()->SnapNewItem(NETOBJTYPE_FLAG, m_Team, sizeof(CNetObj_Flag)));
	if(!pFlag)
		return;

	pFlag->m_X = round_to_int(m_Pos.x);
	pFlag->m_Y = round_to_int(m_Pos.y);
	pFlag->m_Team = m_Team;
}

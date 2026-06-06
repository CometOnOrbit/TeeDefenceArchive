/* (c) Siile / TeeDefenceArchive */
#include <engine/shared/config.h>

#include <generated/protocol.h>
#include <game/server/gamecontext.h>

#include "electro.h"

CElectro::CElectro(CGameWorld *pGameWorld, vec2 Start, vec2 End, vec2 Offset, int Left)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, CGameWorld::ENTFLAG_CHILD, Start, 0)
{
	m_Render = Left <= 0;
	m_End = End;
	m_EvalTick = Server()->Tick();
	GameWorld()->InsertEntity(this);

	if(Left > 0)
	{
		vec2 P = Start + End;
		P /= 2.0f;
		GameServer()->Collision()->IntersectLine(P, P + Offset, 0x0, &P);
		new CElectro(GameWorld(), Start, P, Offset * 0.5f, Left - 1);
		new CElectro(GameWorld(), P, End, Offset * 0.5f, Left - 1);
	}
}

void CElectro::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CElectro::Tick()
{
	if(!m_Render || Server()->Tick() > m_EvalTick + (Server()->TickSpeed() * GameServer()->Tuning()->m_LaserBounceDelay) / 1000)
		GameWorld()->DestroyEntity(this);
}

void CElectro::TickPaused()
{
	++m_EvalTick;
}

void CElectro::Snap(int SnappingClient)
{
	if(Config()->m_SvGESnapTime > 1 && rand() % Config()->m_SvGESnapTime != 0)
		return;
	if(!m_Render || NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_End.x;
	pObj->m_Y = (int)m_End.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;
	pObj->m_StartTick = m_EvalTick;
}

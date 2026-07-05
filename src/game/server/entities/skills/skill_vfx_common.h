#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_VFX_COMMON_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_VFX_COMMON_H

#include <engine/server.h>
#include <generated/protocol.h>

#include <base/math.h>
#include <base/vmath.h>

class IServer;

inline void SnapLaserDot(IServer *pServer, int SnapId, vec2 Pos, int StartTick)
{
	if(!pServer || SnapId < 0)
		return;

	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(pServer->SnapNewItem(NETOBJTYPE_LASER, SnapId, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;

	const int X = round_to_int(Pos.x);
	const int Y = round_to_int(Pos.y);
	pLaser->m_X = X;
	pLaser->m_Y = Y;
	pLaser->m_FromX = X;
	pLaser->m_FromY = Y;
	pLaser->m_StartTick = StartTick;
}

inline void SnapLaserSegment(IServer *pServer, int SnapId, vec2 From, vec2 To, int StartTick)
{
	if(!pServer || SnapId < 0)
		return;

	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(pServer->SnapNewItem(NETOBJTYPE_LASER, SnapId, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;

	pLaser->m_X = round_to_int(To.x);
	pLaser->m_Y = round_to_int(To.y);
	pLaser->m_FromX = round_to_int(From.x);
	pLaser->m_FromY = round_to_int(From.y);
	pLaser->m_StartTick = StartTick;
}

#endif

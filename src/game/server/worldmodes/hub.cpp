#include "hub.h"

#include <game/mapitems.h>
#include <game/server/gamecontext.h>

CGameControllerHub::CGameControllerHub(CGameContext *pGameServer)
	: CGameController(pGameServer)
{
}

void CGameControllerHub::Tick()
{
	TickLoginReminders();
	DoActivityCheck();
}

bool CGameControllerHub::OnEntity(int Index, vec2 Pos)
{
	if(Index >= ENTITY_LOG)
		return true;
	return CGameController::OnEntity(Index, Pos);
}

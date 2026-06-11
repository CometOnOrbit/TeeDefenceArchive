#include "story.h"

#include <game/server/gamecontext.h>

CGameControllerStory::CGameControllerStory(CGameContext *pGameServer)
	: CGameControllerHub(pGameServer)
{
}

void CGameControllerStory::Tick()
{
	TickLoginReminders();
	DoActivityCheck();
}

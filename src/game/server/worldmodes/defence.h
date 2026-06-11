#ifndef GAME_SERVER_WORLDMODES_DEFENCE_H
#define GAME_SERVER_WORLDMODES_DEFENCE_H

#include <game/server/gamecontroller.h>

class CGameControllerDefence : public CGameController
{
public:
	explicit CGameControllerDefence(CGameContext *pGameServer) : CGameController(pGameServer) {}
};

#endif

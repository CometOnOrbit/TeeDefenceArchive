#ifndef GAME_SERVER_WORLDMODES_HUB_H
#define GAME_SERVER_WORLDMODES_HUB_H

#include <game/server/gamecontroller.h>

class CGameControllerHub : public CGameController
{
public:
	explicit CGameControllerHub(CGameContext *pGameServer);

	void Tick() override;
	bool OnEntity(int Index, vec2 Pos) override;
};

#endif

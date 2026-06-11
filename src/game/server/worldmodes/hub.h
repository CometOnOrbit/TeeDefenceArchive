#ifndef GAME_SERVER_WORLDMODES_HUB_H
#define GAME_SERVER_WORLDMODES_HUB_H

#include "defence.h"

class CGameControllerHub : public CGameControllerDefence
{
public:
	explicit CGameControllerHub(CGameContext *pGameServer);

	void Tick() override;
	bool OnEntity(int Index, vec2 Pos) override;
};

#endif

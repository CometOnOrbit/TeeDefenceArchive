#ifndef GAME_SERVER_WORLDMODES_STORY_H
#define GAME_SERVER_WORLDMODES_STORY_H

#include "hub.h"

class CGameControllerStory : public CGameControllerHub
{
public:
	explicit CGameControllerStory(CGameContext *pGameServer);

	void Tick() override;
};

#endif

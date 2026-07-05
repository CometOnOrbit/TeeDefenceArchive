#ifndef GAME_SERVER_WORLDMODES_TDM_H
#define GAME_SERVER_WORLDMODES_TDM_H

#include "arena.h"

class CGameControllerTDM : public CGameControllerArena
{
public:
	explicit CGameControllerTDM(CGameContext *pGameServer);

	void OnCharacterSpawn(CCharacter *pChr) override;
	int OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon) override;
};

#endif

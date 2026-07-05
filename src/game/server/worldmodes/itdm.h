#ifndef GAME_SERVER_WORLDMODES_ITDM_H
#define GAME_SERVER_WORLDMODES_ITDM_H

#include "arena.h"

class CGameControllerITDM : public CGameControllerArena
{
public:
	explicit CGameControllerITDM(CGameContext *pGameServer);

	void PreTick() override;
	void OnCharacterSpawn(CCharacter *pChr) override;
	int OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon) override;
};

#endif

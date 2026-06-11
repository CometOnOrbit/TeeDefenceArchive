#ifndef GAME_SERVER_WORLDMODES_PVP_H
#define GAME_SERVER_WORLDMODES_PVP_H

#include "hub.h"

class CGameControllerPvP : public CGameControllerHub
{
public:
	explicit CGameControllerPvP(CGameContext *pGameServer);

	void OnCharacterSpawn(class CCharacter *pChr) override;
	bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const override;
	bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const override;
	bool OnEntity(int Index, vec2 Pos) override;
};

#endif

#ifndef GAME_SERVER_WORLDMODES_ARENA_H
#define GAME_SERVER_WORLDMODES_ARENA_H

#include "hub.h"

// Shared base for guild-war / PvP arena worlds.
class CGameControllerArena : public CGameControllerHub
{
public:
	explicit CGameControllerArena(CGameContext *pGameServer);

	bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const override;
	bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const override;
	bool OnEntity(int Index, vec2 Pos) override;

protected:
	void StripToWeapons(CCharacter *pChr, const int *pWeapons, int NumWeapons, const int *pAmmo) const;
};

#endif
